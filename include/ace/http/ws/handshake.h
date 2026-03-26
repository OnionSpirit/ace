#ifndef ACE_HTTP_WS_HANDSHAKE_H
#define ACE_HTTP_WS_HANDSHAKE_H

#include "sha1.h"
#include "ace/http/detail/request.h"
#include "ace/futures/network.h"
#include "ace/coroutines/context.h"

namespace ace::http::ws {

static constexpr std::string_view WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

/**
 * @brief Compute the value of the Sec-WebSocket-Accept response header.
 * SHA1(key + GUID) → base64  (RFC 6455 §4.2.2)
 */
inline std::string compute_accept_key(std::string_view client_key) {
    std::string combined(client_key);
    combined += WS_GUID;
    auto digest = detail::sha1(combined);
    return detail::base64_encode(digest.data(), digest.size());
}

/**
 * @brief Send the HTTP 101 Switching Protocols response.
 * @return true on success, false if send failed.
 */
inline ace::async<bool> handshake(
    ace::futures::io_connection_entity& conn,
    const ace::http::request&           req
) {
    auto key    = req.header("sec-websocket-key");
    auto accept = compute_accept_key(key);

    std::string resp;
    resp.reserve(256);
    resp += "HTTP/1.1 101 Switching Protocols\r\n";
    resp += "Upgrade: websocket\r\n";
    resp += "Connection: Upgrade\r\n";
    resp += "Sec-WebSocket-Accept: ";
    resp += accept;
    resp += "\r\n\r\n";

    int n = co_await conn.send(resp.data(), resp.size());
    co_return n > 0;
}

} // namespace ace::http::ws

#endif // ACE_HTTP_WS_HANDSHAKE_H
