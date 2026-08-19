/**
 * @file channel.h
 * @brief Lock-free MPMC channel for async message passing between coroutines.
 *
 * @details The @c ace::futures::channel<T> is a multi-producer/multi-consumer
 * message-passing primitive.  Producers push data via @c push() or
 * @c operator<<; consumers pull via @c pull() (returns an awaitable) or
 * @c operator>>.  Blocking producers can use @c pending_push() which
 * suspends until capacity is available.
 *
 * ### Allocation policies
 *
 * | Policy | Data queue | Waiters queue |
 * |---|---|---|
 * | @c e_dynamic (default) | Dynamic (heap-allocating) | Dynamic |
 * | @c e_static | Bounded, fixed at compile time | Dynamic |
 *
 * ### Variants
 *
 * | Alias | Description |
 * |---|---|
 * | @c channel<T> | Fully dynamic data and waiters |
 * | @c channel_static<T, N, _> | Static data buffer (N slots) |
 * | @c channel_dyn<T> | Alias for @c channel<T> |
 * | @c channel_st<T> | Uses @c std::queue + @c reg_queue (non-lock-free) |
 *
 * @see ace::futures::cutex
 */
#ifndef ACE_FUTURE_CHANNEL_H
#define ACE_FUTURE_CHANNEL_H

#include <nukes/dynamic/mpmc_queue.h>
#include <nukes/dynamic/mpsc_queue.h>
#include <nukes/dynamic/regular_queue.h>
#include <nukes/bounded/mpsc_queue.h>
#include <nukes/bounded/mpmc_queue.h>
#include <nukes/bounded/spsc_queue.h>

#include <ace/core/traits/future.h>
#include <ace/core/runner.h>
#include <ace/core/async.h>


namespace ace::futures {

    /**
     * @brief Channel buffer allocation policy.
     */
    enum class allocation_type {
        e_static,   ///< Fixed-size buffer allocated statically (compile-time size).
        e_on_init,  ///< Fixed-size buffer allocated on channel construction.
        e_dynamic   ///< Fully dynamic buffer (heap-allocating).
    };

    /**
     * @brief Channel access mode — determines producer/consumer topology.
     */
    enum class access_mode {
        e_regular,  ///< Single-threaded usage.
        e_spsc,     ///< Single producer, single consumer.
        e_mpsc,     ///< Many producers, single consumer.
        e_mpmc,     ///< Many producers, many consumers.
    };

/**
 * @brief Lock-free MPMC channel with configurable allocation policy.
 *
 * @details Supports async @c pull() (returns awaitable) and non-blocking
 * @c push().  Blocking producers should use @c pending_push() which
 * suspends until a slot is available.
 *
 * @tparam data_t               Storable data type.
 * @tparam data_buffer_size_v   Bounded data buffer size (for static policy).
 * @tparam data_allocation_v    Allocation policy: @c e_dynamic, @c e_static, or @c e_on_init.
 */
template
<
    typename data_t,

    allocation_type data_allocation_v = allocation_type::e_dynamic,

    access_mode access_mode_v = access_mode::e_mpmc,

    size_t data_buffer_size_v = 1ul
>
class channel {

    template <typename storage_entity_t, allocation_type allocation_v, size_t buff_len_v>
    /**
     * @brief Compile-time selection of the storage queue type.
     * @tparam storage_entity_t Element type stored in the queue.
     * @tparam allocation_v     Allocation policy.
     * @tparam buff_len_v       Static buffer size (for @c e_static policy).
     * @return A representative value whose type becomes the storage type.
     */
    static auto consteval define_storage() {
        if constexpr (allocation_v == allocation_type::e_dynamic) {
            if constexpr (access_mode_v == access_mode::e_mpmc)
                return nukes::dynamic::mpmc_queue<storage_entity_t>{};
            if constexpr (access_mode_v == access_mode::e_mpsc or access_mode_v == access_mode::e_spsc)
                return nukes::dynamic::mpsc_queue<storage_entity_t>{};
            if constexpr (access_mode_v == access_mode::e_regular)
                return nukes::dynamic::reg_queue<storage_entity_t>{};
        } else if constexpr (allocation_v == allocation_type::e_static) {
            if constexpr (access_mode_v == access_mode::e_mpmc)
                return nukes::bounded::mpmc_queue<storage_entity_t, buff_len_v>{};
            if constexpr (access_mode_v == access_mode::e_mpsc)
                return nukes::bounded::mpsc_queue<storage_entity_t, buff_len_v>{};
            if constexpr (access_mode_v == access_mode::e_spsc)
                return nukes::bounded::spsc_queue<storage_entity_t, buff_len_v>{};
            if constexpr (access_mode_v == access_mode::e_regular)
                return nukes::dynamic::reg_queue<storage_entity_t>{};
        } else if constexpr (allocation_v == allocation_type::e_on_init) {
            if constexpr (access_mode_v == access_mode::e_mpmc)
                return nukes::bounded::mpmc_queue<storage_entity_t>{};
            if constexpr (access_mode_v == access_mode::e_mpsc)
                return nukes::bounded::mpsc_queue<storage_entity_t>{};
            if constexpr (access_mode_v == access_mode::e_spsc)
                return nukes::bounded::spsc_queue<storage_entity_t>{};
            // if constexpr (access_mode_v == access_mode::e_regular)
            //     return nukes::dynamic::reg_queue<storage_entity_t>{};
        }
    }

    /**
     * @brief Storage type for the transmitted data.
     * @return Representative value whose type becomes @c data_storage_t.
     */
    static auto consteval define_data_storage() {
        return define_storage<data_t, data_allocation_v, data_buffer_size_v>();
    }

    /**
     * @brief Storage type for the waiting tasks (always dynamic).
     * @return Representative value whose type becomes @c waiters_storage_t.
     */
    static auto consteval define_waiters_storage() {
        return define_storage<task, allocation_type::e_dynamic, data_buffer_size_v>();
    }

    typedef std::decay_t<decltype(define_data_storage())> data_storage_t;
    typedef std::decay_t<decltype(define_waiters_storage())> waiters_storage_t;

    typedef waiters_storage_t::node_t waiters_pool_node_t;

    class pull_impl;
    friend pull_impl;

    struct channel_router;
    friend channel_router;

    /**
     * @brief Wakes up one waiter, if any.
     */
    void notify();

public:

    data_storage_t _container  {}; ///< Storage of transmitting data
    waiters_storage_t _waiters {}; ///< Storage of waiting contexts

    /// @brief Default constructor.
    channel() = default;

    /**
     * @brief Channel emptiness check.
     * @return @c true when no data is buffered.
     */
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
    ACE_AWAIT_NODISCARD pull_impl pull();

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
    ACE_AWAIT_NODISCARD task operator >> (data_t& data) { data = co_await pull(); }

    /**
     * @details @b pull method alternative interface
     * @param data Data to pull
     */
    ACE_AWAIT_NODISCARD task operator >> (data_t&& data) { data = std::move(co_await pull()); }
};

} // namespace ace::futures

// NOTE: The short aliases ace::channel / ace::allocation_type / ace::access_mode
// and the ace::tunnel namespace are only exposed when ace/ace.h (quick-start
// header) was included before this file — its ACE_H guard switches aliases on.
#ifdef ACE_H
namespace ace {
    /// @brief Short alias for @c ace::futures::allocation_type.
    using allocation_type = futures::allocation_type;
    /// @brief Short alias for @c ace::futures::access_mode.
    using access_mode = futures::access_mode;
    /**
     * @brief Short alias for @c ace::futures::channel.
     * @tparam data_t               Storable data type.
     * @tparam data_allocation_v    Allocation policy: @c e_dynamic, @c e_static, or @c e_on_init.
     * @tparam access_mode_v        Producer/consumer topology.
     * @tparam data_buffer_size_v   Bounded data buffer size (for static policy).
     */
    template <typename data_t,
              futures::allocation_type data_allocation_v = futures::allocation_type::e_dynamic,
              futures::access_mode access_mode_v = futures::access_mode::e_mpmc,
              size_t data_buffer_size_v = 1ul>
    using channel = futures::channel<data_t, data_allocation_v, access_mode_v, data_buffer_size_v>;

    /**
     * @brief Channel alias for thread local usage
     */
    template <typename Type, size_t DataBufferSize = 0ul>
    using local = channel<
        Type,
        (DataBufferSize == 0ul) ? allocation_type::e_dynamic : allocation_type::e_static,
        access_mode::e_regular,
        DataBufferSize
    >;

    /**
     * @brief Channel alias for one thread to another thread usage
     */
    template <typename Type, size_t DataBufferSize = 0ul>
    using bridge = channel<
        Type,
        (DataBufferSize == 0ul) ? allocation_type::e_dynamic : allocation_type::e_static,
        access_mode::e_spsc,
        DataBufferSize
    >;

    /**
     * @brief Channel alias for many threads to single thread usage
     */
    template <typename Type, size_t DataBufferSize = 0ul>
    using funnel = channel<
        Type,
        (DataBufferSize == 0ul) ? allocation_type::e_dynamic : allocation_type::e_static,
        access_mode::e_mpsc,
        DataBufferSize
    >;

    /**
     * @brief Channel alias for many to many threads usage
     */
    template <typename Type, size_t DataBufferSize = 0ul>
    using bus = channel<
        Type,
        (DataBufferSize == 0ul) ? allocation_type::e_dynamic : allocation_type::e_static,
        access_mode::e_mpmc,
        DataBufferSize
    >;

}
#endif

//==============================DEFINITIONS==================================

#define ACE_FUTURE_CHANNEL_META                        \
template<                                              \
    typename data_t,                                   \
    ace::futures::allocation_type data_allocation_v,   \
    ace::futures::access_mode access_mode_v,           \
    size_t data_buffer_size_v                          \
>

#define ACE_FUTURE_CHANNEL_SPACE \
ace::futures::channel<data_t, data_allocation_v, access_mode_v, data_buffer_size_v>::

#define ACE_FUTURE_CHANNEL_MEMBER(returnT) \
ACE_FUTURE_CHANNEL_META returnT ACE_FUTURE_CHANNEL_SPACE


ACE_FUTURE_CHANNEL_META
/**
 * @brief Awaitable pull operation — pops data from the channel on await.
 *
 * @details Returns immediately when data is available; otherwise registers
 * the caller in the channel's waiter queue and resumes on the next push.
 */
class ACE_FUTURE_CHANNEL_SPACE pull_impl : public core::traits::busy_future_traits<pull_impl> {

    data_t _output_data{}; ///< Storage for the pulled value.

public:

    IMPORT_BUSY_FUTURE_ENV(pull_impl)

    /// @brief Default construction is forbidden — queues are required.
    pull_impl() =delete;

    /**
     * @brief Binds the pull to the channel's waiter and data queues.
     * @param waiters   Waiter queue to register on suspension.
     * @param container Data queue to pop from.
     */
    pull_impl(waiters_storage_t* waiters, data_storage_t* container)
            : _waiters(waiters), _container(container) {};

    waiters_storage_t* _waiters;  ///< Waiter queue of the owning channel.
    data_storage_t* _container;   ///< Data queue of the owning channel.

    /**
     * @brief @c true when data is already available (no suspension).
     */
    bool await_ready() override;

    /**
     * @brief Registers the caller in the waiter queue when no data is available.
     * @param ctx Caller coroutine promise accessor.
     * @return @c true when suspended, @c false when data was available.
     */
    bool await_suspend(auto ctx);

    /**
     * @brief Returns the pulled value.
     * @return The value moved out of the pull storage.
     */
    auto await_resume() { return std::forward<data_t>(_output_data); }

    /// @brief Default destructor.
    ~pull_impl() override = default;
};


ACE_FUTURE_CHANNEL_META
/**
 * @brief Router that keeps suspended pullers in the channel's waiter queue.
 *
 * @details On @c redirect() the waiting task is enqueued into the waiter
 * storage; on @c cancel() all waiters are woken (the nukes queues do not
 * support single-node ejection).
 */
struct ACE_FUTURE_CHANNEL_SPACE channel_router : runner_router {

    /// @brief Default construction is forbidden — a waiter queue is required.
    channel_router() = delete;

    /**
     * @brief Binds the router to the channel's waiter queue.
     * @param waiters Waiter queue of the owning channel.
     */
    explicit channel_router(waiters_storage_t* waiters) : _waiters(waiters) {};

    /**
     * @brief Registers the suspended task in the waiter queue.
     * @param node Task node of the suspended puller.
     */
    void redirect(omni_node node) override {
        using namespace nukes::details::nodes;
        // if constexpr (access_mode_v == access_mode::e_regular)
        //     _waiters->push_node(node);
        // else {
            _waiters->push_node(node);
        // }
    }

    /**
     * @brief Re-attaches all queued waiters to their runners.
     * @details The nukes queues do not allow ejection of a single node, so on
     * cancel all waiters are woken; the canceled task drops itself as detached.
     */
    void cancel() override {
        // NOTE: Reattaching all tasks because mpmc-queue doesn't allow ejection.
        // NOTE: Target canceled task will be marked as detached and Runner will drop it
        // TODO: Batch read needed
        auto* node = _waiters->pop_node();
        while (node) {
            core::runner::reattach(node);
            node = _waiters->pop_node();
        }
    }

    ~channel_router() override = default;

    waiters_storage_t* _waiters; ///< Waiter queue of the owning channel.
};


ACE_FUTURE_CHANNEL_MEMBER(void)
/**
 * @brief Wakes up one waiter, if any.
 */
notify() {
    if (auto* node = _waiters.pop_node(); node) [[likely]]
        core::runner::reattach(node);
}


ACE_FUTURE_CHANNEL_MEMBER(bool)
/**
 * @brief Pushes data to the channel (lvalue overload).
 * @param data Data to push.
 * @return @c false if the inner buffer overflowed.
 */
push(data_t& data) {
    if (_container.push(std::forward<data_t>(data))) [[likely]] {
        notify();
        return true;
    }
    return false;
}


ACE_FUTURE_CHANNEL_MEMBER(bool)
/**
 * @brief Pushes data to the channel (rvalue overload).
 * @param data Data to push.
 * @return @c false if the inner buffer overflowed.
 */
push(data_t&& data) {
    if (_container.push(std::forward<data_t>(data))) [[likely]] {
        notify();
        return true;
    }
    return false;
}

ACE_FUTURE_CHANNEL_MEMBER(ace::promise<>)
/**
 * @brief Pushes data, suspending until a vacant spot appears (lvalue overload).
 * @param data Data to push.
 */
pending_push(data_t data) {
    while (not _container.push(std::forward<data_t>(data))) [[unlikely]]
        co_await suspend();
    notify();
    co_return;
}


ACE_FUTURE_CHANNEL_MEMBER(ace::promise<>)
/**
 * @brief Pushes data, suspending until a vacant spot appears (rvalue overload).
 * @param data Data to push.
 */
pending_push(data_t&& data) {
    while (not _container.push(std::forward<data_t>(data))) [[unlikely]]
        co_await suspend();
    notify();
    co_return;
}


ACE_FUTURE_CHANNEL_META
ACE_FUTURE_CHANNEL_SPACE pull_impl
/**
 * @brief Constructs a pull operation bound to this channel.
 * @return The awaitable pull implementation.
 */
ACE_FUTURE_CHANNEL_SPACE pull() {
    return pull_impl{&_waiters, &_container};
}


ACE_FUTURE_CHANNEL_MEMBER(bool)
/**
 * @brief Pops data immediately when available.
 * @return @c true when data was popped, @c false when the caller must suspend.
 */
pull_impl::await_ready() {
    return _container->pop(_output_data);
}

ACE_FUTURE_CHANNEL_MEMBER(bool)
/**
 * @brief Pops data or registers the caller as a waiter.
 * @param ctx Caller coroutine promise accessor.
 * @return @c true when suspended, @c false when data was popped.
 */
pull_impl::await_suspend(auto ctx) {
    if (not _container->pop(_output_data)) {
        ctx.promise()._runner_router = channel_router{_waiters};
        return true;
    }
    return false;
}

#undef ACE_FUTURE_CHANNEL_META
#undef ACE_FUTURE_CHANNEL_SPACE
#endif // ACE_FUTURE_CHANNEL_H
