#pragma once

#include "Engine/Core/Memory/Alignment.hpp"
#include "Engine/Core/Result.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <span>
#include <utility>

namespace openETS::Engine::Core::Memory {

/** @brief Explains why a memory request was refused. */
enum class AllocationError : std::uint8_t {
  InvalidSize,
  InvalidAlignment,
  OutOfMemory
};

/**
 * @brief Owns an uninitialized heap block with a chosen alignment and hands it out only as a bounded byte span.
 * @details Created only through TryCreate, so a non-empty block always satisfies its size and alignment. Move-only.
 *          Replacing a block by move assignment allocates the new one first, so a failed request never loses the old one.
 *          Plain and aligned operator new measured as fast as the C runtime on Windows and Linux; page-aligned pages took
 *          about 1.5 microseconds each, so large page-aligned memory belongs to the page allocator.
 */
class AlignedAllocation {
 public:
  /** @brief Alignment that plain operator new already guarantees without the aligned overload. */
  static constexpr std::size_t DEFAULT_ALIGNMENT = __STDCPP_DEFAULT_NEW_ALIGNMENT__;

  /**
   * @brief Allocates sizeInBytes uninitialized bytes aligned to alignment.
   * @details Sizes and alignments above PTRDIFF_MAX are refused because pointer arithmetic on them is undefined.
   *          The return type is deduced because Result cannot check its requirements while this class is still incomplete.
   * @return Result holding the new block, or the AllocationError that explains the refusal.
   */
  [[nodiscard]] static auto TryCreate(std::size_t sizeInBytes, std::size_t alignment = DEFAULT_ALIGNMENT) noexcept {
    constexpr std::size_t LARGEST = static_cast<std::size_t>((std::numeric_limits<std::ptrdiff_t>::max)());
    if (sizeInBytes == 0 || sizeInBytes > LARGEST) {
      return Result<AlignedAllocation, AllocationError>::Failure(AllocationError::InvalidSize);
    }
    if (alignment > LARGEST || !IsValidAlignment(alignment)) {
      return Result<AlignedAllocation, AllocationError>::Failure(AllocationError::InvalidAlignment);
    }
    void* const memory = alignment <= DEFAULT_ALIGNMENT
        ? ::operator new(sizeInBytes, std::nothrow)
        : ::operator new(sizeInBytes, std::align_val_t{alignment}, std::nothrow);
    if (memory == nullptr) {
      return Result<AlignedAllocation, AllocationError>::Failure(AllocationError::OutOfMemory);
    }
    return Result<AlignedAllocation, AllocationError>::Success(AlignedAllocation(static_cast<std::byte*>(memory), sizeInBytes, alignment));
  }

  /** @brief Creates an empty object that owns nothing, for example to be assigned later. */
  AlignedAllocation() noexcept = default;

  ~AlignedAllocation() noexcept { Release(); }

  AlignedAllocation(const AlignedAllocation&) = delete;
  AlignedAllocation& operator=(const AlignedAllocation&) = delete;

  /** @brief Takes the block from other, which becomes empty. */
  AlignedAllocation(AlignedAllocation&& other) noexcept
      : m_bytes(std::exchange(other.m_bytes, nullptr)), m_size(std::exchange(other.m_size, 0)), m_alignment(std::exchange(other.m_alignment, 0)) {}

  /** @brief Frees the current block, then takes the block from other, which becomes empty. */
  AlignedAllocation& operator=(AlignedAllocation&& other) noexcept {
    if (&other != this) {
      Release();
      m_bytes = std::exchange(other.m_bytes, nullptr);
      m_size = std::exchange(other.m_size, 0);
      m_alignment = std::exchange(other.m_alignment, 0);
    }
    return *this;
  }

  /** @brief Frees the block; afterwards the object is empty. Calling it on an empty object does nothing. */
  void Release() noexcept {
    if (m_bytes == nullptr) {
      return;
    }
    if (m_alignment <= DEFAULT_ALIGNMENT) {
      ::operator delete(m_bytes);
    } else {
      ::operator delete(m_bytes, std::align_val_t{m_alignment});
    }
    m_bytes = nullptr;
    m_size = 0;
    m_alignment = 0;
  }

  /** @brief Returns the whole block as a borrowed span; empty when nothing is owned. */
  [[nodiscard]] std::span<std::byte> GetBytes() noexcept { return {m_bytes, m_size}; }
  [[nodiscard]] std::span<const std::byte> GetBytes() const noexcept { return {m_bytes, m_size}; }

  /** @brief Returns the alignment the block was created with, or zero when nothing is owned. */
  [[nodiscard]] std::size_t GetAlignment() const noexcept { return m_alignment; }

  /** @brief Checks whether nothing is owned. */
  [[nodiscard]] bool IsEmpty() const noexcept { return m_bytes == nullptr; }

 private:
  AlignedAllocation(std::byte* bytes, std::size_t size, std::size_t alignment) noexcept : m_bytes(bytes), m_size(size), m_alignment(alignment) {}

  std::byte* m_bytes = nullptr;
  std::size_t m_size = 0;
  std::size_t m_alignment = 0;
};

}  // namespace openETS::Engine::Core::Memory