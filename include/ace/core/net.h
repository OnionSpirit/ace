#ifndef ACE_CORE_NET_H
#define ACE_NET_H

#include <unordered_map>
#include "vortex.h"
#include "poll.h"
#include "ace/futures/channel.h"

namespace ace::core {

    class network_manager : vortex_traits<network_manager, vortex_spawn_mode::e_single> {

        enum class poll_state : uint8_t { ready_for_poll, awaits_processing, save_current }; ///< Enum of poll ability states

        enum class change_command : uint8_t { add, remove, modify }; ///< Enum of change actions for spacateevents

        typedef std::unordered_map<uint, futures::channel_static<pollfd, 1, 1>*> EventPipes; ///< Type of map to store channels for each event

        typedef std::unordered_map<uint, std::pair<pollfd, poll_state>> FDset; ///< Type of hashmap for storing events and the state of the ability to poll each of them

        /**
         * @brief Event set modification record
         */
        struct EventChangeRecord {
            pollfd event;
            change_command command;
            poll_state state;
        };

        std::atomic<short> _event_pool_context_presence{0}; ///< flag of helper coroutine working state

        riot::async::keeper<EventPipes> _awaitable_pipes; ///< Pipes for async access from contexts

        FDset _events_set; ///< Set of events to spectate;

        riot::common::atomic_queue<EventChangeRecord> _events_changes; ///< Queue with changes to apply to _events_set

        /**
         * @brief Function to continue polling
         * @return void
        */
        inline void continue_polling() noexcept;

        /**
         * @brief Applies event changes for _event_set from _event_changes
         * @return void
        */
        inline void apply_event_changes() noexcept;

        /**
         * @brief Sends captured events to it's waiters, excluding them from pollable events set
         * @param pollable_events Array of events to poll
         * @param pollable_events_size Size of the array of events to poll
         * @param common_events_count Count of events captured by polling
         * @return void
        */
        inline void accept_polled_events(pollfd*& pollable_events, size_t& pollable_events_size, int& captured_events_count) noexcept;

        friend class Event;

    public:

        /**
         * @brief Service сontext for polling events
         * @return void
         */
        inline promise<bool> ping() noexcept;

        explicit network_manager(riot::control::context_pool_handler p) : _polling_pool(p) {};

        /**
         * @brief Adds file descriptor to polling.
         * @param fd File descriptor
         * @param event_type poll event mask
         * @param resp_channel Channel to get pollfd-struct of captured event from EventManager
         * @return void
         */
        inline async<> add_fd(const uint& fd, const short& event_type, riot::async::channel_static_static<pollfd, 1, 1>& resp_channel) noexcept;

        /**
         * @brief Modifies exsisting file descriptor.
         * @param fd File descriptor to modify
         * @param event_type poll event mask
         * @param resp_channel Channel to get ready event from manager
         * @return void
         */
        inline async<> modify_fd(const uint& fd, const short& event_type,
            riot::async::channel_static_static<pollfd, 1, 1>& resp_channel) noexcept;

        /**
         * @brief Modifies exsisting file descriptor.
         * @param fd File descriptor to modify
         * @param event_type poll event mask
         * @param resp_channel Channel to get ready event from manager
         * @return void
         */
        inline async<> modify_fd(const uint& fd, const short& event_type) noexcept;

        /**
         * @brief Removes file descriptor from polling.
         * @param fd File descriptor
         * @return void
         */
        inline async<> remove_fd(const uint& fd) noexcept;

        /**
         * @brief Async wait for event at FD @b(co_await required)
         * @param fd File descriptor
         * @return File descriptor event code
         */
        inline async<short> capture_event(const uint& fd) noexcept;

        /**
         * @brief Marks event as processed and prepared for next polling
         * @param fd File descriptor
         * @return File descriptor event code
         */
        inline void release_event(const uint& fd) noexcept;

        /**
         * @brief Function to attach to executing pool
         * @param pool Pool attach to
         * @return void
        */
        inline void attach_polling(context_pool_handler& pool) noexcept { _polling_pool = pool; }
};

inline void network_manager::continue_polling() noexcept {
    if (not _event_pool_context_presence.load()) {
        _event_pool_context_presence++;
        _polling_pool.push(polling());
    }
};

inline void network_manager::apply_event_changes() noexcept {
    while (not _events_changes.empty()) {

            EventChangeRecord record;

            if(not _events_changes.pop(record)) continue;

            switch (record.command) {
                case change_command::add:
                    _events_set.emplace((uint)(record.event.fd), std::pair<pollfd, poll_state>{ record.event, record.state });
                    break;
                case change_command::remove:
                    if (_events_set.contains((uint)(record.event.fd))) {
                        _events_set.erase((uint)(record.event.fd));
                    }
                    break;
                case change_command::modify:
                    if (_events_set.contains((uint)(record.event.fd))) {
                        auto& event_handler = _events_set.at((uint)(record.event.fd));
                        event_handler.first.events = record.event.events;
                        if (record.state not_eq poll_state::save_current) {
                            event_handler.second = record.state;
                        }
                    }
                    break;
                default: continue;
            };
        }
}

inline void network_manager::accept_polled_events(pollfd*& pollable_events, size_t& pollable_events_size, int& captured_events_count) noexcept {
    const auto* pipes { _awaitable_pipes.read_token().capture() };
    for (size_t i {0}; i < pollable_events_size and captured_events_count; ++i) {
        auto& e = pollable_events[i];
        if(e.revents != 0) {
            auto record = EventChangeRecord {
                .event = e,
                .command = change_command::modify,
                .state = poll_state::awaits_processing
            };
            pipes->at(e.fd)->push(e);
            while(not _events_changes.push(record));
            captured_events_count--;
        }
    }
}

inline async<> network_manager::add_fd(const uint& fd,
                                                        const short& event_type,
                                                        riot::async::channel_static_static<pollfd, 1, 1>& resp_channel) noexcept {

    (co_await _awaitable_pipes.update_token().capture()) ->
        emplace(int(fd), &resp_channel);

    auto add_event = EventChangeRecord {
        .event = {.fd = (int)fd, .events = event_type, .revents = 0},
        .command = change_command::add,
        .state = poll_state::ready_for_poll
    };

    while (not _events_changes.push(add_event));

    co_return;
}

inline async<> network_manager::modify_fd(const uint& fd,
                                                    const short& event_type,
                                                    riot::async::channel_static_static<pollfd, 1, 1>& resp_channel) noexcept{

    auto* pipes { (co_await _awaitable_pipes.update_token().capture()) };

    auto modify_event = EventChangeRecord {
        .event = {.fd = (int)fd, .events = event_type, .revents =0 },
        .command = change_command::modify,
        .state = poll_state::save_current
    };

    if (pipes -> contains(fd)) {
        while (not _events_changes.push(modify_event));
        pipes -> at(int(fd)) = &resp_channel;
    }

    co_return;
}

inline async<> network_manager::modify_fd(const uint& fd, const short& event_type) noexcept {

    auto modify_event = EventChangeRecord {
        .event = {.fd = (int)fd, .events = event_type, .revents =0 },
        .command = change_command::modify,
        .state = poll_state::save_current
    };

    if (_awaitable_pipes.read_token().capture() -> contains(fd)) {
        while (not _events_changes.push(modify_event));
    }

    co_return;
}

inline async<> network_manager::remove_fd(const uint& fd) noexcept {

    auto* pipes { (co_await _awaitable_pipes.update_token().capture()) };

    if (pipes -> contains(fd)) {

        auto remove_event = EventChangeRecord {
            .event = {.fd = (int)fd, .events = 0, .revents = 0},
            .command = change_command::remove,
            .state = poll_state::save_current
        };

        while (not _events_changes.push(remove_event));

        pipes -> erase(fd);
    }

    co_return;
}

inline async<> network_manager::polling() noexcept {

    while (_event_pool_context_presence.load(std::memory_order_acquire)) {

        int ret {};
        size_t pollable_size {0};

        apply_event_changes();

        if (_events_set.empty()) co_return break_polling();

        for (auto& event : _events_set | std::views::values) {
            if (event.second == poll_state::ready_for_poll) {
                pollable_size++;
            }
        }

        if (not pollable_size) co_return break_polling();

        auto* pollable_events_arr_view =
            reinterpret_cast<pollfd*>(alloca(pollable_size * sizeof(pollfd)));

        for (size_t i =0; auto& event : _events_set | std::views::values) {
            if (event.second == poll_state::ready_for_poll){
                pollable_events_arr_view[i] = event.first;
                i++;
            }
        }

        ret = ::poll(pollable_events_arr_view, pollable_size, 1);

        if (ret == -1) {
            std::cerr << strerror(errno) << std::endl;
        } else if (ret not_eq 0) {
            accept_polled_events(pollable_events_arr_view, pollable_size, ret);
        }

        co_suspend;
    }
}

inline async<short> network_manager::capture_event(const uint& fd) noexcept {

    const auto* pipes { _awaitable_pipes.read_token().capture() };
    if (pipes->contains(fd)) {
        continue_polling();
        const auto* pipes { _awaitable_pipes.read_token().capture() };
        const auto res = (co_await pipes->at(fd)->pull()).revents;
        co_return res;
    } else co_return 0;
}


inline void network_manager::release_event(const uint& fd) noexcept {

    if (_awaitable_pipes.read_token().capture() -> contains(fd)) [[likely]] {
        const auto stored_event = _events_set.at(fd);
        auto modify_event = EventChangeRecord {
            .event = stored_event.first,
            .command = change_command::modify,
            .state = poll_state::ready_for_poll
        };
        while(not _events_changes.push(modify_event));
        continue_polling();
    }
}


}

#endif //ACE_CORE_NET_H
