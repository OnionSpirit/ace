/**
 * @file
 * @details This file contains AwaitableChannel declaration
 */
#ifndef RIOT_AWAITABLE_CHANNEL_H
#define RIOT_AWAITABLE_CHANNEL_H

#include "riot.h"


namespace riot::async {

/**
 * @details Channel with async call operator @b(co_await) support.
 * @tparam Type Storable data type.
 * @tparam DataBufferSize Size of data buffer
 * @tparam DataBufferAllocationPolicy Data buffer allocation policy
 * @tparam WaitersBufferSize Size of waiters buffer
 * @tparam WaitersBufferAllocationPolicy Waiters buffer allocation policy
 */
template
<
    typename Type,

    size_t DataBufferSize = 16ul,

    template <typename, size_t>
    typename DataBufferAllocationPolicy = riot::component_modes::allocation::dynamic_mode,

    size_t WaitersBufferSize = 16ul,

    template <typename, size_t>
    typename WaitersBufferAllocationPolicy = riot::component_modes::allocation::dynamic_mode
>
requires riot::component_modes::allocation::ModeRequirement<DataBufferAllocationPolicy, Type, DataBufferSize>
     and riot::component_modes::allocation::ModeRequirement<WaitersBufferAllocationPolicy,
                                                  riot::common::coroutine, WaitersBufferSize>
class channel {

    class pull_impl;

    void reset();

    typedef DataBufferAllocationPolicy<Type, DataBufferSize>::container DataStorage;
    typedef WaitersBufferAllocationPolicy<riot::common::coroutine, WaitersBufferSize>::container WaitersStorage;

public:

    DataStorage _container; ///< Storage of transmitting data
    WaitersStorage _waiters; ///< Storage of waiting contexts

    channel() = default;

    explicit operator bool() const { return empty(); };

    /**
     * @details pushes data to tha channel
     * @param data data to push
     * @return False if inner buffer overflowed
     */
    bool push(Type& data);

    /**
     * @details pushes data to tha channel
     * @param data data to push
     * @return False if inner buffer overflowed
     */
    bool push(Type&& data);

    /**
     * @details Checks if channel is empty
     * @return @b True if channel is empty, @b False otherwise
     */
    [[nodiscard]] bool empty() { return _container.empty(); }

    /**
     * @details Represents async operation of gaining data from the channel.
     * @return Returns instance of awaitable pull object,
     * that can be processed with @b co_await
     */
    [[nodiscard]] pull_impl pull();

    /**
     * @details @b push method alternative interface
     * @param data Data to push
     */
    void operator << (Type& data) { push(data); }

    /**
     * @details @b push method alternative interface
     * @param data Data to push
     */
    void operator << (Type&& data) { push(std::move(data)); }

    /**
     * @details @b pull method alternative interface
     * @param data Data to pull
     */
    [[nodiscard]] context<> operator >> (Type& data) { data = co_await pull(); }

    /**
     * @details @b pull method alternative interface
     * @param data Data to pull
     */
    [[nodiscard]] context<> operator >> (Type&& data) { data = std::move(co_await pull()); }
};


template <typename Type, size_t DataBufferSize = 16ul, size_t WaitersBufferSize = 4ul>
using channel_static_static = channel
<
    Type,
    DataBufferSize,
    riot::component_modes::allocation::static_mode,
    WaitersBufferSize,
    riot::component_modes::allocation::static_mode
>;


template <typename Type, size_t DataBufferSize = 16ul, size_t WaitersBufferSize = 4ul>
using channel_static_dynamic = channel
<
    Type,
    DataBufferSize,
    riot::component_modes::allocation::static_mode,
    WaitersBufferSize,
    riot::component_modes::allocation::dynamic_mode
>;


template <typename Type, size_t DataBufferSize = 16ul, size_t WaitersBufferSize = 4ul>
using channel_dynamic_static = channel
<
    Type,
    DataBufferSize,
    riot::component_modes::allocation::dynamic_mode,
    WaitersBufferSize,
    riot::component_modes::allocation::static_mode
>;

} // namespace riot::async

//==============================DEFINITIONS==================================


RIOT_AWAITABLE_CHANNEL_META
class RIOT_AWAITABLE_CHANNEL_SPACE pull_impl : public awaitable<pull_impl> {

    Type _output_data{};

public:

    pull_impl() =delete;

    pull_impl(WaitersStorage & waiters, DataStorage& container)
            : _waiters(&waiters), _container(&container) {};

    WaitersStorage* _waiters;

    DataStorage* _container;

    bool ready();

    bool suspend(auto& ctx);

    auto resume() { return std::forward<Type>(_output_data); }
};


RIOT_AWAITABLE_CHANNEL_META
void RIOT_AWAITABLE_CHANNEL_SPACE reset() {

    riot::common::coroutine temp;
    if (_waiters.pop(temp)) [[likely]] {
        temp._coroutine.promise().reset_pool();
        temp._coroutine.promise()._original_pool.push(std::move(temp));
    }
}


RIOT_AWAITABLE_CHANNEL_META
bool RIOT_AWAITABLE_CHANNEL_SPACE push(Type& data) {

    if (_container.push(std::forward<Type>(Type(data)))) [[likely]] {
        reset();
        return true;
    } return false;
}


RIOT_AWAITABLE_CHANNEL_META
bool RIOT_AWAITABLE_CHANNEL_SPACE push(Type&& data) {

    if (_container.push(std::forward<Type>(data))) [[likely]] {
        reset();
        return true;
    } return false;
}


RIOT_AWAITABLE_CHANNEL_META
RIOT_AWAITABLE_CHANNEL_SPACE pull_impl RIOT_AWAITABLE_CHANNEL_SPACE pull() {
    return channel::pull_impl(_waiters, _container);
}


RIOT_AWAITABLE_CHANNEL_META
bool RIOT_AWAITABLE_CHANNEL_SPACE pull_impl::ready() {
    return _container->pop(_output_data);
}


RIOT_AWAITABLE_CHANNEL_META
bool RIOT_AWAITABLE_CHANNEL_SPACE pull_impl::suspend(auto& ctx) {
    const bool res = _container->pop(_output_data);

    if (not res) {
        ctx.promise().switch_pool(_waiters);
        return true;
    }
    return false;
}

#undef RIOT_AWAITABLE_CHANNEL_META
#undef RIOT_AWAITABLE_CHANNEL_SPACE
#endif // RIOT_AWAITABLE_CHANNEL_H
