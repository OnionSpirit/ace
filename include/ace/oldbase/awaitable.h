/**
 * @file
 * @details This file contains an awaitable class to provide async interface for the inherited type.
 * Also contains awaitable_subscription class that allows to store any of the awaitable inheritors types
 * into the instance of this type
 */
#ifndef RIOT_AWAITABLE_H
#define RIOT_AWAITABLE_H

#include "meta_macro.h"
#include "riot.h"


namespace riot::async {

/**
 * @details Mixin class to provide support for custom awaitable objects into framework
 * @tparam Derived awaitable derived class
 * @tparam ManagementStrategy Implementation strategy that specifies class
 * behavior and extra operations - Basic by defaults.
 * @remark Derived class must represent following functions:
 * @remark @b bool @b ready() - Checks if context is ready to continue.
 * Return @b True to continue, @b False otherwise.
 * @remark @b bool @b suspend(auto) - Checks if context is required to be suspended or not.
 * Return @b True to suspend, @b False otherwise.
 * @remark @b auto @b resume() - Represents some functionality before context continuing.
 * Allows to return user-defined result through @b co_await operator.
 */
template
<
    typename Derived,

    template <typename>
    typename ManagementStrategy = riot::component_modes::async::awaitable::management_strategy::polling
>
requires riot::component_modes::async::awaitable::management_strategy::ModeRequirement<ManagementStrategy<Derived>>
class awaitable :
    public ManagementStrategy<Derived> {

public:

    /**
     * @details Checks if context is ready to continue.
     * @details Uses user specified @b ready() function
     * @remark Function name and interface was specified by C++20 standard.
     * @remark Part of interface that co_await operator uses
     * @return @b True, if context is ready to continue
     * and calls @b await_resume(), @b False otherwise and calls @b await_suspend()
     */
    bool await_ready() { return ManagementStrategy<Derived>::_derived->ready(); }

    /**
     * @details Checks if context is required to be suspended or not.
     * @details Uses user specified @b suspend(auto) function
     * @param coroutine context that called @b co_await @b operator with
     * @b awaitable object, and needed to be checked to allow continuing
     * @remark Function name and interface was specified by C++20 standard.
     * @remark Part of interface that co_await operator uses
     * @return @b True, if object requires context suspension,
     * @b False otherwise and calls @b await_resume()
     */
    bool await_suspend(auto coroutine) const { return ManagementStrategy<Derived>::await_suspend(coroutine); }

    /**
     * @details Represents some functionality before context continuing
     * @details Uses user specified @b resume() function
     * @remark Function name and interface was specified by C++20 standard.
     * @remark Part of interface that co_await operator uses
     * @return Call result of user specified @b resume() function
     */
    auto await_resume() const { return ManagementStrategy<Derived>::_derived->resume(); }

    /**
     * @details Allows
     * use created object with @b co_await
     * @b operator inside context code,
     * and makes derived class @b awaitable
     */
    auto&& operator co_await();

    /**
     * @details Dummy function to help getting return types of awaitable objects
     * @return Constructed instance of return type
     */
    [[maybe_unused]] static auto ret_type_dummy();

    virtual ~awaitable() = default;
};


/**
 * @details Common type to store object that
 * can hold and provide interface of
 * awaitable derived classes
 * @tparam ContextPromise Type of context promise struct
 */
template <typename ContextPromise>
class awaitable_subscription {
public:

    /**
     * @details Ready function of stored object
     * @return @b True, if context is ready to continue, @b False otherwise
     */
    bool ready() { return _ready_impl(_awaiter); }

    /**
     * @details Suspend function of stored object
     * @param coroutine coroutine handler to be processed
     * @return @b True, if stored object requires context suspension, @b False otherwise
     */
    bool suspend(std::coroutine_handle<ContextPromise> coroutine) { return _suspend_impl(_awaiter, coroutine); }

    /**
     * @details Clears stored object
     * @return void
     */
    void clear() const;

    /**
     * @details Calls delete function of stored object, if it was represented
     * @remark If object was passed as pointer class
     * generates delete function for it, this function calls it
     * @return void
     */
    void free() { if (_delete_impl) _delete_impl(_awaiter); }


    awaitable_subscription() = default;

    ~awaitable_subscription() { clear(); }

    template<riot::meta::types::CommonAwaiter Awaiter>
    awaitable_subscription(Awaiter &awaiter) { this->assign(awaiter); }

    template<riot::meta::types::CommonAwaiter Awaiter>
    awaitable_subscription(Awaiter *awaiter) { this->assign(awaiter); }

    explicit operator bool() const;

    /**
     * @details Subscribes object to any awaitable derived class
     * @tparam Awaiter Type of object to subscribe
     * @param awaiter Object reference to subscribe
     * @return This class instance reference
     * @remark Uses corresponding @b assign function
     */
    template<riot::meta::types::CommonAwaiter Awaiter>
    awaitable_subscription& operator=(Awaiter& awaiter) noexcept;

    /**
     * @details Subscribes itself to any awaitable derived class
     * @tparam Awaiter Type of object to subscribe
     * @param awaiter Object pointer to subscribe
     * @return This class instance reference
     * @remark Uses corresponding @b assign function
     */
    template<riot::meta::types::CommonAwaiter Awaiter>
    awaitable_subscription& operator=(Awaiter* awaiter) noexcept;

    /**
     * @details Compares stored and passed object references
     * @tparam Awaiter Type of object to compare
     * @param awaiter Object reference to compare with subscribed object
     * @return Bool result of operation
     */
    template <riot::meta::types::CommonAwaiter Awaiter>
    bool operator==(const Awaiter& awaiter) const { return (static_cast<Awaiter*>(_awaiter) == &awaiter); }

private:

    /**
     * @details Subscribes itself to any awaitable derived class
     * @tparam Awaiter Type of object to subscribe
     * @param awaiter Object reference to subscribe
     * @return Object reference that was stored
     */
    template<riot::meta::types::CommonAwaiter Awaiter>
    Awaiter& assign(Awaiter& awaiter);

    /**
     * @details Subscribes itself to any awaitable derived class
     * @tparam Awaiter Type of object to subscribe
     * @param awaiter Object pointer to subscribe
     * @return Object reference that was stored
     */
    template<riot::meta::types::CommonAwaiter Awaiter>
    Awaiter& assign(Awaiter* awaiter);

    /**
     * @details Static wrapper for delete function generation
     * @tparam Awaiter Object type to specify delete function type
     * @param awaiter Object pointer to delete
     * @return void
     */
    template <riot::meta::types::CommonAwaiter Awaiter>
    static void delete_static_wrap(void* awaiter) { delete reinterpret_cast<Awaiter*>(awaiter); }

    /**
     * @details Represents static wrapper that can be type specified
     * for class method call, and to be stored into C-style callback
     * @param awaiter - Pointer to object that has @b ready() function
     * @return Result of @b awaiter->ready() function call
     */
    template <riot::meta::types::CommonAwaiter Awaiter>
    static bool ready_static_wrap(void* awaiter);

    /**
     * @details Represents static wrapper that can be type specified
     * for class method call, and to be stored into C-style callback
     * @param awaiter - Pointer to object that has @b suspend(auto) function
     * @return Result of @b awaiter->suspend(auto) function call
     */
    template <riot::meta::types::CommonAwaiter Awaiter>
    static bool suspend_static_wrap(void* awaiter, auto coroutine);

    mutable void* _awaiter {nullptr}; ///< Stored object
    mutable bool (*_ready_impl)(void*) {nullptr}; ///< Ready function callback
    mutable bool (*_suspend_impl)(void*, std::coroutine_handle<ContextPromise>) {nullptr}; ///< Suspend function callback
    mutable void(*_delete_impl)(void*) {nullptr}; ///< Delete function callback, generates if object was passed as ptr
};

} // namespace riot::async


//==============================DEFINITIONS==================================


RIOT_AWAITABLE_META
auto RIOT_AWAITABLE_SPACE ret_type_dummy() {
    return awaitable().await_resume();
}


RIOT_AWAITABLE_META
auto&& RIOT_AWAITABLE_SPACE operator co_await() {
return std::move(*this);
}


#undef RIOT_AWAITABLE_META
#undef RIOT_AWAITABLE_SPACE


RIOT_AWAITABLE_SUBSCRIPTION_META
void RIOT_AWAITABLE_SUBSCRIPTION_SPACE clear() const {
    _awaiter = nullptr;
    _ready_impl = nullptr;
    _suspend_impl = nullptr;
    _delete_impl = nullptr;
}


RIOT_AWAITABLE_SUBSCRIPTION_META
RIOT_AWAITABLE_SUBSCRIPTION_SPACE operator bool() const {
    if (_awaiter == nullptr or _ready_impl == nullptr or _suspend_impl == nullptr) return false;
    else return true;
}


RIOT_AWAITABLE_SUBSCRIPTION_META template<riot::meta::types::CommonAwaiter Awaiter>
RIOT_ASYNC_SPACE awaitable_subscription<ContextPromise>&
RIOT_AWAITABLE_SUBSCRIPTION_SPACE operator=(Awaiter& awaiter) noexcept {

    this->assign(awaiter);
    return *this;
}


RIOT_AWAITABLE_SUBSCRIPTION_META template<riot::meta::types::CommonAwaiter Awaiter>
RIOT_ASYNC_SPACE awaitable_subscription<ContextPromise>&
RIOT_AWAITABLE_SUBSCRIPTION_SPACE operator=(Awaiter* awaiter) noexcept {
    this->assign(awaiter);
    return *this;
}


RIOT_AWAITABLE_SUBSCRIPTION_META template<riot::meta::types::CommonAwaiter Awaiter>
Awaiter& RIOT_AWAITABLE_SUBSCRIPTION_SPACE assign(Awaiter& awaiter) {
    clear();
    _awaiter = reinterpret_cast<void*>(&awaiter);
    _ready_impl = ready_static_wrap<Awaiter>;
    _suspend_impl = suspend_static_wrap<Awaiter>;
    return awaiter;
}


RIOT_AWAITABLE_SUBSCRIPTION_META template<riot::meta::types::CommonAwaiter Awaiter>
Awaiter& RIOT_AWAITABLE_SUBSCRIPTION_SPACE assign(Awaiter* awaiter) {
    clear();
    _awaiter = reinterpret_cast<void*>(&awaiter);
    _ready_impl = ready_static_wrap<Awaiter>;
    _suspend_impl = suspend_static_wrap<Awaiter>;
    _delete_impl = delete_static_wrap<Awaiter>;
    return awaiter;
}


RIOT_AWAITABLE_SUBSCRIPTION_META template <riot::meta::types::CommonAwaiter Awaiter>
bool RIOT_AWAITABLE_SUBSCRIPTION_SPACE ready_static_wrap(void* awaiter) {
    auto a = reinterpret_cast<Awaiter*>(awaiter);
    return a->ready();
}


RIOT_AWAITABLE_SUBSCRIPTION_META template <riot::meta::types::CommonAwaiter Awaiter>
bool RIOT_AWAITABLE_SUBSCRIPTION_SPACE suspend_static_wrap(void* awaiter, auto coroutine) {
    auto a = reinterpret_cast<Awaiter*>(awaiter);
    return a->suspend(coroutine);
}

#undef RIOT_AWAITABLE_SUBSCRIPTION_META
#undef RIOT_AWAITABLE_SUBSCRIPTION_SPACE
#endif // RIOT_AWAITABLE_H
