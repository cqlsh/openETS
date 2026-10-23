#include "Engine/Core/Memory/FrameAllocator.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <utility>

namespace openETS::Engine::Core::Memory {

namespace Detail {

constinit thread_local std::size_t threadFrameSlot = 0;

}  // namespace Detail

namespace {

// With a 64th of a small frame per chunk, 16 threads hold back at most a quarter of the frame in partly used chunks.
constexpr std::size_t CHUNKS_PER_SMALL_FRAME = 64;

// Marks a thread that found no free slot number or has already given its number back; it allocates from the shared cursor.
constexpr std::size_t NO_THREAD_SLOT = ~std::size_t{0};

constinit std::array<std::atomic<std::uint64_t>, FrameAllocator::MAX_THREAD_SLOT_COUNT / 64> usedThreadSlots{};

// Gives the slot number back when its thread ends, so numbers stay small even when threads come and go.
struct ThreadSlotRelease {
  std::size_t m_slot = 0;

  ~ThreadSlotRelease() noexcept {
    if (m_slot != 0) {
      const std::size_t index = m_slot - 1;
      usedThreadSlots[index / 64].fetch_and(~(std::uint64_t{1} << (index % 64)), std::memory_order_release);
    }
    // Destructors that run later on this thread must not touch a slot that another thread may already own.
    Detail::threadFrameSlot = NO_THREAD_SLOT;
  }
};

thread_local ThreadSlotRelease threadSlotRelease;

// Takes the lowest free slot number, so threads that start later reuse the slots of threads that have ended.
[[nodiscard]] std::size_t AcquireThreadSlot() noexcept {
  for (std::size_t word = 0; word < usedThreadSlots.size(); ++word) {
    std::uint64_t used = usedThreadSlots[word].load(std::memory_order_relaxed);
    while (used != ~std::uint64_t{0}) {
      const std::uint64_t lowestFree = ~used & (used + 1);
      if (usedThreadSlots[word].compare_exchange_weak(used, used | lowestFree, std::memory_order_acquire, std::memory_order_relaxed)) {
        threadSlotRelease.m_slot = word * 64 + static_cast<std::size_t>(std::countr_zero(lowestFree)) + 1;
        return threadSlotRelease.m_slot;
      }
    }
  }
  return NO_THREAD_SLOT;
}

}  // namespace

void FrameAllocator::BeginFrame() noexcept {
  if (m_storage.IsEmpty()) {
    return;
  }
  m_state.m_peakUsed = (std::max)(m_state.m_peakUsed, m_state.m_sharedOffset->load(std::memory_order_relaxed));
  m_state.m_regionIndex = m_state.m_regionIndex + 1 == m_state.m_framesInFlight ? 0 : m_state.m_regionIndex + 1;
  m_state.m_region = m_storage.GetBytes().data() + (m_state.m_slotCount + 1) * CACHE_LINE_SIZE + m_state.m_regionIndex * m_state.m_capacityPerFrame;
  m_state.m_sharedOffset->store(0, std::memory_order_relaxed);
  for (std::size_t slot = 0; slot < m_state.m_slotCount; ++slot) {
    m_state.m_slots[slot].m_cursor = nullptr;
    m_state.m_slots[slot].m_end = nullptr;
  }
  ++m_state.m_frameNumber;
  PoisonMemory(m_state.m_region, m_state.m_capacityPerFrame);
}

FrameAllocator::FrameAllocator(AlignedAllocation storage, std::size_t capacityPerFrame, std::size_t framesInFlight, std::size_t threadSlotCount) noexcept
    : m_storage(std::move(storage)) {
  std::byte* const bytes = m_storage.GetBytes().data();
  m_state.m_sharedOffset = ::new (bytes) std::atomic<std::size_t>(0);
  m_state.m_slots = reinterpret_cast<ThreadSlot*>(bytes + CACHE_LINE_SIZE);
  std::uninitialized_value_construct_n(m_state.m_slots, threadSlotCount);
  m_state.m_region = bytes + (threadSlotCount + 1) * CACHE_LINE_SIZE;
  m_state.m_slotCount = threadSlotCount;
  m_state.m_capacityPerFrame = capacityPerFrame;
  m_state.m_chunkSize = (std::min)(MAX_CHUNK_SIZE_IN_BYTES, capacityPerFrame / CHUNKS_PER_SMALL_FRAME) & ~(CACHE_LINE_SIZE - 1);
  m_state.m_framesInFlight = framesInFlight;
  PoisonMemory(m_state.m_region, capacityPerFrame * framesInFlight);
}

std::byte* FrameAllocator::TryReserveShared(std::size_t sizeInBytes, std::size_t alignment) noexcept {
  // An allocator without storage has zero capacity, so this also keeps it away from its missing shared cursor.
  if (sizeInBytes > m_state.m_capacityPerFrame) {
    return nullptr;
  }
  const std::size_t rounded = (sizeInBytes + GRANULE - 1) & ~(GRANULE - 1);
  std::size_t offset = m_state.m_sharedOffset->load(std::memory_order_relaxed);
  for (;;) {
    const std::uintptr_t cursor = reinterpret_cast<std::uintptr_t>(m_state.m_region) + offset;
    const std::size_t padding = static_cast<std::size_t>(std::uintptr_t{0} - cursor) & (alignment - 1);
    const std::size_t remaining = m_state.m_capacityPerFrame - offset;
    if (padding > remaining || rounded > remaining - padding) {
      return nullptr;
    }
    if (m_state.m_sharedOffset->compare_exchange_weak(offset, offset + padding + rounded, std::memory_order_relaxed)) {
      return m_state.m_region + offset + padding;
    }
  }
}

std::byte* FrameAllocator::TryAllocateSlow(std::size_t sizeInBytes, std::size_t alignment) noexcept {
  if (Detail::threadFrameSlot == 0) {
    Detail::threadFrameSlot = AcquireThreadSlot();
  }
  const std::size_t slot = Detail::threadFrameSlot - 1;
  std::byte* result = nullptr;
  if (slot >= m_state.m_slotCount || sizeInBytes > m_state.m_chunkSize / 4 || alignment > m_state.m_chunkSize / 4) {
    result = TryReserveShared(sizeInBytes, alignment);
  } else {
    ThreadSlot& threadSlot = m_state.m_slots[slot];
    result = TryTakeFromSlot(threadSlot, sizeInBytes, alignment);
    if (result == nullptr) {
      std::byte* const chunk = TryReserveShared(m_state.m_chunkSize, CACHE_LINE_SIZE);
      if (chunk == nullptr) {
        result = TryReserveShared(sizeInBytes, alignment);
      } else {
        threadSlot.m_cursor = chunk;
        threadSlot.m_end = chunk + m_state.m_chunkSize;
        result = TryTakeFromSlot(threadSlot, sizeInBytes, alignment);
      }
    }
  }
  if (result != nullptr) {
    UnpoisonMemory(result, sizeInBytes);
  }
  return result;
}

}  // namespace openETS::Engine::Core::Memory