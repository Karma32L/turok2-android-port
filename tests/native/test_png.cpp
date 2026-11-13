// Tests del decodificador PNG con imágenes sintéticas generadas en memoria.
#include "png_builder.hpp"
#include "test.hpp"

#include <png.hpp>

#include <string>

using pb::Bytes;
using render::decodePng;

namespace {

std::optional<render::Image> dec(const Bytes& b, std::string* err = nullptr, const render::PngLimits& l = {}) {
    return decodePng(b.data(), b.size(), err, l);
}

// Decodificación debe fallar con un mensaje que contenga `needle`
bool failsWith(const Bytes& b, const char* needle, const render::PngLimits& l = {}) {
    std::string err;
    const auto img = dec(b, &err, l);
    if (img) return false;
    if (err.find(needle) == std::string::npos) {
        std::printf("  error inesperado: '%s' (se esperaba '%s')\n", err.c_str(), needle);
        return false;
    }
    return true;
}

Bytes withIdat(std::uint32_t w, std::uint32_t h, std::uint8_t ct, const Bytes& idatPayload) {
    return pb::concat({pb::signature(), pb::chunk("IHDR", pb::ihdr(w, h, ct)), pb::chunk("IDAT", idatPayload),
                       pb::chunk("IEND", {})});
}

}  // namespace

TEST(png_rgba_todos_los_filtros) {
    const std::uint32_t w = 7, h = 5;  // dimensiones impares: bordes de Sub/Paeth
    const Bytes px = pb::pattern(w * h * 4);
    for (std::uint8_t f = 0; f <= 4; ++f) {
        const auto img = dec(pb::makePng(w, h, 6, px, f));
        CHECK(img.has_value());
        if (!img) continue;
        CHECK(img->width == w && img->height == h && img->channels == 4);
        CHECK(img->pixels == px);
    }
}

TEST(png_rgb_filtros_mezclados_por_fila) {
    const std::uint32_t w = 9, h = 10;
    const Bytes px = pb::pattern(w * h * 3, 7);
    const Bytes raw = pb::filterRows(px, w, h, 3, [](std::uint32_t y) { return static_cast<std::uint8_t>(y % 5); });
    const auto img = dec(withIdat(w, h, 2, pb::zlib(raw)));
    CHECK(img.has_value());
    if (img) {
        CHECK(img->channels == 3);
        CHECK(img->pixels == px);
    }
}

TEST(png_gris_se_expande_a_rgb) {
    const Bytes px = {0, 128, 255, 7};
    const auto img = dec(pb::makePng(2, 2, 0, px, 4));
    CHECK(img.has_value());
    if (img) {
        CHECK(img->channels == 3);
        CHECK((img->pixels == Bytes{0, 0, 0, 128, 128, 128, 255, 255, 255, 7, 7, 7}));
    }
}

TEST(png_idat_partido_y_chunks_auxiliares) {
    const std::uint32_t w = 4, h = 3;
    const Bytes px = pb::pattern(w * h * 4, 3);
    const Bytes z = pb::zlib(pb::filterRows(px, w, h, 4, [](std::uint32_t) { return std::uint8_t{2}; }));
    const Bytes a(z.begin(), z.begin() + 5), b(z.begin() + 5, z.end());
    const Bytes png = pb::concat({pb::signature(), pb::chunk("IHDR", pb::ihdr(w, h, 6)), pb::chunk("gAMA", {0, 1, 2, 3}),
                                  pb::chunk("tEXt", {'a', 0, 'b'}), pb::chunk("IDAT", a), pb::chunk("IDAT", b),
                                  pb::chunk("IEND", {}), {'b', 'a', 's', 'u', 'r', 'a'}});
    const auto img = dec(png);
    CHECK(img.has_value());
    if (img) CHECK(img->pixels == px);
}

TEST(png_firma_y_estructura_invalidas) {
    const Bytes ok = pb::makePng(2, 2, 6, pb::pattern(16));
    Bytes sig = ok;
    sig[1] = 'X';
    CHECK(failsWith(sig, "firma"));
    CHECK(!decodePng(nullptr, 0).has_value());
    CHECK(failsWith(pb::signature(), "truncado"));

    // Primer chunk distinto de IHDR
    CHECK(failsWith(pb::concat({pb::signature(), pb::chunk("gAMA", {0, 0, 0, 1}), pb::chunk("IHDR", pb::ihdr(1, 1, 6))}),
                    "primer chunk"));
    // IHDR duplicado
    CHECK(failsWith(pb::concat({pb::signature(), pb::chunk("IHDR", pb::ihdr(1, 1, 6)), pb::chunk("IHDR", pb::ihdr(1, 1, 6))}),
                    "duplicado"));
    // IHDR de longitud incorrecta
    Bytes shortIhdr = pb::ihdr(1, 1, 6);
    shortIhdr.pop_back();
    CHECK(failsWith(pb::concat({pb::signature(), pb::chunk("IHDR", shortIhdr)}), "longitud distinta"));
    // Chunk crítico desconocido y tipo de chunk no alfabético
    CHECK(failsWith(pb::concat({pb::signature(), pb::chunk("IHDR", pb::ihdr(1, 1, 6)), pb::chunk("ABCD", {})}),
                    "crítico desconocido"));
    CHECK(failsWith(pb::concat({pb::signature(), pb::chunk("IHDR", pb::ihdr(1, 1, 6)), pb::chunk("ab1d", {})}),
                    "tipo de chunk"));
    // Sin IDAT
    CHECK(failsWith(pb::concat({pb::signature(), pb::chunk("IHDR", pb::ihdr(1, 1, 6)), pb::chunk("IEND", {})}),
                    "sin datos IDAT"));
}

TEST(png_crc_incorrecta) {
    CHECK(failsWith(pb::concat({pb::signature(), pb::chunk("IHDR", pb::ihdr(1, 1, 6), true)}), "CRC"));
}

TEST(png_longitud_de_chunk_maliciosa) {
    for (std::uint32_t len : {0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu, 0xFFFFFFF0u, 1000u}) {
        Bytes b = pb::signature();
        pb::put32be(b, len);
        b.insert(b.end(), {'I', 'H', 'D', 'R'});
        b.resize(b.size() + 30, 0);
        CHECK(failsWith(b, "fuera del archivo"));
    }
}

TEST(png_cabeceras_no_soportadas_o_invalidas) {
    auto onlyHeader = [](const Bytes& ih) {
        return pb::concat({pb::signature(), pb::chunk("IHDR", ih), pb::chunk("IEND", {})});
    };
    CHECK(failsWith(onlyHeader(pb::ihdr(0, 4, 6)), "nulas"));
    CHECK(failsWith(onlyHeader(pb::ihdr(4, 0, 6)), "nulas"));
    CHECK(failsWith(onlyHeader(pb::ihdr(8193, 1, 6)), "límite"));
    CHECK(failsWith(onlyHeader(pb::ihdr(0xFFFFFFFFu, 0xFFFFFFFFu, 6)), "límite"));
    CHECK(failsWith(onlyHeader(pb::ihdr(4, 4, 6, 8, 1)), "entrelazado"));
    CHECK(failsWith(onlyHeader(pb::ihdr(4, 4, 6, 8, 2)), "entrelazado"));
    CHECK(failsWith(onlyHeader(pb::ihdr(4, 4, 6, 16)), "8 bits"));
    CHECK(failsWith(onlyHeader(pb::ihdr(4, 4, 3)), "tipo de color"));
    CHECK(failsWith(onlyHeader(pb::ihdr(4, 4, 4)), "tipo de color"));
    Bytes badMethod = pb::ihdr(4, 4, 6);
    badMethod[10] = 1;
    CHECK(failsWith(onlyHeader(badMethod), "compresión"));
}

TEST(png_limites_de_memoria) {
    // 8192x8192 RGBA = 256 MiB > 64 MiB: se rechaza antes de reservar o descomprimir nada
    CHECK(failsWith(withIdat(8192, 8192, 6, pb::zlib({0})), "descomprimida por encima"));
    render::PngLimits tiny;
    tiny.maxDecodedBytes = 16;
    CHECK(failsWith(pb::makePng(4, 4, 6, pb::pattern(64)), "descomprimida por encima", tiny));
    render::PngLimits tinyIdat;
    tinyIdat.maxCompressedBytes = 4;
    CHECK(failsWith(pb::makePng(4, 4, 6, pb::pattern(64)), "IDAT por encima", tinyIdat));
}

TEST(png_flujo_zlib_corto_largo_o_corrupto) {
    const std::uint32_t w = 3, h = 4;
    const Bytes px = pb::pattern(w * h * 4);
    const Bytes raw = pb::filterRows(px, w, h, 4, [](std::uint32_t) { return std::uint8_t{0}; });

    Bytes shortRaw(raw.begin(), raw.end() - 1);
    CHECK(failsWith(withIdat(w, h, 6, pb::zlib(shortRaw)), "de menos"));
    Bytes longRaw = raw;
    longRaw.push_back(0);
    CHECK(failsWith(withIdat(w, h, 6, pb::zlib(longRaw)), "de más"));

    Bytes z = pb::zlib(raw);
    z.back() ^= 0xFF;  // Adler-32 corrupto (la CRC del chunk se recalcula y es válida)
    CHECK(failsWith(withIdat(w, h, 6, z), "zlib"));
    CHECK(failsWith(withIdat(w, h, 6, {0x78, 0x9C, 0xFF, 0xFF}), "zlib"));
}

TEST(png_tipo_de_filtro_invalido) {
    Bytes raw = pb::filterRows(pb::pattern(8), 2, 1, 4, [](std::uint32_t) { return std::uint8_t{0}; });
    raw[0] = 5;
    CHECK(failsWith(withIdat(2, 1, 6, pb::zlib(raw)), "filtro"));
}

// Robustez: todo prefijo y toda inversión de bit de un PNG válido debe rechazarse o
// decodificarse sin errores de memoria (ASan/UBSan vigilan).
TEST(png_truncado_y_bits_invertidos_no_rompen) {
    const Bytes ok = pb::makePng(5, 3, 6, pb::pattern(60), 4);
    CHECK(dec(ok).has_value());
    for (std::size_t n = 0; n < ok.size(); ++n) {
        const Bytes cut(ok.begin(), ok.begin() + static_cast<std::ptrdiff_t>(n));
        CHECK(!dec(cut).has_value());
    }
    for (std::size_t i = 0; i < ok.size(); ++i) {
        for (int bit = 0; bit < 8; ++bit) {
            Bytes m = ok;
            m[i] ^= static_cast<std::uint8_t>(1u << bit);
            (void)dec(m);
        }
    }
}
