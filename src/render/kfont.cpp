#include "kfont.hpp"

#include <vfs.hpp>

#include <vector>

#include "text_parse.hpp"

namespace render {
namespace {

using text::parseInt;
using text::parseNumber;
using text::parseQuoted;
using text::split;
using text::stripComment;
using text::trim;

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
