/**
 * @file config.h
 * @brief Template-based framework configuration — no macros.
 *
 * ============================================================
 * PRIMARY USAGE — compile-time specialisation
 * ============================================================
 *
 * Include @c <ace/core/config.h>, specialise @c ace_param<Tag>, then
 * include @c <ace/ace.h>.  The specialisation is picked up by
 * @c init() automatically:
 *
 * @code{.cpp}
 * #include <ace/core/config.h>
 *
 * template <> struct ace::cfg::ace_param<ace::cfg::runners_amount> {
 *     static constexpr std::size_t value = 4;   // or: static std::size_t value() { ... }
 * };
 *
 * #include <ace/ace.h>
 * @endcode
 *
 * Runtime override (secondary):
 * @code{.cpp}
 * ace::cfg::g_config._runners_amount = 4;
 * ace::reload();
 * @endcode
 *
 * ============================================================
 * WHY TEMPLATE SPECIALISATION?
 * ============================================================
 *
 * @c ace_param<Tag> is the engine-level gate: only tags listed here
 * can be specialised.  A parameter intentionally absent from
 * @c param<> cannot be accidentally overridden before the
 * framework internals are ready.
 *
 * @see ace::cfg::init, ace::cfg::update, ace::cfg::g_config, ace::reload
 */

#ifndef ACE_CORE_CONFIG_H
#define ACE_CORE_CONFIG_H

#include <cstddef>

namespace ace::cfg {

    // ===================================================================
    // Tag types — one per configurable parameter
    // ===================================================================

    /// @brief Number of runner threads (including the main thread).
    struct runners_amount {};

    /// @brief Default value of the coroutine @c _emergency flag (whether backup
    ///        callbacks fire on unhandled exceptions).
    struct emergency_default {};

    /// @brief Application-wide memory limit for the coroutine frame allocator
    ///        (0 = no limit).  Per-arena limit = value / runners_amount.
    struct max_allocation_size {};

    /// @brief Behaviour when the per-arena limit is reached:
    ///        true = fallback to malloc (with a stderr notice), false = throw std::bad_alloc.
    struct breach_memory_limit {};

    // ===================================================================
    // detail::default_of — internal compile-time defaults
    //     DO NOT specialise in user code.
    // ===================================================================

    namespace detail {

        /// @brief Compile-time default value of a configuration tag.
        template <typename Tag>
        struct default_of;

        /// @brief Default runner count — 1.
        template <>
        struct default_of<runners_amount> {
            static constexpr std::size_t value = 1;
        };

        /// @brief Default @c _emergency flag — @c true (backups fire on exceptions too).
        template <>
        struct default_of<emergency_default> {
            static constexpr bool value = true;
        };

        /// @brief Default application-wide frame allocator limit — 0 (no limit).
        template <>
        struct default_of<max_allocation_size> {
            static constexpr std::size_t value = 0;
        };

        /// @brief Default breach behaviour — fallback to malloc.
        template <>
        struct default_of<breach_memory_limit> {
            static constexpr bool value = true;
        };

        // [NEW PARAM]:
        // template <> struct default_of<max_tasks_per_yank> {
        //     static constexpr int value = 128;
        // };

    } // namespace detail

    /**
     * @brief user-overridable entry point
     *
     * Primary template is intentionally empty.  Users specialise it:
     * @code
     *         template <> struct ace_param<runners_amount> {
     *             static constexpr std::size_t value = 4;
     *         };
     * @endcode
     * Also accepts a function instead of a variable:
     * @code
     *         template <> struct ace_param<runners_amount> {
     *             static std::size_t value() { return read_from_env("ACE_RUNNERS"); }
     *         };
     * @endcode
     * Only tags with a primary template here are specialisable.
     */
    template <typename Tag>
    struct ace_param {};

    /**
     * @brief Runtime configuration, read by the dispatcher.
     *
     * Fields are initialised from detail::default_of.  init() and
     * update() overwrite them via detail::resolve<Tag>().
     */
    struct config {
        /// @brief Number of runner threads. Default 1.
        std::size_t _runners_amount = detail::default_of<runners_amount>::value;

        /// @brief Default value of the coroutine @c _emergency flag. Default @c true.
        bool _emergency_default = detail::default_of<emergency_default>::value;

        /// @brief Application-wide memory limit for the coroutine frame allocator.
        ///        Default 0 — no limit.  Per-arena limit = value / runners_amount.
        std::size_t _max_allocation_size = detail::default_of<max_allocation_size>::value;

        /// @brief Behaviour when the per-arena limit is reached.
        ///        Default @c true — fallback to malloc with a stderr notice;
        ///        @c false — throw @c std::bad_alloc.
        bool _breach_memory_limit = detail::default_of<breach_memory_limit>::value;
    };

    /// @brief Global singleton runtime configuration.
    inline config g_config{};

    namespace detail {

        /// @brief User provided `static T value()` (checked first — a function is also a valid `::value` expression).
        template <typename Tag>
        concept has_value_function = requires { ace_param<Tag>::value(); };

        /// @brief User provided `static constexpr T value` (and NOT a function).
        template <typename Tag>
        concept has_value_member = requires { ace_param<Tag>::value; } and not has_value_function<Tag>;

        /**
         * @brief Returns the resolved value for tag @c Tag.
         *
         * Checks for a user specialisation of @c ace_param<Tag> in order:
         *  1. `static T value()`         — runtime lookup (env, file, …)
         *  2. `static constexpr T value` — compile-time constant
         *  3. Falls back to @c detail::default_of<Tag>::value.
         */
        template <typename Tag>
        constexpr auto resolve() {
            if constexpr (has_value_function<Tag>)
                return ace_param<Tag>::value();
            else if constexpr (has_value_member<Tag>)
                return ace_param<Tag>::value;
            else
                return detail::default_of<Tag>::value;
        }

    } // namespace detail

} // namespace ace::cfg

#endif // ACE_CORE_CONFIG_H
