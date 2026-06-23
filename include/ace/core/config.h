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

    /// @brief Number of pre-allocated 512-byte fixed buffers for io_uring.
    struct iovec_fixed_512 {};

    /// @brief Number of pre-allocated 2048-byte fixed buffers for io_uring.
    struct iovec_fixed_2048 {};

    // ===================================================================
    // detail::default_of — internal compile-time defaults
    //     DO NOT specialise in user code.
    // ===================================================================

    namespace detail {

        template <typename Tag>
        struct default_of;

        template <>
        struct default_of<runners_amount> {
            static constexpr std::size_t value = 1;
        };

        template <>
        struct default_of<iovec_fixed_512> {
            static constexpr std::size_t value = 256;
        };

        template <>
        struct default_of<iovec_fixed_2048> {
            static constexpr std::size_t value = 64;
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

        // [NEW PARAM]:
        // int _max_tasks_per_yank = detail::default_of<max_tasks_per_yank>::value;
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
