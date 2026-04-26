/**
 * @file
 * @details This file contains Channel future declaration
 */
#ifndef ACE_FUTURE_CHANNEL_H
#define ACE_FUTURE_CHANNEL_H

#include <nukes/dynamic/mpmc_queue.h>
#include <nukes/bounded/mpmc_queue.h>

#include <ace/core/traits/future.h>
#include <ace/core/runner.h>
#include <ace/core/context.h>


namespace ace::futures {

    enum class allocation_type {
        e_static,
        e_on_init,
        e_dynamic
    };

/**
 * @details Channel with async call operator @b(co_await) support.
 * @tparam data_t Storable data type.
 * @tparam data_buffer_size_v Size of data buffer
 * @tparam data_allocation_v Data buffer allocation policy
 */
template
<
    typename data_t,

    size_t data_buffer_size_v = 1ul,

    allocation_type data_allocation_v = allocation_type::e_dynamic
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

    typedef std::decay_t<decltype(define_data_storage())> data_storage_t;
    typedef nukes::dynamic::mpmc_queue<task> waiters_storage_t;

    class pull_impl;
    friend pull_impl;

    struct channel_conductor;
    friend channel_conductor;

    void notify();

public:

    data_storage_t _container  {}; ///< Storage of transmitting data
    waiters_storage_t _waiters {}; ///< Storage of waiting contexts

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
    [[nodiscard]] task operator >> (data_t& data) { data = co_await pull(); }

    /**
     * @details @b pull method alternative interface
     * @param data Data to pull
     */
    [[nodiscard]] task operator >> (data_t&& data) { data = std::move(co_await pull()); }
};

template<typename data_t>
class channel_st {

    class pull_impl;
    friend pull_impl;

    struct channel_conductor;
    friend channel_conductor;

    void notify();

    typedef std::queue<data_t> data_storage_t;
    typedef nukes::dynamic::reg_queue<task> waiters_storage_t;

public:

    data_storage_t _container  {}; ///< Storage of transmitting data
    waiters_storage_t _waiters {}; ///< Storage of waiting contexts

    channel_st() = default;

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
    [[nodiscard]] task operator >> (data_t& data) { data = co_await pull(); }

    /**
     * @details @b pull method alternative interface
     * @param data Data to pull
     */
    [[nodiscard]] task operator >> (data_t&& data) { data = std::move(co_await pull()); }
};


/**
 * @brief Static Channel with bounded amount of waiters
 */
template <typename Type, size_t DataBufferSize = 1ul>
using channel_static = channel
<
    Type,
    DataBufferSize,
    allocation_type::e_static
>;

/**
 * @brief Dynamic Channel with bounded amount of waiters
 */
template <typename Type>
using channel_dyn = channel<Type>;


} // namespace ace::futures

//==============================DEFINITIONS==================================

#define ACE_FUTURE_CHANNEL_META                        \
template<                                              \
    typename data_t,                                   \
    size_t data_buffer_size_v,                         \
    ace::futures::allocation_type data_allocation_v    \
>

#define ACE_FUTURE_CHANNEL_SPACE \
ace::futures::channel<data_t, data_buffer_size_v, data_allocation_v>::

#define ACE_FUTURE_CHANNEL_MEMBER(returnT) \
ACE_FUTURE_CHANNEL_META returnT ACE_FUTURE_CHANNEL_SPACE


ACE_FUTURE_CHANNEL_META
class ACE_FUTURE_CHANNEL_SPACE pull_impl : public core::traits::busy_future_traits<pull_impl> {

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

    node_t* forward_node(node_t* node) override {
        auto* n = nukes::details::nodes::cast_node(node);
        _waiters->push_node(n);
        return nullptr;
    }

    void cancel() override {
        // NOTE: Reattaching all tasks because mpmc-queue doesn't allow ejection.
        // NOTE: Target canceled task will be marked as detached and Runner will drop it
        // TODO: Batch read needed
        auto* node = _waiters->pop_node();
        while (node)
            core::runner::threadsafe_reattach(node);
    }

    ~channel_conductor() override = default;

    waiters_storage_t* _waiters;
};


ACE_FUTURE_CHANNEL_MEMBER(void) notify() {
    if (auto* node = _waiters.pop_node(); node) [[likely]]
        core::runner::reattach(node);
}


ACE_FUTURE_CHANNEL_MEMBER(bool) push(data_t& data) {
    if (_container.push(std::forward<data_t&>(data))) [[likely]] {
        notify();
        return true;
    }
    return false;
}


ACE_FUTURE_CHANNEL_MEMBER(bool) push(data_t&& data) {
    static constexpr bool is_move_only {
        (std::is_move_constructible_v<data_t> or std::is_move_assignable_v<data_t>)
        and not
        (std::is_copy_constructible_v<data_t> or std::is_copy_assignable_v<data_t>)
    };
    if constexpr (is_move_only) {
        if (_container.push(std::forward<data_t&&>(data))) [[likely]] {
            notify();
            return true;
        }
    } else {
        if (data_t instance {std::forward<data_t>(data)}; _container.push(instance)) [[likely]] {
            notify();
            return true;
        }
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

#define ACE_FUTURE_CHANNEL_ST_META template <typename data_t>

#define ACE_FUTURE_CHANNEL_ST_SPACE \
ace::futures::channel_st<data_t>::

#define ACE_FUTURE_CHANNEL_ST_MEMBER(returnT) \
ACE_FUTURE_CHANNEL_ST_META returnT ACE_FUTURE_CHANNEL_ST_SPACE

ACE_FUTURE_CHANNEL_ST_META
class ACE_FUTURE_CHANNEL_ST_SPACE pull_impl : public core::traits::busy_future_traits<pull_impl> {

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


ACE_FUTURE_CHANNEL_ST_META
struct ACE_FUTURE_CHANNEL_ST_SPACE channel_conductor : conductor_handler_t {

    channel_conductor() = delete;

    explicit channel_conductor(waiters_storage_t* waiters) : _waiters(waiters) {};

    node_t* forward_node(node_t* node) override {
        _waiters->push_node(node);
        return nullptr;
    }

    void cancel() override {
        // NOTE: Reattaching all tasks because mpmc-queue doesn't allow ejection.
        // NOTE: Target canceled task will be marked as detached and Runner will drop it
        // TODO: Batch read needed
        auto* node = _waiters->pop_node();
        while (node)
            core::runner::reattach(node);
    }

    ~channel_conductor() override = default;

    waiters_storage_t* _waiters;
};


ACE_FUTURE_CHANNEL_ST_MEMBER(void) notify() {
    if (auto* node = _waiters.pop_node(); node) [[likely]]
        core::runner::reattach(node);
}


ACE_FUTURE_CHANNEL_ST_MEMBER(bool) push(data_t& data) {
    _container.push(std::forward<data_t&>(data));
    notify();
    return true;
}


ACE_FUTURE_CHANNEL_ST_MEMBER(bool) push(data_t&& data) {
    static constexpr bool is_move_only {
        (std::is_move_constructible_v<data_t> or std::is_move_assignable_v<data_t>)
        and not
        (std::is_copy_constructible_v<data_t> or std::is_copy_assignable_v<data_t>)
    };
    if constexpr (is_move_only) {
        if (_container.push(std::forward<data_t&&>(data))) [[likely]] {
            notify();
            return true;
        }
    } else {
        if (data_t instance {std::forward<data_t>(data)}; _container.push(instance)) [[likely]] {
            notify();
            return true;
        }
    }
    return false;
}

ACE_FUTURE_CHANNEL_ST_MEMBER(ace::promise<>) pending_push(data_t data) {
    while (not _container.push(std::forward<data_t>(data))) [[unlikely]]
        co_await suspend();
    notify();
    co_return;
}


ACE_FUTURE_CHANNEL_ST_MEMBER(ace::promise<>) pending_push(data_t&& data) {
    while (not _container.push(std::forward<data_t>(data))) [[unlikely]]
        co_await suspend();
    notify();
    co_return;
}


ACE_FUTURE_CHANNEL_ST_META
ACE_FUTURE_CHANNEL_ST_SPACE pull_impl
ACE_FUTURE_CHANNEL_ST_SPACE pull() {
    return std::forward<pull_impl>(pull_impl{&_waiters, &_container});
}


ACE_FUTURE_CHANNEL_ST_MEMBER(bool) pull_impl::await_ready() {
    if (not _container->empty()) {
        _output_data = std::move(_container->front());
        _container->pop();
        return true;
    }
    return false;
}

ACE_FUTURE_CHANNEL_ST_MEMBER(bool) pull_impl::await_suspend(auto ctx) {
    if (_container->empty()) {
        ctx.promise()._runner_conductor = channel_conductor{_waiters};
        return true;
    }
    _output_data = std::move(_container->front());
    _container->pop();
    return false;
}

#undef ACE_FUTURE_CHANNEL_META
#undef ACE_FUTURE_CHANNEL_SPACE
#undef ACE_FUTURE_CHANNEL_ST_META
#undef ACE_FUTURE_CHANNEL_ST_SPACE
#endif // ACE_FUTURE_CHANNEL_H
