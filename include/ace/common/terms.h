/**
 * @file terms.h
 * @brief Shared compile-time constants and alignment macros.
 *
 * @details This header defines platform-agnostic size and alignment helpers
 * used throughout the ACE framework to optimise memory layout and prevent
 * false sharing.
 *
 * | Macro | Value | Purpose |
 * |---|---|---|
 * | `ACE_BUS_SIZE` | `sizeof(std::size_t)` | Native pointer/word size. Used for `alignas` on small fields. |
 * | `ACE_CONDUCTOR_MEM_SIZE` | cache line − bus size | Maximum byte size for a conductor in a `conductor_slot`. |
 * | `ACE_CACHE_LINE_SIZE` | `hardware_constructive_interference_size` | CPU cache line size. |
 * | `ACE_CACHE_LINE(n)` | zero-size struct | Inserts a named cache-line padding marker at position `n`. |
 */
#ifndef ACE_COMMON_TERMS_H
#define ACE_COMMON_TERMS_H

/// @brief Native word size (bytes).  Used as `alignas` value for small fields.
#define ACE_BUS_SIZE sizeof(std::size_t)

#ifndef ACE_CONDUCTOR_MEM_SIZE
/// @brief Maximum byte size for a concrete conductor stored in a `conductor_slot`.
/// @details Derived so that a `conductor_slot` including its pointer fits within
/// one cache line.
#define ACE_CONDUCTOR_MEM_SIZE std::hardware_constructive_interference_size - ACE_BUS_SIZE
#endif

/// @brief CPU cache line size in bytes.
/// @details Equal to `std::hardware_constructive_interference_size`.
/// Used to size `alignas` on performance-critical structs.
#define ACE_CACHE_LINE_SIZE std::hardware_constructive_interference_size

/**
 * @brief Insert a named zero-size padding sentinel at a cache-line boundary.
 * @details Used inside `alignas(ACE_CACHE_LINE_SIZE)` structs to visually
 * mark where one cache line ends and another begins.
 * @param number  A unique integer suffix to prevent name collisions.
 */
#define ACE_CACHE_LINE(number) [[maybe_unused]] struct {} _cache_line_##number[0] {};

/// @brief A zero-byte type used as a default template argument placeholder.
typedef struct {} ACE_EMPTY_TYPE;

#endif // ACE_COMMON_TERMS_H
