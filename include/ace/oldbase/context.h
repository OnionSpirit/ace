#ifndef RIOT_CONTEXT_H
#define RIOT_CONTEXT_H
#define LOG_HEADER(id) RIOT_LOG_HEADER << "context [id]"

#include "riot.h"


namespace riot::async {

template< typename ReturnType = void >
class context :
        public awaitable< context<ReturnType> >,
        riot::meta::technical::context_id {

private:

    typedef riot::meta::technical::context_execution_state State;

    template <typename Promise, typename ReturnT = ReturnType>
    class promise_mixin;

    template <typename Promise>
    class promise_mixin <Promise, void>;

    struct smart_dispatch;

    template <riot::meta::types::AwaitableType ContextT>
    context<void> void_wrapper(ContextT&& ctx) noexcept { co_await ctx; }

public:

    struct promise_type;

    typedef std::coroutine_handle<promise_type> coroutine_handle_type;

    coroutine_handle_type _coroutine;

    context() = default;

    context(const context &) =delete;

    context &operator=(const context &) =delete;

    context(context&& t) noexcept;

    context &operator=(context&& t) noexcept;

    template <riot::meta::types::NonVoidObject Ret = ReturnType>
    context<> operator >> (Ret& ret_val) noexcept { ret_val = co_await *this; }

    template <riot::meta::types::NonVoidObject Ret = ReturnType>
    context<> operator >> (Ret&& ret_val) noexcept { ret_val = std::move(co_await *this); }

    explicit context(coroutine_handle_type&& h);

    bool ready() { if (_coroutine.done()) return true; else return false; }

    bool suspend(auto& coroutineHandle);

    ReturnType resume();

    ~context() override;

    ReturnType awake(State * _res = nullptr) noexcept;

    void continue_external() noexcept;
};

RIOT_CONTEXT_META
struct RIOT_CONTEXT_SPACE smart_dispatch {
    uint8_t enabled : 1 = 1;
    uint8_t wrapped : 1 = 0;
    uint8_t is_wrapper : 1 = 0;
    uint8_t pure_resume : 1 = 0;
};

} // end riot::Awatables


namespace riot::common {

    typedef riot::async::context<> coroutine;
    typedef riot::common::atomic_queue_static<riot::common::coroutine, 1> coroutine_qifc;

    /// NOTE: Class that holds info about coroutine execution states and owner
    class context_info {
        public:

            riot::meta::technical::context_execution_state
                _call_result { riot::meta::technical::context_execution_state::e_blocked };
            uint _id =0;
    };
}


namespace riot::control {

    typedef riot::control::undefined_context_pool_handler<riot::async::context<>> context_pool_handler;

}


namespace riot::meta::types {
    template <typename T>
    concept HasPoolSetup = requires(T n) {
        { n._current_pool } -> std::same_as<riot::control::context_pool_handler&>;
        { n._original_pool } -> std::same_as<riot::control::context_pool_handler&>;
    };
}

namespace riot::async {

RIOT_CONTEXT_META
struct RIOT_CONTEXT_SPACE promise_type :
        public promise_mixin<promise_type, ReturnType> {

    promise_type() =default;

    ~promise_type() =default;

    auto initial_suspend() const noexcept { return std::suspend_always{}; }

    auto final_suspend() noexcept { return std::suspend_always{}; }

    auto get_return_object() noexcept { return context{coroutine_handle_type::from_promise(*this)}; }

    void unhandled_exception();

    void interrupt(const std::string& str);

    std::suspend_always await_transform(std::suspend_always&& e) { return e; }

    std::suspend_never await_transform(std::suspend_never&& e) { return e; }

    template <riot::meta::types::CommonAwaiter Awaiter>
    Awaiter& await_transform(Awaiter&& awaiter) { _awaitable = awaiter; return awaiter; }

    template <riot::meta::types::SchedulerCommandAwaiter Awaiter>
    Awaiter& await_transform(Awaiter&& awaiter) { return awaiter; }

    inline void attach_to_pool(riot::control::undefined_context_pool_handler<context<>> pool);

    inline void switch_pool(riot::control::undefined_context_pool_handler<context<>> pool);

    inline void attach_to_pool_forced(riot::control::undefined_context_pool_handler<context<>> pool);

    inline void switch_pool_forced(riot::control::undefined_context_pool_handler<context<>> pool);

    template <riot::meta::types::HasPoolSetup Ctx>
    inline void clone_pool_setup(const Ctx& ctx) {
        _current_pool = ctx._current_pool;
        _original_pool = ctx._original_pool;
    } //NOTE: Cant make it out-of-line because of llvm and MSVC bug (https://github.com/llvm/llvm-project/issues/56442)

    inline bool reset_pool();

    static context get_return_object_on_allocation_failure() { return context(nullptr); }

    static inline void* operator new(size_t memsize) noexcept;

    static inline void operator delete(void* memptr, size_t memsize) noexcept;

    uint id =0;
    riot::common::token _token;
    State _call_result {State::e_blocked};
    volatile uint _retcode =0;
    smart_dispatch _dispatching {};

    awaitable_subscription<context::promise_type> _awaitable{};
    riot::control::context_pool_handler _current_pool{};
    riot::control::context_pool_handler _original_pool{};
    riot::common::coroutine_qifc _external_context{};
};


RIOT_CONTEXT_META template <typename Promise, typename ReturnT>
class RIOT_CONTEXT_SPACE promise_mixin {

    Promise* _derived = static_cast<Promise*>(this);

public:

    ReturnT _current_value {};

    auto return_value(ReturnT return_value);

    auto yield_value(ReturnT yield_value);
};


RIOT_CONTEXT_META template <typename Promise>
class RIOT_CONTEXT_SPACE promise_mixin <Promise, void> {

    Promise* _derived = static_cast<Promise*>(this);

public:

    auto return_void() { return std::suspend_never{}; }
};

} // end namespace riot::async


//==============================DEFINITIONS==================================


RIOT_CONTEXT_META template <typename Promise, typename ReturnT>
auto RIOT_CONTEXT_SPACE promise_mixin<Promise, ReturnT>::return_value(ReturnT return_value) {

    _current_value =return_value;
    return std::suspend_never{};
}


RIOT_CONTEXT_META template <typename Promise, typename ReturnT>
auto RIOT_CONTEXT_SPACE promise_mixin<Promise, ReturnT>::yield_value(ReturnT yield_value) {

    _derived->_call_result = State::e_executed_with_value;
    _current_value =yield_value;
    return std::suspend_always{};
}


RIOT_CONTEXT_META
void RIOT_CONTEXT_SPACE promise_type::unhandled_exception() {
    this->interrupt("Interrupted by unhandled exception. Destroying context...");
}


RIOT_CONTEXT_META
void RIOT_CONTEXT_SPACE promise_type::interrupt(const std::string& str) {
    JunkYard::toss(JunkYard::e_fw_error, LOG_HEADER(std::to_string(id)), str);

    this->final_suspend();
    _retcode = -1;
}


RIOT_CONTEXT_META
inline void RIOT_CONTEXT_SPACE promise_type::attach_to_pool(riot::control::undefined_context_pool_handler<context<>> pool) {

    if (not this->_retcode) [[likely]] {
        _current_pool = pool;
        _original_pool = pool;
    }
}


RIOT_CONTEXT_META
inline void RIOT_CONTEXT_SPACE promise_type::switch_pool(riot::control::undefined_context_pool_handler<context<>> pool) {

    if (not this->_retcode) [[likely]] {
        _original_pool = _current_pool;
        _current_pool = pool;
    }
}


RIOT_CONTEXT_META
inline void RIOT_CONTEXT_SPACE promise_type::attach_to_pool_forced(riot::control::undefined_context_pool_handler<context<>> pool) {

    attach_to_pool(pool);
    _current_pool.push(std::forward<context<>>(Context<>(*this)));
}


RIOT_CONTEXT_META
inline void RIOT_CONTEXT_SPACE promise_type::switch_pool_forced(riot::control::undefined_context_pool_handler<context<>> pool) {

    switch_pool(pool);
    _current_pool.push(std::forward<context<>>(Context<>(*this)));
}


RIOT_CONTEXT_META
inline bool RIOT_CONTEXT_SPACE promise_type::reset_pool() {

    if (not this->_retcode) [[likely]] {
        _current_pool = _original_pool;
        return true;
    }
    return false;
}


RIOT_CONTEXT_META
inline void* RIOT_CONTEXT_SPACE promise_type::operator new(size_t memsize) noexcept {
    return riot::control::allocator::allocate(memsize);
}


RIOT_CONTEXT_META
inline void RIOT_CONTEXT_SPACE promise_type::operator delete(void* memptr, size_t memsize) noexcept {
    riot::control::allocator::deallocate(memptr, memsize);
}


RIOT_CONTEXT_META
RIOT_CONTEXT_SPACE context(context &&t) noexcept {

    _coroutine = t._coroutine;
    t._coroutine = nullptr;
}


RIOT_CONTEXT_META
RIOT_ASYNC_SPACE context<ReturnType>&
RIOT_CONTEXT_SPACE operator =(context<ReturnType> &&t) noexcept {

    if (_coroutine) [[likely]] _coroutine.destroy();
    _coroutine = t._coroutine;
    t._coroutine = nullptr;
    return *this;
}


RIOT_CONTEXT_META
RIOT_CONTEXT_SPACE context(context::coroutine_handle_type &&h) : _coroutine{h} {

    if(not context_id::lostID.pop(_coroutine.promise().id))
        _coroutine.promise().id =context_id::ID.fetch_add(1);
}


RIOT_CONTEXT_META
bool RIOT_CONTEXT_SPACE suspend(auto& external_context) {

    if (not external_context.promise()._dispatching.enabled
        or not _coroutine.promise()._dispatching.enabled) {

        _coroutine.promise()._dispatching.enabled = false;
        external_context.promise()._dispatching.enabled = false;

        _coroutine.promise()._current_pool = external_context.promise()._current_pool;
        _coroutine.promise()._original_pool = external_context.promise()._original_pool;
        awake();
        external_context.promise()._original_pool = _coroutine.promise()._original_pool;
        external_context.promise()._retcode =  _coroutine.promise()._retcode;
        if (_coroutine.promise()._retcode or _coroutine.done()
            or _coroutine.promise()._call_result == State::e_executed_with_value) {
            return false;
        } else [[likely]] {
            JunkYard::toss(JunkYard::e_fw_debug, LOG_HEADER(std::to_string(id)), "Initiate context suspending...");
            return true;
        }
    } else {

        _coroutine.promise().clone_pool_setup(external_context.promise());
        if (external_context.promise()._dispatching.is_wrapper and external_context.promise()._dispatching.pure_resume)
            _coroutine.promise()._awaitable.clear();
        awake();
        external_context.promise().clone_pool_setup(_coroutine.promise());
        external_context.promise()._retcode = _coroutine.promise()._retcode;
        if (_coroutine.promise()._retcode or _coroutine.done()
            or _coroutine.promise()._call_result == State::e_executed_with_value) [[unlikely]] { return false; }
        else [[likely]] {
            JunkYard::toss(JunkYard::e_fw_debug, LOG_HEADER(std::to_string(id)), "Initiate context suspending...");
            JunkYard::toss(JunkYard::e_fw_debug, LOG_HEADER(std::to_string(id)), "Smart dispatch started...");
            if constexpr (riot::meta::types::NonVoidObject<ReturnType>) {
                if (not _coroutine.promise()._dispatching.wrapped) {
                    _coroutine.promise()._dispatching.wrapped = true;
                    auto ctx = void_wrapper(std::forward<context>(*this));
                    ctx._coroutine.promise()._dispatching.is_wrapper = true;
                    ctx._coroutine.promise().clone_pool_setup(_coroutine.promise());
                    external_context.promise()._current_pool = ctx._coroutine.promise()._external_context;
                    _coroutine.promise()._current_pool.push(std::move(ctx));

                    JunkYard::toss(JunkYard::e_fw_debug, LOG_HEADER(std::to_string(id)), "Non-void context wrapped and set to current pool");
                }
            } else if constexpr (riot::meta::types::VoidObject<ReturnType>) {
                external_context.promise()._current_pool = _coroutine.promise()._external_context;
                _coroutine.promise()._current_pool.push(std::move(*this));
                JunkYard::toss(JunkYard::e_fw_debug, LOG_HEADER(std::to_string(id)), "Void context set to current pool");
            }
            return true;
        }
    }
}


RIOT_CONTEXT_META
ReturnType RIOT_CONTEXT_SPACE resume() {

    if constexpr (riot::meta::types::NonVoidObject<ReturnType>) {
        return _coroutine.promise()._current_value;
    }
}


RIOT_CONTEXT_META
void RIOT_CONTEXT_SPACE continue_external() noexcept {

    if (riot::common::coroutine ctx;
            _coroutine.promise()._dispatching.enabled and _coroutine.promise()._external_context.pop(ctx)
        and ctx._coroutine) {
        JunkYard::toss(JunkYard::e_fw_debug, LOG_HEADER(std::to_string(id)), "Dispatched context ended, resuming external task");
        if (ctx._coroutine.promise()._dispatching.is_wrapper) ctx._coroutine.promise()._dispatching.pure_resume = true;
        else ctx._coroutine.promise()._awaitable.clear();
        ctx.awake();
        if (not (ctx._coroutine.promise()._retcode or ctx._coroutine.done()
                 or ctx._coroutine.promise()._call_result == State::e_executed_with_value)) {
            ctx._coroutine.promise()._current_pool.push(std::move(ctx));
        } else { ctx.continue_external(); }
    }
}


RIOT_CONTEXT_META
RIOT_CONTEXT_SPACE ~context() {

    if (_coroutine) [[likely]] {
        continue_external();
        while (not context_id::lostID.push(std::move(_coroutine.promise().id)));
        _coroutine.destroy();
    }
}


RIOT_CONTEXT_META
ReturnType RIOT_CONTEXT_SPACE awake(typename RIOT_CONTEXT_SPACE State * const _res) noexcept {

    if (_coroutine) [[likely]] {

        // std::cout << "RESUMING ID: " << _coroutine.promise().id << std::endl;

        _coroutine.promise()._call_result = State::e_blocked;

        if (not _coroutine.promise()._awaitable) {

        if constexpr (riot::meta::types::VoidObject<ReturnType>) {
            _coroutine();
            _coroutine.promise()._call_result = State::e_executed;
        } else {
            _coroutine();
            if (_coroutine.promise()._call_result not_eq State::e_executed_with_value) {
                _coroutine.promise()._call_result = State::e_executed;
            }
        }

        } else if (_coroutine.promise()._awaitable.ready() or
                   not _coroutine.promise()._awaitable.suspend(_coroutine)) {

            _coroutine.promise()._awaitable.clear();
            _coroutine();
            _coroutine.promise()._call_result = State::e_executed;

        } else { _coroutine.promise()._call_result = State::e_blocked; }

        if (_res != nullptr) {
            *_res = _coroutine.promise()._call_result;
        }

        if constexpr (riot::meta::types::NonVoidObject<ReturnType>) return ReturnType{};
    }

    if constexpr (riot::meta::types::NonVoidObject<ReturnType>) {
        return this->_coroutine.promise()._current_value;
    }
}

#undef LOG_HEADER
#undef RIOT_CONTEXT_META
#undef RIOT_CONTEXT_SPACE
#endif // RIOT_CONTEXT_H
