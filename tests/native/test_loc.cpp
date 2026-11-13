// Tests del parser de localización con texto sintético que imita la gramática del juego.
#include "test.hpp"

#include <localization.hpp>

TEST(loc_gramatica_mixta) {
    const std::string text =
        "\xEF\xBB\xBF\"ninguno\"\r\n"
        "\"faro activado.\\nmisión cumplida\"\r\n"
        "\r\n"
        "// Special pickups\r\n"
        "item_level_key           = \"llave de nivel\"\r\n"
        "item_power_cell=\"célula de energía\"  // comentario final\r\n"
        "  \t \r\n"
        "\"¡demo concluida!\"\r\n"
        "item_level_key = \"duplicada\"\n"
        "\"con \\\"comillas\\\" y \\\\ barra\"";
    const loc::Table t = loc::parse(text);
    CHECK(t.indexed().size() == 4);
    CHECK(t.at(0) && *t.at(0) == "ninguno");
    CHECK(t.at(1) && *t.at(1) == "faro activado.\nmisión cumplida");
    CHECK(t.at(2) && *t.at(2) == "¡demo concluida!");
    CHECK(t.at(3) && *t.at(3) == "con \"comillas\" y \\ barra");
    CHECK(!t.at(4));
    CHECK(t.keyed().size() == 2);
    CHECK(t.get("item_level_key") && *t.get("item_level_key") == "llave de nivel");
    CHECK(t.get("item_power_cell") && *t.get("item_power_cell") == "célula de energía");
    CHECK(!t.get("no_existe"));
    CHECK(t.duplicates() == 1);
    CHECK(t.malformed() == 0);
}

TEST(loc_lineas_mal_formadas) {
    const std::string text =
        "\"sin cierre\n"
        "clave sin igual \"x\"\n"
        "= \"sin clave\"\n"
        "clave = sin comillas\n"
        "\"basura\" detrás\n"
        "cla-ve = \"guion\"\n"
        "\"invalido \xC3\x28\"\n"   // UTF-8 inválido
        "\"válida\"\n";
    const loc::Table t = loc::parse(text);
    CHECK(t.malformed() == 7);
    CHECK(t.indexed().size() == 1);
    CHECK(t.at(0) && *t.at(0) == "válida");
}

TEST(loc_utf8_estricto) {
    CHECK(loc::isValidUtf8("ñ¡€😀"));
    CHECK(!loc::isValidUtf8("\xC0\x80"));          // overlong de NUL
    CHECK(!loc::isValidUtf8("\xE0\x80\xAF"));      // overlong de '/'
    CHECK(!loc::isValidUtf8("\xED\xA0\x80"));      // surrogate
    CHECK(!loc::isValidUtf8("\xF4\x90\x80\x80"));  // > U+10FFFF
    CHECK(!loc::isValidUtf8("\xE2\x82"));          // truncada
    CHECK(!loc::isValidUtf8("\x80"));              // continuación suelta
    CHECK(!loc::isValidUtf8("\xFF"));
}

TEST(loc_vacio) {
    const loc::Table t = loc::parse("");
    CHECK(t.indexed().empty() && t.keyed().empty() && t.malformed() == 0);
}
