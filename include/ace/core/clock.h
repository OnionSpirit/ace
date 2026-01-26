/**
 * @file
 * @details This file contains a @b clock class, that provides service for time-dependent operations.
 * Clock handles global timestamp and releases passed tasks to those runners when time comes.
 */
#ifndef ACE_CLOCK_H
#define ACE_CLOCK_H
#include <chrono>
#include <queue>
#include <set>

#include "service.h"
#include "ace/coroutines/context.h"

namespace ace::core {

    struct clock_record {
        std::chrono::time_point<std::chrono::steady_clock> _release_ts;
        async<> _context;

        // NOTE: Clock record is greater if its completion time is later
        bool operator > (const clock_record& p) const {
            return this->_release_ts > p._release_ts;
        }
    };

    struct clock : global_service_traits<clock> {

        clock() = default;

        void setup_releases_per_yank(const int releases_per_yank) {
            _releases_per_yank = releases_per_yank;
        }

        // TODO: Add record detach
        bool service_yank() {
            _current_ts = std::chrono::steady_clock::now();
            clock_record record;

            while (_requests.pop(record)) {
                _heap.insert(std::move(record));
            }
            // TODO: FIX BATCH!!! After fix replace while block
            // for (auto&& record : _requests.pop_batch())
            //     _heap.insert(std::forward<clock_record>(record));

            for (int i =0;
                not _heap.empty()
                and _heap.begin()->_release_ts <= _current_ts
                and i < _releases_per_yank;
            ++i) {
                auto [_release_ts, _context]
                    = std::forward<clock_record>(_heap.extract(_heap.begin()).value());
                runner::schedule(std::move(_context));
            }

            return _heap.empty() and _requests.empty();
        }

        void subscribe(clock_record&& record) { _requests.push(std::move(record)); }

        static auto current_time() {
            return get_instance()._current_ts;
        }

        nukes::dynamic::mpsc_queue<clock_record> _requests;
        std::multiset<clock_record, std::greater<>> _heap;
        std::chrono::time_point<std::chrono::steady_clock> _current_ts = std::chrono::steady_clock::now();
        int _releases_per_yank {1024};
    };

}

#endif //ACE_CLOCK_H
