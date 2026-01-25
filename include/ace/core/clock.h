/**
 * @file
 * @details This file contains a @b clock class, that provides service for time-dependent operations.
 * Clock handles global timestamp and releases passed tasks to those runners when time comes.
 */
#ifndef ACE_CLOCK_H
#define ACE_CLOCK_H
#include <chrono>
#include <queue>
#include <unordered_set>

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
        //
        // // NOTE: Clock record is less if its completion time is earlier
        // bool operator < (const clock_record& p) const {
        //     return (this-> _request_ts + this->_duration) < (p._request_ts + p._duration);
        // }
    };

    struct clock : global_service_traits<clock> {

        clock() = default;

        void setup_releases_per_yank(const int releases_per_yank) {
            _releases_per_yank = releases_per_yank;
        }

        // TODO: Add record detach
        async<> service_yank() {
            _current_ts = std::chrono::steady_clock::now();
            clock_record record;

            while (_requests.pop(record)) {
                _heap.insert(std::move(record));
            }
            // TODO: FIX BATCH!!! After fix replace while block
            // for (auto&& record : _requests.pop_batch())
            //     _heap.insert(std::forward<clock_record>(record));

            int i = 0; auto el = _heap.begin();
            while (el not_eq _heap.end() and i < _releases_per_yank) {
                auto node = std::forward<clock_record>(_heap.extract(el).value());
                if (node._request_ts + node._duration >= _current_ts) {
                    node._context._coroutine.promise()._future = nullptr;
                    runner::schedule(std::move(node._context));
                } else break;
                _heap.insert(std::forward<clock_record>(node));
                ++i; ++el;
            }
            co_return;
        }

        void subscribe(clock_record&& record) { _requests.push(std::move(record)); }

        static auto current_time() {
            return get_instance()._current_ts;
        }

        nukes::dynamic::mpsc_queue<clock_record> _requests;
        std::set<clock_record, std::greater<>> _heap;
        std::chrono::time_point<std::chrono::steady_clock> _current_ts = std::chrono::steady_clock::now();
        int _releases_per_yank {1024};
    };

}

#endif //ACE_CLOCK_H
