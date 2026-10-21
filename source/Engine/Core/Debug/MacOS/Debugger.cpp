#include "Engine/Core/Debug/Debugger.hpp"

#include <cstddef>
#include <iterator>

#include <sys/sysctl.h>
#include <unistd.h>

namespace openETS::Engine::Core::Debug {

bool IsDebuggerAttached() noexcept {
  int query[] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, ::getpid()};
  kinfo_proc info{};
  std::size_t size = sizeof(info);
  if (::sysctl(query, static_cast<unsigned int>(std::size(query)), &info, &size, nullptr, 0) != 0) {
    return false;
  }
  // P_TRACED stays set while a debugger or another ptrace user controls the process.
  return (info.kp_proc.p_flag & P_TRACED) != 0;
}

}  // namespace openETS::Engine::Core::Debug