#pragma once

// MSVC keeps __cplusplus at 199711L unless /Zc:__cplusplus is set, but _MSVC_LANG is always correct.
#if defined(_MSVC_LANG)
#if _MSVC_LANG < 202002L
#error "openETS requires C++20 or newer."
#endif
#elif __cplusplus < 202002L
#error "openETS requires C++20 or newer."
#endif

// clang-cl defines _MSC_VER and Clang on Linux and macOS defines __GNUC__, so Clang has to be checked first.
#if defined(__clang__)
#define OPENETS_COMPILER_CLANG 1
#define OPENETS_COMPILER_MSVC 0
#define OPENETS_COMPILER_GCC 0
#elif defined(_MSC_VER)
#define OPENETS_COMPILER_CLANG 0
#define OPENETS_COMPILER_MSVC 1
#define OPENETS_COMPILER_GCC 0
#elif defined(__GNUC__)
#define OPENETS_COMPILER_CLANG 0
#define OPENETS_COMPILER_MSVC 0
#define OPENETS_COMPILER_GCC 1
#else
#error "openETS supports MSVC, Clang and GCC only."
#endif

// OPENETS_OS_POSIX selects the Posix/ folders that Linux and macOS share.
#if defined(_WIN32)
#define OPENETS_OS_WINDOWS 1
#define OPENETS_OS_LINUX 0
#define OPENETS_OS_MACOS 0
#define OPENETS_OS_POSIX 0
#elif defined(__linux__) && !defined(__ANDROID__)
#define OPENETS_OS_WINDOWS 0
#define OPENETS_OS_LINUX 1
#define OPENETS_OS_MACOS 0
#define OPENETS_OS_POSIX 1
#elif defined(__APPLE__)
#include <TargetConditionals.h>

#if !TARGET_OS_OSX
#error "openETS supports macOS, but not iOS, tvOS or visionOS."
#endif

#define OPENETS_OS_WINDOWS 0
#define OPENETS_OS_LINUX 0
#define OPENETS_OS_MACOS 1
#define OPENETS_OS_POSIX 1
#else
#error "openETS supports Windows, Linux and macOS only."
#endif

// ARM64EC defines the x64 macros too, but mixes x64 and ARM64 code in one process.
#if defined(_M_ARM64EC) || defined(__arm64ec__)
#error "openETS does not support ARM64EC."
#elif defined(_M_X64) || defined(__x86_64__)
#define OPENETS_ARCH_X64 1
#define OPENETS_ARCH_ARM64 0
#elif defined(_M_ARM64) || defined(__aarch64__)
#define OPENETS_ARCH_X64 0
#define OPENETS_ARCH_ARM64 1
#else
#error "openETS supports x64 and ARM64 only."
#endif

// The x32 ABI on Linux reports x86_64 but uses 32-bit pointers.
static_assert(sizeof(void*) == 8, "openETS requires 64-bit pointers.");

// SCS formats and the network protocol are little-endian, so values can be read without swapping.
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "openETS requires a little-endian target."
#endif

/**
 * @brief Enables debug-only checks and diagnostics.
 * @details Follows NDEBUG unless the build defines it as 0 or 1.
 */
#ifndef OPENETS_DEBUG
#if defined(NDEBUG)
#define OPENETS_DEBUG 0
#else
#define OPENETS_DEBUG 1
#endif
#endif

static_assert(OPENETS_DEBUG == 0 || OPENETS_DEBUG == 1, "OPENETS_DEBUG must be 0 or 1.");

/** @brief Reports whether AddressSanitizer instruments the build, so custom allocators can poison their unused memory. */
#if defined(__SANITIZE_ADDRESS__)
#define OPENETS_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define OPENETS_ASAN 1
#else
#define OPENETS_ASAN 0
#endif
#else
#define OPENETS_ASAN 0
#endif

/** @brief Forces inlining where a benchmark shows that the compiler's own decision is slower. */
#if OPENETS_COMPILER_MSVC
#define OPENETS_FORCE_INLINE __forceinline
#else
#define OPENETS_FORCE_INLINE inline __attribute__((always_inline))
#endif

/** @brief Keeps cold paths such as error reporting out of the hot calling code. */
#if OPENETS_COMPILER_MSVC
#define OPENETS_NOINLINE __declspec(noinline)
#else
#define OPENETS_NOINLINE __attribute__((noinline))
#endif