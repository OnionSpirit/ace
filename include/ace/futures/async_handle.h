#ifndef ACE_FUTURE_ASYNC_HANDLE_H
#define ACE_FUTURE_ASYNC_HANDLE_H

#include "future.h"
#include "ace/core/runner.h"
#include "ace/coroutines/context.h"

namespace ace::futures {

    class async_handle {

        class join_handler;

        coroutines::control_block_handle _handle;

    public:

        async_handle() = delete;

        explicit async_handle(const coroutines::control_block_handle& handle)
            : _handle{handle} {}

        [[nodiscard]] auto join() const;

        [[nodiscard]] bool done() const { return _handle.done(); }

        void cancel() { _handle.cancel(); }

    };

    class async_handle::join_handler final : future_traits<join_handler> {

        DECLARE_FUTURE(join_handler)
        IMPORT_FUTURE_ENV

        coroutines::control_block_handle _handle;

        struct join_handler_conductor;

    public:

        join_handler() = delete;

        explicit join_handler(const coroutines::control_block_handle& handle) : _handle{handle} {}

        bool await_ready() override { return false; }

        template<typename promise_u>
        bool await_suspend(std::coroutine_handle<promise_u> outer);

        bool await_resume() const { return _handle.done(); }
    };

    struct async_handle::join_handler::join_handler_conductor final : conductor_handler_t {

        coroutines::control_block_handle _handle;

        join_handler_conductor() = delete;

        explicit join_handler_conductor(const coroutines::control_block_handle& handle) : _handle{handle} {}

        void forward(async<>&& ctx) override { _handle.subscribe(&ctx); }

        // TODO: Finish later
        void cancel() override {  }

        ~join_handler_conductor() override = default;

    };

} // end namespace ace::futures


//==============================- DEFINITIONS -==================================


#define ACE_FUTURE_ASYNC_HANDLE_SPACE \
ace::futures::async_handle::

#define ACE_FUTURE_ASYNC_HANDLE_MEMBER(returnT) \
returnT ACE_FUTURE_ASYNC_HANDLE_SPACE

#define ACE_FUTURE_JOIN_HANDLER_FUTURE_SPACE \
ace::futures::async_handle::join_handler::

#define ACE_FUTURE_JOIN_HANDLER_FUTURE_MEMBER(returnT) \
returnT ACE_FUTURE_JOIN_HANDLER_FUTURE_SPACE


ACE_FUTURE_ASYNC_HANDLE_MEMBER(auto) join() const { return join_handler{_handle}; }

ACE_FUTURE_JOIN_HANDLER_FUTURE_MEMBER(template<typename promise_u> bool)
await_suspend(std::coroutine_handle<promise_u> outer) {
    outer.promise()._future_conductor = join_handler_conductor{_handle};
    return false;
}

#undef ACE_FUTURE_ASYNC_HANDLE_SPACE
#undef ACE_FUTURE_ASYNC_HANDLE_MEMBER
#undef ACE_FUTURE_JOIN_HANDLER_FUTURE_SPACE
#undef ACE_FUTURE_JOIN_HANDLER_FUTURE_MEMBER

#endif //ACE_FUTURE_ASYNC_HANDLE_H
