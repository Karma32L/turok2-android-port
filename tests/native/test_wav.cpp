// Tests del parser WAV con archivos sintéticos.
#include "test.hpp"
#include "wav_builder.hpp"

#include <wav.hpp>

#include <string>

using wb::Bytes;

namespace {

bool failsWith(const Bytes& b, const char* needle) {
    std::string err;
    if (audio::parseWav(b.data(), b.size(), &err)) return false;
    if (err.find(needle) == std::string::npos) {
        std::printf("  error inesperado: '%s' (se esperaba '%s')\n", err.c_str(), needle);
        return false;
    }
    return true;
}

Bytes simple(const wb::Fmt& f, const Bytes& data) { return wb::riff({wb::chunk("fmt ", wb::fmtBody(f)), wb::chunk("data", data)}); }

}  // namespace

TEST(wav_mono_valido) {
    const Bytes b = simple({}, wb::samples({1, -2, 32767, -32768}));
    auto p = audio::parseWav(b.data(), b.size(), nullptr);
    CHECK(p.has_value());
    if (!p) return;
    CHECK(p->rate == 44100);
    CHECK(p->channels == 1);
    CHECK(p->frames() == 4);
    CHECK(p->samples[2] == 32767);
    CHECK(p->samples[3] == -32768);
}

TEST(wav_estereo_11025_con_chunks_extra_y_cola) {
    wb::Fmt f;
    f.channels = 2;
    f.rate = 11025;
    // LIST impar (padding) antes de data, id3 después y bytes basura tras el RIFF
    Bytes b = wb::riff({wb::chunk("fmt ", wb::fmtBody(f)), wb::chunk("LIST", Bytes{1, 2, 3}),
                        wb::chunk("data", wb::samples({10, 20, 30, 40})), wb::chunk("id3 ", Bytes(5, 0))});
    b.insert(b.end(), {0xAA, 0xBB, 0xCC});
    auto p = audio::parseWav(b.data(), b.size(), nullptr);
    CHECK(p.has_value());
    if (!p) return;
    CHECK(p->channels == 2);
    CHECK(p->rate == 11025);
    CHECK(p->frames() == 2);
    CHECK(p->samples[3] == 40);
}

TEST(wav_cabeceras_invalidas) {
    CHECK(failsWith(Bytes{}, "corto"));
    CHECK(failsWith(Bytes{'R', 'I', 'F', 'F'}, "corto"));
    Bytes b = simple({}, wb::samples({1}));
    b[8] = 'X';  // WAVE -> XAVE
    CHECK(failsWith(b, "RIFF/WAVE"));
    CHECK(!audio::parseWav(nullptr, 100, nullptr));
}

TEST(wav_chunk_mayor_que_archivo) {
    const Bytes b = wb::riff({wb::chunk("fmt ", wb::fmtBody({})), wb::chunk("data", wb::samples({1, 2}), 0x7FFFFFF0)});
    CHECK(failsWith(b, "truncado"));
    const Bytes c = wb::riff({wb::chunk("fmt ", wb::fmtBody({}), 0xFFFFFFFF)});
    CHECK(failsWith(c, "truncado"));
}

TEST(wav_riff_menor_que_contenido_recorta) {
    // RIFF declara menos bytes de los que hay: el chunk data queda fuera del rango válido
    Bytes b = simple({}, wb::samples({1, 2, 3, 4}));
    b[4] = 4 + 8 + 16;  // solo WAVE + fmt
    b[5] = b[6] = b[7] = 0;
    CHECK(failsWith(b, "falta el chunk data"));
}

TEST(wav_formatos_no_admitidos) {
    wb::Fmt f;
    f.tag = 3;  // float
    CHECK(failsWith(simple(f, wb::samples({1})), "PCM sin comprimir"));
    f = {};
    f.bits = 24;
    CHECK(failsWith(simple(f, Bytes(6, 0)), "16 bits"));
    f = {};
    f.bits = 8;
    CHECK(failsWith(simple(f, Bytes(2, 0)), "16 bits"));
    f = {};
    f.channels = 0;
    CHECK(failsWith(simple(f, wb::samples({1})), "canales"));
    f = {};
    f.channels = 6;
    CHECK(failsWith(simple(f, Bytes(12, 0)), "canales"));
    f = {};
    f.rate = 1000;
    CHECK(failsWith(simple(f, wb::samples({1})), "frecuencia"));
    f = {};
    f.rate = 400000;
    CHECK(failsWith(simple(f, wb::samples({1})), "frecuencia"));
}

TEST(wav_campos_incoherentes) {
    wb::Fmt f;
    f.blockAlign = 4;  // mono 16 bits => 2
    CHECK(failsWith(simple(f, wb::samples({1, 2})), "blockAlign"));
    f = {};
    f.byteRate = 1;
    CHECK(failsWith(simple(f, wb::samples({1})), "byteRate"));
    f = {};
    f.channels = 2;
    CHECK(failsWith(simple(f, wb::samples({1, 2, 3})), "múltiplo"));
}

TEST(wav_chunks_ausentes_o_duplicados) {
    CHECK(failsWith(wb::riff({wb::chunk("data", wb::samples({1}))}), "fmt"));
    CHECK(failsWith(wb::riff({wb::chunk("fmt ", wb::fmtBody({}))}), "data"));
    CHECK(failsWith(simple({}, Bytes{}), "sin muestras"));
    const Bytes body = wb::fmtBody({});
    CHECK(failsWith(wb::riff({wb::chunk("fmt ", body), wb::chunk("fmt ", body), wb::chunk("data", wb::samples({1}))}),
                    "duplicado"));
    CHECK(failsWith(wb::riff({wb::chunk("fmt ", Bytes(10, 0)), wb::chunk("data", wb::samples({1}))}), "corto"));
}

TEST(wav_riff_menor_que_cabecera) {
    // Regresión: tamaño RIFF 0..3 deja end < 12 y antes provocaba lectura fuera de límites
    for (std::uint8_t riff = 0; riff < 4; ++riff) {
        const Bytes minimal{'R', 'I', 'F', 'F', riff, 0, 0, 0, 'W', 'A', 'V', 'E'};
        CHECK(failsWith(minimal, "RIFF inválido"));
        // También con un WAV válido detrás: el tamaño declarado manda
        Bytes full = simple({}, wb::samples({1, 2, 3, 4}));
        full[4] = riff;
        full[5] = full[6] = full[7] = 0;
        CHECK(failsWith(full, "RIFF inválido"));
    }
    // RIFF = 4 (solo "WAVE"): cabecera coherente pero sin chunks
    Bytes four = simple({}, wb::samples({1}));
    four[4] = 4;
    four[5] = four[6] = four[7] = 0;
    CHECK(failsWith(four, "falta el chunk fmt"));
}

TEST(wav_todos_los_tamanos_riff_no_rompen) {
    // Recorre valores del campo RIFF (pequeños, alrededor del real y extremos): ASan vigila
    const Bytes base = wb::riff({wb::chunk("fmt ", wb::fmtBody({})), wb::chunk("data", wb::samples({1, 2, 3}))});
    std::vector<std::uint32_t> values;
    for (std::uint32_t v = 0; v <= base.size() + 8; ++v) values.push_back(v);
    for (std::uint32_t v : {0x7FFFFFFFu, 0x80000000u, 0xFFFFFFF7u, 0xFFFFFFF8u, 0xFFFFFFFFu}) values.push_back(v);
    for (std::uint32_t v : values) {
        Bytes b = base;
        for (int i = 0; i < 4; ++i) b[4 + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(v >> (8 * i));
        auto p = audio::parseWav(b.data(), b.size(), nullptr);
        if (p) CHECK(p->frames() == 3);
    }
}

TEST(wav_truncado_en_cada_byte_no_rompe) {
    // Cualquier prefijo debe fallar limpio o decodificar algo coherente (ASan vigila)
    const Bytes full = wb::riff({wb::chunk("fmt ", wb::fmtBody({})), wb::chunk("LIST", Bytes{9}),
                                 wb::chunk("data", wb::samples({1, 2, 3, 4, 5}))});
    for (std::size_t n = 0; n < full.size(); ++n) {
        Bytes cut(full.begin(), full.begin() + static_cast<long>(n));
        auto p = audio::parseWav(cut.data(), cut.size(), nullptr);
        if (p) CHECK(p->frames() > 0);
    }
}
