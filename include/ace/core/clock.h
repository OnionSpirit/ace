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

        bool operator<(const clock_record& p) const {
            return this->_duration < p._duration;
        }
    };

    struct clock : service<clock> {

        clock() = delete;

        explicit clock(const int releases_per_yank)
            : service()
            , _releases_per_yank(releases_per_yank) {
        };

        // TODO: Add record detach
        async<> service_yank() {
            _current_ts = std::chrono::steady_clock::now();
            auto [_request_ts, _duration, _context] = _queue.top();
            for (int i = 0; _request_ts + _duration.count() >= _current_ts and i < _releases_per_yank; ++i ) {
                runner::schedule(std::move(_context));
                _queue.pop();
                std::tie(_request_ts, _duration, _context) = _queue.top();
            }
            co_return;
        }

        std::priority_queue<clock_record> _queue;
        std::chrono::time_point<std::chrono::steady_clock> _current_ts;
        int _releases_per_yank {1024};
    };

}

#endif //ACE_CLOCK_H
