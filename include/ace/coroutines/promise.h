/**
 * @file
 * @details This file contains a future_handler, future_trait classes and its
 * dispatching concepts: is_future_accept_promise, is_future_accept_coroutine,
 * is_future. Types are intended to be used to create derived future objects,
 * that will be processed by co_await operator
 */
#ifndef ACE_PROMISE_H
#define ACE_PROMISE_H

#include "ace/futures/future.h"
#include "ace/common/dispatch.h"

#include <concepts>
#include <coroutine>
#include <type_traits>

#include "ace/common/id_alloc.h"

namespace ace::coroutines {

    enum promise_touch_result : uint8_t  {
        e_failed,
        e_blocked,
        e_executed,
        e_executed_with_value,
        e_finished,
        e_detached,
    };

    struct promise_control_block {

        std::atomic<uint64_t> _weak_refcount {0};
        std::atomic<uint64_t> _strong_refcount {0};
        std::atomic<bool> _exists {false};
        void* _frame_ptr { nullptr };

        promise_control_block() = delete;

        explicit promise_control_block(void* _frame_ptr)
            : _frame_ptr(_frame_ptr) {
            _weak_refcount.store(1, std::memory_order_release);
            _strong_refcount.store(1, std::memory_order_release);
            _exists.store(true, std::memory_order_release);
            // std::cout << "Control block inited\n";
        };

        ~promise_control_block() = default;
        // {
        //     std::cout << "Control block destroyed\n";
        // };

        static bool clear(void* v_block);

        static bool dec_strong(void* v_block);

        static bool inc_weak(void* v_block);

        static bool dec_weak(void* v_block);

        static promise_control_block* get_block_from_address(void* address);

    };

    inline constexpr std::size_t promise_control_block_size = sizeof(promise_control_block);

    inline bool promise_control_block::clear(void* v_block) {
        const auto block = static_cast<promise_control_block*>(v_block);
        const bool is_lost {
            block->_weak_refcount.load(std::memory_order_acquire) == 0
            and block->_strong_refcount.load(std::memory_order_acquire) == 0
        };
        if (is_lost) {
            block->~promise_control_block();
            return true;
        }
        return false;
    }

    inline bool promise_control_block::dec_strong(void* v_block) {
        const auto block = static_cast<promise_control_block*>(v_block);
        if (not block->_exists.load(std::memory_order_relaxed)) [[unlikely]] return false;
        block->_strong_refcount.fetch_sub(1, std::memory_order_relaxed);
        block->_weak_refcount.fetch_sub(1, std::memory_order_relaxed);
        block->_exists.store(false, std::memory_order_release);
        return clear(block);
    }

    inline bool promise_control_block::inc_weak(void* v_block) {
        const auto block = static_cast<promise_control_block*>(v_block);
        if (block->_weak_refcount.load(std::memory_order_acquire) == 0) return false;
        block->_weak_refcount.fetch_add(1, std::memory_order_release);
        return true;
    }

    inline bool promise_control_block::dec_weak(void* v_block) {
        const auto block = static_cast<promise_control_block*>(v_block);
        block->_weak_refcount.fetch_sub(1, std::memory_order_relaxed);
        return clear(block);
    }

    inline promise_control_block* promise_control_block::get_block_from_address(void* address) {
        return reinterpret_cast<promise_control_block*>(static_cast<uint8_t*>(address) - promise_control_block_size);
    }


    struct promise_rule_traits { struct e_promise_rule {}; };

    struct permanent : promise_rule_traits {
        consteval static auto action() noexcept { return std::suspend_never{}; };
    };

    struct differed : promise_rule_traits {
        consteval static auto action() noexcept { return std::suspend_always{}; };
    };

    template <typename modeT>
    concept is_promise_rule = requires { typename modeT::e_promise_rule; }
        and (std::same_as<decltype(modeT::action()), std::suspend_never>
        or std::same_as<decltype(modeT::action()), std::suspend_always>);


    template <typename promiseT, typename returnT>
    struct promise_return_traits {

        promiseT* _derived = static_cast<promiseT*>(this);

        promise_touch_result _status { e_blocked };

        returnT _return_value {};

        auto return_value(returnT return_value) {
            _return_value =return_value;
            _derived->_status = promise_touch_result::e_finished;
            return std::suspend_never{};
        }

        auto yield_value(returnT yield_value) {
            _derived->_status = promise_touch_result::e_executed_with_value;
            _return_value =yield_value;
            return std::suspend_always{};
        }
    };


    template <typename promiseT>
    struct promise_return_traits <promiseT, void> {

        promiseT* _derived = static_cast<promiseT*>(this);

        promise_touch_result _status { e_blocked };

        static auto return_void() { return std::suspend_never{}; }
    };

    template <typename return_t>
        struct promise_traits : promise_return_traits<promise_traits<return_t>, return_t> {

        typedef futures::future_handler* future_handler_ptr_t;
        typedef promise_return_traits<promise_traits, return_t> promise_return_traits_t;
        using promise_return_traits_t::_status;

        promise_traits() = default;

        ~promise_traits() {
            if (_trace_id) [[unlikely]]
                common::context_id_allocator::get_instance().id_free(_trace_id.value());
        };

        std::suspend_always await_transform(const std::suspend_always& e) {
            _status = e_executed;
            _future = nullptr;
            return e;
        }

        std::suspend_never await_transform(const std::suspend_never& e) {
            _status = e_executed;
            _future = nullptr;
            return e;
        }

        template <typename futureT>
        requires ace::common::dispatch::is_future<std::remove_reference_t<futureT>, return_t>
        futureT& await_transform(futureT& future) {
            _status = e_blocked;
            _future = &future;
            return future;
        }

        template <typename futureT>
        requires ace::common::dispatch::is_future<std::remove_reference_t<futureT>, return_t>
        futureT&& await_transform(futureT&& future) {
            _status = e_blocked;
            _future = &future;
            return std::forward<futureT>(future);
        }

        template <typename commandT>
        requires ace::common::dispatch::is_command<std::remove_reference_t<commandT>, return_t>
        commandT& await_transform(commandT& command) {
            _status = e_executed;
            _future = nullptr;
            return command;
        }

        template <typename commandT>
        requires ace::common::dispatch::is_command<std::remove_reference_t<commandT>, return_t>
        commandT&& await_transform(commandT&& command) {
            _status = e_executed;
            _future = nullptr;
            return command;
        }

        std::size_t setup_trace() {
            _trace_id = common::context_id_allocator::get_instance().id_alloc();
            return _trace_id.value();
        }

        // TODO: Define in future to attach custom allocator
        void* operator new(size_t mem_size) noexcept {
            const auto ptr = static_cast<uint8_t*>(::operator new(mem_size + promise_control_block_size));
            void* mem_ptr = ptr + promise_control_block_size;
            new (ptr) promise_control_block(mem_ptr);
            return mem_ptr;
        }

        void operator delete(void* mem_ptr, size_t mem_size) noexcept {
            if (void* base_ptr = promise_control_block::get_block_from_address(mem_ptr);
                promise_control_block::dec_strong(base_ptr))
                ::operator delete(base_ptr);
        }

        future_handler_ptr_t _future { nullptr };
        std::optional<std::size_t> _trace_id;
    };

#define DECLARE_PROMISE_TRAITS(return_type_t) typedef coroutines::promise_traits<return_type_t> promise_traits_t;

#define IMPORT_PROMISE_TRAITS_ENV               \
    using promise_traits_t::_future;            \
    using promise_traits_t::_status;

}

#endif // ACE_PROMISE_H
