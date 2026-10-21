#pragma once

#include "Engine/Core/Config.hpp"

#include <cstdint>
#include <source_location>

namespace openETS::Engine::Core {

/** @brief Names the macro that detected a broken invariant. */
enum class FailureKind : std::uint8_t {
  Assertion,
  Check,
  Unreachable
};

/**
 * @brief Describes a broken invariant at the moment it is reported.
 * @details All strings are static and stay valid until the process ends. The message is nullptr when none was given.
 */
struct AssertionFailure {
  FailureKind m_kind = FailureKind::Assertion;
  const char* m_expression = nullptr;
  const char* m_message = nullptr;
  std::source_location m_location;
};

/**
 * @brief Receives a failure before the process terminates, for example to flush logs or write a crash report.
 * @details Runs once, on the failing thread. It must not throw and should not rely on the heap.
 *          A failure inside the handler terminates the process immediately.
 */
using AssertionHandler = void (*)(const AssertionFailure& failure) noexcept;

/** @brief Installs the handler for later failures; nullptr keeps only the report on stderr. */
void SetAssertionHandler(AssertionHandler handler) noexcept;

namespace Detail {

/** @brief Reports a broken invariant, breaks into an attached debugger and terminates the process. */
[[noreturn]] OPENETS_NOINLINE void ReportFailure(FailureKind kind, const char* expression, const char* message = nullptr,
    std::source_location location = std::source_location::current()) noexcept;

}  // namespace Detail

}  // namespace openETS::Engine::Core

// MSVC warns about constant conditions, but asserting one is legitimate, for example a sizeof check inside a template.
#if OPENETS_COMPILER_MSVC
#define OPENETS_DETAIL_CONSTANT_CONDITION_BEGIN __pragma(warning(push)) __pragma(warning(disable : 4127))
#define OPENETS_DETAIL_CONSTANT_CONDITION_END __pragma(warning(pop))
#else
#define OPENETS_DETAIL_CONSTANT_CONDITION_BEGIN
#define OPENETS_DETAIL_CONSTANT_CONDITION_END
#endif

/**
 * @brief Checks an invariant in every build, with an optional static message.
 * @details Meant for guarantees that protect memory safety, such as bounds and sizes.
 *          The passing path costs a compare and a predicted branch; the report stays out of line.
 *          Tight loops measured up to 30% slower with a check per element, so check ranges once before the loop.
 */
#define OPENETS_CHECK(condition, ...) \
  do { \
    OPENETS_DETAIL_CONSTANT_CONDITION_BEGIN \
    if (!static_cast<bool>(condition)) [[unlikely]] { \
      ::openETS::Engine::Core::Detail::ReportFailure(::openETS::Engine::Core::FailureKind::Check, #condition, {__VA_ARGS__}); \
    } \
    OPENETS_DETAIL_CONSTANT_CONDITION_END \
  } while (false)

/**
 * @brief Marks a path that internal invariants rule out, with an optional static message.
 * @details Reports and terminates in every build instead of telling the compiler the path cannot happen.
 *          A benchmark showed no measurable gain from the undefined-behavior hint, so safety wins.
 */
#define OPENETS_UNREACHABLE(...) \
  ::openETS::Engine::Core::Detail::ReportFailure(::openETS::Engine::Core::FailureKind::Unreachable, "unreachable code", {__VA_ARGS__})

/**
 * @brief Checks an internal invariant in debug builds, with an optional static message.
 * @details Release builds evaluate neither the condition nor the message.
 *          Required side effects and validation of external input must stay outside assertions.
 */
#if OPENETS_DEBUG
#define OPENETS_ASSERT(condition, ...) \
  do { \
    OPENETS_DETAIL_CONSTANT_CONDITION_BEGIN \
    if (!static_cast<bool>(condition)) [[unlikely]] { \
      ::openETS::Engine::Core::Detail::ReportFailure(::openETS::Engine::Core::FailureKind::Assertion, #condition, {__VA_ARGS__}); \
    } \
    OPENETS_DETAIL_CONSTANT_CONDITION_END \
  } while (false)
#else
#define OPENETS_ASSERT(condition, ...) static_cast<void>(sizeof(static_cast<bool>(condition)))
#endif