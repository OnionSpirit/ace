#include "ace/ace.h"
#include "ace/http/http.h"
#include <iostream>

// GET /ping → "pong"
struct ping_handler {
    ace::async<ace::http::response> operator()(ace::http::request) {
        co_return ace::http::response::text("pong\n");
    }
};

// POST /echo → возвращает тело запроса
struct echo_handler {
    ace::async<ace::http::response> operator()(ace::http::request req) {
        co_return ace::http::response::text(req.body);
    }
};

// GET /hello/:name → {"hello":"<name>"}
struct greet_handler {
    ace::async<ace::http::response> operator()(ace::http::request req) {
        co_return ace::http::response::json(R"({"hello":")" + req.params["name"] + R"("})");
    }
};

// WS /ws → echo каждого сообщения обратно
struct ws_echo {
    ace::async<> operator()(ace::http::ws::connection conn, ace::http::request) {
        while (true) {
            auto msg = co_await conn.recv();
            if (msg.is_close()) break;
            if (msg.is_ping()) { co_await conn.ping(); continue; }
            co_await conn.send(msg.payload);
        }
    }
};

ace::async<> run() {
    ace::http::server s{{ .port = 8080 }};

    s.get("/ping",        ping_handler{})
     .post("/echo",       echo_handler{})
     .get("/hello/:name", greet_handler{})
     .ws("/ws",           ws_echo{});

    std::cout << "Listening on http://localhost:8080\n";
    co_await s.listen();
}

int main() {
    ace::schedule(run());
    ace::run();
}
