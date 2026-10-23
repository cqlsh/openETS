#pragma once

#include "Engine/Core/Memory/AlignedAllocation.hpp"
#include "Engine/Core/Memory/Alignment.hpp"
#include "Engine/Core/Memory/Poisoning.hpp"
#include "Engine/Core/Numeric/CheckedArithmetic.hpp"
#include "Engine/Core/Result.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>

namespace openETS::Engine::Core::Memory {

namespace Detail {

// Slot number of the calling thread plus one, or zero before its first slow allocation; defined once in FrameAllocator.cpp.
extern constinit thread_local std::size_t threadFrameSlot;

}  // namespace Detail

/**
 * @brief Hands out memory that lives for a fixed number of frames, to any number of threads at the same time.
 * @details Owns framesInFlight regions; BeginFrame recycles the region of the oldest frame, so memory stays valid until
 *          BeginFrame has been called framesInFlight more times. TryAllocate is safe on any thread, while BeginFrame, moves and
 *          destruction need exclusive access. Runs no destructors. With AddressSanitizer, recycled memory is poisoned. Move-only.
 *          Every thread bumps through a chunk in a slot of its own and takes only the next chunk from the shared cursor, so it holds
 *          at most one partly used chunk per allocator. With 16 threads this was 3 to 7 times faster than one shared atomic cursor
 *          for every request, and 3 times faster on one thread. Threads beyond the slot count allocate from the shared cursor.
 */
class FrameAllocator {
 public:
  /** @brief Largest chunk a thread reserves at once; smaller frames reserve a 64th of their capacity, in whole cache lines. */
  static constexpr std::size_t MAX_CHUNK_SIZE_IN_BYTES = std::size_t{64} * 1024;

  /** @brief Thread slots TryCreate prepares unless asked otherwise; CPUs with more hardware threads need a larger count. */
  static constexpr std::size_t DEFAULT_THREAD_SLOT_COUNT = 64;

  /** @brief Most threads that can hold a slot number at the same time; slot numbers return when their thread ends. */
  static constexpr std::size_t MAX_THREAD_SLOT_COUNT = 1024;

  /**
   * @brief Creates framesInFlight regions of capacityPerFrameInBytes bytes, rounded up to whole cache lines; frame zero is current.
   * @details threadSlotCount threads get a slot of their own; zero sends every request to the shared cursor.
   *          The return type is deduced because Result cannot check its requirements while this class is still incomplete.
   * @return Result holding the allocator, or InvalidSize for a zero argument, too many slots or overflow, or OutOfMemory.
   */
  [[nodiscard]] static auto TryCreate(std::size_t capacityPerFrameInBytes, std::size_t framesInFlight,
      std::size_t threadSlotCount = DEFAULT_THREAD_SLOT_COUNT) noexcept {
    using Created = Result<FrameAllocator, AllocationError>;
    std::size_t capacityPerFrame = 0;
    std::size_t regionsSize = 0;
    std::size_t storageSize = 0;
    if (capacityPerFrameInBytes == 0 || framesInFlight == 0 || threadSlotCount > MAX_THREAD_SLOT_COUNT ||
        !TryAlignUp(capacityPerFrameInBytes, CACHE_LINE_SIZE, capacityPerFrame) || !Numeric::TryMultiply(capacityPerFrame, framesInFlight, regionsSize) ||
        !Numeric::TryAdd(regionsSize, (threadSlotCount + 1) * CACHE_LINE_SIZE, storageSize)) {
      return Created::Failure(AllocationError::InvalidSize);
    }
    auto storage = AlignedAllocation::TryCreate(storageSize, CACHE_LINE_SIZE);
    if (storage.HasError()) {
      return Created::Failure(storage.GetError());
    }
    return Created::Success(FrameAllocator(std::move(storage).GetValue(), capacityPerFrame, framesInFlight, threadSlotCount));
  }

  ~FrameAllocator() noexcept { UnpoisonMemory(m_storage.GetBytes().data(), m_storage.GetBytes().size()); }

  FrameAllocator(const FrameAllocator&) = delete;
  FrameAllocator& operator=(const FrameAllocator&) = delete;

  /** @brief Takes the storage, the thread slots and the current frame from other, which is left without storage. */
  FrameAllocator(FrameAllocator&& other) noexcept : m_storage(std::move(other.m_storage)), m_state(std::exchange(other.m_state, State{})) {}

  /** @brief Returns the current storage to the heap, then takes the storage, the thread slots and the current frame from other. */
  FrameAllocator& operator=(FrameAllocator&& other) noexcept {
    if (&other != this) {
      UnpoisonMemory(m_storage.GetBytes().data(), m_storage.GetBytes().size());
      m_storage = std::move(other.m_storage);
      m_state = std::exchange(other.m_state, State{});
    }
    return *this;
  }

  /**
   * @brief Returns sizeInBytes uninitialized bytes aligned to alignment from the current frame.
   * @details Safe on any number of threads at once. Requests above a quarter of a chunk never start a new chunk.
   * @return Null for zero bytes, an invalid alignment or too little space in the current frame; the allocator remains unchanged.
   */
  [[nodiscard]] std::byte* TryAllocate(std::size_t sizeInBytes, std::size_t alignment) noexcept {
    if (sizeInBytes == 0 || !IsValidAlignment(alignment)) {
      return nullptr;
    }
    // A thread without a slot number reads zero, which wraps around here and takes the slow path.
    const std::size_t slot = Detail::threadFrameSlot - 1;
    std::byte* const result = slot < m_state.m_slotCount ? TryTakeFromSlot(m_state.m_slots[slot], sizeInBytes, alignment) : nullptr;
    if (result == nullptr) {
      return TryAllocateSlow(sizeInBytes, alignment);
    }
    UnpoisonMemory(result, sizeInBytes);
    return result;
  }

  /**
   * @brief Reserves count uninitialized elements of Type in the current frame. Zero elements succeed with an empty span.
   * @return False if the byte size overflows or does not fit; outArray and the allocator remain unchanged.
   */
  template <typename Type>
  requires std::is_trivially_default_constructible_v<Type> && std::is_trivially_destructible_v<Type>
  [[nodiscard]] bool TryAllocateArray(std::size_t count, std::span<Type>& outArray) noexcept {
    if (count == 0) {
      outArray = {};
      return true;
    }
    std::size_t sizeInBytes = 0;
    if (!Numeric::TryMultiply(count, sizeof(Type), sizeInBytes)) {
      return false;
    }
    std::byte* const bytes = TryAllocate(sizeInBytes, alignof(Type));
    if (bytes == nullptr) {
      return false;
    }
    outArray = std::span<Type>(reinterpret_cast<Type*>(bytes), count);
    return true;
  }

  /**
   * @brief Makes the next frame current and recycles the region of the frame that began framesInFlight frames ago.
   * @details Needs exclusive access: no thread may allocate meanwhile, and nothing may still use memory of the recycled frame,
   *          the GPU included. Does nothing on an allocator without storage.
   */
  void BeginFrame() noexcept;

  /** @brief Returns the bytes each frame can hand out, rounded up to whole cache lines. */
  [[nodiscard]] std::size_t GetCapacityPerFrameInBytes() const noexcept { return m_state.m_capacityPerFrame; }

  /** @brief Returns how many frames keep their memory at the same time. */
  [[nodiscard]] std::size_t GetFramesInFlight() const noexcept { return m_state.m_framesInFlight; }

  /** @brief Returns how many threads get a slot of their own; the others allocate from the shared cursor. */
  [[nodiscard]] std::size_t GetThreadSlotCount() const noexcept { return m_state.m_slotCount; }

  /** @brief Returns the number of the current frame; it starts at zero and BeginFrame adds one. */
  [[nodiscard]] std::uint64_t GetFrameNumber() const noexcept { return m_state.m_frameNumber; }

  /** @brief Returns the bytes taken from the current frame, including chunk space that threads have not handed out yet. */
  [[nodiscard]] std::size_t GetUsedInBytes() const noexcept {
    return m_state.m_sharedOffset == nullptr ? 0 : m_state.m_sharedOffset->load(std::memory_order_relaxed);
  }

  /** @brief Returns the most bytes any frame has taken so far, which shows how large capacityPerFrameInBytes has to be. */
  [[nodiscard]] std::size_t GetPeakUsedInBytes() const noexcept { return (std::max)(m_state.m_peakUsed, GetUsedInBytes()); }

  /** @brief Checks whether memory handed out during frameNumber is still valid. */
  [[nodiscard]] bool IsFrameAlive(std::uint64_t frameNumber) const noexcept {
    return frameNumber <= m_state.m_frameNumber && m_state.m_frameNumber - frameNumber < m_state.m_framesInFlight;
  }

 private:
  // Apple ARM64 cores have 128-byte cache lines, so ARM64 keeps the shared cursor and every slot 128 bytes apart.
  static constexpr std::size_t CACHE_LINE_SIZE = OPENETS_ARCH_ARM64 ? 128 : 64;
  // Reservations from the shared cursor are whole 16-byte steps, so two threads never share an AddressSanitizer granule.
  static constexpr std::size_t GRANULE = 16;

  // Every slot fills a whole cache line, so threads bumping their own cursors never write to the same line.
  struct ThreadSlot {
    std::byte* m_cursor = nullptr;
    std::byte* m_end = nullptr;
    std::array<std::byte, CACHE_LINE_SIZE - 2 * sizeof(std::byte*)> m_padding{};
  };

  struct State {
    std::atomic<std::size_t>* m_sharedOffset = nullptr;
    ThreadSlot* m_slots = nullptr;
    std::byte* m_region = nullptr;
    std::size_t m_slotCount = 0;
    std::size_t m_capacityPerFrame = 0;
    std::size_t m_chunkSize = 0;
    std::size_t m_framesInFlight = 0;
    std::size_t m_regionIndex = 0;
    std::uint64_t m_frameNumber = 0;
    std::size_t m_peakUsed = 0;
  };

  FrameAllocator(AlignedAllocation storage, std::size_t capacityPerFrame, std::size_t framesInFlight, std::size_t threadSlotCount) noexcept;

  [[nodiscard]] static std::byte* TryTakeFromSlot(ThreadSlot& slot, std::size_t sizeInBytes, std::size_t alignment) noexcept {
    const std::size_t padding = static_cast<std::size_t>(std::uintptr_t{0} - reinterpret_cast<std::uintptr_t>(slot.m_cursor)) & (alignment - 1);
    const std::size_t remaining = static_cast<std::size_t>(slot.m_end - slot.m_cursor);
    if (padding > remaining || sizeInBytes > remaining - padding) {
      return nullptr;
    }
    std::byte* const result = slot.m_cursor + padding;
    slot.m_cursor = result + sizeInBytes;
    return result;
  }

  [[nodiscard]] std::byte* TryReserveShared(std::size_t sizeInBytes, std::size_t alignment) noexcept;

  [[nodiscard]] std::byte* TryAllocateSlow(std::size_t sizeInBytes, std::size_t alignment) noexcept;

  AlignedAllocation m_storage;
  State m_state;
};

}  // namespace openETS::Engine::Core::Memory