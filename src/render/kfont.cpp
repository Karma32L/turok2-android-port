#include "kfont.hpp"

#include <vfs.hpp>

#include <charconv>
#include <vector>

namespace render {
namespace {

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r'; }

std::string_view trim(std::string_view s) {
    while (!s.empty() && isSpace(s.front())) s.remove_prefix(1);
    while (!s.empty() && isSpace(s.back())) s.remove_suffix(1);
    return s;
}

// Elimina un comentario // que no esté dentro de comillas
std::string_view stripComment(std::string_view s) {
    bool quoted = false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '"') quoted = !quoted;
        else if (!quoted && s[i] == '/' && i + 1 < s.size() && s[i + 1] == '/') return s.substr(0, i);
    }
    return s;
}

std::vector<std::string_view> split(std::string_view s) {
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

template <class T>
bool parseInt(std::string_view s, T& out) {
    const char* end = s.data() + s.size();
    const auto r = std::from_chars(s.data(), end, out);
    return r.ec == std::errc() && r.ptr == end;
}

// Decimal simple sin locale ni exponentes: [-]dígitos[.dígitos]. Acotado para evitar
// valores absurdos; std::from_chars de float no está disponible en la libc++ del NDK 26.
std::optional<float> parseNumber(std::string_view s) {
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

// "ruta" sin comillas internas ni escapes
std::optional<std::string_view> parseQuoted(std::string_view s) {
    if (s.size() < 2 || s.front() != '"' || s.back() != '"') return std::nullopt;
    s = s.substr(1, s.size() - 2);
    if (s.find('"') != std::string_view::npos) return std::nullopt;
    return s;
}

bool parseGlyphLine(std::string_view line, Font& font) {
    const auto tok = split(line);
    if (tok.size() != 6) return false;
    std::uint16_t code = 0, x = 0, y = 0, w = 0, h = 0;
    std::int16_t yOff = 0;
    if (!parseInt(tok[0], code) || !parseInt(tok[1], x) || !parseInt(tok[2], y) || !parseInt(tok[3], w) ||
        !parseInt(tok[4], h) || !parseInt(tok[5], yOff)) {
        return false;
    }
    if (code > 255 || w == 0 || h == 0) return false;
    Glyph& g = font.glyphs[code];
    if (g.present) {
        ++font.duplicates;
        return true;
    }
    g = Glyph{true, x, y, w, h, yOff};
    ++font.glyphCount;
    return true;
}

bool fail(std::string* err, const char* msg) {
    if (err) *err = msg;
    return false;
}

}  // namespace

std::optional<Font> parseKfont(std::string_view text, std::string* err) {
    if (text.size() > kMaxKfontBytes) {
        fail(err, ".kfont demasiado grande");
        return std::nullopt;
    }
    enum class State { Top, ExpectBrace, InMap } state = State::Top;
    Font font;
    bool sawMap = false;

    std::size_t pos = 0;
    while (pos <= text.size()) {
        std::size_t nl = text.find('\n', pos);
        if (nl == std::string_view::npos) nl = text.size();
        const std::string_view line = trim(stripComment(text.substr(pos, nl - pos)));
        pos = nl + 1;
        if (line.empty()) continue;

        if (state == State::ExpectBrace) {
            if (line != "{") {
                fail(err, "se esperaba '{' tras mapchar");
                return std::nullopt;
            }
            state = State::InMap;
            continue;
        }
        if (state == State::InMap) {
            if (line == "}") state = State::Top;
            else if (!parseGlyphLine(line, font)) ++font.malformed;
            continue;
        }

        const auto tok = split(line);
        const std::string_view word = tok[0];
        if (word == "mapchar") {
            if (tok.size() == 1) state = State::ExpectBrace;
            else if (tok.size() == 2 && tok[1] == "{") state = State::InMap;
            else {
                fail(err, "línea mapchar inválida");
                return std::nullopt;
            }
            sawMap = true;
        } else if (word == "texture") {
            // El argumento es el resto de la línea: la ruta puede contener espacios
            const auto q = parseQuoted(trim(line.substr(word.size())));
            auto norm = q ? vfs::normalizePath(*q) : std::nullopt;
            if (!norm) {
                fail(err, "ruta de textura inválida o insegura");
                return std::nullopt;
            }
            font.texture = std::move(*norm);
        } else if (word == "filter" && tok.size() == 2 && (tok[1] == "linear" || tok[1] == "nearest")) {
            font.linearFilter = tok[1] == "linear";
        } else if (word == "nosdf" && tok.size() == 1) {
            font.sdf = false;
        } else if ((word == "scale" || word == "padwidth") && tok.size() == 2) {
            const auto v = parseNumber(tok[1]);
            const bool ok = v && (word == "scale" ? (*v > 0.0f && *v <= 64.0f) : (*v >= -1024.0f && *v <= 1024.0f));
            if (!ok) ++font.malformed;
            else if (word == "scale") font.scale = *v;
            else font.padWidth = *v;
        } else {
            ++font.malformed;  // directiva desconocida o con argumentos incorrectos
        }
    }

    if (state != State::Top) {
        fail(err, "bloque mapchar sin cerrar");
        return std::nullopt;
    }
    if (font.texture.empty()) {
        fail(err, "falta la directiva texture");
        return std::nullopt;
    }
    if (!sawMap || font.glyphCount == 0) {
        fail(err, "la fuente no define glifos");
        return std::nullopt;
    }
    return font;
}

std::size_t dropGlyphsOutside(Font& font, std::uint32_t texW, std::uint32_t texH) {
    std::size_t dropped = 0;
    for (Glyph& g : font.glyphs) {
        if (!g.present) continue;
        if (std::uint32_t{g.x} + g.w > texW || std::uint32_t{g.y} + g.h > texH) {
            g = Glyph{};
            --font.glyphCount;
            ++dropped;
        }
    }
    return dropped;
}

}  // namespace render
