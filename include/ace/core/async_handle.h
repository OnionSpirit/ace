/**
 * @file async_handle.h
 * @brief External handle for spawned coroutines (@c ace::core::async_handle).
 *
 * @details @c async_handle<resume_t, rule_t> provides join/ping/done/cancel.
 * Behavior depends on the coroutine rule:
 *  - @c differed / @c permanent — classic handle, join waits for completion.
 *  - @c automaton — move-only, destructor cancels.  Join returns the next
 *    co_yield value then cancels.  @c ping() consumes one co_yield at a time.
 */
#ifndef ACE_FUTURE_ASYNC_HANDLE_H
#define ACE_FUTURE_ASYNC_HANDLE_H

#include "ace/core/traits/future.h"
#include "ace/core/async.h"

namespace ace::core {

    // ── join_handler (regular tasks) ───────────────────────────────────

    template <typename resume_t = void>
    struct ACE_AWAIT_NODISCARD join_handler : traits::future_traits<join_handler<resume_t>> {

    protected:

        control_block_handle _handle;
        struct join_handler_router;

    public:

        IMPORT_FUTURE_ENV(join_handler)

        join_handler() = default;

        explicit join_handler(const control_block_handle& handle)
            : _handle{handle} {}

        bool await_ready() override {
            if (_handle.is_idle()) return true;
            return _handle.done();
        }

        template<typename promise_u>
        bool await_suspend(std::coroutine_handle<promise_u> outer);

        [[nodiscard]] std::optional<resume_t> await_resume() const requires (not std::is_void_v<resume_t>) {
            if (resume_t res; _handle.return_value(&res))
                return res;
            return std::nullopt;
        }

        bool await_resume() const { return _handle.finished(); }
    };

    // ── ping_handler (automaton — consume one co_yield via router) ─────

    template <typename resume_t = void>
    struct ACE_AWAIT_NODISCARD ping_handler : traits::future_traits<ping_handler<resume_t>> {

    protected:

        control_block_handle _handle;
        struct ping_router;

    public:

        IMPORT_FUTURE_ENV(ping_handler)

        ping_handler() = default;

        explicit ping_handler(const control_block_handle& handle)
            : _handle{handle} {}

        bool await_ready() override {
            if (_handle.is_idle()) return true;
            if (_handle.done() or _handle.finished()) return true;
            return _handle.has_yield();
        }

        template<typename promise_u>
        bool await_suspend(std::coroutine_handle<promise_u> outer);

        [[nodiscard]] std::optional<resume_t> await_resume() requires (not std::is_void_v<resume_t>) {
            if (_handle.is_idle()) return std::nullopt;
            if (_handle.done() or _handle.finished()) {
                resume_t res;
                if (_handle.return_value(&res)) return res;
                return std::nullopt;
            }
            resume_t res;
            _handle.yield_value(&res);
            return res;
        }

        ~ping_handler() override = default;
    };

    // ── automaton_join_handler (ping then cancel, via router) ──────────

    template <typename resume_t = void>
    class ACE_AWAIT_NODISCARD automaton_join_handler : public traits::future_traits<automaton_join_handler<resume_t>> {

        control_block_handle _handle;
        struct join_router;

    public:

        IMPORT_FUTURE_ENV(automaton_join_handler)

        automaton_join_handler() = default;

        explicit automaton_join_handler(const control_block_handle& handle)
            : _handle{handle} {}

        bool await_ready() override {
            if (_handle.is_idle()) return true;
            if (_handle.done() or _handle.finished()) return true;
            return _handle.has_yield();
        }

        template<typename promise_u>
        bool await_suspend(std::coroutine_handle<promise_u> outer);

        [[nodiscard]] std::optional<resume_t> await_resume() requires (not std::is_void_v<resume_t>) {
            if (_handle.is_idle()) return std::nullopt;
            if (_handle.done() or _handle.finished()) {
                resume_t res;
                if (_handle.return_value(&res)) return res;
                return std::nullopt;
            }
            resume_t res;
            _handle.yield_value(&res);
            _handle.cancel();
            return res;
        }

        bool await_resume() {
            if (_handle.is_idle()) return false;
            if (_handle.done() or _handle.finished()) { _handle.cancel(); return true; }
            _handle.cancel();
            return false;
        }

        ~automaton_join_handler() override = default;
    };

    // ── async_handle ───────────────────────────────────────────────────

    template <typename resume_t = void, template <typename> typename rule_t = lazy_rule>
        requires ace::core::is_spawnable_rule<rule_t>
    class ACE_AWAIT_NODISCARD async_handle final {

        control_block_handle _handle;

        void auto_cancel() {
            if constexpr (std::same_as<rule_t<std::monostate>, automaton_rule<std::monostate>>)
                if (not _handle.is_idle() and not _handle.done())
                    _handle.cancel();
        }

    public:

        async_handle() = delete;

        explicit async_handle(const control_block_handle& handle) : _handle{handle} {}

        async_handle(const async_handle&) = delete;
        async_handle& operator=(const async_handle&) = delete;

        async_handle(async_handle&& other) noexcept
            : _handle{std::exchange(other._handle, control_block_handle{})} {}

        async_handle& operator=(async_handle&& other) noexcept {
            if (this != &other) {
                auto_cancel();
                _handle = std::exchange(other._handle, control_block_handle{});
            }
            return *this;
        }

        ~async_handle() { auto_cancel(); }

        auto join() noexcept {
            if constexpr (std::same_as<rule_t<std::monostate>, automaton_rule<std::monostate>>)
                return automaton_join_handler<resume_t>{_handle};
            else
                return join_handler<resume_t>{_handle};
        }

        auto ping() noexcept requires (std::same_as<rule_t<std::monostate>, automaton_rule<std::monostate>>) {
            return ping_handler<resume_t>{_handle};
        }

        [[nodiscard]] bool done() const { return _handle.done(); }

        void cancel() { if (not _handle.is_idle()) _handle.cancel(); }

    };

    // ── routers ────────────────────────────────────────────────────────

    template <typename resume_t>
    struct join_handler<resume_t>::join_handler_router final : runner_router {

        control_block_handle _handle;

        join_handler_router() = delete;

        explicit join_handler_router(const control_block_handle& handle) : _handle{handle} {}

        void redirect(const omni_node node) override { _handle.forward(node); }

        void cancel() override {  }

        ~join_handler_router() override = default;
    };

    template <typename resume_t>
    struct ping_handler<resume_t>::ping_router final : runner_router {

        control_block_handle _handle;

        ping_router() = delete;

        explicit ping_router(const control_block_handle& handle) : _handle{handle} {}

        void redirect(const omni_node node) override { _handle.set_yield_waiter(node); }

        void cancel() override { _handle.cancel_yield(); }

        ~ping_router() override = default;
    };

    template <typename resume_t>
    struct automaton_join_handler<resume_t>::join_router final : runner_router {

        control_block_handle _handle;

        join_router() = delete;

        explicit join_router(const control_block_handle& handle) : _handle{handle} {}

        void redirect(const omni_node node) override { _handle.set_yield_waiter(node); }

        void cancel() override { _handle.cancel_yield(); }

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

ACE_JOIN_MEMBER(template<typename promise_u> bool)
await_suspend(std::coroutine_handle<promise_u> outer) {
    outer.promise()._runner_router = join_handler_router{_handle};
    return true;
}

ACE_PING_MEMBER(template<typename promise_u> bool)
await_suspend(std::coroutine_handle<promise_u> outer) {
    outer.promise()._runner_router = ping_router{_handle};
    return true;
}

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
