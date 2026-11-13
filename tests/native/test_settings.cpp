// Tests del parseo y la persistencia de ajustes.
#include "test.hpp"

#include <settings.hpp>

#include <cstdio>
#include <string>
#include <unistd.h>

using ui::Language;
using ui::Settings;

TEST(settings_ida_y_vuelta) {
    Settings s;
    s.musicVolume = 3;
    s.sfxVolume = 0;
    s.language = Language::German;
    std::size_t bad = 99;
    CHECK(ui::parseSettings(ui::serializeSettings(s), &bad) == s);
    CHECK(bad == 0);
}

TEST(settings_basura_y_fuera_de_rango) {
    std::size_t bad = 0;
    const Settings s = ui::parseSettings(
        "music_volume=11\n"       // fuera de rango
        "sfx_volume=-1\n"         // signo
        "sfx_volume=4x\n"         // basura
        "music_volume=007\n"      // demasiado largo
        "language=klingon\n"      // idioma desconocido
        "desconocida=1\n"         // clave desconocida
        "sin igual\n"
        "\xff\xfe=\x01\n"         // bytes no ASCII
        "# comentario\n"
        "\n"
        "  sfx_volume = 6 \r\n"   // espacios y CRLF: válido
        "language=italian",       // sin salto final: válido
        &bad);
    CHECK(bad == 8);
    CHECK(s.musicVolume == Settings{}.musicVolume);
    CHECK(s.sfxVolume == 6);
    CHECK(s.language == Language::Italian);
}

TEST(settings_sobredimensionado) {
    std::string big(ui::kMaxSettingsBytes + 1, '#');
    big = "sfx_volume=1\n" + big;
    std::size_t bad = 0;
    CHECK(ui::parseSettings(big, &bad) == Settings{});
    CHECK(bad == 1);
    CHECK(ui::parseSettings("") == Settings{});
}

TEST(settings_ciclo_de_idiomas) {
    CHECK(ui::cycleLanguage(Language::Spanish, +1) == Language::English);
    CHECK(ui::cycleLanguage(Language::Spanish, -1) == Language::Italian);
    CHECK(ui::cycleLanguage(Language::Italian, +1) == Language::Spanish);
    CHECK(ui::languageFromCode("french") == Language::French);
    CHECK(!ui::languageFromCode("French"));
    CHECK(!ui::languageFromCode(""));
    CHECK(std::string(ui::languageInfo(Language::English).locKey) == "language_english");
}

TEST(settings_archivo) {
    char dir[] = "/tmp/turok2_settings_XXXXXX";
    CHECK(mkdtemp(dir) != nullptr);
    const std::string path = std::string(dir) + "/settings.cfg";
    CHECK(ui::loadSettingsFile(path) == Settings{});  // no existe: valores por defecto

    Settings s;
    s.musicVolume = 1;
    s.language = Language::French;
    std::string err;
    CHECK(ui::saveSettingsFile(path, s, &err));
    CHECK(ui::loadSettingsFile(path) == s);
    CHECK(std::fopen((path + ".tmp").c_str(), "rb") == nullptr);  // el temporal no queda

    // Archivo sobredimensionado: se ignora entero
    if (std::FILE* f = std::fopen(path.c_str(), "wb")) {
        const std::string big(ui::kMaxSettingsBytes + 10, 'a');
        std::fwrite(big.data(), 1, big.size(), f);
        std::fclose(f);
    }
    CHECK(ui::loadSettingsFile(path) == Settings{});

    // Directorio inexistente: error explícito, sin crash
    CHECK(!ui::saveSettingsFile(std::string(dir) + "/no/existe.cfg", s, &err));
    CHECK(!err.empty());

    std::remove(path.c_str());
    rmdir(dir);
}
