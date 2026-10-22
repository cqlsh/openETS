#pragma once

#include "Engine/Core/Bit.hpp"

#include <type_traits>

/**
 * @brief Lets two values of a flag enum combine with | into EnumFlags.
 * @details Use it once in the namespace of the enum, so argument-dependent lookup finds the operator.
 */
#define OPENETS_ENUM_FLAGS(Enum) \
  [[nodiscard]] constexpr ::openETS::Engine::Core::EnumFlags<Enum> operator|(Enum left, Enum right) noexcept { \
    return ::openETS::Engine::Core::EnumFlags<Enum>(left) | ::openETS::Engine::Core::EnumFlags<Enum>(right); \
  } \
  static_assert(true, "OPENETS_ENUM_FLAGS needs a trailing semicolon.")

namespace openETS::Engine::Core {

/** @brief Accepts scoped enums with an unsigned underlying type, the only kind EnumFlags combines. */
template <typename Enum>
concept FlagEnum = std::is_enum_v<Enum> && !std::is_convertible_v<Enum, std::underlying_type_t<Enum>> && UnsignedInteger<std::underlying_type_t<Enum>>;

/**
 * @brief Combines values of a scoped enum as type-safe bit flags.
 * @details Has no complement operator, so no operation can switch on bits that the enum does not define.
 *          Bits from files or the network enter only through TryFromBits.
 *          Inlined, it compiles to the same machine code as hand-written masks; MSVC returns it through memory from calls that are not inlined.
 */
template <FlagEnum Enum>
class EnumFlags {
 public:
  using Bits = std::underlying_type_t<Enum>;

  constexpr EnumFlags() noexcept = default;

  /** @brief Wraps a single flag, so enum values convert implicitly where flags are expected. */
  constexpr EnumFlags(Enum flag) noexcept : m_bits(static_cast<Bits>(flag)) {}

  /**
   * @brief Accepts raw bits only if every set bit belongs to allowed.
   * @return False if bits contains a flag outside allowed; outFlags remains unchanged.
   */
  [[nodiscard]] static constexpr bool TryFromBits(Bits bits, EnumFlags allowed, EnumFlags& outFlags) noexcept {
    if ((bits & static_cast<Bits>(~allowed.m_bits)) != 0) {
      return false;
    }
    outFlags.m_bits = bits;
    return true;
  }

  /** @brief Switches on every flag in flags. */
  constexpr void Set(EnumFlags flags) noexcept { m_bits = static_cast<Bits>(m_bits | flags.m_bits); }

  /** @brief Switches off every flag in flags. */
  constexpr void Clear(EnumFlags flags) noexcept { m_bits = static_cast<Bits>(m_bits & static_cast<Bits>(~flags.m_bits)); }

  /** @brief Checks whether every flag in flags is set. An empty set returns true. */
  [[nodiscard]] constexpr bool HasAll(EnumFlags flags) const noexcept { return (m_bits & flags.m_bits) == flags.m_bits; }

  /** @brief Checks whether at least one flag in flags is set. An empty set returns false. */
  [[nodiscard]] constexpr bool HasAny(EnumFlags flags) const noexcept { return (m_bits & flags.m_bits) != 0; }

  /** @brief Checks whether no flag is set. */
  [[nodiscard]] constexpr bool IsEmpty() const noexcept { return m_bits == 0; }

  /** @brief Returns the raw bits, for example to store them. */
  [[nodiscard]] constexpr Bits GetBits() const noexcept { return m_bits; }

  [[nodiscard]] friend constexpr EnumFlags operator|(EnumFlags left, EnumFlags right) noexcept {
    return FromValidBits(static_cast<Bits>(left.m_bits | right.m_bits));
  }

  [[nodiscard]] friend constexpr EnumFlags operator&(EnumFlags left, EnumFlags right) noexcept {
    return FromValidBits(static_cast<Bits>(left.m_bits & right.m_bits));
  }

  [[nodiscard]] friend constexpr EnumFlags operator^(EnumFlags left, EnumFlags right) noexcept {
    return FromValidBits(static_cast<Bits>(left.m_bits ^ right.m_bits));
  }

  [[nodiscard]] friend constexpr bool operator==(EnumFlags left, EnumFlags right) noexcept = default;

  constexpr EnumFlags& operator|=(EnumFlags flags) noexcept { return *this = *this | flags; }
  constexpr EnumFlags& operator&=(EnumFlags flags) noexcept { return *this = *this & flags; }
  constexpr EnumFlags& operator^=(EnumFlags flags) noexcept { return *this = *this ^ flags; }

 private:
  // Only used on results of |, & and ^, which cannot contain bits that neither operand had.
  [[nodiscard]] static constexpr EnumFlags FromValidBits(Bits bits) noexcept {
    EnumFlags flags;
    flags.m_bits = bits;
    return flags;
  }

  Bits m_bits = 0;
};

}  // namespace openETS::Engine::Core