#include "localization.hpp"

#include <cstdint>
#include <optional>

namespace loc {

const std::string* Table::get(std::string_view key) const {
    auto it = keyIndex_.find(std::string(key));
    return it == keyIndex_.end() ? nullptr : &keyed_[it->second].second;
}

const std::string* Table::at(std::size_t index) const {
    return index < indexed_.size() ? &indexed_[index] : nullptr;
}

bool isValidUtf8(std::string_view s) {
    std::size_t i = 0;
    while (i < s.size()) {
        const auto c = static_cast<unsigned char>(s[i]);
        std::size_t len;
        std::uint32_t cp;
        if (c < 0x80) {
            ++i;
            continue;
        } else if ((c & 0xE0) == 0xC0) {
            len = 2;
            cp = c & 0x1F;
        } else if ((c & 0xF0) == 0xE0) {
            len = 3;
            cp = c & 0x0F;
        } else if ((c & 0xF8) == 0xF0) {
            len = 4;
            cp = c & 0x07;
        } else {
            return false;  // byte de continuación suelto o 0xF8..0xFF
        }
        if (s.size() - i < len) return false;  // secuencia truncada
        for (std::size_t k = 1; k < len; ++k) {
            const auto cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (cc & 0x3F);
        }
        static constexpr std::uint32_t kMin[] = {0, 0, 0x80, 0x800, 0x10000};
        if (cp < kMin[len]) return false;                  // overlong
        if (cp >= 0xD800 && cp <= 0xDFFF) return false;    // surrogate
        if (cp > 0x10FFFF) return false;
        i += len;
    }
    return true;
}

namespace {

bool isSpace(char c) { return c == ' ' || c == '\t'; }

std::string_view trim(std::string_view s) {
    while (!s.empty() && isSpace(s.front())) s.remove_prefix(1);
    while (!s.empty() && isSpace(s.back())) s.remove_suffix(1);
    return s;
}

bool isKeyChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

// Parsea una cadena entre comillas al inicio de `s`. Solo puede ir seguida de espacios o de
// un comentario "//". Devuelve nullopt si está mal formada (sin cierre, basura detrás...).
std::optional<std::string> parseQuoted(std::string_view s) {
    if (s.empty() || s.front() != '"') return std::nullopt;
    std::string out;
    std::size_t i = 1;
    for (; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '"') break;
        if (c == '\\' && i + 1 < s.size()) {
            const char e = s[++i];
            switch (e) {
            case 'n': out.push_back('\n'); break;
            case 't': out.push_back('\t'); break;
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            default:  // escape desconocido: se conserva tal cual
                out.push_back('\\');
                out.push_back(e);
            }
            continue;
        }
        out.push_back(c);
    }
    if (i >= s.size()) return std::nullopt;  // sin comilla de cierre
    std::string_view rest = trim(s.substr(i + 1));
    if (!rest.empty() && rest.substr(0, 2) != "//") return std::nullopt;
    return out;
}

}  // namespace

Table parse(std::string_view text) {
    Table t;
    if (text.substr(0, 3) == "\xEF\xBB\xBF") text.remove_prefix(3);  // BOM UTF-8

    while (!text.empty()) {
        const std::size_t nl = text.find('\n');
        std::string_view line = text.substr(0, nl);
        text = (nl == std::string_view::npos) ? std::string_view{} : text.substr(nl + 1);

        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        line = trim(line);
        if (line.empty() || line.substr(0, 2) == "//") continue;

        if (!isValidUtf8(line)) {
            ++t.malformed_;
            continue;
        }

        if (line.front() == '"') {
            if (auto v = parseQuoted(line)) {
                t.indexed_.push_back(std::move(*v));
            } else {
                ++t.malformed_;
            }
            continue;
        }

        std::size_t k = 0;
        while (k < line.size() && isKeyChar(line[k])) ++k;
        std::string_view key = line.substr(0, k);
        std::string_view rest = trim(line.substr(k));
        if (key.empty() || rest.empty() || rest.front() != '=') {
            ++t.malformed_;
            continue;
        }
        auto v = parseQuoted(trim(rest.substr(1)));
        if (!v) {
            ++t.malformed_;
            continue;
        }
        if (t.keyIndex_.count(std::string(key))) {
            ++t.duplicates_;
            continue;
        }
        t.keyIndex_.emplace(std::string(key), t.keyed_.size());
        t.keyed_.emplace_back(std::string(key), std::move(*v));
    }
    return t;
}

}  // namespace loc
