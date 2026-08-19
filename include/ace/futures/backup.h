/**
 * @file backup.h
 * @brief Cancellation safety net futures: @c backup, @c insure and @c emergency.
 *
 * @details A coroutine can register cleanup callbacks that fire when the
 * coroutine is cancelled (or, with @c emergency, when it fails with an
 * unhandled exception) before reaching @c co_return:
 *
 * @code{.cpp}
 * ace::task guarded() {
 *     co_await ace::backup([]{ release_resource(); });
 *     co_await ace::insure(other_task());
 *     co_await some_future();
 *     co_return;
 * }
 * @endcode
 *
 * ### backup vs insure
 *
 * - @c backup(payload) — permanent record: executes on cancel together with
 *   all other backups, in reverse registration order (LIFO).
 * - @c insure(payload) — one-shot record: protects exactly the next
 *   co_await / co_yield operation.  If the coroutine is cancelled while
 *   suspended at that operation, the record fires; once the operation
 *   completes (the coroutine resumes past it), the record is removed.
 *   Registering a new backup/insure discards the pending insure.
 *
 * ### Execution on fire
 *
 * All records are moved into a single fire task scheduled on the cancelled
 * coroutine's runner (or via @c ace::schedule() when no runner is assigned).
 * The task walks the records in reverse order: callables are invoked
 * directly, tasks are co_awaited to completion before the next record.
 *
 * ### emergency flag
 *
 * The @c _emergency promise flag controls whether backups fire on unhandled
 * exceptions (@c e_failed).  Its default comes from
 * @c ace::cfg::g_config._emergency_default (@c true); @c co_await emergency(false)
 * disables firing on exceptions for the calling coroutine.  Cancellation
 * (@c e_canceled) always fires the backups.
 *
 * @note A TU that cancels or destroys coroutines must include this header
 * (via ace/ace.h) because @c promise_type::fire_backups() is defined here.
 */
#ifndef ACE_FUTURE_BACKUP_H
#define ACE_FUTURE_BACKUP_H

#include <stdexcept>
#include <ace/core/traits/future.h>
#include <ace/core/dispatcher.h>

namespace ace::futures {

    /**
     * @brief Awaitable command that registers a permanent backup callback.
     *
     * @details Accepts either a synchronous callable or an @c ace::task.
     * Never suspends — the record is appended to the calling coroutine's
     * backup list and the coroutine continues.
     */
    class ACE_AWAIT_NODISCARD backup : public core::traits::future_traits<backup> {

        core::backup_record _record {}; ///< Payload to register (callable or task).

    public:

        IMPORT_FUTURE_ENV(backup)

        /// @brief Default construction is forbidden — a payload is required.
        backup() = delete;
        /// @brief Copying a backup command is forbidden.
        backup(const backup&) = delete;
        /// @brief Copy assignment is forbidden.
        backup& operator=(const backup&) = delete;

        /**
         * @brief Construct from a synchronous callable.
         * @tparam CallableT  Invocable type (function pointer, lambda, functor).
         * @param callable    Callable invoked when the backups fire.
         */
        template <typename CallableT>
        requires std::invocable<CallableT>
            and (not std::same_as<std::decay_t<CallableT>, core::async<>>)
        explicit backup(CallableT&& callable)
            : _record { std::variant<std::function<void()>, core::async<>>{
                  std::in_place_index<0>, std::function<void()>(std::forward<CallableT>(callable)) } } {}

        /**
         * @brief Construct from a task coroutine.
         * @details The task is co_awaited to completion when the backups fire.
         * @param task  Lazy task to run on fire.
         */
        explicit backup(core::async<>&& task)
            : _record { std::variant<std::function<void()>, core::async<>>{
                  std::in_place_index<1>, std::move(task) } } {}

        /**
         * @brief C++20 awaitable protocol — register the record, never suspend.
         * @param coroutine  Handle to the calling coroutine's promise.
         * @return Always @c false — no suspension.
         */
        bool await_suspend(auto coroutine) {
            coroutine.promise().register_backup(std::move(_record));
            return false;
        }

        /// @brief No value produced.
        void await_resume() {}
    };

    /**
     * @brief Awaitable command that registers a one-shot insure callback.
     *
     * @details Same payload rules as @c backup, but the record protects
     * exactly the next co_await / co_yield operation: it is removed as soon
     * as that operation completes.  Registering another backup/insure while
     * an insure is pending discards the pending one.
     */
    class ACE_AWAIT_NODISCARD insure : public core::traits::future_traits<insure> {

        core::backup_record _record {}; ///< Payload to register (callable or task).

    public:

        IMPORT_FUTURE_ENV(insure)

        /// @brief Default construction is forbidden — a payload is required.
        insure() = delete;
        /// @brief Copying an insure command is forbidden.
        insure(const insure&) = delete;
        /// @brief Copy assignment is forbidden.
        insure& operator=(const insure&) = delete;

        /**
         * @brief Construct from a synchronous callable.
         * @tparam CallableT  Invocable type (function pointer, lambda, functor).
         * @param callable    Callable invoked if the next operation is not passed.
         */
        template <typename CallableT>
        requires std::invocable<CallableT>
            and (not std::same_as<std::decay_t<CallableT>, core::async<>>)
        explicit insure(CallableT&& callable)
            : _record { std::variant<std::function<void()>, core::async<>>{
                  std::in_place_index<0>, std::function<void()>(std::forward<CallableT>(callable)) } } {}

        /**
         * @brief Construct from a task coroutine.
         * @param task  Lazy task to run if the next operation is not passed.
         */
        explicit insure(core::async<>&& task)
            : _record { std::variant<std::function<void()>, core::async<>>{
                  std::in_place_index<1>, std::move(task) } } {}

        /**
         * @brief C++20 awaitable protocol — register the record, never suspend.
         * @param coroutine  Handle to the calling coroutine's promise.
         * @return Always @c false — no suspension.
         */
        bool await_suspend(auto coroutine) {
            coroutine.promise().register_insure(std::move(_record));
            return false;
        }

        /// @brief No value produced.
        void await_resume() {}
    };

    /**
     * @brief Awaitable command that toggles the @c _emergency flag.
     *
     * @details Mirrors @c roaming: sets the flag on the calling promise and
     * never suspends.  When @c _emergency is @c true (default from
     * @c ace::cfg::g_config._emergency_default) backup callbacks fire on
     * unhandled exceptions as well as on cancellation.
     */
    class ACE_AWAIT_NODISCARD emergency : public core::traits::future_traits<emergency> {

        bool _emergency { true }; ///< Target emergency state.

    public:

        IMPORT_FUTURE_ENV(emergency)

        /// @brief Default: enable emergency handling.
        emergency() = default;

        /**
         * @brief Construct with an explicit emergency state.
         * @param value  @c true to fire backups on exceptions, @c false to
         *               fire them only on cancellation.
         */
        explicit emergency(const bool value) : _emergency{value} {}

        /// @brief Copying an emergency command is forbidden.
        emergency(const emergency&) = delete;
        /// @brief Copy assignment is forbidden.
        emergency& operator=(const emergency&) = delete;

        /**
         * @brief Apply the flag to the promise — never suspends.
         * @param coroutine  Handle to the calling coroutine's promise.
         * @return Always @c false — no suspension.
         */
        bool await_suspend(auto coroutine) {
            coroutine.promise()._emergency = _emergency;
            return false;
        }

        /// @brief No value produced.
        void await_resume() {}
    };

    /**
     * @brief Task that executes backup records in reverse registration order.
     * @details Callables are invoked directly; tasks are co_awaited to
     * completion before the next record is processed.
     * @param records  Backup records moved out of the cancelled promise.
     */
    inline ace::task fire_backups_task(std::vector<core::backup_record> records) {
        for (auto it = records.rbegin(); it != records.rend(); ++it) {
            if (auto* task_ptr = std::get_if<ace::task>(&it->_payload)) {
                // NOTE: Skipping moved-from / already-finished tasks (a task
                // payload can be registered only once)
                if (task_ptr->is_exist()) co_await *task_ptr;
            } else {
                std::get<std::function<void()>>(it->_payload)();
            }
        }
        co_return;
    }

} // end namespace ace::futures

// NOTE: The short aliases ace::backup / ace::insure / ace::emergency are only
// exposed when ace/ace.h (quick-start header) was included before this file —
// its ACE_H guard switches aliases on.
#ifdef ACE_H
namespace ace {
    /// @brief Short alias for @c ace::futures::backup.
    using backup = futures::backup;
    /// @brief Short alias for @c ace::futures::insure.
    using insure = futures::insure;
    /// @brief Short alias for @c ace::futures::emergency.
    using emergency = futures::emergency;
}
#endif


//==============================- DEFINITIONS -==================================


template <typename returnT, template <typename> typename promise_rule_t>
requires ace::core::is_rule<promise_rule_t>
void ace::core::async<returnT, promise_rule_t>::promise_type::register_backup(ace::core::backup_record record) {
    // NOTE: A new registration discards the pending one-shot insure
    if (promise_locals::_insured) {
        promise_locals::_backups.pop_back();
        promise_locals::_insured = false;
    }
    promise_locals::_insured_prev = false;
    promise_locals::_backups.push_back(std::move(record));
}


template <typename returnT, template <typename> typename promise_rule_t>
requires ace::core::is_rule<promise_rule_t>
void ace::core::async<returnT, promise_rule_t>::promise_type::register_insure(ace::core::backup_record record) {
    register_backup(std::move(record));
    promise_locals::_insured = true;
}


template <typename returnT, template <typename> typename promise_rule_t>
requires ace::core::is_rule<promise_rule_t>
void ace::core::async<returnT, promise_rule_t>::promise_type::fire_backups() {
    using namespace ace;
    if (promise_locals::_backups.empty()) [[unlikely]] return;
    auto records = std::move(promise_locals::_backups);
    try {
        task fire = futures::fire_backups_task(std::move(records));
        // NOTE: Attaching the fire task to the coroutine's own runner (the
        // pool pointer reinterpreted as the runner — pool is the first member)
        if (auto* rnr = promise_locals::_runner.template as<core::runner>())
            rnr->attach(std::move(fire));
        else
            schedule(std::move(fire));
    } catch (const std::bad_alloc&) {
        // NOTE: Never silently drop the registered tasks — report loudly
        throw std::runtime_error("failed to init backup context. out of memory.");
    }
}

#endif // ACE_FUTURE_BACKUP_H
