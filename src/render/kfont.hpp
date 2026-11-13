// Parser de fuentes bitmap .kfont (texto) de game.kpf.
//
// Gramática observada (ver docs/FORMATS.md):
//   filter linear|nearest       filtrado de la textura (opcional)
//   texture "fonts/x.png"       atlas en el VFS (obligatorio)
//   scale 0.5                   escala de dibujo (opcional, por defecto 1)
//   padwidth -8.0               ajuste del avance horizontal en px del atlas (opcional)
//   nosdf                       marca de atlas sin campo de distancia (opcional)
//   mapchar { ... }             un glifo por línea: código x y ancho alto desplazamiento_y
// Comentarios con //, fin de línea CRLF o LF, '{' en la misma línea que mapchar o en la
// siguiente. Los códigos son Latin-1 (0..255).
// Una línea de glifo mal formada se cuenta y se omite; los errores estructurales (sin
// textura, bloque sin cerrar, ruta insegura, límites superados) invalidan la fuente.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace render {

struct Glyph {
    bool present = false;
    std::uint16_t x = 0, y = 0, w = 0, h = 0;  // rectángulo en el atlas (px)
    std::int16_t yOffset = 0;
};

struct Font {
    std::string texture;  // ruta normalizada del VFS
    bool linearFilter = true;
    bool sdf = true;      // false si el archivo declara nosdf
    float scale = 1.0f;
    float padWidth = 0.0f;
    std::array<Glyph, 256> glyphs{};
    std::size_t glyphCount = 0;
    std::size_t malformed = 0;   // líneas o directivas ignoradas
    std::size_t duplicates = 0;  // códigos repetidos: gana el primero
};

constexpr std::size_t kMaxKfontBytes = 1u << 20;

std::optional<Font> parseKfont(std::string_view text, std::string* err = nullptr);

// Descarta los glifos cuyo rectángulo no cabe en un atlas de w x h. Devuelve cuántos.
std::size_t dropGlyphsOutside(Font& font, std::uint32_t texW, std::uint32_t texH);

}  // namespace render
