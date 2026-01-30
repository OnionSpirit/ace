#ifndef ACE_FUTURE_TIMER_H
#define ACE_FUTURE_TIMER_H

#include <future>

#include "future.h"
#include "ace/common/selection.h"
#include "ace/coroutines/context.h"
#include "ace/core/clock.h"

using namespace std::chrono_literals;

namespace ace::futures {


class timer : public future_traits<timer> {

    core::duration_t _duration;
    bool _released {false};

    struct timer_conductor;
    friend timer_conductor;

    public:

        DECLARE_FUTURE(timer)
        IMPORT_FUTURE_ENV

        template <typename I, typename T>
        requires std::is_integral_v<I>
        explicit timer(std::chrono::duration<I, T> t) {
            _duration = std::chrono::duration_cast<std::chrono::milliseconds, uint64_t, std::milli>(t);
        };

        bool await_ready() override { return _released; }

        bool await_suspend(auto coroutine);

        static void await_resume() {}

        // TODO: Support detatch later
        // void detach() { _detached = true; }
        //
        // bool is_detached() { return _detached;}
};

} // end namespace ace::futures


//==============================- DEFINITIONS -==================================

#define ACE_FUTURE_TIMER_SPACE \
ace::futures::timer::

#define ACE_FUTURE_TIMER_MEMBER(returnT) \
returnT ACE_FUTURE_TIMER_SPACE

struct ACE_FUTURE_TIMER_SPACE timer_conductor : conductor_handler_t {

    timer_conductor() = delete;

    explicit timer_conductor(timer* timer_)
        : _timer(timer_) {};

    void forward(async<>&& ctx) override {
        // NOTE: Marking timer released.
        // NOTE: Because await_ready() will be called after context retreatment to runner.
        // NOTE: And retreatment will happen only when timer actually expired
        _timer->_released = true;
        core::clock::get_instance().subscribe(std::move(ctx), _timer->_duration);
    }

    ~timer_conductor() override = default;

    timer* const _timer;
};


ACE_FUTURE_TIMER_MEMBER(bool)
await_suspend(auto coroutine) {
    coroutine.promise()._conductor = timer_conductor{this};
    return true;
}

// /**
//  * @details Timer object that allows coroutines wait announced time
//  * before continuation (optionally can be attached to TimeManager)
//  * @tparam buffLenV Size of inner buffer of time records
//  * @tparam allocationT allocation mode
//  */
// template
// <
//     typename duration_t,
//     size_t buffLenV =16ul,
//     allocation_type allocationT = allocation_type::e_dynamic
// >
// class timer : public future_traits<timer<duration_t, buffLenV, allocationT>>
// {
//
//     typedef riot::meta::helpers::timed_pool<> timed_pool;
//     typedef riot::meta::helpers::pool_timed_record record;
//
//
//     class time_manager_handler {
//
//         template <typename TimeM>
//         static std::chrono::time_point<std::chrono::steady_clock>*
//         get_local_timestamp_ptr_wrap(void* time_manager_instance);
//
//         template <typename TimeM>
//         static void peak_wrap(void* time_manager_instance);
//
//         template <typename TimeM>
//         static void attach_wrap(void* time_manager_instance, timer& timer);
//
//         template <typename TimeM>
//         void assign (TimeM& t);
//
//     public:
//
//         time_manager_handler() = default;
//
//         template <typename TimeM>
//         time_manager_handler (TimeM* t);
//
//         template <typename TimeM>
//         TimeM& operator = (TimeM& t) { assign(t); return t; }
//
//         void peak() { _peak_impl(_time_manager_instance); }
//
//         void attach(timer& t) { _attach_impl(_time_manager_instance, t); }
//
//         auto get_local_timestamp_ptr() { return _get_local_timestamp_ptr_impl(_time_manager_instance); }
//
//         void* _time_manager_instance {nullptr};
//         std::chrono::time_point<std::chrono::steady_clock>*(*_get_local_timestamp_ptr_impl)(void*) {nullptr};
//         void(*_peak_impl)(void*) {nullptr};
//         void(*_attach_impl)(void*, timer&) {nullptr};
//     };
//
//     static bool time_is_up(const record& rec);
//
//     mutable timed_pool _time_records;
//     mutable std::chrono::time_point<std::chrono::steady_clock>** _current_time = &_time_records._current_time;
//     mutable time_manager_handler _time_manager;
//     bool _detached {false};
//
// public:
//
//     template <typename T, size_t TimeManagerSize, template <typename, size_t> typename TimeManagerMode>
//     explicit timer(std::chrono::duration<int64_t, T> t, riot::control::time_manager<TimeManagerSize, TimeManagerMode>& ts);
//
//     bool await_ready() override { return false; }
//
//     bool suspend(auto& coroutine);
//
//     void resume() const {}
//
//     unsigned int release_completed_records();
//
//     void detach() { _detached = true; }
//
//     bool is_detached() { return _detached;}
// };
//
// } // end namespace riot::async
//
//
// //==============================- DEFINITIONS -==================================
//
//
// ACE_FUTURE_TIMER_META
// bool ACE_FUTURE_TIMER_SPACE time_is_up(const record& rec) {
//
//     const auto& _start_point = std::get<1>(rec);
//     const auto* _current_time = std::get<2>(rec);
//     const auto& _duration = std::get<3>(rec);
//
//     return not _current_time or ((*_current_time - _start_point) >= _duration);
// }
//
//
// ACE_FUTURE_TIMER_META
// unsigned int ACE_FUTURE_TIMER_SPACE release_completed_records() {
//     record temp;
//     unsigned int _released_count {0};
//     if (_detached) {
//         while (not _time_records._container.empty()) {
//             while (_time_records._container.pop(temp));
//             auto& context = std::get<0>(temp);
//             if (context._coroutine.promise().reset_pool()) {
//                 context._coroutine.promise()._retcode =6;
//                 context._coroutine.promise()._current_pool.push(std::move(context));
//                 _released_count++;
//             }
//         }
//         return _released_count;
//     }
//     while(_time_records._container.pop<time_is_up>(temp)) {
//         auto& context = std::get<0>(temp);
//         if (context._coroutine.promise().reset_pool()) {
//             context._coroutine.promise()._current_pool.push(std::move(context));
//             _released_count++;
//         }
//     }
//     return _released_count;
// }
//
//
// ACE_FUTURE_TIMER_META
// template <typename TimeM>
// std::chrono::time_point<std::chrono::steady_clock>*
// ACE_FUTURE_TIMER_SPACE time_manager_handler::get_local_timestamp_ptr_wrap(void* time_manager_instance) {
//     auto* time_manager = reinterpret_cast<TimeM*>(time_manager_instance);
//     return time_manager->get_local_timestamp_ptr();
// }
//
//
// ACE_FUTURE_TIMER_META
// template <typename TimeM>
// void ACE_FUTURE_TIMER_SPACE time_manager_handler::peak_wrap(void* time_manager_instance) {
//     auto* time_manager = reinterpret_cast<TimeM*>(time_manager_instance);
//     return time_manager->peak();
// }
//
//
// ACE_FUTURE_TIMER_META
// template <typename TimeM>
// void ACE_FUTURE_TIMER_SPACE time_manager_handler::attach_wrap(void* time_manager_instance, timer& timer) {
//     auto* time_manager = reinterpret_cast<TimeM*>(time_manager_instance);
//     return time_manager->attach(timer);
// }
//
//
// ACE_FUTURE_TIMER_META
// template <typename TimeM>
// void ACE_FUTURE_TIMER_SPACE time_manager_handler::assign (TimeM& t) {
//     _time_manager_instance = reinterpret_cast<void*>(&t);
//     _get_local_timestamp_ptr_impl = get_local_timestamp_ptr_wrap<TimeM>;
//     _peak_impl = peak_wrap<TimeM>;
//     _attach_impl = attach_wrap<TimeM>;
// }
//
//
//
// ACE_FUTURE_TIMER_META
// template <typename TimeM>
// ACE_FUTURE_TIMER_SPACE time_manager_handler::time_manager_handler (TimeM* t) {
//     _time_manager_instance = reinterpret_cast<void*>(t);
//     _get_local_timestamp_ptr_impl = get_local_timestamp_ptr_wrap<TimeM>;
//     _peak_impl = peak_wrap<TimeM>;
//     _attach_impl = attach_wrap<TimeM>;
// }
//
//
// ACE_FUTURE_TIMER_META
// template <typename T, size_t TimeManagerSize, template <typename, size_t> typename TimeManagerMode>
// ACE_FUTURE_TIMER_SPACE timer(std::chrono::duration<int64_t, T> t,
//                                  riot::control::time_manager<TimeManagerSize, TimeManagerMode>& ts) {
//     _time_records._interval = t;
//     _time_manager = ts;
//     *_current_time = _time_manager.get_local_timestamp_ptr();
//     _time_manager.attach(*this);
// }
//
//
// ACE_FUTURE_TIMER_META
// bool ACE_FUTURE_TIMER_SPACE suspend(auto& coroutine) {
//     coroutine.promise().switch_pool(_time_records);
//     _time_manager.peak();
//     return true;
// }
#undef ACE_FUTURE_TIMER_META
#undef ACE_FUTURE_TIMER_SPACE
#endif // ACE_FUTURE_TIMER_H
