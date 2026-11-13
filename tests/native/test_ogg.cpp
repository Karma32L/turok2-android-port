// Tests de robustez de OggMusic (stb_vorbis) con entradas sintéticas malformadas.
//
// No hay un codificador Vorbis en el repo, así que la decodificación positiva solo se prueba
// si el usuario apunta TURK2_OGG_SAMPLE a un .ogg propio (nunca versionado), p. ej.:
//   TURK2_OGG_SAMPLE=$PWD/android/app/src/main/assets/game_data/music/n64/title.ogg ctest ...
#include "test.hpp"

#include <ogg_music.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

void put32(Bytes& b, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

// Página Ogg de un solo segmento (stb_vorbis no verifica el CRC en modo memoria)
Bytes oggPage(std::uint8_t flags, const Bytes& payload) {
    Bytes b{'O', 'g', 'g', 'S', 0, flags};
    for (int i = 0; i < 8; ++i) b.push_back(0);  // granule
    put32(b, 0x1234);                            // serial
    put32(b, 0);                                 // secuencia
    put32(b, 0);                                 // CRC
    b.push_back(1);
    b.push_back(static_cast<std::uint8_t>(payload.size()));
    b.insert(b.end(), payload.begin(), payload.end());
    return b;
}

// Cabecera de identificación Vorbis (30 bytes)
Bytes vorbisId(std::uint8_t channels, std::uint32_t rate, std::uint8_t blocksizes = 0xB8) {
    Bytes b{1, 'v', 'o', 'r', 'b', 'i', 's'};
    put32(b, 0);  // versión
    b.push_back(channels);
    put32(b, rate);
    put32(b, 0);
    put32(b, 128000);
    put32(b, 0);
    b.push_back(blocksizes);
    b.push_back(1);  // framing
    return b;
}

bool openFails(Bytes data, const char* needle = nullptr) {
    std::string err;
    auto m = audio::OggMusic::open(std::move(data), true, &err);
    if (m) return false;
    if (needle && err.find(needle) == std::string::npos) {
        std::printf("  error inesperado: '%s' (se esperaba '%s')\n", err.c_str(), needle);
        return false;
    }
    return true;
}

std::uint32_t lcg(std::uint32_t& s) { return s = s * 1664525u + 1013904223u; }

Bytes readFile(const char* path) {
    Bytes out;
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return out;
    std::uint8_t buf[1 << 16];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0 && out.size() < audio::kMaxOggBytes) {
        out.insert(out.end(), buf, buf + n);
    }
    std::fclose(f);
    return out;
}

}  // namespace

TEST(ogg_vacio_y_demasiado_grande) {
    CHECK(openFails({}, "vacío"));
    CHECK(openFails(Bytes(audio::kMaxOggBytes + 1, 0), "grande"));
}

TEST(ogg_basura_aleatoria) {
    std::uint32_t seed = 12345;
    for (int iter = 0; iter < 200; ++iter) {
        Bytes b(1 + lcg(seed) % 4096);
        for (auto& x : b) x = static_cast<std::uint8_t>(lcg(seed) >> 24);
        CHECK(openFails(std::move(b)));
    }
}

TEST(ogg_paginas_truncadas) {
    const Bytes page = oggPage(0x02, vorbisId(2, 44100));
    for (std::size_t n = 1; n < page.size(); ++n) {
        CHECK(openFails(Bytes(page.begin(), page.begin() + static_cast<long>(n))));
    }
    // Página de identificación completa pero sin cabeceras de comentario/setup
    CHECK(openFails(page, "Vorbis"));
}

TEST(ogg_cabecera_vorbis_corrupta) {
    CHECK(openFails(oggPage(0x02, vorbisId(0, 44100)), "Vorbis"));        // 0 canales
    CHECK(openFails(oggPage(0x02, vorbisId(255, 44100)), "Vorbis"));      // demasiados canales
    CHECK(openFails(oggPage(0x02, vorbisId(2, 0)), "Vorbis"));            // frecuencia 0
    CHECK(openFails(oggPage(0x02, vorbisId(2, 44100, 0x00)), "Vorbis"));  // blocksize < 256
    CHECK(openFails(oggPage(0x00, vorbisId(2, 44100)), "Vorbis"));        // sin flag BOS
    Bytes notVorbis = vorbisId(2, 44100);
    notVorbis[1] = 'X';
    CHECK(openFails(oggPage(0x02, notVorbis), "Vorbis"));
}

TEST(ogg_mutaciones_de_cabecera) {
    // Mutaciones deterministas de una página válida: nunca debe haber crash ni UB
    const Bytes base = oggPage(0x02, vorbisId(2, 44100));
    std::uint32_t seed = 777;
    for (int iter = 0; iter < 2000; ++iter) {
        Bytes b = base;
        const int flips = 1 + static_cast<int>(lcg(seed) % 4);
        for (int k = 0; k < flips; ++k) b[lcg(seed) % b.size()] ^= static_cast<std::uint8_t>(1u << (lcg(seed) % 8));
        std::string err;
        auto m = audio::OggMusic::open(std::move(b), false, &err);
        CHECK(!m);  // sin cabeceras de setup nunca puede abrirse
    }
}

TEST(ogg_muestra_real_opcional) {
    const char* path = std::getenv("TURK2_OGG_SAMPLE");
    if (!path || !*path) {
        std::printf("  (omitido: define TURK2_OGG_SAMPLE para probar con un .ogg propio)\n");
        return;
    }
    Bytes full = readFile(path);
    if (full.empty()) {
        // ctest se ejecuta desde build-host/: usar una ruta absoluta
        std::printf("  no se pudo leer TURK2_OGG_SAMPLE='%s' (¿ruta relativa?)\n", path);
        CHECK(!full.empty());
        return;
    }

    // Prefijo de ~256 KiB: stream truncado (≈ 10 s), suficiente y rápido bajo ASan
    const std::size_t cut = std::min<std::size_t>(full.size(), 256u << 10);
    Bytes prefix(full.begin(), full.begin() + static_cast<long>(cut));

    std::string err;
    auto once = audio::OggMusic::open(prefix, false, &err);
    CHECK(once != nullptr);
    if (!once) {
        std::printf("  %s\n", err.c_str());
        return;
    }
    CHECK(once->channels() >= 1 && once->channels() <= 2);
    std::vector<std::int16_t> buf(4096 * 2);
    std::size_t total = 0, got;
    bool nonSilent = false;
    while ((got = once->read(buf.data(), 4096)) > 0) {
        total += got;
        for (std::size_t i = 0; i < got * static_cast<std::size_t>(once->channels()); ++i) nonSilent |= buf[i] != 0;
        CHECK(total < 44100u * 600);  // cota de seguridad: el prefijo no puede durar 10 min
        if (total >= 44100u * 600) break;
    }
    CHECK(total > 0);
    CHECK(nonSilent);
    CHECK(once->read(buf.data(), 16) == 0);  // sin bucle: sigue en el final

    // Con bucle: se pueden leer más frames de los que tiene el stream, sin cortes
    auto looped = audio::OggMusic::open(prefix, true, &err);
    CHECK(looped != nullptr);
    if (!looped) return;
    std::size_t loopTotal = 0;
    while (loopTotal < total * 2 + 1000) {
        got = looped->read(buf.data(), 4096);
        CHECK(got == 4096);
        if (got != 4096) break;
        loopTotal += got;
    }

    // Mutaciones del prefijo real: el decodificador debe fallar o decodificar sin UB
    std::uint32_t seed = 4242;
    for (int iter = 0; iter < 40; ++iter) {
        Bytes b = prefix;
        for (int k = 0; k < 8; ++k) b[lcg(seed) % b.size()] = static_cast<std::uint8_t>(lcg(seed) >> 24);
        auto m = audio::OggMusic::open(std::move(b), false, nullptr);
        if (!m) continue;
        for (int blocks = 0; blocks < 64 && m->read(buf.data(), 4096) > 0; ++blocks) {
        }
    }
}
