#ifndef ACE_HTTP_DETAIL_CONNECTION_H
#define ACE_HTTP_DETAIL_CONNECTION_H

#include "parser.h"
#include "router.h"
#include "ace/http/ws/handshake.h"
#include "ace/http/ws/connection.h"
#include "ace/futures/network.h"
#include "ace/coroutines/context.h"
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ace::http::detail {

struct ws_route {
    std::string pattern;
    std::function<ace::async<>(ws::connection, request)> handler;
};

/**
 * @brief Handle a single accepted TCP connection.
 *
 * Reads HTTP/1.1 requests in a keep-alive loop.
 * Upgrades to WebSocket transparently when the request matches a ws_route.
 *
 * conn     — accepted connection (owned by this coroutine)
 * router   — HTTP route table (shared, read-only)
 * ws_routes — WebSocket upgrade routes (shared, read-only)
 */
inline ace::async<> handle_connection(
    ace::futures::io_connection_entity  conn,
    const router&                       r,
    const std::vector<ws_route>&        ws_routes
) {
    std::array<char, 8192> buf{};
    parser p;

    while (true) {
        int n = co_await conn.recv(buf.data(), buf.size());
        if (n <= 0) co_return;

        auto [complete, error] = p.feed({buf.data(), static_cast<std::size_t>(n)});
        if (error) co_return;
        if (!complete) continue;

        request& req = p.get();

        // Save connection header before we potentially move req
        const bool keep_alive = req.header("connection") != "close";

        // ── WebSocket upgrade ──────────────────────────────────────────
        if (req.is_ws_upgrade()) {
            for (auto& wr : ws_routes) {
                std::unordered_map<std::string, std::string> params;
                if (!match_path(wr.pattern, req.path, params)) continue;

                req.params = std::move(params);
                const bool ok = co_await ws::handshake(conn, req);
                if (!ok) co_return;

                ws::connection ws_conn{conn};
                co_await wr.handler(std::move(ws_conn), std::move(req));
                co_return;
            }
            // No matching WS route
            constexpr std::string_view not_found =
                "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
            co_await conn.send(not_found.data(), not_found.size());
            co_return;
        }

        // ── HTTP dispatch ──────────────────────────────────────────────
        auto resp = co_await r.dispatch(std::move(req));
        auto s    = resp.serialize();
        co_await conn.send(s.data(), s.size());

        if (!keep_alive) co_return;
        p.reset();
    }
}

} // namespace ace::http::detail

#endif // ACE_HTTP_DETAIL_CONNECTION_H
