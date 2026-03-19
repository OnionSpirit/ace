#ifndef ACE_FUTURE_ASYNC_HANDLE_H
#define ACE_FUTURE_ASYNC_HANDLE_H

#include "future.h"
#include "ace/coroutines/context.h"

namespace ace::futures {

    class join_handler : public future_traits<join_handler> {

    protected:

        coroutines::control_block_handle _handle;

        struct join_handler_conductor;

    public:

        DECLARE_FUTURE(join_handler)
        IMPORT_FUTURE_ENV

        join_handler() = default;

        explicit join_handler(const coroutines::control_block_handle& handle)
            : _handle{handle} {}

        bool await_ready() override {
            if (_handle.is_idle()) return true;
            return _handle.done();
        }

        template<typename promise_u>
        bool await_suspend(std::coroutine_handle<promise_u> outer);

        [[nodiscard]] bool await_resume() const { return _handle.done(); }
    };

    class async_handle final : protected join_handler {

    public:

        async_handle() = delete;

        explicit async_handle(const coroutines::control_block_handle& handle)
            : join_handler(handle) {}

        [[nodiscard]] auto join() noexcept -> join_handler&;

        [[nodiscard]] bool done() const { return _handle.done(); }

        void cancel() { _handle.cancel(); }

    };

    struct join_handler::join_handler_conductor final : conductor_handler_t {

        coroutines::control_block_handle _handle;

        join_handler_conductor() = delete;

        explicit join_handler_conductor(const coroutines::control_block_handle& handle) : _handle{handle} {}

        void forward(async<>&& ctx) override { _handle.forward(&ctx); }

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
ace::futures::join_handler::

#define ACE_FUTURE_JOIN_HANDLER_FUTURE_MEMBER(returnT) \
returnT ACE_FUTURE_JOIN_HANDLER_FUTURE_SPACE


ACE_FUTURE_ASYNC_HANDLE_MEMBER(auto)
join() noexcept -> join_handler& { return *static_cast<join_handler*>(this); }

ACE_FUTURE_JOIN_HANDLER_FUTURE_MEMBER(template<typename promise_u> bool)
await_suspend(std::coroutine_handle<promise_u> outer) {
    outer.promise()._runner_conductor = join_handler_conductor{_handle};
    return true;
}


#undef ACE_FUTURE_ASYNC_HANDLE_SPACE
#undef ACE_FUTURE_ASYNC_HANDLE_MEMBER
#undef ACE_FUTURE_JOIN_HANDLER_FUTURE_SPACE
#undef ACE_FUTURE_JOIN_HANDLER_FUTURE_MEMBER

#endif //ACE_FUTURE_ASYNC_HANDLE_H
