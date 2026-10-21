#include "Engine/Core/Debug/Debugger.hpp"
#include "Engine/Core/Windows/Win32.hpp"

namespace openETS::Engine::Core::Debug {

bool IsDebuggerAttached() noexcept {
  return ::IsDebuggerPresent() != FALSE;
}

}  // namespace openETS::Engine::Core::Debug