#ifndef ACE_HTTP_REQUEST_H
#define ACE_HTTP_REQUEST_H

#include <cctype>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>

namespace ace::http {

struct request {
    std::string method;
    std::string path;
    std::string query;
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;
    // Filled by router after path matching ("/users/:id" → {id: "42"})
    std::unordered_map<std::string, std::string> params;

    // Case-insensitive header lookup (headers are stored lowercase)
    [[nodiscard]] std::string_view header(std::string_view name) const noexcept {
        for (auto& [k, v] : headers)
            if (k == name) return v;
        return {};
    }

    [[nodiscard]] bool is_ws_upgrade() const noexcept {
        // upgrade value is NOT lowercased by the parser — do case-insensitive compare
        auto upg = header("upgrade");
        if (upg.size() != 9) return false;
        for (std::size_t i = 0; i < 9; ++i)
            if (std::tolower(static_cast<unsigned char>(upg[i])) != "websocket"[i]) return false;
        return !header("sec-websocket-key").empty();
    }
};

} // namespace ace::http

#endif // ACE_HTTP_REQUEST_H
