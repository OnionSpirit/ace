/**
* @file
 * @details This file contains a @b dispatcher singleton object,
 * allows to spawn tasks from any place, execute them.
 * Provides necessary services for active futures
 */

#ifndef ACE_CORE_DISPATCHER_H
#define ACE_CORE_DISPATCHER_H

#include "ace/core/signal.h"
#include "ace/core/balancer.h"

namespace ace::core {

class dispatcher {

    dispatcher() = default;

    balancer _balancer {};
    sig_pipe_t _sig_pipe{};

public:

    static dispatcher& get_instance() noexcept {
        static dispatcher instance;
        return instance;
    }

    static sig_pipe_t& get_sig_pipe() noexcept {
        return get_instance()._sig_pipe;
    }

    /**
     * @details Function to spawn task at the dispatcher
     * @param new_task Task to be pushed into the dispatcher
     * @param rnr Specific runner to spawn on
     * @return void
     */
    void spawn(async<>&& new_task, runner* rnr = nullptr) noexcept {
        _balancer.spawn(std::forward<async<>>(new_task), rnr);
    }

    /**
     * @details Checks if any Tasks stored in the dispatcher
     * @return @b true if empty, @b false otherwise
     */
    [[nodiscard]] bool empty() const noexcept { return _balancer.empty(); };

    /**
     * @details Resumes all tasks from the runners.
     */
    void run() noexcept { while ( not empty() ) _balancer.run(); }

    /**
     * @brief Reloads balancer configuration
     */
    void reload() noexcept { while (not _balancer.reload()); }

};

} // end namespace ace::core


namespace ace {

    /**
     * @details Function to spawn task
     * @param new_task Task to be pushed into the dispatcher
     * @param rnr Specific runner to spawn on
     * @return void
     */
    static void spawn(async<>&& new_task, core::runner* rnr = nullptr) noexcept {
        core::dispatcher::get_instance().spawn(std::forward<async<>>(new_task), rnr);
    }

    /**
     * @details Checks if there are tasks to do
     * @return @b true if there are no tasks to proceed, @b false otherwise
     */
    inline bool empty() noexcept { return core::dispatcher::get_instance().empty(); }

    /**
     * @details Processing all spawned tasks.
     */
    inline void run() noexcept { core::dispatcher::get_instance().run(); }

    /**
     * @brief Reloads dispatcher configurations
     */
    inline void reload() noexcept {
        core::dispatcher::get_instance().reload();
    }

} // end namespace ace

 //
 // /**
 //     * @details Starts the coroutines execution, provides auto-stop
 //     * mode and terminates if runner is idle.
 //     * @tparam CallbackRC void(uint coroutine ID, int RetCode)
 //     * for custom ret code processing
 //     * @param workers_count Count of worker threads
 //     * @warning Can be used ONLY if all code managed in the coroutines. If that
 //     * statement is incorrect for your code, you should use @b loop function,
 //     * because it will launch the runner in infinite loop mode
 //     */
 //    template<void(CallbackRC)(uint, int) = nullptr>
 //    void start(size_t workers_count = 1) noexcept;
 //
 //    /**
 //     * @details Starts the coroutines execution. Will launch the runner in infinite loop mode
 //     * @tparam CallbackRC void(uint coroutine ID, int RetCode)
 //     * for custom ret code processing
 //     * @param workers_count Count of worker threads
 //     * @warning Requires @b terminate function call, to end @b runners
 //     * execution attempts. If all code managed in the coroutines @b start function
 //     * can be called, because it provides auto-stop mode and terminates if runner is idle.
 //     */
 //    template<void(CallbackRC)(uint, int) = nullptr>
 //    void loop(size_t workers_count= 1) noexcept;
 //
 //    /**
 //     * @details Shuts down the runner execution process
 //     */
 //    void terminate() noexcept { this->_break_flag.fetch_add(1, std::memory_order_acq_rel); };
 //
 //    /**
 //     * @details Makes one round of execution through defined pools
 //     * @tparam CallbackRC void(uint coroutine ID, int RetCode)
 //     * for custom ret code processing
 //     */
 //    template<void(CallbackRC)(uint, int) = nullptr>
 //    void proceed() noexcept { _pool_manager.template proceed_pools<CallbackRC>(); }
 //
 //    /**
 //     * @details Function to add context to the runner
 //     * @tparam Policy of corresponding pool to interact with it
 //     * @param new_context context to be pushed into the runner
 //     * @return ID of placed Context
 //     */
 //    template <ace::meta::types::PoolPolicyConcept Policy>
 //    uint spawn(ace::async::context<> && new_context) noexcept;
 //
 //    /**
 //     * @details Function to add context to the runner
 //     * @tparam Index of corresponding pool to interact with it
 //     * @param new_context Context to be pushed into the runner
 //     * @return ID of placed Context
 //     */
 //    template <size_t Index = ResolveContextManagerPolicyPool<Policies...>::Type::get_pool_count() - 1>
 //    uint spawn(ace::async::context<> && new_context) noexcept;
 //
 //    /**
 //     * @details Function to get pool pointer
 //     * @tparam Pool type of corresponding pool to interact with it
 //     * @param handler ace::control::ContextPoolHandler instance to collect interface
 //     * of requested pool
 //     * @return Pointer to requested pool
 //     */
 //    template <ace::meta::types::ContextPoolConcept Pool>
 //    [[nodiscard]] auto *get_pool(ace::core::context_pool_handler &handler);
 //
 //    /**
 //     * @details Function to get pool pointer
 //     * @tparam Policy of corresponding pool to interact with it
 //     * @param handler ace::control::ContextPoolHandler instance to collect interface
 //     * of requested pool
 //     * @return Pointer to requested pool
 //     */
 //    template <ace::meta::types::PoolPolicyConcept Policy>
 //    [[nodiscard]] auto *get_pool(ace::core::context_pool_handler &handler);
 //
 //    /**
 //     * @details Function to get pool pointer
 //     * @tparam Index of corresponding pool to interact with it
 //     * @param handler ace::control::ContextPoolHandler instance to collect interface
 //     * of requested pool
 //     * @return Pointer to requested pool
 //     */
 //    template <size_t Index = 0>
 //    [[nodiscard]] auto *get_pool(ace::core::context_pool_handler &handler);
 //
 //    /**
 //     * @details Function to get pool pointer
 //     * @tparam Pool of corresponding pool to interact with it
 //     * @return Pointer to requested pool
 //     */
 //    template <ace::meta::types::ContextPoolConcept Pool>
 //    [[nodiscard]] auto *get_pool() { return _pool_manager.template get_pool<Pool>(); }
 //
 //    /**
 //     * @details Function to get pool pointer
 //     * @tparam Policy of corresponding pool to interact with it
 //     * @return Pointer to requested pool
 //     */
 //    template <ace::meta::types::PoolPolicyConcept Policy>
 //    [[nodiscard]] auto *get_pool() { return _pool_manager.template get_pool<Policy>(); }
 //
 //    /**
 //     * @details Function to get pool pointer
 //     * @tparam Index of corresponding pool to interact with it
 //     * @return Pointer to requested pool
 //     */
 //    template <size_t Index = 0>
 //    [[nodiscard]] auto *get_pool() { return _pool_manager.template get_pool<Index>(); }
 //
 //    /**
 //     * @return count of Contexts in all pools
 //     */
 //    [[nodiscard]] size_t get_context_pool_size() noexcept { return _pool_manager.size(); };
 //
 //    /**
 //     * @details Checks if any Context stored in the runner
 //     * @return @b true if empty, @b false otherwise
 //     */
 //    [[nodiscard]] bool empty() noexcept { return _pool_manager.empty(); };
 //
 //    /**
 //     * @details Getter of unique runner id
 //     * @return unique runner id
 //     */
 //    [[nodiscard]] uint get_runner_id() noexcept { return _runner_id; };




// ACE_RUNNER_META template <typename ... T>
// class ACE_CONTROL_runner_SPACE ResolveContextManagerPolicyPool {
//
//     static auto Resolve(T...);
//
// public:
//
//     typedef decltype(Resolve(T()...)) Type;
//
// };
//
//
// ACE_RUNNER_META template <typename ... T>
// auto ACE_RUNNER_MEMBER ResolveContextManagerPolicyPool<T...>::Resolve(T...) {
//
//     if constexpr (sizeof ... (T)) return pool_manager<container, Policies...>{};
//     else return pool_manager<container, pool_policy<1>>{};
// }
//
//
// ACE_RUNNER_META
// ACE_RUNNER_MEMBER runner() {
//
//     ace::kernel_bus::init();
//
//     if (not scheduler_id::lostID.pop(_runner_id)) [[unlikely]]
//         _runner_id = scheduler_id::ID.fetch_add(1);
// }
//
//
// ACE_RUNNER_META
// ACE_RUNNER_MEMBER ~runner() {
//     while (not scheduler_id::lostID.push(_runner_id));
// }
//
//
// ACE_RUNNER_META
// template<void(CallbackRC)(uint, int)>
// void ACE_RUNNER_MEMBER start(size_t workers_count) noexcept {
//
//     if (workers_count == 0) [[unlikely]] {
//         exit(1);
//     } else if (workers_count == 1) {
//         while (not empty() and not this->_break_flag.load(std::memory_order_acquire)) {
//             this->proceed<CallbackRC>();
//         }
//     } else {
//         std::vector<std::thread> workers;
//         std::atomic<uint> sleepers{};
//         workers.reserve(workers_count);
//         std::function thread_function = [&]() {
//             while (not this->_break_flag.load(std::memory_order_acquire)) {
//                 if (empty()) [[unlikely]] {
//                     sleepers++;
//                     if (sleepers.load(std::memory_order_acquire) == workers_count) break;
//                     std::this_thread::sleep_for(std::chrono::microseconds(1));
//                     sleepers--;
//                 }
//                 this->proceed<CallbackRC>();
//             }
//         };
//
//         for (size_t i = 0; i < workers_count - 1; i++) {
//             workers.emplace_back(std::thread(thread_function));
//         }
//         thread_function();
//         for (auto &e: workers) {
//             e.join();
//         }
//         _break_flag.store(0, std::memory_order_release);
//     }
// }
//
//
// ACE_RUNNER_META
// template<void(CallbackRC)(uint, int)>
// void ACE_RUNNER_MEMBER loop(size_t workers_count) noexcept {
//
//     if (workers_count == 0) [[unlikely]] {
//         exit(1);
//     } else if (workers_count == 1) {
//         while (not this->_break_flag.load(std::memory_order_acquire)) {
//             this->proceed<CallbackRC>();
//         }
//     } else {
//         std::vector<std::thread> workers;
//         workers.reserve(workers_count);
//         std::function thread_function = [&]() {
//             while (not this->_break_flag.load(std::memory_order_acquire)) {
//                 this->proceed<CallbackRC>();
//             }
//         };
//
//         for (size_t i = 0; i < workers_count - 1; i++) {
//             workers.emplace_back(std::thread(thread_function));
//         }
//         thread_function();
//         for (auto &e: workers) {
//             e.join();
//         }
//         _break_flag.store(0, std::memory_order_release);
//     }
// }
//
//
// ACE_RUNNER_META template<size_t Index>
// uint ACE_RUNNER_MEMBER spawn(ace::async::context<> &&new_context) noexcept {
//
//     new_context._coroutine.promise()._current_pool = *_pool_manager.template get_pool<Index>();
//     auto context_id = new_context._coroutine.promise().id;
//     if (not _pool_manager.template add_to_pool<Index>(std::move(new_context))) [[unlikely]] {
//         context_id = 0;
//     }
//     return context_id;
// }
//
//
// ACE_RUNNER_META template<ace::meta::types::PoolPolicyConcept Policy>
// uint ACE_RUNNER_MEMBER spawn(ace::async::context<> &&new_context) noexcept {
//
//     new_context._coroutine.promise()._current_pool = *_pool_manager.template get_pool<Policy>();
//     auto context_id = new_context._coroutine.promise().id;
//     if (not _pool_manager.template add_to_pool<Policy>(std::move(new_context))) [[unlikely]] {
//         context_id = 0;
//     }
//     return context_id;
// }
//
//
// ACE_RUNNER_META template<ace::meta::types::ContextPoolConcept Pool>
// auto* ACE_RUNNER_MEMBER get_pool(ace::core::context_pool_handler &handler) {
//
//     auto a = _pool_manager.template get_pool<Pool>();
//     handler = a;
//     return a;
// }
//
//
// ACE_RUNNER_META template<ace::meta::types::PoolPolicyConcept Policy>
// auto* ACE_RUNNER_MEMBER get_pool(ace::core::context_pool_handler &handler) {
//
//     auto a = _pool_manager.template get_pool<Policy>();
//     handler = a;
//     return a;
// }
//
//
// ACE_RUNNER_META template<size_t Index>
// auto* ACE_RUNNER_MEMBER get_pool(ace::core::context_pool_handler &handler) {
//
//     auto a = _pool_manager.template get_pool<Index>();
//     handler = a;
//     return a;
// }

#endif // ACE_CORE_DISPATCHER_H
