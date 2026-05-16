#ifndef ACE_VISUAL_DETAILS_PIPE_H
#define ACE_VISUAL_DETAILS_PIPE_H

#include <optional>

#include "ace/core/tools/macro.h"
#include "ace/core/traits/future.h"
#include "ace/core/traits/promise.h"
#include "ace/core/async_handle.h"

namespace ace::visual::details {
    enum class pipeline_state {
        e_idle,
        e_in_progress,
        e_complete,
        e_broken,
    };
}

template <>
struct std::formatter<ace::visual::details::pipeline_state> : std::formatter<std::string_view> {
    auto format(ace::visual::details::pipeline_state state, std::format_context& ctx) const {
        std::string_view name = "Unknown";
        switch (state) {
            case ace::visual::details::pipeline_state::e_idle:         name = "Idle"; break;
            case ace::visual::details::pipeline_state::e_in_progress:  name = "In Progress"; break;
            case ace::visual::details::pipeline_state::e_complete:     name = "Complete"; break;
            case ace::visual::details::pipeline_state::e_broken:       name = "Broken"; break;
        }
        return std::formatter<std::string_view>::format(name, ctx);
    }
};

namespace ace::visual::details {

    struct token {
        explicit token(const pipeline_state state) : _state(state) {}
        pipeline_state _state = pipeline_state::e_idle;
    };

    struct resume : token { resume() : token(pipeline_state::e_in_progress) {}; };

    struct cancel : token { cancel() : token(pipeline_state::e_broken) {}; };

    struct finish : token { finish() : token(pipeline_state::e_complete) {}; };


    // NOTE: Object to interact with the pipeline from Coroutine body
    template <typename output_t = void>
    struct pipe {

        pipeline_state _state = pipeline_state::e_idle;

        pipe() = default;

        explicit pipe(const token tkn) { _state = tkn._state; }

        explicit pipe(const output_t& out) {
            _output = out;
            _state = pipeline_state::e_in_progress;
        }

        explicit pipe(output_t&& out) {
            _output = std::forward<output_t>(out);
            _state = pipeline_state::e_in_progress;
        }

        pipe(const pipe& p) {
            _output = p._output;
            _state = p._state;
        }

        // pipe(pipe&& p) noexcept { _output = std::forward<output_t>(p._output); }

        template <typename input_t>
        // requires (not std::same_as<input_t, output_t> and not std::same_as<input_t, pipe>)
        pipe(const input_t& out) {
            _output = std::forward<output_t>(output_t{out});
            _state = pipeline_state::e_in_progress;
        }

        template <typename input_t>
        requires (not std::same_as<input_t, output_t> and not std::same_as<input_t, pipe>)
        pipe(const input_t out) {
            _output = std::forward<output_t>(output_t{out});
            _state = pipeline_state::e_in_progress;
        }

        // template <typename input_t>
        // requires (not std::same_as<input_t, output_t> and not std::same_as<input_t, pipe>)
        // pipe(input_t&& out) { _output = std::forward<output_t>(output_t{out}); }

        output_t _output;

        pipe interrupt() { _state = pipeline_state::e_broken; return this; }

        pipe resume() { _state = pipeline_state::e_complete; return this; }
    };

    // NOTE: Object to interact with the pipeline from Coroutine body
    template <>
    struct pipe<void> {

        pipeline_state _state = pipeline_state::e_idle;

        pipe() = default;

        pipe(const token tkn) { _state = tkn._state; }

        pipe(const pipe& p) { _state = p._state; }

        static pipe interrupt() { pipe p{}; p._state = pipeline_state::e_broken; return p;}

        static pipe resume() { pipe p{}; p._state = pipeline_state::e_complete; return p;}
    };

    template <typename lhs_t, typename rhs_t>
    concept is_compatible = std::same_as<std::decay_t<lhs_t>, std::decay_t<rhs_t>>
        or std::constructible_from<rhs_t, lhs_t>;
}

#endif //ACE_VISUAL_DETAILS_PIPE_H
