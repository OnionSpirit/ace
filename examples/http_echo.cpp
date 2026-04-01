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

struct lifetime_watchdog {

    std::string _name;

    explicit lifetime_watchdog(const std::string_view name) : _name(name) {
        std::cout << _name << " constructed" << std::endl;
    };

    ~lifetime_watchdog() { std::cout << _name << " destroyed" << std::endl; }
};

ace::async<> commandor(ace::futures::async_handle loop) {
    const auto _check = std::make_unique<lifetime_watchdog>("<commandor>");
    static constexpr int READ_BUFF_LEN = 128;
    char buff[READ_BUFF_LEN] = {};
    while (true) {
        if (co_await ace::core::read_query(STDIN_FILENO, buff, READ_BUFF_LEN) < 0) {
            std::cout << _check->_name << " : failed to collect input" << std::endl;
            loop.cancel();
            co_return;
        }
        if (std::string_view{buff} == ":q\n") {
            std::cout << _check->_name << " : exit command received...\n";
            loop.cancel();
            co_return;
        }
        bzero(buff, READ_BUFF_LEN);
    }
}


ace::async<> co_main() {
    const auto _check = std::make_unique<lifetime_watchdog>("<co_main>");

    const ace::http::server_config config {
        .addr = inet_network("127.0.0.1"),
        .port = 8080
    };

    ace::http::server serv {config};

    serv.get("/ping",        ping_handler{})
        .post("/echo",       echo_handler{})
        .get("/hello/:name", greet_handler{})
        .ws("/ws",           ws_echo{});

    std::cout << _check->_name << " : start listening on http://" << inet_ntoa(*(in_addr*)&config.addr) << ":" << config.port << '\n';
    const auto loop = co_await ace::spawn(serv.listen());
    ace::schedule(commandor(loop));
}

int main() {
    ace::schedule(co_main());
    ace::run();
}
