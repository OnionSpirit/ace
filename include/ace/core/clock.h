/**
 * @file
 * @details This file contains a @b clock class, that provides service for time-dependent operations.
 * Clock handles global timestamp and releases passed tasks to those runners when time comes.
 */
#ifndef ACE_CLOCK_H
#define ACE_CLOCK_H
#include <chrono>
#include <queue>

#include "service.h"
#include "ace/coroutines/context.h"
#include "ace/futures/channel.h"

namespace ace::core {

    struct clock_record {
        std::chrono::time_point<std::chrono::steady_clock> _request_ts;
        std::chrono::duration<uint64_t, std::micro> _duration;
        async<> _context;

        // NOTE: Clock record is greater if its completion time is later
        bool operator > (const clock_record& p) const {
            return (this-> _request_ts + this->_duration) > (p._request_ts + p._duration);
        }
    };

    struct clock : service_traits<clock> {

        clock() = delete;

        explicit clock(const int releases_per_yank)
            : service_traits()
            , _releases_per_yank(releases_per_yank) {
        };

        // TODO: Add record detach
        async<> service_yank() {
            _current_ts = std::chrono::steady_clock::now();
            for (auto&& record : _requests.pop_batch())
                _queue.push(std::forward<clock_record>(record));
            for (int i = 0; not _queue.empty() and i < _releases_per_yank; ++i) {
                const auto&[_request_ts, _duration, _context] = _queue.top();
                if (_request_ts + _duration >= _current_ts) {
                    // TODO: Need to write own priority queue because std type can't pop move-only type handy
                    runner::schedule(std::move((const_cast<async<>&&>(_context))));
                    _queue.pop();
                }
            }
            co_return;
        }

        void subscribe(clock_record&& record) { _requests.push(std::move(record)); }

        nukes::dynamic::mpsc_queue<clock_record> _requests;
        std::priority_queue<clock_record, std::deque<clock_record>, std::greater<>> _queue;
        std::chrono::time_point<std::chrono::steady_clock> _current_ts;
        int _releases_per_yank {1024};
    };

}

#endif //ACE_CLOCK_H
