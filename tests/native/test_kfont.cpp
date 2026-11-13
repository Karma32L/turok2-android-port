// Tests del parser .kfont con fuentes sintéticas (ninguna fuente del juego).
#include "test.hpp"

#include <kfont.hpp>
#include <localization.hpp>

#include <string>

using render::parseKfont;

namespace {

bool failsWith(std::string_view text, const char* needle) {
    std::string err;
    if (parseKfont(text, &err)) return false;
    if (err.find(needle) == std::string::npos) {
        std::printf("  error inesperado: '%s' (se esperaba '%s')\n", err.c_str(), needle);
        return false;
    }
    return true;
}

}  // namespace

TEST(kfont_gramatica_completa_crlf_y_comentarios) {
    const char* text =
        "filter nearest\r\n"
        "texture \"Fonts/Mi Fuente.png\"   // comentario\r\n"
        "nosdf\r\n"
        "scale 0.25\r\n"
        "padwidth -8.0\r\n"
        "\r\n"
        "mapchar\r\n"
        "{\r\n"
        "    65 0 0 16 8 0     // A\r\n"
        "    241 20 10 16 8 -2 // ñ en Latin-1\r\n"
        "}\r\n";
    const auto f = parseKfont(text);
    CHECK(f.has_value());
    if (!f) return;
    CHECK(f->texture == "fonts/mi fuente.png");  // normalizada por el VFS
    CHECK(!f->linearFilter);
    CHECK(!f->sdf);
    CHECK(f->scale == 0.25f);
    CHECK(f->padWidth == -8.0f);
    CHECK(f->glyphCount == 2);
    CHECK(f->malformed == 0);
    CHECK(f->glyphs[65].present && f->glyphs[65].w == 16 && f->glyphs[65].h == 8);
    const auto& n = f->glyphs[241];
    CHECK(n.present && n.x == 20 && n.y == 10 && n.yOffset == -2);
}

TEST(kfont_llave_en_la_misma_linea_y_valores_por_defecto) {
    const auto f = parseKfont("texture \"a.png\"\nmapchar {\n48 0 0 64 64 0\n}");
    CHECK(f.has_value());
    if (!f) return;
    CHECK(f->linearFilter && f->sdf && f->scale == 1.0f && f->padWidth == 0.0f);
    CHECK(f->glyphs['0'].present);
}

TEST(kfont_lineas_mal_formadas_se_cuentan) {
    const auto f = parseKfont(
        "texture \"a.png\"\n"
        "scale abc\n"           // número inválido
        "scale 0\n"             // fuera de rango
        "padwidth 1e9\n"        // exponentes no admitidos
        "desconocida 1\n"       // directiva desconocida
        "filter bicubic\n"      // valor desconocido
        "mapchar\n{\n"
        "65 0 0 8 8 0\n"
        "65 9 9 8 8 0\n"        // duplicado: gana el primero
        "256 0 0 8 8 0\n"       // código fuera de Latin-1
        "-1 0 0 8 8 0\n"        // negativo
        "66 0 0 8 8\n"          // faltan columnas
        "67 0 0 8 8 0 9\n"      // sobran columnas
        "68 0 0 0 8 0\n"        // ancho cero
        "69 70000 0 8 8 0\n"    // desborda uint16
        "70 0x10 0 8 8 0\n"     // no decimal
        "}\n");
    CHECK(f.has_value());
    if (!f) return;
    CHECK(f->glyphCount == 1);
    CHECK(f->duplicates == 1);
    CHECK(f->glyphs[65].x == 0);
    CHECK(f->malformed == 5 + 7);  // 5 directivas + 7 glifos (el duplicado va aparte)
    CHECK(f->scale == 1.0f);
}

TEST(kfont_errores_estructurales) {
    CHECK(failsWith("mapchar\n{\n65 0 0 8 8 0\n}\n", "texture"));
    CHECK(failsWith("texture \"a.png\"\n", "no define glifos"));
    CHECK(failsWith("texture \"a.png\"\nmapchar\n{\n}\n", "no define glifos"));
    CHECK(failsWith("texture \"a.png\"\nmapchar\n{\n65 0 0 8 8 0\n", "sin cerrar"));
    CHECK(failsWith("texture \"a.png\"\nmapchar\n", "sin cerrar"));
    CHECK(failsWith("texture \"a.png\"\nmapchar\n65 0 0 8 8 0\n", "'{'"));
    CHECK(failsWith("texture \"a.png\"\nmapchar { 65 0 0 8 8 0 }\n", "mapchar inválida"));
    CHECK(failsWith(std::string(render::kMaxKfontBytes + 1, ' '), "demasiado grande"));
}

TEST(kfont_rutas_de_textura_inseguras) {
    for (const char* tex : {"\"../../etc/passwd\"", "\"/abs.png\"", "\"C:/x.png\"", "\"a\\\\..\\\\b.png\"",
                            "\"\"", "sin_comillas.png", "\"a\"b.png\"", "\"a.png\" extra"}) {
        const std::string text = std::string("texture ") + tex + "\nmapchar\n{\n65 0 0 8 8 0\n}\n";
        CHECK(failsWith(text, "textura"));
    }
}

TEST(kfont_descarta_glifos_fuera_del_atlas) {
    auto f = parseKfont("texture \"a.png\"\nmapchar\n{\n65 0 0 8 8 0\n66 60 0 8 8 0\n67 0 60 8 8 0\n68 56 56 8 8 0\n}\n");
    CHECK(f.has_value());
    if (!f) return;
    CHECK(render::dropGlyphsOutside(*f, 64, 64) == 2);
    CHECK(f->glyphCount == 2);
    CHECK(f->glyphs['A'].present && f->glyphs['D'].present);
    CHECK(!f->glyphs['B'].present && !f->glyphs['C'].present);
}

TEST(kfont_entrada_arbitraria_no_rompe) {
    // Recorre prefijos de una fuente válida: nunca debe haber accesos fuera de rango
    const std::string text = "filter linear\r\ntexture \"f.png\"\r\nscale 0.5\r\nmapchar\r\n{\r\n65 1 2 3 4 0 // A\r\n}\r\n";
    for (std::size_t n = 0; n <= text.size(); ++n) (void)parseKfont(std::string_view(text).substr(0, n));
    std::string bytes;
    for (int c = 0; c < 256; ++c) bytes.push_back(static_cast<char>(c));
    (void)parseKfont(bytes);
    CHECK(true);
}

TEST(loc_next_codepoint_resincroniza) {
    const std::string s = "a\xC3\xB1\xFF\xE2\x82\xAC\xC0\xAF\xED\xA0\x80z";  // a ñ <inválido> € overlong surrogate z
    std::size_t i = 0;
    std::uint32_t cp = 0;
    CHECK(loc::nextCodepoint(s, i, cp) && cp == 'a');
    CHECK(loc::nextCodepoint(s, i, cp) && cp == 0xF1);
    CHECK(!loc::nextCodepoint(s, i, cp) && i == 4);
    CHECK(loc::nextCodepoint(s, i, cp) && cp == 0x20AC);
    std::size_t fails = 0;
    while (i < s.size()) {
        if (!loc::nextCodepoint(s, i, cp)) ++fails;
    }
    CHECK(cp == 'z');
    CHECK(fails == 2 + 3);  // overlong: 2 bytes; surrogate: 3 bytes (cada uno falla por separado)
}
