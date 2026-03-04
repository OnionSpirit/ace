#ifndef ACE_CONTROL_H
#define ACE_CONTROL_H

#include <atomic>
#include <cstddef>

#include "conductor.h"
#include "ace/common/terms.h"

namespace ace::coroutines {

    struct promise_conductor_handle {

        virtual void cancel() noexcept = 0;

        virtual bool subscribe(void*) noexcept = 0;

        virtual ~promise_conductor_handle() = default;
    };

    struct control_block {

        uint64_t _weak_refcount {1};
        uint64_t _strong_refcount {1};
        promise_conductor_handle* _promise_conductor { nullptr };
        alignas(ACE_BUS_SIZE) bool _exists {true};

        control_block() = default;

        ~control_block() = default;

        static bool is_untracked(void* v_block);

        static bool disown(void* v_block);

        static bool watch(void* v_block);

        static bool unwatch(void* v_block);

        static bool is_disowned(void* address);

        static control_block* get_block_from_address(void* address);

    };

    inline constexpr std::size_t control_block_size = sizeof(control_block);

    template <typename promise_t>
    concept is_controled_promise = requires (promise_t p) {
        { std::remove_reference_t<decltype(p._block)>{} } -> std::same_as<control_block*>;
    };

    class control_block_handle {

        control_block* _block { nullptr };

        void release() {
            if (control_block::unwatch(_block))
                delete _block;
            _block = nullptr;
        }

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

        ~control_block_handle() { release(); }

        void cancel() {
            if (done() or not _block->_promise_conductor) [[unlikely]]
                return;
            _block->_promise_conductor->cancel();
            release();
        }

        [[nodiscard]] bool done() const {
            if (not _block) [[unlikely]] return false;
            return not _block->_exists;
        }

        bool subscribe(void* waiter) const {
            if (done() or not _block->_promise_conductor or waiter == nullptr) [[unlikely]]
                return false;
            return _block->_promise_conductor->subscribe(waiter);
        }
    };


    // NOTE: Checks if there are no watchers or owners at the control block
    inline bool control_block::is_untracked(void* v_block) {
        if (v_block == nullptr) [[unlikely]] return false;
        const auto block = static_cast<control_block*>(v_block);
        return  block->_weak_refcount == 0
            and block->_strong_refcount == 0;
    }

    // NOTE: Detaches owner from the control block
    inline bool control_block::disown(void* v_block) {
        if (v_block == nullptr) [[unlikely]] return false;
        const auto block = static_cast<control_block*>(v_block);
        if (not block->_exists) [[unlikely]] goto end;
        --block->_strong_refcount;
        --block->_weak_refcount;
        block->_exists = false;
        end: return is_untracked(block);
    }

    // NOTE: Attaches spectator (not owner) to the control block
    inline bool control_block::watch(void* v_block) {
        if (v_block == nullptr) [[unlikely]] return false;
        const auto block = static_cast<control_block*>(v_block);
        if (block->_weak_refcount == 0) [[unlikely]] goto end;
        ++block->_weak_refcount;
        end: return is_untracked(block);
    }

    // NOTE: Detaches spectator (not owner) from the control block
    inline bool control_block::unwatch(void* v_block) {
        if (v_block == nullptr) [[unlikely]] return false;
        const auto block = static_cast<control_block*>(v_block);
        --block->_weak_refcount;
        return is_untracked(block);
    }

    // NOTE: Gets control block of passed promise address, and checks ownership status
    inline bool control_block::is_disowned(void* address) {
        return not get_block_from_address(address)->_exists;
    }

    // NOTE: Gets control block pointer from the raw promise address
    inline control_block* control_block::get_block_from_address(void* address) {
        return reinterpret_cast<control_block*>(static_cast<uint8_t*>(address) - control_block_size);
    }

} // end namespace ace::coroutines

#endif //ACE_CONTROL_H
