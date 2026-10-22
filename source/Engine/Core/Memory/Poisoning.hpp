#pragma once

#include "Engine/Core/Config.hpp"

#include <cstddef>

#if OPENETS_ASAN
#include <sanitizer/asan_interface.h>
#endif

namespace openETS::Engine::Core::Memory {

/**
 * @brief Marks bytes that an allocator holds back, so AddressSanitizer reports every access to them.
 * @details Compiles to nothing without AddressSanitizer. AddressSanitizer works in 8-byte granules,
 *          so a few bytes at the edges of an unaligned region may stay accessible.
 */
inline void PoisonMemory([[maybe_unused]] const void* data, [[maybe_unused]] std::size_t sizeInBytes) noexcept {
#if OPENETS_ASAN
  __asan_poison_memory_region(data, sizeInBytes);
#endif
}

/**
 * @brief Makes bytes accessible again before an allocator hands them out or returns them to the heap.
 * @details Compiles to nothing without AddressSanitizer.
 */
inline void UnpoisonMemory([[maybe_unused]] const void* data, [[maybe_unused]] std::size_t sizeInBytes) noexcept {
#if OPENETS_ASAN
  __asan_unpoison_memory_region(data, sizeInBytes);
#endif
}

}  // namespace openETS::Engine::Core::Memory