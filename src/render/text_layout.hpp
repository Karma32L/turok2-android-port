// Layout de texto UTF-8 con una fuente .kfont: genera un quad por glifo (CPU, sin GL).
//
// Coordenadas en píxeles con origen arriba-izquierda (y hacia abajo); UV normalizadas al
// atlas. Avance horizontal = (ancho del glifo + padwidth) * scale * pixelScale.
// Codepoints > 255 o sin glifo se sustituyen por '?' (si existe) y se cuentan; el UTF-8
// inválido se cuenta y cada byte erróneo cuenta como un carácter ausente.
#pragma once

#include "kfont.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace render {

struct Quad {
    float x0, y0, x1, y1;  // píxeles
    float u0, v0, u1, v1;  // UV del atlas
};

struct TextLayout {
    std::vector<Quad> quads;
    float width = 0.0f;   // ancho de la línea más larga
    float height = 0.0f;  // líneas * altura de línea
    std::size_t missing = 0;
};

constexpr std::size_t kMaxLayoutChars = 1u << 16;  // límite de cordura por llamada

TextLayout layoutText(const Font& font, std::string_view utf8, std::uint32_t texW, std::uint32_t texH,
                      float pixelScale = 1.0f);

}  // namespace render
