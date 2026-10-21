#include "Engine/Core/Assert.hpp"
#include "Engine/Core/Debug/DebugBreak.hpp"
#include "Engine/Core/Debug/Debugger.hpp"

#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <system_error>
#include <thread>

namespace openETS::Engine::Core {

namespace {

// Long enough for the failing thread to finish its report, short enough that a deadlocked handler cannot hang the process.
constexpr std::chrono::seconds OTHER_THREADS_WAIT_LIMIT{5};

std::atomic<AssertionHandler> installedHandler{nullptr};
std::atomic_flag processIsFailing;
thread_local bool threadIsFailing = false;

[[nodiscard]] const char* DescribeKind(FailureKind kind) noexcept {
  switch (kind) {
    case FailureKind::Assertion:
      return "Assertion failed";
    case FailureKind::Check:
      return "Check failed";
    case FailureKind::Unreachable:
      return "Unreachable code reached";
  }
  return "Invariant broken";
}

// Only fputs and to_chars are used, so the report still works when the heap is exhausted.
void WriteReport(const AssertionFailure& failure) noexcept {
  std::array<char, 16> line{};
  const std::to_chars_result converted = std::to_chars(line.data(), line.data() + line.size() - 1, failure.m_location.line());
  if (converted.ec != std::errc{}) {
    line[0] = '?';
  }
  std::fputs("[openETS] ", stderr);
  std::fputs(DescribeKind(failure.m_kind), stderr);
  std::fputs(": ", stderr);
  std::fputs(failure.m_expression != nullptr ? failure.m_expression : "?", stderr);
  std::fputs("\n[openETS] Location: ", stderr);
  std::fputs(failure.m_location.file_name(), stderr);
  std::fputs(":", stderr);
  std::fputs(line.data(), stderr);
  std::fputs("\n[openETS] Function: ", stderr);
  std::fputs(failure.m_location.function_name(), stderr);
  if (failure.m_message != nullptr) {
    std::fputs("\n[openETS] Message: ", stderr);
    std::fputs(failure.m_message, stderr);
  }
  std::fputs("\n", stderr);
  std::fflush(stderr);
}

}  // namespace

void SetAssertionHandler(AssertionHandler handler) noexcept {
  installedHandler.store(handler, std::memory_order_release);
}

namespace Detail {

void ReportFailure(FailureKind kind, const char* expression, const char* message, std::source_location location) noexcept {
  // A failure inside the report or the handler must not recurse.
  if (threadIsFailing) {
    std::abort();
  }
  threadIsFailing = true;
  // Only the first failing thread reports; the others give it time and then end the process themselves.
  if (processIsFailing.test_and_set(std::memory_order_acq_rel)) {
    const auto deadline = std::chrono::steady_clock::now() + OTHER_THREADS_WAIT_LIMIT;
    while (std::chrono::steady_clock::now() < deadline) {
      std::this_thread::yield();
    }
    std::abort();
  }
  const AssertionFailure failure{kind, expression, message, location};
  WriteReport(failure);
  if (const AssertionHandler handler = installedHandler.load(std::memory_order_acquire); handler != nullptr) {
    handler(failure);
  }
  if (Debug::IsDebuggerAttached()) {
    OPENETS_DEBUG_BREAK();
  }
  std::abort();
}

}  // namespace Detail

}  // namespace openETS::Engine::Core