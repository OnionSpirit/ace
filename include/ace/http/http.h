#ifndef ACE_HTTP_H
#define ACE_HTTP_H

/**
 * @file ace/http/http.h
 * @brief HTTP/1.1 + WebSocket server built on the ace async runtime.
 *
 * Single include for the full HTTP layer:
 *   #include "ace/http/http.h"
 *
 * Quick start:
 * @code
 *   #include "ace/ace.h"
 *   #include "ace/http/http.h"
 *
 *   ace::http::server s{{ .port = 8080 }};
 *
 *   s.get("/ping", [](ace::http::request) -> ace::async<ace::http::response> {
 *       co_return ace::http::response::text("pong");
 *   });
 *
 *   s.ws("/chat/:room", [](ace::http::ws::connection conn, ace::http::request req) -> ace::async<> {
 *       while (true) {
 *           auto msg = co_await conn.recv();
 *           if (msg.is_close()) break;
 *           co_await conn.send(msg.payload);
 *       }
 *   });
 *
 *   ace::schedule(s.listen());
 *   ace::run();
 * @endcode
 */

#include "server.h"

// Re-export sub-namespaces for convenience
// ace::http::request, ace::http::response — from detail/
// ace::http::ws::connection, ws::message — from ws/
// ace::http::server, server_config       — from server.h

#endif // ACE_HTTP_H
