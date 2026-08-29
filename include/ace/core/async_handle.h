/**
 * @file async_handle.h
 * @brief External handle for spawned coroutines (@c ace::core::async_handle).
 *
 * @details @c async_handle<resume_t, rule_t> provides join/ping/done/cancel.
 * Behavior depends on the coroutine rule:
 *  - @c lazy_rule / @c eager_rule — classic handle, join waits for completion.
 *  - @c automaton_rule — move-only, destructor cancels.  Join returns the
 *    next co_yield value then cancels.  @c ping() consumes one co_yield at a time.
 */
#ifndef ACE_FUTURE_ASYNC_HANDLE_H
#define ACE_FUTURE_ASYNC_HANDLE_H

#include "ace/core/traits/future.h"
#include "ace/core/async.h"

namespace ace::core {

    // ── join_handler (regular tasks) ───────────────────────────────────

    /**
     * @brief Awaitable that waits for a spawned task to finish.
     * @details Co-awaiting a @c join_handler suspends until the referenced
     * coroutine reaches a terminal state (@c e_finished, @c e_failed or
     * @c e_canceled), then reports the result.
     * @tparam resume_t  Return type of the awaited coroutine (@c void by default).
     */
    template <typename resume_t = void>
    struct ACE_AWAIT_NODISCARD join_handler : traits::future_traits<join_handler<resume_t>> {

    protected:

        /// @brief Handle to the spawned coroutine being awaited.
        control_block_handle _handle;
        struct join_handler_router;

    public:

        IMPORT_FUTURE_ENV(join_handler)

        /// @brief Default constructor — creates an idle (unbound) handler.
        join_handler() = default;

        /// @brief Bind the handler to the target coroutine's control block.
        explicit join_handler(const control_block_handle& handle)
            : _handle{handle} {}

        /// @brief @c true if the target is idle or already done — no suspension needed.
        bool await_ready() override {
            if (_handle.is_idle()) return true;
            return _handle.done();
        }

        /**
         * @brief Install the @c join_handler_router into the outer promise.
         * @tparam promise_u  Promise type of the outer coroutine.
         * @param outer       Handle to the outer (waiting) coroutine.
         * @return Always @c true — the outer coroutine suspends.
         */
        template<typename promise_u>
        bool await_suspend(std::coroutine_handle<promise_u> outer);

        /// @brief Valued variant — extract the finished coroutine's result, if any.
        [[nodiscard]] std::optional<resume_t> await_resume() const requires (not std::is_void_v<resume_t>) {
            if (resume_t res; _handle.return_value(&res))
                return res;
            return std::nullopt;
        }

        /// @brief @c void variant — @c true if the coroutine finished with @c e_finished.
        bool await_resume() const { return _handle.finished(); }
    };

    // ── ping_handler (automaton — consume one co_yield via router) ─────

    /**
     * @brief Awaitable that consumes one @c co_yield value from an automaton.
     * @details Co-awaiting a @c ping_handler resumes when the automaton has a
     * pending yield value (or finishes), and returns it.
     * @tparam resume_t  Yielded value type (@c void by default).
     */
    template <typename resume_t = void>
    struct ACE_AWAIT_NODISCARD ping_handler : traits::future_traits<ping_handler<resume_t>> {

    protected:

        /// @brief Handle to the automaton being pinged.
        control_block_handle _handle;
        struct ping_router;

    public:

        IMPORT_FUTURE_ENV(ping_handler)

        /// @brief Default constructor — creates an idle (unbound) handler.
        ping_handler() = default;

        /// @brief Bind the handler to the target automaton's control block.
        explicit ping_handler(const control_block_handle& handle)
            : _handle{handle} {}

        /// @brief @c true if idle, finished, or a yield value is already pending.
        bool await_ready() override {
            if (_handle.is_idle()) return true;
            if (_handle.done() or _handle.finished()) return true;
            return _handle.has_yield();
        }

        /**
         * @brief Install the @c ping_router into the outer promise.
         * @tparam promise_u  Promise type of the outer coroutine.
         * @param outer       Handle to the outer (waiting) coroutine.
         * @return Always @c true — the outer coroutine suspends.
         */
        template<typename promise_u>
        bool await_suspend(std::coroutine_handle<promise_u> outer);

        /// @brief Return the next yield value, or the final result if finished.
        [[nodiscard]] std::optional<resume_t> await_resume() requires (not std::is_void_v<resume_t>) {
            if (_handle.is_idle()) return std::nullopt;
            if (_handle.done() or _handle.finished()) {
                resume_t res;
                if (_handle.return_value(&res)) return res;
                return std::nullopt;
            }
            resume_t res;
            if (_handle.yield_value(&res)) return res;
            return std::nullopt;
        }

        /// @brief Defaulted destructor.
        ~ping_handler() override = default;
    };

    // ── automaton_join_handler (ping then cancel, via router) ──────────

    /**
     * @brief Awaitable that joins an automaton by reading its next available
     * value and canceling a non-terminal automaton.
     * @details Co-awaiting an @c automaton_join_handler resumes when a yield
     * value is pending or the automaton reaches a terminal state. The valued
     * overload requests cancellation after attempting to read a pending yield,
     * regardless of whether the read succeeds; it reads a terminal result
     * without an additional cancellation request. The @c void overload
     * requests cancellation for every non-idle automaton.
     * @tparam resume_t  Yielded / returned value type (@c void by default).
     */
    template <typename resume_t = void>
    class ACE_AWAIT_NODISCARD automaton_join_handler : public traits::future_traits<automaton_join_handler<resume_t>> {

        /// @brief Handle to the automaton being joined.
        control_block_handle _handle;
        struct join_router;

    public:

        IMPORT_FUTURE_ENV(automaton_join_handler)

        /// @brief Default constructor — creates an idle (unbound) handler.
        automaton_join_handler() = default;

        /// @brief Bind the handler to the target automaton's control block.
        explicit automaton_join_handler(const control_block_handle& handle)
            : _handle{handle} {}

        /// @brief @c true if idle, finished, or a yield value is already pending.
        bool await_ready() override {
            if (_handle.is_idle()) return true;
            if (_handle.done() or _handle.finished()) return true;
            return _handle.has_yield();
        }

        /**
         * @brief Install the @c join_router into the outer promise.
         * @tparam promise_u  Promise type of the outer coroutine.
         * @param outer       Handle to the outer (waiting) coroutine.
         * @return Always @c true — the outer coroutine suspends.
         */
        template<typename promise_u>
        bool await_suspend(std::coroutine_handle<promise_u> outer);

        /**
         * @brief Reads the next available value and applies join cancellation.
         * @return For an idle handle, @c std::nullopt without cancellation. For
         * a terminal automaton, its final result when available, otherwise
         * @c std::nullopt, without an additional cancellation request. For a
         * non-terminal automaton, the yielded value when the read succeeds,
         * otherwise @c std::nullopt; cancellation is requested after either
         * yield-read outcome.
         */
        [[nodiscard]] std::optional<resume_t> await_resume() requires (not std::is_void_v<resume_t>) {
            if (_handle.is_idle()) return std::nullopt;
            if (_handle.done() or _handle.finished()) {
                resume_t res;
                if (_handle.return_value(&res)) return res;
                return std::nullopt;
            }
            resume_t res;
            const bool has_value = _handle.yield_value(&res);
            _handle.cancel();
            if (not has_value) return std::nullopt;
            return res;
        }

        /**
         * @brief Requests cancellation for a non-idle automaton.
         * @return @c true when the automaton was already terminal; @c false
         * when the handle was idle or the automaton was still active.
         * Cancellation is requested for both terminal and active automatons,
         * but not for an idle handle.
         */
        bool await_resume() {
            if (_handle.is_idle()) return false;
            if (_handle.done() or _handle.finished()) { _handle.cancel(); return true; }
            _handle.cancel();
            return false;
        }

        /// @brief Defaulted destructor.
        ~automaton_join_handler() override = default;
    };

    // ── async_handle ───────────────────────────────────────────────────

    /**
     * @brief External handle for a spawned coroutine: join / ping / done / cancel.
     * @details For automaton rules the handle is move-only and its destructor
     * cancels the automaton; for lazy / eager rules it behaves as a classic
     * completion handle.
     * @tparam resume_t  Return (or yield) type of the spawned coroutine.
     * @tparam rule_t    Coroutine rule of the spawned coroutine (must be spawnable).
     */
    template <typename resume_t = void, template <typename> typename rule_t = lazy_rule>
        requires ace::core::is_spawnable_rule<rule_t>
    class ACE_AWAIT_NODISCARD async_handle final {

        /// @brief Handle to the spawned coroutine's control block.
        control_block_handle _handle;

        /// @brief Cancel the automaton unless it is idle or already done (automaton only).
        void auto_cancel() {
            if constexpr (std::same_as<rule_t<std::monostate>, automaton_rule<std::monostate>>)
                if (not _handle.is_idle() and not _handle.done())
                    _handle.cancel();
        }

    public:

        /// @brief Deleted — a handle must wrap a valid control block.
        async_handle() = delete;

        /// @brief Wrap an existing control block handle.
        explicit async_handle(const control_block_handle& handle) : _handle{handle} {}

        /// @brief Handles are non-copyable.
        async_handle(const async_handle&) = delete;
        async_handle& operator=(const async_handle&) = delete;

        /// @brief Move constructor — transfers the wrapped handle.
        async_handle(async_handle&& other) noexcept
            : _handle{other._handle} {
            // NOTE: Release the reference of the source handle.  The plain
            // pointer steal (std::exchange) leaks the source's reference.
            other._handle = control_block_handle{};
        }

        /// @brief Move assignment — cancels the current automaton (if any) before taking over.
        async_handle& operator=(async_handle&& other) noexcept {
            if (this != &other) {
                auto_cancel();
                _handle = other._handle;
                other._handle = control_block_handle{};
            }
            return *this;
        }

        /// @brief Destructor — auto-cancels the automaton if it is still alive.
        ~async_handle() { auto_cancel(); }

        /// @brief Awaitable that waits for completion (ping-and-cancel for automatons).
        auto join() noexcept {
            if constexpr (std::same_as<rule_t<std::monostate>, automaton_rule<std::monostate>>)
                return automaton_join_handler<resume_t>{_handle};
            else
                return join_handler<resume_t>{_handle};
        }

        /// @brief Awaitable that consumes one @c co_yield value (automaton only).
        auto ping() noexcept requires (std::same_as<rule_t<std::monostate>, automaton_rule<std::monostate>>) {
            return ping_handler<resume_t>{_handle};
        }

        /// @brief Whether the spawned coroutine has reached a terminal state.
        [[nodiscard]] bool done() const { return _handle.done(); }

        /// @brief Request cancellation of the spawned coroutine.
        void cancel() { if (not _handle.is_idle()) _handle.cancel(); }

    };

    // ── routers ────────────────────────────────────────────────────────

    /**
     * @brief Router installed into the waiting coroutine to forward its node
     *        to the target's control block on completion.
     */
    template <typename resume_t>
    struct join_handler<resume_t>::join_handler_router final : runner_router {

        /// @brief Handle to the target coroutine's control block.
        control_block_handle _handle;

        /// @brief Deleted — a router must wrap a valid control block.
        join_handler_router() = delete;

        /// @brief Bind the router to the target coroutine.
        explicit join_handler_router(const control_block_handle& handle) : _handle{handle} {}

        /// @brief Forward the waiting node to the target's control block.
        bool redirect(const omni_node node) override {
            _handle.forward(node);
            return true;
        }

        /// @brief No-op — cancellation is handled by the control block.
        void cancel() override {  }

        /// @brief Defaulted destructor.
        ~join_handler_router() override = default;
    };

    /**
     * @brief Router installed into the waiting coroutine to register it as the
     *        consumer of the next @c co_yield value (automaton ping).
     */
    template <typename resume_t>
    struct ping_handler<resume_t>::ping_router final : runner_router {

        /// @brief Handle to the automaton's control block.
        control_block_handle _handle;

        /// @brief Deleted — a router must wrap a valid control block.
        ping_router() = delete;

        /// @brief Bind the router to the automaton.
        explicit ping_router(const control_block_handle& handle) : _handle{handle} {}

        /// @brief Register the waiting node as the automaton's yield waiter.
        bool redirect(const omni_node node) override {
            _handle.set_yield_waiter(node);
            return true;
        }

        /// @brief Drop the yield waiter registration.
        void cancel() override { _handle.cancel_yield(); }

        /// @brief Defaulted destructor.
        ~ping_router() override = default;
    };

    /**
     * @brief Router installed into the waiting coroutine by automaton join;
     *        registers it as the yield waiter so it is woken on the next value.
     */
    template <typename resume_t>
    struct automaton_join_handler<resume_t>::join_router final : runner_router {

        /// @brief Handle to the automaton's control block.
        control_block_handle _handle;

        /// @brief Deleted — a router must wrap a valid control block.
        join_router() = delete;

        /// @brief Bind the router to the automaton.
        explicit join_router(const control_block_handle& handle) : _handle{handle} {}

        /// @brief Register the waiting node as the automaton's yield waiter.
        bool redirect(const omni_node node) override {
            _handle.set_yield_waiter(node);
            return true;
        }

        /// @brief Drop the yield waiter registration.
        void cancel() override { _handle.cancel_yield(); }

        /// @brief Defaulted destructor.
        ~join_router() override = default;
    };

} // end namespace ace::core

// ── definitions ───────────────────────────────────────────────────────

#define ACE_JOIN_SPACE ace::core::join_handler<resume_t>::
#define ACE_JOIN_MEMBER(RT) template <typename resume_t> RT ACE_JOIN_SPACE

#define ACE_PING_SPACE ace::core::ping_handler<resume_t>::
#define ACE_PING_MEMBER(RT) template <typename resume_t> RT ACE_PING_SPACE

#define ACE_AJOIN_SPACE ace::core::automaton_join_handler<resume_t>::
#define ACE_AJOIN_MEMBER(RT) template <typename resume_t> RT ACE_AJOIN_SPACE

/// @brief Definition of @c join_handler::await_suspend — installs the join router.
ACE_JOIN_MEMBER(template<typename promise_u> bool)
await_suspend(std::coroutine_handle<promise_u> outer) {
    outer.promise()._runner_router = join_handler_router{_handle};
    return true;
}

/// @brief Definition of @c ping_handler::await_suspend — installs the ping router.
ACE_PING_MEMBER(template<typename promise_u> bool)
await_suspend(std::coroutine_handle<promise_u> outer) {
    outer.promise()._runner_router = ping_router{_handle};
    return true;
}

/// @brief Definition of @c automaton_join_handler::await_suspend — installs the join router.
ACE_AJOIN_MEMBER(template<typename promise_u> bool)
await_suspend(std::coroutine_handle<promise_u> outer) {
    outer.promise()._runner_router = join_router{_handle};
    return true;
}

#undef ACE_JOIN_SPACE
#undef ACE_JOIN_MEMBER
#undef ACE_PING_SPACE
#undef ACE_PING_MEMBER
#undef ACE_AJOIN_SPACE
#undef ACE_AJOIN_MEMBER

#endif //ACE_FUTURE_ASYNC_HANDLE_H
