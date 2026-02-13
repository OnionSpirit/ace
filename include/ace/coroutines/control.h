#ifndef ACE_CONTROL_H
#define ACE_CONTROL_H

#include <atomic>
#include <cstddef>

#include "conductor.h"

namespace ace::coroutines {

    struct access_handle {

        virtual void cancel() noexcept = 0;

        virtual ~access_handle() = default;
    };

    struct control_block {

        std::atomic<uint64_t> _weak_refcount {1};
        std::atomic<uint64_t> _strong_refcount {1};
        std::atomic<bool> _exists {true};
        access_handle* _access { nullptr };

        control_block() = default;

        ~control_block() { delete _access; };

        static bool is_untracked(void* v_block);

        static bool disown(void* v_block);

        static bool watch(void* v_block);

        static bool unwatch(void* v_block);

        static bool disowned(void* address);

        static control_block* get_block_from_address(void* address);

    };

    inline constexpr std::size_t control_block_size = sizeof(control_block);

    template <typename promise_t>
    concept is_controled_promise = requires (promise_t p) {
        { std::remove_reference_t<decltype(p._block)>{} } -> std::same_as<control_block*>;
    };

    class control_block_handle {

        control_block* _block { nullptr };

    public:

        control_block_handle() = default;

        control_block_handle(const control_block_handle& h) {
            this->_block = h._block;
            control_block::watch(_block);
        }

        template <is_controled_promise promise_t>
        explicit control_block_handle(const std::coroutine_handle<promise_t>& promise) {
            _block = promise.promise()._block;
            control_block::watch(_block);
        }

        ~control_block_handle() {
            if (control_block::unwatch(_block))
                delete _block;
        }

        void cancel() const {
            if (not done() and _block->_access) [[likely]]
                _block->_access->cancel();
        }

        [[nodiscard]] bool done() const {
            return not _block or not _block->_exists.load(std::memory_order_relaxed);
        }
    };


    inline bool control_block::is_untracked(void* v_block) {
        if (v_block == nullptr) [[unlikely]] return false;
        const auto block = static_cast<control_block*>(v_block);
        return  block->_weak_refcount.load(std::memory_order_acquire) == 0
            and block->_strong_refcount.load(std::memory_order_acquire) == 0;
    }

    inline bool control_block::disown(void* v_block) {
        if (v_block == nullptr) [[unlikely]] return false;
        const auto block = static_cast<control_block*>(v_block);
        if (not block->_exists.load(std::memory_order_relaxed)) [[unlikely]] goto end;
        block->_strong_refcount.fetch_sub(1, std::memory_order_relaxed);
        block->_weak_refcount.fetch_sub(1, std::memory_order_relaxed);
        block->_exists.store(false, std::memory_order_release);
        end: return is_untracked(block);
    }

    inline bool control_block::watch(void* v_block) {
        if (v_block == nullptr) [[unlikely]] return false;
        const auto block = static_cast<control_block*>(v_block);
        if (block->_weak_refcount.load(std::memory_order_acquire) == 0) [[unlikely]] goto end;
        block->_weak_refcount.fetch_add(1, std::memory_order_release);
        end: return is_untracked(block);
    }

    inline bool control_block::unwatch(void* v_block) {
        if (v_block == nullptr) [[unlikely]] return false;
        const auto block = static_cast<control_block*>(v_block);
        block->_weak_refcount.fetch_sub(1, std::memory_order_relaxed);
        return is_untracked(block);
    }

    inline bool control_block::disowned(void* address) {
        return not get_block_from_address(address)->_exists.load(std::memory_order_acquire);
    }

    inline control_block* control_block::get_block_from_address(void* address) {
        return reinterpret_cast<control_block*>(static_cast<uint8_t*>(address) - control_block_size);
    }

} // end namespace ace::coroutines

#endif //ACE_CONTROL_H
