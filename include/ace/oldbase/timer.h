#ifndef RIOT_AWAITABLE_TIMER_H
#define RIOT_AWAITABLE_TIMER_H

#include "riot.h"


namespace riot::async {

/**
 * @details Timer object that allows coroutines wait announced time
 * before continuation (optionally can be attached to TimeManager)
 * @tparam TimeRecordsBufferSize Size of inner buffer of time records
 * @tparam RecordsStorage allocation strategy for inner time records buffer
 */
template
<
    size_t TimeRecordsBufferSize =16ul,

    template <typename, size_t>
    typename AllocationPolicy = riot::component_modes::allocation::dynamic_mode
>
requires riot::component_modes::allocation::ModeRequirement<
        AllocationPolicy, riot::meta::helpers::pool_timed_record, TimeRecordsBufferSize>
class timer :
        public awaitable
            <
                timer<TimeRecordsBufferSize, AllocationPolicy>,
                riot::component_modes::async::awaitable::management_strategy::command
            >
{

    typedef riot::meta::helpers::timed_pool<> timed_pool;
    typedef riot::meta::helpers::pool_timed_record record;

    class time_manager_handler {

        template <typename TimeM>
        static std::chrono::time_point<std::chrono::steady_clock>*
        get_local_timestamp_ptr_wrap(void* time_manager_instance);

        template <typename TimeM>
        static void peak_wrap(void* time_manager_instance);

        template <typename TimeM>
        static void attach_wrap(void* time_manager_instance, timer& timer);

        template <typename TimeM>
        void assign (TimeM& t);

    public:

        time_manager_handler() = default;

        template <typename TimeM>
        time_manager_handler (TimeM* t);

        template <typename TimeM>
        TimeM& operator = (TimeM& t) { assign(t); return t; }

        void peak() { _peak_impl(_time_manager_instance); }

        void attach(timer& t) { _attach_impl(_time_manager_instance, t); }

        auto get_local_timestamp_ptr() { return _get_local_timestamp_ptr_impl(_time_manager_instance); }

        void* _time_manager_instance {nullptr};
        std::chrono::time_point<std::chrono::steady_clock>*(*_get_local_timestamp_ptr_impl)(void*) {nullptr};
        void(*_peak_impl)(void*) {nullptr};
        void(*_attach_impl)(void*, timer&) {nullptr};
    };

    static bool time_is_up(const record& rec);

    mutable timed_pool _time_records;
    mutable std::chrono::time_point<std::chrono::steady_clock>** _current_time = &_time_records._current_time;
    mutable time_manager_handler _time_manager;
    bool _detached {false};

public:

    template <typename T, size_t TimeManagerSize, template <typename, size_t> typename TimeManagerMode>
    explicit timer(std::chrono::duration<int64_t, T> t, riot::control::time_manager<TimeManagerSize, TimeManagerMode>& ts);

    bool ready() const { return false; }

    bool suspend(auto& coroutine);

    void resume() const {}

    unsigned int release_completed_records();

    void detach() { _detached = true; }

    bool is_detached() { return _detached;}
};

} // end namespace riot::async


//==============================- DEFINITIONS -==================================


RIOT_AWAITABLE_TIMER_META
bool RIOT_AWAITABLE_TIMER_SPACE time_is_up(const record& rec) {

    const auto& _start_point = std::get<1>(rec);
    const auto* _current_time = std::get<2>(rec);
    const auto& _duration = std::get<3>(rec);

    return not _current_time or ((*_current_time - _start_point) >= _duration);
}


RIOT_AWAITABLE_TIMER_META
unsigned int RIOT_AWAITABLE_TIMER_SPACE release_completed_records() {
    record temp;
    unsigned int _released_count {0};
    if (_detached) {
        while (not _time_records._container.empty()) {
            while (_time_records._container.pop(temp));
            auto& context = std::get<0>(temp);
            if (context._coroutine.promise().reset_pool()) {
                context._coroutine.promise()._retcode =6;
                context._coroutine.promise()._current_pool.push(std::move(context));
                _released_count++;
            }
        }
        return _released_count;
    }
    while(_time_records._container.pop<time_is_up>(temp)) {
        auto& context = std::get<0>(temp);
        if (context._coroutine.promise().reset_pool()) {
            context._coroutine.promise()._current_pool.push(std::move(context));
            _released_count++;
        }
    }
    return _released_count;
}


RIOT_AWAITABLE_TIMER_META
template <typename TimeM>
std::chrono::time_point<std::chrono::steady_clock>*
RIOT_AWAITABLE_TIMER_SPACE time_manager_handler::get_local_timestamp_ptr_wrap(void* time_manager_instance) {
    auto* time_manager = reinterpret_cast<TimeM*>(time_manager_instance);
    return time_manager->get_local_timestamp_ptr();
}


RIOT_AWAITABLE_TIMER_META
template <typename TimeM>
void RIOT_AWAITABLE_TIMER_SPACE time_manager_handler::peak_wrap(void* time_manager_instance) {
    auto* time_manager = reinterpret_cast<TimeM*>(time_manager_instance);
    return time_manager->peak();
}


RIOT_AWAITABLE_TIMER_META
template <typename TimeM>
void RIOT_AWAITABLE_TIMER_SPACE time_manager_handler::attach_wrap(void* time_manager_instance, timer& timer) {
    auto* time_manager = reinterpret_cast<TimeM*>(time_manager_instance);
    return time_manager->attach(timer);
}


RIOT_AWAITABLE_TIMER_META
template <typename TimeM>
void RIOT_AWAITABLE_TIMER_SPACE time_manager_handler::assign (TimeM& t) {
    _time_manager_instance = reinterpret_cast<void*>(&t);
    _get_local_timestamp_ptr_impl = get_local_timestamp_ptr_wrap<TimeM>;
    _peak_impl = peak_wrap<TimeM>;
    _attach_impl = attach_wrap<TimeM>;
}



RIOT_AWAITABLE_TIMER_META
template <typename TimeM>
RIOT_AWAITABLE_TIMER_SPACE time_manager_handler::time_manager_handler (TimeM* t) {
    _time_manager_instance = reinterpret_cast<void*>(t);
    _get_local_timestamp_ptr_impl = get_local_timestamp_ptr_wrap<TimeM>;
    _peak_impl = peak_wrap<TimeM>;
    _attach_impl = attach_wrap<TimeM>;
}


RIOT_AWAITABLE_TIMER_META
template <typename T, size_t TimeManagerSize, template <typename, size_t> typename TimeManagerMode>
RIOT_AWAITABLE_TIMER_SPACE timer(std::chrono::duration<int64_t, T> t,
                                 riot::control::time_manager<TimeManagerSize, TimeManagerMode>& ts) {
    _time_records._interval = t;
    _time_manager = ts;
    *_current_time = _time_manager.get_local_timestamp_ptr();
    _time_manager.attach(*this);
}


RIOT_AWAITABLE_TIMER_META
bool RIOT_AWAITABLE_TIMER_SPACE suspend(auto& coroutine) {
    coroutine.promise().switch_pool(_time_records);
    _time_manager.peak();
    return true;
}

#undef RIOT_AWAITABLE_TIMER_META
#undef RIOT_AWAITABLE_TIMER_SPACE
#endif // RIOT_AWAITABLE_TIMER_H
