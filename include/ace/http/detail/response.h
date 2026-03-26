#ifndef ACE_HTTP_RESPONSE_H
#define ACE_HTTP_RESPONSE_H

#include <string>
#include <string_view>
#include <vector>
#include <utility>

namespace ace::http {

struct response {
    int status = 200;
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;

    static response json(std::string b, int s = 200) {
        response r;
        r.status = s;
        r.body   = std::move(b);
        r.headers.emplace_back("Content-Type", "application/json");
        return r;
    }

    static response text(std::string b, int s = 200) {
        response r;
        r.status = s;
        r.body   = std::move(b);
        r.headers.emplace_back("Content-Type", "text/plain; charset=utf-8");
        return r;
    }

    static response empty(int s) {
        response r;
        r.status = s;
        return r;
    }

    response& set(std::string name, std::string value) {
        headers.emplace_back(std::move(name), std::move(value));
        return *this;
    }

    [[nodiscard]] std::string_view status_text() const noexcept {
        switch (status) {
            case 200: return "OK";
            case 201: return "Created";
            case 204: return "No Content";
            case 101: return "Switching Protocols";
            case 400: return "Bad Request";
            case 401: return "Unauthorized";
            case 403: return "Forbidden";
            case 404: return "Not Found";
            case 405: return "Method Not Allowed";
            case 409: return "Conflict";
            case 500: return "Internal Server Error";
            default:  return "Unknown";
        }
    }

    [[nodiscard]] std::string serialize() const {
        std::string s;
        s.reserve(256 + body.size());

        s += "HTTP/1.1 ";
        s += std::to_string(status);
        s += ' ';
        s += status_text();
        s += "\r\n";

        bool has_content_length = false;
        for (auto& [k, v] : headers) {
            s += k; s += ": "; s += v; s += "\r\n";
            if (k == "Content-Length") has_content_length = true;
        }

        if (!has_content_length) {
            s += "Content-Length: ";
            s += std::to_string(body.size());
            s += "\r\n";
        }

        s += "\r\n";
        s += body;
        return s;
    }
};

} // namespace ace::http

#endif // ACE_HTTP_RESPONSE_H
