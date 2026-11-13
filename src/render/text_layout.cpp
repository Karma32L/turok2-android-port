#include "text_layout.hpp"

#include <localization.hpp>

#include <algorithm>

namespace render {

TextLayout layoutText(const Font& font, std::string_view utf8, std::uint32_t texW, std::uint32_t texH,
                      float pixelScale) {
    TextLayout out;
    if (texW == 0 || texH == 0 || !(pixelScale > 0.0f)) return out;
    const float s = font.scale * pixelScale;

    // Altura de línea: el glifo más alto de la fuente
    std::uint16_t maxH = 0;
    for (const Glyph& g : font.glyphs) {
        if (g.present) maxH = std::max(maxH, g.h);
    }
    const float lineH = static_cast<float>(maxH) * s;
    const Glyph* fallback = font.glyphs['?'].present ? &font.glyphs['?'] : nullptr;

    float penX = 0.0f, penY = 0.0f;
    std::size_t i = 0, chars = 0;
    std::uint32_t cp = 0;
    while (i < utf8.size() && chars++ < kMaxLayoutChars) {
        const Glyph* g = nullptr;
        if (!loc::nextCodepoint(utf8, i, cp)) {
            cp = 0xFFFD;  // no reutilizar el codepoint anterior
            ++out.missing;
            g = fallback;
        } else if (cp == '\n') {
            out.width = std::max(out.width, penX);
            penX = 0.0f;
            penY += lineH;
            continue;
        } else if (cp <= 255 && font.glyphs[cp].present) {
            g = &font.glyphs[cp];
        } else {
            ++out.missing;
            g = fallback;
        }
        if (!g) continue;

        const float w = static_cast<float>(g->w) * s, h = static_cast<float>(g->h) * s;
        const float y0 = penY + static_cast<float>(g->yOffset) * s;
        if (cp != ' ') {
            out.quads.push_back(Quad{penX, y0, penX + w, y0 + h,
                                     static_cast<float>(g->x) / static_cast<float>(texW),
                                     static_cast<float>(g->y) / static_cast<float>(texH),
                                     static_cast<float>(g->x + g->w) / static_cast<float>(texW),
                                     static_cast<float>(g->y + g->h) / static_cast<float>(texH)});
        }
        penX += std::max(0.0f, (static_cast<float>(g->w) + font.padWidth) * s);
    }
    out.width = std::max(out.width, penX);
    out.height = penY + lineH;
    return out;
}

}  // namespace render
