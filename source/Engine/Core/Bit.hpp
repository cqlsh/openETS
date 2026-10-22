#pragma once

#include "Engine/Core/Assert.hpp"

#include <bit>
#include <concepts>
#include <limits>
#include <type_traits>

namespace openETS::Engine::Core {

/** @brief Accepts unsigned integer types and rejects bool and character types. */
template <typename Integer>
concept UnsignedInteger = std::unsigned_integral<Integer> && !std::is_same_v<std::remove_cv_t<Integer>, bool> &&
    !std::is_same_v<std::remove_cv_t<Integer>, char> && !std::is_same_v<std::remove_cv_t<Integer>, wchar_t> &&
    !std::is_same_v<std::remove_cv_t<Integer>, char8_t> && !std::is_same_v<std::remove_cv_t<Integer>, char16_t> &&
    !std::is_same_v<std::remove_cv_t<Integer>, char32_t>;

/** @brief Number of value bits in an unsigned integer type. */
template <UnsignedInteger Integer>
inline constexpr unsigned int BIT_COUNT = static_cast<unsigned int>(std::numeric_limits<Integer>::digits);

/** @brief Checks whether value is a power of two. Zero returns false. */
template <UnsignedInteger Integer>
[[nodiscard]] constexpr bool IsPowerOfTwo(Integer value) noexcept {
  return std::has_single_bit(value);
}

/**
 * @brief Rounds value up to the next power of two. Zero rounds up to one.
 * @return False if the result does not fit into the type; outValue remains unchanged.
 */
template <UnsignedInteger Integer>
[[nodiscard]] constexpr bool TryCeilPowerOfTwo(Integer value, Integer& outValue) noexcept {
  constexpr Integer HIGHEST_POWER = static_cast<Integer>(Integer{1} << (BIT_COUNT<Integer> - 1u));
  if (value > HIGHEST_POWER) {
    return false;
  }
  outValue = std::bit_ceil(value);
  return true;
}

/**
 * @brief Returns a mask with the lowest count bits set.
 * @details Counts of BIT_COUNT or more return all bits instead of shifting out of range. Branch-free.
 */
template <UnsignedInteger Integer>
[[nodiscard]] constexpr Integer LowBitMask(unsigned int count) noexcept {
  const Integer full = static_cast<Integer>(Integer{0} - static_cast<Integer>(count >= BIT_COUNT<Integer>));
  return static_cast<Integer>(((Integer{1} << (count & (BIT_COUNT<Integer> - 1u))) - 1u) | full);
}

/**
 * @brief Returns the field of compile-time width Count that starts at bit offset.
 * @details The field must lie inside the value; debug builds assert it.
 *          Release builds wrap the offset to the type width, so no input causes undefined behavior.
 *          Measured as fast as the unchecked shift-and-mask expression.
 */
template <unsigned int Count, UnsignedInteger Integer>
requires (Count >= 1 && Count <= BIT_COUNT<Integer>)
[[nodiscard]] constexpr Integer ExtractBits(Integer value, unsigned int offset) noexcept {
  OPENETS_ASSERT(offset <= BIT_COUNT<Integer> - Count, "The bit field reaches beyond the integer.");
  constexpr Integer MASK = LowBitMask<Integer>(Count);
  return static_cast<Integer>((value >> (offset & (BIT_COUNT<Integer> - 1u))) & MASK);
}

/**
 * @brief Returns count bits of value that start at bit offset, for widths only known at runtime.
 * @details The offset must be below BIT_COUNT; debug builds assert it, release builds wrap it.
 *          Counts of BIT_COUNT or more return every bit above the offset. No input causes undefined behavior.
 *          Of the safe variants measured, this branch-free one was fastest with MSVC and on par with GCC.
 */
template <UnsignedInteger Integer>
[[nodiscard]] constexpr Integer ExtractBits(Integer value, unsigned int offset, unsigned int count) noexcept {
  OPENETS_ASSERT(offset < BIT_COUNT<Integer>, "The bit offset lies beyond the integer.");
  return static_cast<Integer>((value >> (offset & (BIT_COUNT<Integer> - 1u))) & LowBitMask<Integer>(count));
}

}  // namespace openETS::Engine::Core