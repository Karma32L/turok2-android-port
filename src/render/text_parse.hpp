// Utilidades de tokenizado compartidas por los parsers de texto del juego (.kfont, .kmat).
// Sin locale ni asignaciones ocultas; todos los números se acotan.
#pragma once

#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace render::text {

inline bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r'; }

inline std::string_view trim(std::string_view s) {
    while (!s.empty() && isSpace(s.front())) s.remove_prefix(1);
    while (!s.empty() && isSpace(s.back())) s.remove_suffix(1);
    return s;
}

// Elimina un comentario // que no esté dentro de comillas
inline std::string_view stripComment(std::string_view s) {
    bool quoted = false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '"') quoted = !quoted;
        else if (!quoted && s[i] == '/' && i + 1 < s.size() && s[i + 1] == '/') return s.substr(0, i);
    }
    return s;
}

// Separa por espacios (las comillas no agrupan: usar splitQuoted si hacen falta)
inline std::vector<std::string_view> split(std::string_view s) {
    std::vector<std::string_view> out;
    std::size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && isSpace(s[i])) ++i;
        const std::size_t start = i;
        while (i < s.size() && !isSpace(s[i])) ++i;
        if (i > start) out.push_back(s.substr(start, i - start));
    }
    return out;
}

// Como split, pero "texto con espacios" es un solo token (comillas incluidas).
// nullopt si hay comillas sin cerrar.
inline std::optional<std::vector<std::string_view>> splitQuoted(std::string_view s) {
    std::vector<std::string_view> out;
    std::size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && isSpace(s[i])) ++i;
        if (i >= s.size()) break;
        const std::size_t start = i;
        if (s[i] == '"') {
            const std::size_t close = s.find('"', i + 1);
            if (close == std::string_view::npos) return std::nullopt;
            i = close + 1;
        } else {
            while (i < s.size() && !isSpace(s[i])) ++i;
        }
        out.push_back(s.substr(start, i - start));
    }
    return out;
}

template <class T>
bool parseInt(std::string_view s, T& out) {
    const char* end = s.data() + s.size();
    const auto r = std::from_chars(s.data(), end, out);
    return r.ec == std::errc() && r.ptr == end;
}

// Decimal simple sin locale ni exponentes: [-]dígitos[.dígitos]. Acotado para evitar
// valores absurdos; std::from_chars de float no está disponible en la libc++ del NDK 26.
inline std::optional<float> parseNumber(std::string_view s) {
    bool neg = false;
    if (!s.empty() && (s.front() == '-' || s.front() == '+')) {
        neg = s.front() == '-';
        s.remove_prefix(1);
    }
    std::uint32_t intPart = 0, frac = 0, fracDiv = 1;
    std::size_t intDigits = 0, fracDigits = 0, i = 0;
    for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i, ++intDigits) {
        if (intDigits >= 6) return std::nullopt;
        intPart = intPart * 10 + static_cast<std::uint32_t>(s[i] - '0');
    }
    if (i < s.size() && s[i] == '.') {
        for (++i; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i, ++fracDigits) {
            if (fracDigits >= 6) return std::nullopt;
            frac = frac * 10 + static_cast<std::uint32_t>(s[i] - '0');
            fracDiv *= 10;
        }
    }
    if (i != s.size() || intDigits + fracDigits == 0) return std::nullopt;
    const float v = static_cast<float>(intPart) + static_cast<float>(frac) / static_cast<float>(fracDiv);
    return neg ? -v : v;
}

// "texto" sin comillas internas ni escapes
inline std::optional<std::string_view> parseQuoted(std::string_view s) {
    if (s.size() < 2 || s.front() != '"' || s.back() != '"') return std::nullopt;
    s = s.substr(1, s.size() - 2);
    if (s.find('"') != std::string_view::npos) return std::nullopt;
    return s;
}

}  // namespace render::text
