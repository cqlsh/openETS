#pragma once

#include "Engine/Core/Assert.hpp"
#include "Engine/Core/Memory/AlignedAllocation.hpp"
#include "Engine/Core/Memory/Alignment.hpp"
#include "Engine/Core/Memory/Poisoning.hpp"
#include "Engine/Core/Numeric/CheckedArithmetic.hpp"
#include "Engine/Core/Numeric/ReciprocalDivider.hpp"
#include "Engine/Core/Result.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace openETS::Engine::Core::Memory {

/**
 * @brief Hands out equally sized blocks one at a time and takes each back individually in constant time.
 * @details All bookkeeping lives outside the blocks: a stack of free indices and an in-use bitmap. A write after free can
 *          therefore never corrupt the pool, and freeing a foreign, interior or already free pointer terminates in every build.
 *          Free blocks are poisoned under AddressSanitizer. Not synchronized. Move-only. All blocks together stay below 4 GiB.
 *          A free list threaded through the blocks measured about 25% faster, but use after free could corrupt it, so it lost.
 */
class FixedBlockPool {
 public:
  /**
   * @brief Creates blockCount blocks of blockSizeInBytes bytes, each aligned to alignment, all free.
   * @details The return type is deduced because Result cannot check its requirements while this class is still incomplete.
   * @return Result holding the pool, or InvalidSize, InvalidAlignment or OutOfMemory.
   */
  [[nodiscard]] static auto TryCreate(std::size_t blockSizeInBytes, std::size_t blockCount,
      std::size_t alignment = AlignedAllocation::DEFAULT_ALIGNMENT) noexcept {
    using Created = Result<FixedBlockPool, AllocationError>;
    constexpr std::size_t LARGEST_POOL = (std::numeric_limits<std::uint32_t>::max)();
    if (blockSizeInBytes == 0 || blockCount == 0 || blockCount > LARGEST_POOL) {
      return Created::Failure(AllocationError::InvalidSize);
    }
    if (!IsValidAlignment(alignment)) {
      return Created::Failure(AllocationError::InvalidAlignment);
    }
    std::size_t stride = 0;
    std::size_t poolSize = 0;
    if (!TryAlignUp(blockSizeInBytes, alignment, stride) || !Numeric::TryMultiply(stride, blockCount, poolSize) || poolSize > LARGEST_POOL) {
      return Created::Failure(AllocationError::InvalidSize);
    }
    const std::size_t bitmapSize = (blockCount + 63) / 64 * sizeof(std::uint64_t);
    auto blocks = AlignedAllocation::TryCreate(poolSize, alignment);
    if (blocks.HasError()) {
      return Created::Failure(blocks.GetError());
    }
    auto bookkeeping = AlignedAllocation::TryCreate(bitmapSize + blockCount * sizeof(std::uint32_t), alignof(std::uint64_t));
    if (bookkeeping.HasError()) {
      return Created::Failure(bookkeeping.GetError());
    }
    return Created::Success(FixedBlockPool(std::move(blocks).GetValue(), std::move(bookkeeping).GetValue(), blockSizeInBytes,
        static_cast<std::uint32_t>(stride), static_cast<std::uint32_t>(blockCount), bitmapSize));
  }

  ~FixedBlockPool() noexcept { UnpoisonMemory(m_blocks.GetBytes().data(), m_blocks.GetBytes().size()); }

  FixedBlockPool(const FixedBlockPool&) = delete;
  FixedBlockPool& operator=(const FixedBlockPool&) = delete;

  /** @brief Takes all blocks and bookkeeping from other, which is left without blocks. */
  FixedBlockPool(FixedBlockPool&& other) noexcept
      : m_blocks(std::move(other.m_blocks)),
        m_bookkeeping(std::move(other.m_bookkeeping)),
        m_inUse(std::exchange(other.m_inUse, nullptr)),
        m_freeIndices(std::exchange(other.m_freeIndices, nullptr)),
        m_strideDivider(other.m_strideDivider),
        m_blockSize(std::exchange(other.m_blockSize, 0)),
        m_blockCount(std::exchange(other.m_blockCount, 0)),
        m_freeCount(std::exchange(other.m_freeCount, 0)) {}

  /** @brief Returns the current blocks to the heap, then takes all blocks and bookkeeping from other. */
  FixedBlockPool& operator=(FixedBlockPool&& other) noexcept {
    if (&other != this) {
      UnpoisonMemory(m_blocks.GetBytes().data(), m_blocks.GetBytes().size());
      m_blocks = std::move(other.m_blocks);
      m_bookkeeping = std::move(other.m_bookkeeping);
      m_inUse = std::exchange(other.m_inUse, nullptr);
      m_freeIndices = std::exchange(other.m_freeIndices, nullptr);
      m_strideDivider = other.m_strideDivider;
      m_blockSize = std::exchange(other.m_blockSize, 0);
      m_blockCount = std::exchange(other.m_blockCount, 0);
      m_freeCount = std::exchange(other.m_freeCount, 0);
    }
    return *this;
  }

  /** @brief Returns an uninitialized free block, or nullptr when every block is in use. */
  [[nodiscard]] std::byte* TryAllocate() noexcept {
    if (m_freeCount == 0) {
      return nullptr;
    }
    const std::uint32_t index = m_freeIndices[--m_freeCount];
    m_inUse[index / 64] |= std::uint64_t{1} << (index % 64);
    std::byte* const block = m_blocks.GetBytes().data() + std::size_t{index} * m_strideDivider.GetDivisor();
    UnpoisonMemory(block, m_blockSize);
    return block;
  }

  /**
   * @brief Takes back a block that TryAllocate returned; it must no longer be used afterwards.
   * @details Null, foreign, interior and already free pointers terminate in every build.
   */
  void Free(const std::byte* block) noexcept {
    const std::uintptr_t offset = reinterpret_cast<std::uintptr_t>(block) - reinterpret_cast<std::uintptr_t>(m_blocks.GetBytes().data());
    OPENETS_CHECK(offset < m_blocks.GetBytes().size(), "The pointer does not belong to this pool.");
    const std::uint32_t index = m_strideDivider.Divide(static_cast<std::uint32_t>(offset));
    OPENETS_CHECK(std::uintptr_t{index} * m_strideDivider.GetDivisor() == offset, "The pointer does not start a block.");
    std::uint64_t& word = m_inUse[index / 64];
    const std::uint64_t bit = std::uint64_t{1} << (index % 64);
    OPENETS_CHECK((word & bit) != 0, "The block is already free.");
    word &= ~bit;
    m_freeIndices[m_freeCount++] = index;
    PoisonMemory(block, m_blockSize);
  }

  /** @brief Takes back every block at once; pointers to blocks must no longer be used afterwards. */
  void Reset() noexcept {
    for (std::uint32_t i = 0; i < m_blockCount; ++i) {
      m_freeIndices[i] = m_blockCount - 1 - i;
    }
    for (std::uint32_t word = 0; word < (m_blockCount + 63) / 64; ++word) {
      m_inUse[word] = 0;
    }
    m_freeCount = m_blockCount;
    PoisonMemory(m_blocks.GetBytes().data(), m_blocks.GetBytes().size());
  }

  /** @brief Returns the usable size of each block in bytes. */
  [[nodiscard]] std::size_t GetBlockSizeInBytes() const noexcept { return m_blockSize; }

  /** @brief Returns the distance between block starts, which includes padding for the alignment. */
  [[nodiscard]] std::size_t GetStrideInBytes() const noexcept { return m_blockCount == 0 ? 0 : m_strideDivider.GetDivisor(); }

  /** @brief Returns the total number of blocks. */
  [[nodiscard]] std::size_t GetBlockCount() const noexcept { return m_blockCount; }

  /** @brief Returns how many blocks are free. */
  [[nodiscard]] std::size_t GetFreeBlockCount() const noexcept { return m_freeCount; }

 private:
  FixedBlockPool(AlignedAllocation blocks, AlignedAllocation bookkeeping, std::size_t blockSize, std::uint32_t stride, std::uint32_t blockCount,
      std::size_t bitmapSize) noexcept
      : m_blocks(std::move(blocks)),
        m_bookkeeping(std::move(bookkeeping)),
        m_inUse(reinterpret_cast<std::uint64_t*>(m_bookkeeping.GetBytes().data())),
        m_freeIndices(reinterpret_cast<std::uint32_t*>(m_bookkeeping.GetBytes().data() + bitmapSize)),
        m_strideDivider(stride),
        m_blockSize(blockSize),
        m_blockCount(blockCount) {
    Reset();
  }

  AlignedAllocation m_blocks;
  AlignedAllocation m_bookkeeping;
  std::uint64_t* m_inUse = nullptr;
  std::uint32_t* m_freeIndices = nullptr;
  Numeric::ReciprocalDivider m_strideDivider;
  std::size_t m_blockSize = 0;
  std::uint32_t m_blockCount = 0;
  std::uint32_t m_freeCount = 0;
};

}  // namespace openETS::Engine::Core::Memory