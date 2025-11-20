/**
 * @file
 * @details This file contains Channel future declaration
 */
#ifndef ACE_FUTURE_CHANNEL_H
#define ACE_FUTURE_CHANNEL_H

#include <future>

#include "future.h"
#include "ace/common/selection.h"
#include <nukes/dynamic/mpsc_queue.h>
#include <nukes/dynamic/mpmc_queue.h>
#include <nukes/bounded/mpsc_queue.h>
#include <nukes/bounded/mpmc_queue.h>

#include "ace/core/runner.h"
#include "ace/coroutines/context.h"


namespace ace::futures {

/**
 * @details Channel with async call operator @b(co_await) support.
 * @tparam data_t Storable data type.
 * @tparam data_buffer_size_v Size of data buffer
 * @tparam data_allocation_v Data buffer allocation policy
 * @tparam waiters_buffer_size_v Size of waiters buffer
 * @tparam waiters_allocation_v Waiters buffer allocation policy
 */
template
<
    typename data_t,

    size_t data_buffer_size_v = 16ul,

    allocation_type data_allocation_v = allocation_type::e_dynamic,

    size_t waiters_buffer_size_v = 16ul,

    allocation_type waiters_allocation_v = allocation_type::e_dynamic
>
class channel {

    class pull_impl;

    struct channel_conductor;

    void reset();

    static auto consteval define_data_storage() {
        if constexpr (waiters_allocation_v == allocation_type::e_dynamic)
            return nukes::dynamic::mpmc_queue<data_t>{};
        if constexpr (waiters_allocation_v == allocation_type::e_static)
            return nukes::bounded::mpmc_queue<data_t, data_buffer_size_v>{};
        if constexpr (waiters_allocation_v == allocation_type::e_on_init)
            return nukes::bounded::mpmc_queue<data_t>{};
    }

    static auto consteval define_waiters_storage() {
        if constexpr (waiters_allocation_v == allocation_type::e_dynamic)
            return nukes::dynamic::mpsc_queue<async<>>{};
        if constexpr (waiters_allocation_v == allocation_type::e_static)
            return nukes::bounded::mpsc_queue<async<>, waiters_buffer_size_v>{};
        if constexpr (waiters_allocation_v == allocation_type::e_on_init)
            return nukes::bounded::mpsc_queue<async<>>{};
    }

    typedef std::decay_t<decltype(define_data_storage())> data_storage_t;
    typedef std::decay_t<decltype(define_waiters_storage())> waiters_storage_t;

    friend pull_impl;

    friend channel_conductor;

public:

    data_storage_t _container; ///< Storage of transmitting data
    waiters_storage_t _waiters; ///< Storage of waiting contexts

    channel() = default;

    explicit operator bool() const { return empty(); };

    /**
     * @details pushes data to tha channel
     * @param data data to push
     * @return False if inner buffer overflowed
     */
    bool push(data_t& data);

    /**
     * @details pushes data to tha channel
     * @param data data to push
     * @return False if inner buffer overflowed
     */
    bool push(data_t&& data);

    /**
     * @details Checks if channel is empty
     * @return @b True if channel is empty, @b False otherwise
     */
    [[nodiscard]] bool empty() { return _container.empty(); }

    /**
     * @details Represents async operation of gaining data from the channel.
     * @return Returns instance of future pull object,
     * that can be processed with @b co_await
     */
    [[nodiscard]] pull_impl pull();

    /**
     * @details @b push method alternative interface
     * @param data Data to push
     */
    void operator << (data_t& data) { push(data); }

    /**
     * @details @b push method alternative interface
     * @param data Data to push
     */
    void operator << (data_t&& data) { push(std::move(data)); }

    /**
     * @details @b pull method alternative interface
     * @param data Data to pull
     */
    [[nodiscard]] async<> operator >> (data_t& data) { data = co_await pull(); }

    /**
     * @details @b pull method alternative interface
     * @param data Data to pull
     */
    [[nodiscard]] async<> operator >> (data_t&& data) { data = std::move(co_await pull()); }
};


template <typename Type, size_t DataBufferSize = 16ul, size_t WaitersBufferSize = 4ul>
using channel_bounded = channel
<
    Type,
    DataBufferSize,
    allocation_type::e_static,
    WaitersBufferSize
>;


template <typename Type, size_t DataBufferSize = 16ul, size_t WaitersBufferSize = 4ul>
using channel_dyn = channel
<
    Type,
    DataBufferSize,
    allocation_type::e_dynamic,
    WaitersBufferSize
>;


} // namespace ace::futures

//==============================DEFINITIONS==================================

#define ACE_FUTURE_CHANNEL_META          \
template<                                \
    typename data_t,                     \
    size_t data_buffer_size_v,           \
    allocation_type data_allocation_v,   \
    size_t waiters_buffer_size_v,        \
    allocation_type waiters_allocation_v \
>

#define ACE_FUTURE_CHANNEL_SPACE \
ace::futures::channel<data_t, data_buffer_size_v, data_allocation_v, waiters_buffer_size_v, waiters_allocation_v>::

#define ACE_FUTURE_CHANNEL_MEMBER(returnT) \
ACE_FUTURE_CHANNEL_META returnT ACE_FUTURE_CHANNEL_SPACE


ACE_FUTURE_CHANNEL_META
class ACE_FUTURE_CHANNEL_SPACE pull_impl : public future_traits<pull_impl> {

    data_t _output_data{};

public:

    pull_impl() =delete;

    pull_impl(waiters_storage_t & waiters, data_storage_t& container)
            : _waiters(&waiters), _container(&container) {};

    waiters_storage_t* _waiters;

    data_storage_t* _container;

    bool ready();

    bool suspend(auto& ctx);

    auto resume() { return std::forward<data_t>(_output_data); }
};


ACE_FUTURE_CHANNEL_META
struct ACE_FUTURE_CHANNEL_SPACE channel_conductor final : conductor_handler_t {

    channel_conductor() =delete;

    explicit channel_conductor(waiters_storage_t & waiters) : _waiters(&waiters) {};

    waiters_storage_t* _waiters;

    void forward(async<>&& ctx) override { _waiters->push(std::move(ctx)); }

    ~channel_conductor() override = default;
};


ACE_FUTURE_CHANNEL_MEMBER(void) reset() {
    if (async<> temp; _waiters.pop(temp)) [[likely]]
        core::runner::schedule(std::move(temp));
}


ACE_FUTURE_CHANNEL_MEMBER(bool) push(data_t& data) {
    if (_container.push(std::forward<data_t>(data_t(data)))) [[likely]] {
        reset();
        return true;
    }
    return false;
}


ACE_FUTURE_CHANNEL_MEMBER(bool) push(data_t&& data) {
    if (_container.push(std::forward<data_t>(data))) [[likely]] {
        reset();
        return true;
    }
    return false;
}


ACE_FUTURE_CHANNEL_META
ACE_FUTURE_CHANNEL_SPACE pull_impl
ACE_FUTURE_CHANNEL_SPACE pull() {
    return pull_impl(_waiters, _container);
}


ACE_FUTURE_CHANNEL_MEMBER(bool) pull_impl::ready() {
    return _container->pop(_output_data);
}

ACE_FUTURE_CHANNEL_MEMBER(bool) pull_impl::suspend(auto& ctx) {
    if (not _container->pop(_output_data)) {
        ctx.promise()._condutor = std::make_unique<conductor_handler_t>(channel_conductor(_waiters));
        return true;
    }
    return false;
}

#undef ACE_FUTURE_CHANNEL_META
#undef ACE_FUTURE_CHANNEL_SPACE
#endif // ACE_FUTURE_CHANNEL_H
