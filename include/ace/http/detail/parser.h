#ifndef ACE_HTTP_PARSER_H
#define ACE_HTTP_PARSER_H

#include "request.h"
#include <charconv>
#include <cctype>
#include <string>
#include <string_view>

namespace ace::http::detail {

/**
 * @brief Incremental HTTP/1.1 request parser.
 *
 * Feed it chunks via feed() until result::complete is true.
 * Call reset() before reusing for the next request (keep-alive).
 *
 * Limitations: no chunked transfer-encoding, no trailers, no HTTP/2.
 * Headers are lowercased on ingestion for case-insensitive lookup.
 */
class parser {
    enum class state { request_line, headers, body, done };

    state       _state           = state::request_line;
    std::string _buf;
    request     _req;
    std::size_t _content_length  = 0;

    bool parse_request_line(std::string_view line) noexcept {
        // METHOD<SP>PATH[?QUERY]<SP>HTTP/1.x
        auto sp1 = line.find(' ');
        if (sp1 == std::string_view::npos) return false;
        _req.method = std::string(line.substr(0, sp1));

        auto rest = line.substr(sp1 + 1);
        auto sp2  = rest.rfind(' ');
        if (sp2 == std::string_view::npos) return false;

        auto path_query = rest.substr(0, sp2);
        auto q = path_query.find('?');
        if (q != std::string_view::npos) {
            _req.path  = std::string(path_query.substr(0, q));
            _req.query = std::string(path_query.substr(q + 1));
        } else {
            _req.path = std::string(path_query);
        }
        return true;
    }

    bool parse_header_line(std::string_view line) noexcept {
        auto colon = line.find(':');
        if (colon == std::string_view::npos) return false;

        auto field = line.substr(0, colon);
        auto value = line.substr(colon + 1);
        while (!value.empty() && value.front() == ' ') value.remove_prefix(1);
        while (!value.empty() && value.back()  == ' ') value.remove_suffix(1);

        std::string k(field);
        // Lowercase field name for case-insensitive lookup
        for (auto& c : k) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        if (k == "content-length")
            std::from_chars(value.data(), value.data() + value.size(), _content_length);

        _req.headers.emplace_back(std::move(k), std::string(value));
        return true;
    }

public:
    struct result {
        bool complete = false;
        bool error    = false;
    };

    result feed(std::string_view chunk) {
        _buf.append(chunk);

        while (true) {
            if (_state == state::request_line || _state == state::headers) {
                auto pos = _buf.find("\r\n");
                if (pos == std::string::npos) return {};

                std::string_view line(_buf.data(), pos);

                if (_state == state::request_line) {
                    if (!parse_request_line(line)) return { .error = true };
                    _state = state::headers;
                } else {
                    if (line.empty()) {
                        // Blank line → end of headers
                        _buf.erase(0, pos + 2);
                        if (_content_length > 0) {
                            _state = state::body;
                            continue;
                        }
                        _state = state::done;
                        return { .complete = true };
                    } else {
                        if (!parse_header_line(line)) return { .error = true };
                    }
                }
                _buf.erase(0, pos + 2);

            } else if (_state == state::body) {
                if (_buf.size() >= _content_length) {
                    _req.body = _buf.substr(0, _content_length);
                    _buf.erase(0, _content_length);
                    _state = state::done;
                    return { .complete = true };
                }
                return {};
            } else {
                return { .complete = true };
            }
        }
    }

    [[nodiscard]] request& get() noexcept { return _req; }

    void reset() {
        _state          = state::request_line;
        _content_length = 0;
        _buf.clear();
        _req = {};
    }
};

} // namespace ace::http::detail

#endif // ACE_HTTP_PARSER_H
