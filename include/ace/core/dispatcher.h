/**
* @file
 * @details This file contains a @b dispatcher singleton object,
 * allows to schedule tasks from any place, execute them.
 * Provides necessary services for active futures
 */

#ifndef ACE_CORE_DISPATCHER_H
#define ACE_CORE_DISPATCHER_H

#include "ace/commands/spawn.h"
#include "ace/core/signal.h"
#include "ace/core/balancer.h"

namespace ace::core {

class dispatcher {

    dispatcher() = default;

    balancer _balancer {};
    sig_pipe_t _sig_pipe{};

public:

    static dispatcher& get_instance() noexcept {
        static dispatcher instance;
        return instance;
    }

    static sig_pipe_t& get_sig_pipe() noexcept {
        return get_instance()._sig_pipe;
    }

    /**
     * @brief Function to schedule task at the dispatcher
     * @param new_task Task to be pushed into the dispatcher
     * @param rnr Specific runner to schedule on
     * @return void
     */
    void schedule(task&& new_task, const runner* rnr = nullptr) noexcept {
        new_task._coroutine.promise()._roaming = true;
        _balancer.schedule(std::forward<task>(new_task), rnr);
    }

    /**
     * @brief Checks if any Tasks stored in the dispatcher
     * @return @b true if empty, @b false otherwise
     */
    [[nodiscard]] bool empty() const noexcept { return _balancer.empty(); };

    /**
     * @brief Resumes all tasks from the runners.
     */
    void run() noexcept { while ( not empty() ) _balancer.run(); }

    /**
     * @brief Reloads balancer configuration
     */
    void reload() noexcept { while (not _balancer.reload()); }

};

} // end namespace ace::core


namespace ace {

    /**
     * @details Function to schedule task
     * @param new_task Task to be pushed into the dispatcher
     * @param rnr Specific runner to schedule on
     * @return void
     */
    static void schedule(task&& new_task, const core::runner* rnr = nullptr) noexcept {
        core::dispatcher::get_instance().schedule(std::forward<task>(new_task), rnr);
    }

    /**
     * @details Function to spawn parallel task from calling task
     * @param new_task Task to be pushed into the same runner as calling task
     * @return @b 'ace::core::commands::spawn' awaitable entity
     */
    static commands::spawn spawn(task&& new_task) noexcept {
        return commands::spawn(std::move(new_task));
    }

    /**
     * @details Checks if there are tasks to do
     * @return @b true if there are no tasks to proceed, @b false otherwise
     */
    inline bool empty() noexcept { return core::dispatcher::get_instance().empty(); }

    /**
     * @details Processing all scheduled tasks.
     */
    inline void run() noexcept { core::dispatcher::get_instance().run(); }

    /**
     * @brief Reloads dispatcher configurations
     */
    inline void reload() noexcept {
        core::dispatcher::get_instance().reload();
    }

    inline void reset_signal() {
        std::unique_ptr<core::signal_handler> _sigh;
        while (not core::dispatcher::get_sig_pipe().pop(_sigh) and not core::dispatcher::get_sig_pipe().empty()) _sigh.reset();
    }

    inline void interrupt() {
        core::dispatcher::get_sig_pipe().push(ace::core::make_signal(ace::core::interruption_signal{}));
        // reset_signal();
    }

    inline void terminate() {
        core::dispatcher::get_sig_pipe().push(ace::core::make_signal(ace::core::termination_signal{}));
        // reset_signal();
    }

} // end namespace ace

#endif // ACE_CORE_DISPATCHER_H
