#ifndef RIOT_UTILS_H
#define RIOT_UTILS_H

#include "riot.h"


namespace riot::meta::helpers {

    template <riot::meta::types::AwaitableType Awaiter>
    riot::async::context<> all_signaling_context(std::atomic<ushort>& common_counter, Awaiter& awaiter) {
        co_await awaiter;
        common_counter++;
        co_return;
    }

    template <riot::meta::types::AwaitableType Awaiter>
    riot::async::context<> all_signaling_context(std::atomic<ushort>& common_counter, Awaiter& awaiter, auto& return_value) {
        return_value = co_await awaiter;
        common_counter++;
        co_return;
    }

    template <size_t Index, riot::meta::types::AwaitableType Awaiter>
    riot::async::context<> any_signaling_context(Awaiter& awaiter, std::atomic<uint8_t>& res_flag, ushort& index) {
        co_await awaiter;
        index = Index;
        res_flag.fetch_add(1, std::memory_order_release);
        co_return;
    }

    template <size_t Index, riot::meta::types::AwaitableType Awaiter>
    riot::async::context<> any_signaling_context(Awaiter& awaiter, std::atomic<uint8_t>& res_flag, ushort& index,
                                auto& return_value) {
        return_value = co_await awaiter;
        index = Index;
        res_flag.fetch_add(1, std::memory_order_release);
        co_return;
    }

    template <size_t BufferSize =16>
    class local_awaitable_pool :
        public riot::async::awaitable<local_awaitable_pool<BufferSize>> {
        public:

        mutable riot::common::atomic_queue_static<riot::common::coroutine, BufferSize> _container;
        mutable riot::common::atomic_queue_static<riot::common::coroutine, 1> _waiter;

        void reset() {
            riot::common::coroutine temp {};
            while(_waiter.pop(temp)) {
                if (not temp._coroutine.promise()._retcode){
                    if (temp._coroutine.promise().reset_pool())
                        temp._coroutine.promise()._current_pool.push(std::move(temp));
                    return;
                }
            }
        }

        bool ready() { return not this->empty(); }

        bool suspend(auto& coroutine) {
            if (this->empty()) {
                coroutine.promise().switch_pool(_waiter);
                return true;
            } else return false;
        }

        void resume() {}

        bool empty() { return _container.empty(); }

        bool push(riot::common::coroutine&& data_) noexcept {
            if (not data_._coroutine.promise()._original_pool) {
                data_._coroutine.promise()._original_pool = this;
            }
            data_._coroutine.promise()._current_pool = this;
            if (auto res = this->_container.push(std::move(data_))) {
                this->reset();
                return res;
            } else return res;
        }

        bool push(riot::common::coroutine& data_) noexcept {
            if (not data_._coroutine.promise()._original_pool) {
                data_._coroutine.promise()._original_pool = this;
            }
            data_._coroutine.promise()._current_pool = this;
            if (auto res = this->_container.push(std::move(data_))) {
                this->reset();
                return res;
            } else return res;

        }

        bool pop(riot::common::coroutine& data_) {
            return _container.pop(data_);
        }

        void proceed_pool() noexcept {
            uint rc {0};
            riot::meta::technical::context_execution_state call_response = riot::meta::technical::context_execution_state::e_blocked;
            riot::common::coroutine temp {};

            if (not _container.pop(temp)) { return; }

            /// NOTE: Proceeding context
            temp.awake(&call_response);
            rc = temp._coroutine.promise()._retcode;

            if (call_response not_eq riot::meta::technical::context_execution_state::e_failed
                and not temp._coroutine.done() and not rc) {

                if (temp._coroutine.promise()._current_pool not_eq *this) {
                    temp._coroutine.promise()._current_pool.push(std::move(temp));
                } else {
                    this->push(std::move(temp));
                }
            }
        }
    };

    /// Gains any number of awaitable objects. Function will return param index of the first awaited object.
    template<size_t ... IndexSeq, riot::meta::types::AwaitableType... Args>
    inline riot::async::context<size_t> await_any_non_variadic_impl(std::index_sequence<IndexSeq...>, Args&... awaitables) noexcept {

        local_awaitable_pool<sizeof...(awaitables)> LocalPool;
        std::atomic<uint8_t> done {false};
        ushort index =0;
        volatile uint* retcodes [sizeof...(awaitables)];

        (... ,
            [&]() {
                ::riot::async::context<> sig_ctx;
                sig_ctx = any_signaling_context<IndexSeq>(awaitables, done, index);
                sig_ctx._coroutine.promise()._dispatching.enabled = false;
                sig_ctx._coroutine.promise().attach_to_pool(LocalPool);
                retcodes[IndexSeq] = &sig_ctx._coroutine.promise()._retcode;
                LocalPool.push(std::move(sig_ctx));
            }()
        );

        while (not done.load(std::memory_order_acquire)) {
            LocalPool.proceed_pool();
            co_suspend;
            if (not LocalPool.empty()) continue;
            if (done.load(std::memory_order_acquire)) break;
            co_await LocalPool;
        }

        for (size_t i =0; i < sizeof...(awaitables); ++i) {
            if (i != index)
                *retcodes[i] = 1;
        }

        (... ,
            [&]() {
                if constexpr (riot::meta::types::OrderingAwaitable<Args>)
                    awaitables.clear_canceled();
            }()
        );

        co_return index;
    }

    /// Gains any number of awaitable objects. Function will return param index of the first
    /// awaited object and std::variant with awaitable object returns
    template<typename ... RetTypes, size_t ... IndexSeq, riot::meta::types::AwaitableType... Args>
    inline riot::async::context<std::pair<size_t, std::variant<RetTypes...>>> await_any_variadic_impl(
            std::index_sequence<IndexSeq...>, Args&... async) noexcept {

        std::variant<RetTypes...> result_variant;
        local_awaitable_pool<sizeof...(async)> LocalPool;
        std::atomic<uint8_t> done {false};
        ushort index =0;
        volatile uint* retcodes [sizeof...(async)];

        (... ,
            [&]() {
                riot::async::context<> sig_ctx;
                if constexpr(std::is_void_v<typename riot::meta::types::ReturnType<Args>::type>) {
                    sig_ctx = any_signaling_context<IndexSeq>(async, done, index);
                } else {
                    sig_ctx = any_signaling_context<IndexSeq>(async, done, index, result_variant);
                }
                sig_ctx._coroutine.promise().attach_to_pool(LocalPool);
                retcodes[IndexSeq] = &sig_ctx._coroutine.promise()._retcode;
                LocalPool.push(std::move(sig_ctx));
            }()
        );

        while (not done.load(std::memory_order_acquire)) {
            LocalPool.proceed_pool();
            co_suspend;
            if (not LocalPool.empty()) continue;
            if (done.load(std::memory_order_acquire)) break;
            co_await LocalPool;
        }

        for (size_t i =0; i < sizeof...(async); ++i)
            if (i != index) *retcodes[i] = 1;

        co_return std::pair{index, result_variant};
    }



    /// Gains any number of awaitable objects. All of them will be awaited. Function will return tuple of results
    template<size_t ... IndexSeq, riot::meta::types::AwaitableType... Args>
    inline riot::async::context<typename riot::meta::types::ResumeTypesToTuple<Args...>::type>
        await_all_impl(std::index_sequence<IndexSeq...>, Args&... awaitables) noexcept {

        typename riot::meta::types::ResumeTypesToTuple<Args...>::type rets;
        std::atomic<ushort> common_counter;
        local_awaitable_pool<sizeof...(awaitables)> LocalPool;

        (... ,
                [&]() {
                    ::riot::async::context<> sig_ctx;
                    if constexpr(std::is_void_v<typename riot::meta::types::ReturnType<Args>::type>) {
                        sig_ctx = all_signaling_context(common_counter, awaitables);
                    } else {
                        sig_ctx = all_signaling_context(common_counter, awaitables, std::get<IndexSeq>(rets));
                    }
                    sig_ctx._coroutine.promise()._current_pool = LocalPool;
                    sig_ctx._coroutine.promise()._original_pool = LocalPool;
                    LocalPool.push(std::move(sig_ctx));
                }()
        );

        while (common_counter.load(std::memory_order_acquire) not_eq sizeof...(awaitables)-1) {
            LocalPool.proceed_pool();
            co_suspend;
            if (not LocalPool.empty()) continue;
            if (common_counter.load(std::memory_order_acquire) == sizeof...(awaitables)-1) break;
            co_await LocalPool;
        }

        co_return rets;
    }

    template <size_t Size>
    inline consteval size_t size( auto (&)[Size] ) { return Size; }

} // namespace riot::meta::helpers


template <typename Awaiter>
struct ResultTransmitOperator {

    template <riot::meta::types::NonVoidObject Ret>
    riot::async::context<> operator >> (Ret& ret_val) noexcept { ret_val = co_await *this; }

    template <riot::meta::types::NonVoidObject Ret>
    riot::async::context<> operator >> (Ret&& ret_val) noexcept { ret_val = std::move(co_await *this); }
};


/// Gains any number of awaitable objects. Function will return param index of the first
/// awaited object or pair of first awaited object index and std::variant with return value
/// of awaited entity (if extra template params of return types given)
template<typename ... RetTypes, riot::meta::types::AwaitableType... Args>
auto await_any(Args&&... AwaitableArgs) noexcept {

    if constexpr (sizeof...(RetTypes)) {
        auto cb = riot::meta::helpers::await_any_variadic_impl<RetTypes...>(std::index_sequence_for<Args...>{}, AwaitableArgs...);
        cb._coroutine.promise()._dispatching.enabled = false;
        return std::move(cb);
    } else if constexpr (not sizeof...(RetTypes)) {
        auto cb = riot::meta::helpers::await_any_non_variadic_impl(std::index_sequence_for<Args...>{}, AwaitableArgs...);
        cb._coroutine.promise()._dispatching.enabled = false;
        return std::move(cb);
    }
}


/// Gains any number of awaitable objects. All of them will be awaited. Function will return tuple of results;
template<riot::meta::types::AwaitableType... Args>
auto await_all(Args&&... AwaitableArgs) noexcept {
    return riot::meta::helpers::await_all_impl(std::index_sequence_for<Args...>{}, AwaitableArgs...);
}


/// polling event like POLL do, requires awaitable fd and POLL event type
riot::async::context<short> await_event(const int& fd_, const short& event_type_ = POLLIN) {

    struct pollfd event{};
    event.fd = fd_;
    event.events = event_type_;
    for(int ret =0; ; ret  = poll(&event, 1, 1)) {
        if(ret == -1) {
            co_return -1;
        } else if (ret == 0) {
            co_suspend;
        } else {
            co_return event.revents;
        }
    }
}

#endif // RIOT_UTILS_H
