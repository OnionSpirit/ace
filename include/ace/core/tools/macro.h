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
 * | @c ACE_BUS_SIZE | @c sizeof(std::size_t) | Native pointer/word size. Used for @c alignas on small fields. |
 * | @c ACE_CONDUCTOR_MEM_SIZE | cache line − bus size | Maximum byte size for a conductor in a @c conductor_slot. |
 * | @c ACE_CACHE_LINE_SIZE | @c hardware_constructive_interference_size | CPU cache line size. |
 * | @c ACE_CACHE_LINE(n) | zero-size struct | Inserts a named cache-line padding marker at position @c n. |
 */
#ifndef ACE_COMMON_TERMS_H
#define ACE_COMMON_TERMS_H

/// @brief Native word size (bytes).  Used as @c alignas value for small fields.
#define ACE_BUS_SIZE sizeof(std::size_t)

#ifndef ACE_CONDUCTOR_MEM_SIZE
/// @brief Maximum byte size for a concrete conductor stored in a @c conductor_slot.
/// @details Derived so that a @c conductor_slot including its pointer fits within
/// one cache line.
#define ACE_CONDUCTOR_MEM_SIZE std::hardware_constructive_interference_size - ACE_BUS_SIZE
#endif

/// @brief CPU cache line size in bytes.
/// @details Equal to @c std::hardware_constructive_interference_size.
/// Used to size @c alignas on performance-critical structs.
#define ACE_CACHE_LINE_SIZE std::hardware_constructive_interference_size

/**
 * @brief Insert a named zero-size padding sentinel at a cache-line boundary.
 * @details Used inside @c alignas(ACE_CACHE_LINE_SIZE) structs to visually
 * mark where one cache line ends and another begins.
 * @param number  A unique integer suffix to prevent name collisions.
 */
#define ACE_CACHE_LINE(number) [[maybe_unused]] struct {} _ace_cache_line_##number[0] {};

/// @brief A zero-byte type used as a default template argument placeholder.
typedef struct {} ACE_EMPTY_TYPE;

#define ACE_AWAIT_MISSING_MSG "probably 'co_await' operator missing"

#define ACE_AWAIT_NODISCARD [[nodiscard(ACE_AWAIT_MISSING_MSG)]]

#define ACE_INCOMPATIBLE_COMPOSE_ERROR "Receiver's (Right Operand) input does not compatible with Sender's (Left Operand) return type"

#endif // ACE_COMMON_TERMS_H
