#pragma once

#include "Engine/Core/Config.hpp"

#include <type_traits>
#include <utility>

namespace openETS::Engine::Core {

/**
 * @brief Runs a cleanup callback when the guard leaves its scope, unless it was released.
 * @details Stores the callback by value, so captured references must outlive the guard.
 *          The callback must be noexcept because it runs inside a destructor. Move-only.
 */
template <typename Function>
requires std::is_object_v<Function> && std::is_same_v<Function, std::decay_t<Function>> &&
    std::is_nothrow_invocable_v<Function&> && std::is_nothrow_move_constructible_v<Function> && std::is_nothrow_destructible_v<Function>
class [[nodiscard]] ScopeExit {
 public:
  /** @brief Takes over the callback that runs on destruction. */
  explicit ScopeExit(Function function) noexcept : m_function(std::move(function)) {}

  ~ScopeExit() noexcept {
    if (m_isActive) {
      static_cast<void>(m_function());
    }
  }

  ScopeExit(const ScopeExit&) = delete;
  ScopeExit& operator=(const ScopeExit&) = delete;

  /** @brief Transfers the pending cleanup and deactivates the source. */
  ScopeExit(ScopeExit&& other) noexcept : m_function(std::move(other.m_function)), m_isActive(std::exchange(other.m_isActive, false)) {}

  ScopeExit& operator=(ScopeExit&&) = delete;

  /** @brief Cancels the cleanup; the callback is destroyed with the guard without running. */
  void Release() noexcept { m_isActive = false; }

  /** @brief Checks whether destruction will run the callback. */
  [[nodiscard]] bool IsActive() const noexcept { return m_isActive; }

 private:
  Function m_function;
  bool m_isActive = true;
};

template <typename Function>
ScopeExit(Function) -> ScopeExit<Function>;

}  // namespace openETS::Engine::Core