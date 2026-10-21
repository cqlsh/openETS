#include "Engine/Core/Debug/Debugger.hpp"

#include <array>
#include <cerrno>
#include <cstddef>
#include <string_view>

#include <fcntl.h>
#include <unistd.h>

namespace openETS::Engine::Core::Debug {

namespace {

// /proc/self/status stays well below 4 KiB, and a stack buffer keeps the check free of heap allocations.
constexpr std::size_t STATUS_BUFFER_SIZE = 4096;

[[nodiscard]] std::size_t ReadStatus(int descriptor, std::array<char, STATUS_BUFFER_SIZE>& buffer) noexcept {
  std::size_t total = 0;
  while (total < buffer.size()) {
    const ssize_t count = ::read(descriptor, buffer.data() + total, buffer.size() - total);
    if (count < 0 && errno == EINTR) {
      continue;
    }
    if (count <= 0) {
      break;
    }
    total += static_cast<std::size_t>(count);
  }
  return total;
}

}  // namespace

bool IsDebuggerAttached() noexcept {
  const int descriptor = ::open("/proc/self/status", O_RDONLY | O_CLOEXEC);
  if (descriptor < 0) {
    return false;
  }
  std::array<char, STATUS_BUFFER_SIZE> buffer;
  const std::size_t size = ReadStatus(descriptor, buffer);
  ::close(descriptor);
  constexpr std::string_view KEY = "TracerPid:";
  const std::string_view status(buffer.data(), size);
  const std::size_t keyPosition = status.find(KEY);
  if (keyPosition == std::string_view::npos) {
    return false;
  }
  // The tracer pid is 0 without a tracer, so any digit from 1 to 9 means a debugger or another ptrace user.
  for (std::size_t i = keyPosition + KEY.size(); i < status.size() && status[i] != '\n'; ++i) {
    if (status[i] >= '1' && status[i] <= '9') {
      return true;
    }
  }
  return false;
}

}  // namespace openETS::Engine::Core::Debug