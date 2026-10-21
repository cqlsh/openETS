#pragma once

#include "Engine/Core/Config.hpp"

#if OPENETS_COMPILER_MSVC
#include <intrin.h>
#endif

/**
 * @brief Stops execution at the call site so an attached debugger can inspect the state.
 * @details Without a debugger, the process ends with a breakpoint exception or SIGTRAP.
 *          Code that has to keep running checks for a debugger first.
 */
#if OPENETS_COMPILER_MSVC
#define OPENETS_DEBUG_BREAK() __debugbreak()
#elif OPENETS_COMPILER_CLANG
#define OPENETS_DEBUG_BREAK() __builtin_debugtrap()
#elif OPENETS_ARCH_X64
#define OPENETS_DEBUG_BREAK() __asm__ volatile("int3")
#else
#define OPENETS_DEBUG_BREAK() __asm__ volatile("brk #0xf000")
#endif