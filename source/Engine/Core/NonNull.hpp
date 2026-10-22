#pragma once

#include "Engine/Core/Assert.hpp"

#include <compare>
#include <cstddef>
#include <type_traits>

namespace openETS::Engine::Core {

/**
 * @brief Borrows a pointer that is never null.
 * @details Checks once on construction in every build; afterwards no access needs a null check.
 *          Does not own the object, so the object must outlive every NonNull that refers to it.
 *          Offers no default state, no pointer arithmetic and no implicit conversion back to a raw pointer.
 */
template <typename Type>
requires (!std::is_reference_v<Type>)
class NonNull {
 public:
  /** @brief Wraps pointer; a null pointer terminates in every build. */
  explicit NonNull(Type* pointer) noexcept : m_pointer(pointer) {
    OPENETS_CHECK(pointer != nullptr, "NonNull received a null pointer.");
  }

  NonNull(std::nullptr_t) = delete;

  /** @brief Converts from a compatible NonNull, for example to a base class or to const, without a new check. */
  template <typename Other>
  requires (!std::is_same_v<Other, Type>) && std::is_convertible_v<Other*, Type*>
  NonNull(NonNull<Other> other) noexcept : m_pointer(other.Get()) {}

  /** @brief Returns the borrowed pointer, which is never null. */
  [[nodiscard]] Type* Get() const noexcept { return m_pointer; }

  [[nodiscard]] std::add_lvalue_reference_t<Type> operator*() const noexcept requires (!std::is_void_v<Type>) { return *m_pointer; }
  [[nodiscard]] Type* operator->() const noexcept { return m_pointer; }

  [[nodiscard]] friend bool operator==(NonNull left, NonNull right) noexcept = default;

  /** @brief Orders by address with the total order of std::compare_three_way, so unrelated pointers sort reliably. */
  [[nodiscard]] friend std::strong_ordering operator<=>(NonNull left, NonNull right) noexcept {
    return std::compare_three_way{}(left.m_pointer, right.m_pointer);
  }

 private:
  Type* m_pointer;
};

}  // namespace openETS::Engine::Core