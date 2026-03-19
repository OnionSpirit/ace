/**
 * @file
 * @details The file contains a class defining for 'conductor' interface objects.
 * The 'runner_conductor' describes how to forward 'context' from the 'runner' storage
 * to the 'future' storage. When the 'future' decides to suspend 'context',
 * it puts its special 'conductor' in the 'promise_type' of the 'context'.
 * If 'context' is nested, it will pass 'conductor' to the outer 'context'.
 * The 'runner' always looking for the 'conductor' in the 'promise_type' of the 'context'.
 * If the 'runner' finds it, 'context' would be forwarded by the founded 'conductor',
 * instead of putting it into the own storage.
 * Also, 'runner_conductor' provides 'cancel' interface to support cancellation of the 'future'.
 * <br>The 'control_conductor' is almost the same as the 'runner_conductor' but for 'promise' objects.
 * The main purpose of this type of the 'conductor' is to provide handles to 'promise' control block.
 * Which is necessary to apply action onto 'promise' object from external or even shared context without ownage.
 * This 'conductor' also provides 'forward' and 'cancel' operations.
 * The 'forward' operation will store passed 'promise' object into a 'waiters' queue of a 'promise' object,
 * which is a producer of the 'conductor'.
 * The 'cancel' operation is a cancellation handle for a 'promise' object, which is a producer of the 'conductor'.
 */
#ifndef ACE_CONDUCTOR_H
#define ACE_CONDUCTOR_H

#include "ace/common/terms.h"

namespace ace::coroutines {

    template <typename runner_context_t>
    struct runner_conductor_handle {

        runner_conductor_handle() noexcept = default;

        runner_conductor_handle(const runner_conductor_handle&) noexcept = default;

        runner_conductor_handle(runner_conductor_handle&&) noexcept = default;

        virtual void forward(runner_context_t&& context) = 0;

        virtual void cancel() = 0;

        virtual ~runner_conductor_handle() = default;
    };

    struct control_conductor_handle {

        control_conductor_handle() noexcept = default;

        virtual bool forward(void*) noexcept = 0;

        virtual void cancel() noexcept = 0;

        virtual ~control_conductor_handle() = default;
    };

    // NOTE: Static storage for conductor objects
    template <typename conductor_handle_t, std::size_t slot_memsize_v = ACE_CONDUCTOR_MEM_SIZE>
    struct conductor_slot {
        template <typename conductor_t>
        requires std::derived_from<conductor_t, conductor_handle_t>
        conductor_slot& operator =(const conductor_t& conductor) {
            static_assert(sizeof(conductor_t) <= slot_memsize_v,
            "[conductor_carry]: conductor size can't be larger than passed slot memsize");
            _conductor = new (_area) conductor_t(std::forward<const conductor_t&>(conductor));
            return *this;
        }

        template <typename conductor_t>
        requires std::derived_from<conductor_t, conductor_handle_t>
        conductor_slot& operator =(conductor_t&& conductor) {
            static_assert(sizeof(conductor_t) <= slot_memsize_v,
            "[conductor_carry]: conductor size can't be larger than passed slot memsize");
            release();
            _conductor = new (_area) conductor_t(std::forward<conductor_t&&>(conductor));
            return *this;
        }

        template<typename carry_t>
        requires requires { carry_t::_conductor; carry_t::_area; }
        conductor_slot& operator <<(carry_t& carry) noexcept {
            if (carry._conductor) {
                _conductor = carry._conductor;
                carry._conductor = nullptr;
            }
            return *this;
        }

        // NOTE: Releases conductor from carry with distracting
        void release() {
            if (_conductor) {
                _conductor->~conductor_handle_t();
                _conductor = nullptr;
            }
        }

        // NOTE: Wipes conductor data from carry without distracting
        void reset() {
            if (_conductor)
                _conductor = nullptr;
        }

        [[nodiscard]] conductor_handle_t* get() const { return _conductor; }

        conductor_handle_t* operator->() const { return get(); }

        explicit operator bool() const { return _conductor != nullptr; };

        ~conductor_slot() { release(); };

        conductor_handle_t* _conductor {nullptr};
        alignas(ACE_BUS_SIZE) uint8_t _area [slot_memsize_v] {};
    };

}

#endif //ACE_CONDUCTOR_H
