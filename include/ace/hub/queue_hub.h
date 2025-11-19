#ifndef ACE_QUEUE_HUB_H
#define ACE_QUEUE_HUB_H

#include "ace/promises/async.h"
#include <nukes/dynamic/mpsc_queue.h>
#include <nukes/dynamic/mpmc_queue.h>

namespace ace::hubs {

    struct queue_hub : hub_handler_t {

        bool emplace(promises::task&& task_) override {
            task_._coroutine.promise()._actual_hub = this;
            return _waiters.push(std::forward<promises::task>(task_));
        }

        bool release(void * = nullptr) override {
            auto* task_node = _waiters.pop_node();
            task_node->_data._coroutine.promise()._actual_hub = task_node->_data._coroutine.promise()._runner_hub;
            return task_node->_data._coroutine.promise()._runner_hub->emplace(std::move(task_node->_data));
        }

        nukes::dynamic::mpsc_queue<promises::task> _waiters{};

        ~queue_hub() override = default;
    };

}

#endif //ACE_QUEUE_HUB_H
