#pragma once

#include "Engine/Core/Config.hpp"

#if !OPENETS_OS_WINDOWS
#error "openETS uses Win32.hpp on Windows only."
#endif

// Windows 10 is the oldest supported version; newer APIs such as IoRing are loaded at runtime.
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#if _WIN32_WINNT < 0x0A00
#error "openETS requires _WIN32_WINNT 0x0A00 (Windows 10) or newer."
#endif

// Keeps windows.h from defining min and max as macros and skips rarely used parts such as DDE and RPC.
#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>