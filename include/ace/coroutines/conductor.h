/**
 * @file The file contains a class defining the 'conductor' interface.
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
    struct conductor_traits {

        virtual void forward(runner_context_t&& context) = 0;

        virtual ~conductor_traits() = default;
    };

}

#endif //ACE_CONDUCTOR_H
