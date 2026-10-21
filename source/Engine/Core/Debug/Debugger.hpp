#pragma once

#include "Engine/Core/Config.hpp"

namespace openETS::Engine::Core::Debug {

/**
 * @brief Checks whether a debugger is attached to the current process.
 * @details Asks the operating system on every call and is meant for cold paths such as assertion failures.
 *          Does not allocate, so it stays usable when the heap is exhausted.
 */
[[nodiscard]] bool IsDebuggerAttached() noexcept;

}  // namespace openETS::Engine::Core::Debug