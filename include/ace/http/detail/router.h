#ifndef ACE_HTTP_ROUTER_H
#define ACE_HTTP_ROUTER_H

#include "request.h"
#include "response.h"
#include "ace/coroutines/context.h"
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ace::http {

/**
 * @brief Any callable (lambda, struct with operator(), free fn) that takes a
 * request by value and returns ace::async<response>.
 */
template<typename H>
concept http_handler = requires(H h, request req) {
    { h(std::move(req)) } -> std::same_as<ace::async<response>>;
};

namespace detail {

/**
 * @brief Match a route pattern against a concrete path.
 *
 * Pattern segments starting with ':' are named captures.
 * Example: "/rooms/:room/messages" matches "/rooms/general/messages"
 * and fills params with {room: "general"}.
 *
 * @return true if matched, false otherwise.
 */
inline bool match_path(
    std::string_view pattern,
    std::string_view path,
    std::unordered_map<std::string, std::string>& params
) noexcept {
    auto split = [](std::string_view sv, std::vector<std::string_view>& out) {
        std::size_t start = 0;
        for (std::size_t i = 0; i <= sv.size(); ++i) {
            if (i == sv.size() || sv[i] == '/') {
                if (i > start) out.push_back(sv.substr(start, i - start));
                start = i + 1;
            }
        }
    };

    std::vector<std::string_view> pat_parts, pth_parts;
    split(pattern, pat_parts);
    split(path,    pth_parts);

    if (pat_parts.size() != pth_parts.size()) return false;

    std::unordered_map<std::string, std::string> tmp;
    for (std::size_t i = 0; i < pat_parts.size(); ++i) {
        if (pat_parts[i].starts_with(':')) {
            tmp.emplace(std::string(pat_parts[i].substr(1)), std::string(pth_parts[i]));
        } else if (pat_parts[i] != pth_parts[i]) {
            return false;
        }
    }
    params = std::move(tmp);
    return true;
}

struct http_route {
    std::string method;
    std::string pattern;
    std::function<ace::async<response>(request)> handler;
};

} // namespace detail

class router {
    std::vector<detail::http_route> _routes;

public:
    template<http_handler H>
    router& add(std::string_view method, std::string_view pattern, H&& h) {
        _routes.push_back({
            std::string(method),
            std::string(pattern),
            std::forward<H>(h)
        });
        return *this;
    }

    template<http_handler H> router& get (std::string_view p, H&& h) { return add("GET",    p, std::forward<H>(h)); }
    template<http_handler H> router& post(std::string_view p, H&& h) { return add("POST",   p, std::forward<H>(h)); }
    template<http_handler H> router& put (std::string_view p, H&& h) { return add("PUT",    p, std::forward<H>(h)); }
    template<http_handler H> router& del (std::string_view p, H&& h) { return add("DELETE", p, std::forward<H>(h)); }

    [[nodiscard]] ace::async<response> dispatch(request req) const {
        for (auto& route : _routes) {
            if (route.method != req.method) continue;
            std::unordered_map<std::string, std::string> params;
            if (detail::match_path(route.pattern, req.path, params)) {
                req.params = std::move(params);
                co_return co_await route.handler(std::move(req));
            }
        }
        co_return response::empty(404);
    }
};

} // namespace ace::http

#endif // ACE_HTTP_ROUTER_H
