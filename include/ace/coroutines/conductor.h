/**
 * @file
 * @details The file contains a class defining the 'conductor' interface.
 * The 'conductor' describes how to forward 'context' from the 'runner' storage
 * to the 'future' storage. When the 'future' decides to suspend 'context',
 * it puts its special 'conductor' in the 'promise_type' of the 'context'.
 * If 'context' is nested, it will pass 'conductor' to the outer 'context'.
 * The 'runner' always looking for the 'conductor' in the 'promise_type' of the 'context'.
 * If the 'runner' finds it, 'context' would be forwarded by the founded 'conductor',
 * instead of putting it into the own storage.
 */
#ifndef ACE_CONDUCTOR_H
#define ACE_CONDUCTOR_H

namespace ace::coroutines {

    template <typename runner_context_t>
    struct future_conductor_handle {

        future_conductor_handle() noexcept = default;

        future_conductor_handle(const future_conductor_handle&) noexcept = default;

        future_conductor_handle(future_conductor_handle&&) noexcept = default;

        virtual void forward(runner_context_t&& context) = 0;

        virtual void cancel() = 0;

        virtual ~future_conductor_handle() = default;
    };

    struct promise_conductor_handle {

        promise_conductor_handle() noexcept = default;

        virtual bool forward(void*) noexcept = 0;

        virtual void cancel() noexcept = 0;

        virtual ~promise_conductor_handle() = default;
    };

}

#endif //ACE_CONDUCTOR_H
