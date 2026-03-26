/**
 * @file
 * @details This file contains Channel future declaration
 */
#ifndef ACE_FUTURE_CHANNEL_H
#define ACE_FUTURE_CHANNEL_H

#include "future.h"
#include "ace/common/selection.h"
#include <nukes/dynamic/mpmc_queue.h>
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

    size_t data_buffer_size_v = 1ul,

    allocation_type data_allocation_v = allocation_type::e_dynamic,

    size_t waiters_buffer_size_v = 1ul,

    allocation_type waiters_allocation_v = allocation_type::e_dynamic
>
class channel {

    template <typename storage_entity_t, allocation_type allocation_v, size_t buff_len_v>
    static auto consteval define_storage() {
        if constexpr (allocation_v == allocation_type::e_dynamic)
            return nukes::dynamic::mpmc_queue<storage_entity_t>{};
        if constexpr (allocation_v == allocation_type::e_static)
            return nukes::bounded::mpmc_queue<storage_entity_t, buff_len_v>{};
        if constexpr (allocation_v == allocation_type::e_on_init)
            return nukes::bounded::mpmc_queue<storage_entity_t>{};
    }

    static auto consteval define_data_storage() {
        return define_storage<data_t, data_allocation_v, data_buffer_size_v>();
    }

    static auto consteval define_waiters_storage() {
        return define_storage<async<>, waiters_allocation_v, waiters_buffer_size_v>();
    }

    typedef std::decay_t<decltype(define_data_storage())> data_storage_t;
    typedef std::decay_t<decltype(define_waiters_storage())> waiters_storage_t;

    class pull_impl;
    friend pull_impl;

    struct channel_conductor;
    friend channel_conductor;

    void notify();

public:

    data_storage_t _container; ///< Storage of transmitting data
    waiters_storage_t _waiters; ///< Storage of waiting contexts

    channel() = default;

    explicit operator bool() const { return empty(); };

    /**
     * @brief The function pushes data to the channel
     * @param data data to push
     * @return False if inner buffer overflowed
     */
    bool push(data_t& data);

    /**
     * @brief The function pushes data to the channel
     * @param data data to push
     * @return False if inner buffer overflowed
     */
    bool push(data_t&& data);

    /**
     * @brief The function pushes data to the channel with waiting for a vacant spot in the data queue
     * @param data data to push
     */
    promise<> pending_push(data_t data);

    /**
     * @brief The function pushes data to the channel with waiting for a vacant spot in the data queue
     * @param data data to push
     */
    promise<> pending_push(data_t&& data);

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
    void operator << (data_t& data) { push(std::forward<data_t&>(data)); }

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


/**
 * @brief Static Channel with bounded amount of waiters
 */
template <typename Type, size_t DataBufferSize = 1ul, size_t WaitersBufferSize = 1ul>
using channel_static = channel
<
    Type,
    DataBufferSize,
    allocation_type::e_static,
    WaitersBufferSize,
    allocation_type::e_static
>;

/**
 * @brief Dynamic Channel with bounded amount of waiters
 */
template <typename Type, size_t WaitersBufferSize = 1ul>
using channel_dyn = channel
<
    Type,
    1ul,
    allocation_type::e_dynamic,
    WaitersBufferSize,
    allocation_type::e_static
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
class ACE_FUTURE_CHANNEL_SPACE pull_impl : public busy_future_traits<pull_impl> {

    data_t _output_data{};

public:

    IMPORT_BUSY_FUTURE_ENV(pull_impl)

    pull_impl() =delete;

    pull_impl(waiters_storage_t* waiters, data_storage_t* container)
            : _waiters(waiters), _container(container) {};

    waiters_storage_t* _waiters;

    data_storage_t* _container;

    bool await_ready() override;

    bool await_suspend(auto ctx);

    auto await_resume() { return std::forward<data_t>(_output_data); }
};


ACE_FUTURE_CHANNEL_META
struct ACE_FUTURE_CHANNEL_SPACE channel_conductor : conductor_handler_t {

    channel_conductor() = delete;

    explicit channel_conductor(waiters_storage_t* waiters) : _waiters(waiters) {};

    void forward(async<>&& ctx) override { while (not _waiters->push(std::move(ctx))) {}; }

    // TODO: Finish later
    void cancel() override {  }

    ~channel_conductor() override = default;

    waiters_storage_t* _waiters;
};


ACE_FUTURE_CHANNEL_MEMBER(void) notify() {
    if (async<> ctx; _waiters.pop(ctx)) [[likely]]
        core::runner::reattach(std::move(ctx));
}


ACE_FUTURE_CHANNEL_MEMBER(bool) push(data_t& data) {
    if (_container.push(std::forward<data_t&>(data))) [[likely]] {
        notify();
        return true;
    }
    return false;
}


ACE_FUTURE_CHANNEL_MEMBER(bool) push(data_t&& data) {
    if (_container.push(std::forward<data_t&&>(data))) [[likely]] {
        notify();
        return true;
    }
    return false;
}

ACE_FUTURE_CHANNEL_MEMBER(ace::promise<>) pending_push(data_t data) {
    while (not _container.push(std::forward<data_t>(data))) [[unlikely]]
        co_await suspend();
    notify();
    co_return;
}


ACE_FUTURE_CHANNEL_MEMBER(ace::promise<>) pending_push(data_t&& data) {
    while (not _container.push(std::forward<data_t>(data))) [[unlikely]]
        co_await suspend();
    notify();
    co_return;
}


ACE_FUTURE_CHANNEL_META
ACE_FUTURE_CHANNEL_SPACE pull_impl
ACE_FUTURE_CHANNEL_SPACE pull() {
    return std::forward<pull_impl>(pull_impl{&_waiters, &_container});
}


ACE_FUTURE_CHANNEL_MEMBER(bool) pull_impl::await_ready() {
    return _container->pop(_output_data);
}

ACE_FUTURE_CHANNEL_MEMBER(bool) pull_impl::await_suspend(auto ctx) {
    if (not _container->pop(_output_data)) {
        ctx.promise()._runner_conductor = channel_conductor{_waiters};
        return true;
    }
    return false;
}

#undef ACE_FUTURE_CHANNEL_META
#undef ACE_FUTURE_CHANNEL_SPACE
#endif // ACE_FUTURE_CHANNEL_H
