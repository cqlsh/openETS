#pragma once

#include "Engine/Core/Assert.hpp"
#include "Engine/Core/Memory/AlignedAllocation.hpp"
#include "Engine/Core/Memory/Alignment.hpp"
#include "Engine/Core/Memory/Poisoning.hpp"
#include "Engine/Core/Numeric/CheckedArithmetic.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>

namespace openETS::Engine::Core::Memory {

/**
 * @brief Hands out memory from one owned block by moving a cursor forward and frees everything at once.
 * @details Meant for data that lives for one frame or one task: no allocation touches the heap and nothing is freed on its own.
 *          Runs no destructors, so typed arrays are limited to trivially destructible types. Not synchronized. Move-only.
 *          With AddressSanitizer, memory that is not handed out is poisoned, so access after Reset or RewindTo is reported.
 *          Of four bump strategies measured, this offset form had the smallest worst case across MSVC and GCC.
 */
class LinearArena {
 public:
  /** @brief Remembers the cursor, so everything allocated afterwards can be given back with RewindTo. */
  struct Marker {
    std::size_t m_offset = 0;
  };

  /** @brief Takes over storage as backing memory; the arena starts empty. An empty storage makes every request fail. */
  explicit LinearArena(AlignedAllocation storage) noexcept : m_storage(std::move(storage)) {
    PoisonMemory(m_storage.GetBytes().data(), m_storage.GetBytes().size());
  }

  ~LinearArena() noexcept { UnpoisonMemory(m_storage.GetBytes().data(), m_storage.GetBytes().size()); }

  LinearArena(const LinearArena&) = delete;
  LinearArena& operator=(const LinearArena&) = delete;

  /** @brief Takes the storage and cursor from other, which is left without storage. */
  LinearArena(LinearArena&& other) noexcept : m_storage(std::move(other.m_storage)), m_offset(std::exchange(other.m_offset, 0)) {}

  /** @brief Returns the current storage to the heap, then takes the storage and cursor from other. */
  LinearArena& operator=(LinearArena&& other) noexcept {
    if (&other != this) {
      UnpoisonMemory(m_storage.GetBytes().data(), m_storage.GetBytes().size());
      m_storage = std::move(other.m_storage);
      m_offset = std::exchange(other.m_offset, 0);
    }
    return *this;
  }

  /**
   * @brief Returns sizeInBytes uninitialized bytes aligned to alignment.
   * @return Null for zero bytes, an invalid alignment or too little remaining space; the arena remains unchanged.
   */
  [[nodiscard]] std::byte* TryAllocate(std::size_t sizeInBytes, std::size_t alignment) noexcept {
    if (sizeInBytes == 0 || !IsValidAlignment(alignment)) {
      return nullptr;
    }
    std::byte* const base = m_storage.GetBytes().data();
    const std::size_t remaining = m_storage.GetBytes().size() - m_offset;
    const std::uintptr_t cursor = reinterpret_cast<std::uintptr_t>(base) + m_offset;
    const std::size_t padding = static_cast<std::size_t>(std::uintptr_t{0} - cursor) & (alignment - 1);
    if (padding > remaining || sizeInBytes > remaining - padding) {
      return nullptr;
    }
    std::byte* const result = base + m_offset + padding;
    m_offset += padding + sizeInBytes;
    UnpoisonMemory(result, sizeInBytes);
    return result;
  }

  /**
   * @brief Reserves count uninitialized elements of Type. Zero elements succeed with an empty span.
   * @return False if the byte size overflows or does not fit; outArray and the arena remain unchanged.
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

  /** @brief Returns the current cursor for a later RewindTo. */
  [[nodiscard]] Marker GetMarker() const noexcept { return Marker{m_offset}; }

  /**
   * @brief Gives back everything allocated after marker; pointers into that part must no longer be used.
   * @details A marker ahead of the cursor, for example one taken before a Reset, terminates in every build.
   */
  void RewindTo(Marker marker) noexcept {
    OPENETS_CHECK(marker.m_offset <= m_offset, "The marker lies ahead of the arena cursor.");
    PoisonMemory(m_storage.GetBytes().data() + marker.m_offset, m_offset - marker.m_offset);
    m_offset = marker.m_offset;
  }

  /** @brief Gives back every allocation while keeping the storage. */
  void Reset() noexcept { RewindTo(Marker{}); }

  /** @brief Returns the size of the backing storage in bytes. */
  [[nodiscard]] std::size_t GetCapacityInBytes() const noexcept { return m_storage.GetBytes().size(); }

  /** @brief Returns the bytes in use, including alignment padding. */
  [[nodiscard]] std::size_t GetUsedInBytes() const noexcept { return m_offset; }

  /** @brief Returns the bytes after the cursor; a request may need some of them as padding. */
  [[nodiscard]] std::size_t GetRemainingInBytes() const noexcept { return m_storage.GetBytes().size() - m_offset; }

 private:
  AlignedAllocation m_storage;
  std::size_t m_offset = 0;
};

}  // namespace openETS::Engine::Core::Memory