// Tests del layout de texto con una fuente sintética.
#include "test.hpp"

#include <text_layout.hpp>

#include <string>

using render::layoutText;

namespace {

// Atlas de 100x50; glifos 10x20 salvo la 'ñ' (Latin-1 241) con desplazamiento vertical
render::Font testFont(bool withFallback = true) {
    std::string text = "texture \"t.png\"\nmapchar\n{\n";
    text += "32 0 0 10 20 0\n";  // espacio
    text += "65 10 0 10 20 0\n";  // A
    text += "241 20 0 10 20 4\n";  // ñ
    text += "191 30 0 10 20 0\n";  // ¿
    if (withFallback) text += "63 40 0 10 20 0\n";  // ?
    text += "}\n";
    auto f = render::parseKfont(text);
    return f ? *f : render::Font{};
}

}  // namespace

TEST(layout_espanol_latin1) {
    const render::Font f = testFont();
    const auto l = layoutText(f, "\xC2\xBF" "A\xC3\xB1", 100, 50);  // "¿Añ" en UTF-8
    CHECK(l.missing == 0);
    CHECK(l.quads.size() == 3);
    if (l.quads.size() != 3) return;
    CHECK(l.quads[0].u0 == 0.30f);  // ¿ en x=30
    CHECK(l.quads[1].x0 == 10.0f && l.quads[1].u0 == 0.10f);
    const auto& n = l.quads[2];
    CHECK(n.x0 == 20.0f && n.y0 == 4.0f && n.y1 == 24.0f);  // desplazamiento y = 4
    CHECK(n.u0 == 0.20f && n.v0 == 0.0f && n.u1 == 0.30f && n.v1 == 0.40f);
    CHECK(l.width == 30.0f && l.height == 20.0f);
}

TEST(layout_escala_padwidth_y_saltos_de_linea) {
    render::Font f = testFont();
    f.scale = 0.5f;
    f.padWidth = -4.0f;
    const auto l = layoutText(f, "A A\nA", 100, 50, 2.0f);  // escala efectiva 1
    CHECK(l.quads.size() == 3);  // el espacio avanza pero no genera quad
    if (l.quads.size() != 3) return;
    CHECK(l.quads[1].x0 == 12.0f);  // avance (10 - 4) * 1 por carácter
    CHECK(l.quads[2].x0 == 0.0f && l.quads[2].y0 == 20.0f);
    CHECK(l.width == 18.0f && l.height == 40.0f);
}

TEST(layout_caracteres_ausentes_e_invalidos) {
    const render::Font f = testFont();
    // 'z' sin glifo, U+20AC > 255, byte inválido 0xFF: los tres usan '?'
    const auto l = layoutText(f, "z\xE2\x82\xAC\xFF", 100, 50);
    CHECK(l.missing == 3);
    CHECK(l.quads.size() == 3);
    for (const auto& q : l.quads) CHECK(q.u0 == 0.40f);

    const render::Font noFallback = testFont(false);
    const auto l2 = layoutText(noFallback, "zA", 100, 50);
    CHECK(l2.missing == 1 && l2.quads.size() == 1);
}

TEST(layout_parametros_degenerados) {
    const render::Font f = testFont();
    CHECK(layoutText(f, "A", 0, 50).quads.empty());
    CHECK(layoutText(f, "A", 100, 0).quads.empty());
    CHECK(layoutText(f, "A", 100, 50, 0.0f).quads.empty());
    CHECK(layoutText(f, "A", 100, 50, -1.0f).quads.empty());
    CHECK(layoutText(render::Font{}, "AAA", 100, 50).quads.empty());
    const std::string huge(render::kMaxLayoutChars + 100, 'A');
    CHECK(layoutText(f, huge, 100, 50).quads.size() == render::kMaxLayoutChars);
}
