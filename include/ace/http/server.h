#ifndef ACE_HTTP_SERVER_H
#define ACE_HTTP_SERVER_H

#include "detail/connection.h"
#include "ace/futures/network.h"
#include "ace/commands/spawn.h"
#include "ace/coroutines/context.h"
#include <cstdint>
#include <netinet/in.h>
#include <string>
#include <string_view>

namespace ace::http {

/**
 * @brief Any callable that handles a WebSocket connection.
 * Receives the upgraded ws::connection and the original HTTP request
 * (populated with route params).
 */
template<typename H>
concept ws_handler = requires(H h, ws::connection conn, request req) {
    { h(std::move(conn), std::move(req)) } -> std::same_as<ace::async<>>;
};

struct server_config {
    in_addr_t addr    = INADDR_LOOPBACK;
    uint16_t  port    = 8080;
    int       backlog = 128;
};

/**
 * @brief Async HTTP/WebSocket server.
 *
 * Usage:
 * @code
 *   ace::http::server s{{ .port = 8080 }};
 *   s.get("/ping", [](request) -> ace::async<response> { co_return response::text("pong"); })
 *    .ws("/chat/:room", ChatHandler{broker});
 *
 *   ace::schedule(s.listen());
 *   ace::run();
 * @endcode
 */
class server {
    router                        _router;
    std::vector<detail::ws_route> _ws_routes;
    server_config                 _cfg;

public:
    explicit server(server_config cfg = {}) : _cfg(cfg) {}

    // ── HTTP routes ────────────────────────────────────────────────────
    template<http_handler H> server& get (std::string_view p, H&& h) { _router.get (p, std::forward<H>(h)); return *this; }
    template<http_handler H> server& post(std::string_view p, H&& h) { _router.post(p, std::forward<H>(h)); return *this; }
    template<http_handler H> server& put (std::string_view p, H&& h) { _router.put (p, std::forward<H>(h)); return *this; }
    template<http_handler H> server& del (std::string_view p, H&& h) { _router.del (p, std::forward<H>(h)); return *this; }

    // ── WebSocket routes ───────────────────────────────────────────────
    template<ws_handler H>
    server& ws(std::string_view p, H&& h) {
        _ws_routes.push_back({ std::string(p), std::forward<H>(h) });
        return *this;
    }

    /**
     * @brief Accept loop — schedule this with ace::schedule() and call ace::run().
     */
    [[nodiscard]] ace::async<> listen() {
        auto sock  = co_await ace::futures::io_socket_tcp{};
        auto bound = co_await sock.bind(_cfg.addr, _cfg.port);
        auto lstnr = co_await bound.listen(_cfg.backlog);

        while (true) {
            auto conn = co_await lstnr.accept(INADDR_ANY, 0);
            if (!conn) continue;  // accept failed (e.g. EMFILE), skip

            // Spawn connection handler as a sibling task — fire and forget
            co_await ace::commands::spawn(
                detail::handle_connection(std::move(conn), _router, _ws_routes)
            );
        }
    }
};

} // namespace ace::http

#endif // ACE_HTTP_SERVER_H
