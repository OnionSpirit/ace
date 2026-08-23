/**
 * @file macro.h
 * @brief Shared compile-time constants and alignment macros.
 *
 * @details This header defines platform-agnostic size and alignment helpers
 * used throughout the ACE framework to optimise memory layout and prevent
 * false sharing.
 *
 * | Macro | Value | Purpose |
 * |---|---|---|
 * | @c ACE_BUS_SIZE | @c sizeof(std::size_t) | Native pointer/word size. Used for @c alignas on small fields. |
 * | @c ACE_ROUTER_MEM_SIZE | cache line − bus size | Maximum byte size for a router in a @c router_slot. |
 * | @c ACE_CACHE_LINE_SIZE | @c hardware_constructive_interference_size | CPU cache line size. |
 * | @c ACE_CACHE_LINE(n) | zero-size struct | Inserts a named cache-line padding marker at position @c n. |
 */
#ifndef ACE_COMMON_TERMS_H
#define ACE_COMMON_TERMS_H

/// @brief Native word size (bytes).  Used as @c alignas value for small fields.
#define ACE_BUS_SIZE sizeof(std::size_t)

#ifndef ACE_ROUTER_MEM_SIZE
/// @brief Maximum byte size for a concrete router stored in a @c router_slot.
/// @details Derived so that a @c router_slot including its pointer fits within
/// one cache line.
#define ACE_ROUTER_MEM_SIZE std::hardware_constructive_interference_size - ACE_BUS_SIZE
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

/// @brief Message emitted by @c ACE_AWAIT_NODISCARD when the await is missing.
#define ACE_AWAIT_MISSING_MSG "probably 'co_await' operator missing"

/// @brief Marks awaitables whose result must be consumed via @c co_await.
#define ACE_AWAIT_NODISCARD [[nodiscard(ACE_AWAIT_MISSING_MSG)]]

/// @brief Static-assert message for incompatible @c compose / @c operator>> operands.
#define ACE_INCOMPATIBLE_COMPOSE_ERROR "Receiver's (Right Operand) input does not compatible with Sender's (Left Operand) return type"

#if defined(__GNUC__) || defined(__clang__)
/// @brief Force-inline attribute (GCC/Clang).
#define ACE_INLINE inline __attribute__((always_inline))
#elif defined(_MSC_VER)
/// @brief Force-inline attribute (MSVC).
#define ACE_INLINE __forceinline
#else
/// @brief Force-inline fallback — plain inline.
#define ACE_INLINE inline
#endif

#if defined(__GNUC__) || defined(__clang__)
/// @brief Weak symbol attribute (GCC/Clang) — allows user override of @c main().
#define ACE_WEAK __attribute__((weak))
#elif defined(_MSC_VER)
/// @brief Weak symbol attribute (MSVC) — selectany.
#define ACE_WEAK __declspec(selectany)
#else
/// @brief Weak symbol fallback — no attribute.
#define ACE_WEAK
#endif

/// @brief Maximum number of chunks an @c io::buffer may keep assembled.
#define ACE_IO_BUFFER_CHUNK_LIMIT 16

#ifndef NDEBUG
/// @brief @c true in debug builds (NDEBUG is not defined).
inline constexpr bool is_debug = true;
#else
/// @brief @c false in release builds (NDEBUG is defined).
inline constexpr bool is_debug = false;
#endif

#endif // ACE_COMMON_TERMS_H
