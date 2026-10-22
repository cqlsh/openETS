#pragma once

#include "Engine/Core/Assert.hpp"

#include <cstdint>
#include <type_traits>

#if OPENETS_COMPILER_MSVC
#include <intrin.h>
#endif

namespace openETS::Engine::Core::Numeric {

/**
 * @brief Divides 32-bit values by a divisor fixed at construction with one multiplication instead of a division instruction.
 * @details Exact for every 32-bit value and divisor, following Lemire's reciprocal method with a 64-bit reciprocal.
 *          A divisor of one is handled without a branch. In a block pool it beat the division instruction with MSVC and GCC.
 */
class ReciprocalDivider {
 public:
  /** @brief Prepares division by divisor; zero terminates in every build. */
  explicit constexpr ReciprocalDivider(std::uint32_t divisor) noexcept
      : m_reciprocal(divisor > 1 ? ~std::uint64_t{0} / divisor + 1 : 0), m_divisor(divisor), m_identityMask(divisor == 1 ? ~std::uint32_t{0} : 0) {
    OPENETS_CHECK(divisor != 0, "ReciprocalDivider needs a divisor above zero.");
  }

  /** @brief Returns value divided by the divisor, rounded down. */
  [[nodiscard]] constexpr std::uint32_t Divide(std::uint32_t value) const noexcept {
    return static_cast<std::uint32_t>(MultiplyHigh(m_reciprocal, value)) + (value & m_identityMask);
  }

  /** @brief Returns the divisor this object divides by. */
  [[nodiscard]] constexpr std::uint32_t GetDivisor() const noexcept { return m_divisor; }

 private:
  [[nodiscard]] static constexpr std::uint64_t MultiplyHigh(std::uint64_t left, std::uint64_t right) noexcept {
#if OPENETS_COMPILER_MSVC
    if (!std::is_constant_evaluated()) {
      return ::__umulh(left, right);
    }
    const std::uint64_t leftLow = left & 0xFFFFFFFFu;
    const std::uint64_t leftHigh = left >> 32;
    const std::uint64_t rightLow = right & 0xFFFFFFFFu;
    const std::uint64_t rightHigh = right >> 32;
    const std::uint64_t middle = leftHigh * rightLow + ((leftLow * rightLow) >> 32);
    return leftHigh * rightHigh + (middle >> 32) + ((leftLow * rightHigh + (middle & 0xFFFFFFFFu)) >> 32);
#else
    // __extension__ keeps -Wpedantic quiet about the 128-bit type that GCC and Clang provide.
    __extension__ typedef unsigned __int128 WideUnsigned;
    return static_cast<std::uint64_t>((static_cast<WideUnsigned>(left) * right) >> 64);
#endif
  }

  std::uint64_t m_reciprocal;
  std::uint32_t m_divisor;
  std::uint32_t m_identityMask;
};

}  // namespace openETS::Engine::Core::Numeric