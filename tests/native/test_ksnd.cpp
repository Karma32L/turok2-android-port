// Shaders de sonido .ksnd y banco de sonidos con datos sintéticos (nunca del juego).
#include "test.hpp"

#include <ksnd.hpp>

#include <map>
#include <string>

#include "wav_builder.hpp"

using audio::parseKsnd;
using audio::SoundBank;

namespace {

std::string shader(const std::string& type, const std::string& wav) {
    return "sounds[1] =\n{\n        volume_min = 100\n        type = " + type +
           "\n        weight = 100\n    {\n        wavefile = \"" + wav + "\"\n        delay = 0\n    }\n}\n";
}

wb::Bytes tinyWav(std::int16_t v) {
    return wb::riff({wb::chunk("fmt ", wb::fmtBody({})), wb::chunk("data", wb::samples({v, v, v, v}))});
}

// Lector en memoria que cuenta las lecturas por ruta
struct MemFs {
    std::map<std::string, wb::Bytes> files;
    std::map<std::string, int> reads;
    SoundBank::Reader reader() {
        return [this](const std::string& p) -> std::optional<wb::Bytes> {
            ++reads[p];
            const auto it = files.find(p);
            if (it == files.end()) return std::nullopt;
            return it->second;
        };
    }
    void text(const std::string& p, const std::string& s) { files[p] = wb::Bytes(s.begin(), s.end()); }
};

}  // namespace

TEST(ksnd_valido_toma_type_y_primer_wavefile) {
    std::string s = shader("453", "sounds/waves/XPLOMED2.WAV");
    s += "    {\n        wavefile = \"sounds/waves/OTRO.WAV\"\n    }\n";
    const auto k = parseKsnd(s);
    CHECK(k.has_value());
    if (k) CHECK(k->id == 453 && k->wav == "sounds/waves/XPLOMED2.WAV");
}

TEST(ksnd_invalidos) {
    std::string err;
    CHECK(!parseKsnd("sounds[1] =\n{\n    {\n wavefile = \"a/b.wav\"\n }\n}\n", &err) && err == "sin type");
    CHECK(!parseKsnd(shader("12", "").substr(0, 40), &err));
    CHECK(!parseKsnd(shader("-3", "a/b.wav")));
    CHECK(!parseKsnd(shader("99999999999", "a/b.wav")));
    CHECK(!parseKsnd(shader("12x", "a/b.wav")));
    CHECK(!parseKsnd(shader("4294967295", "a/b.wav")));  // fuera de kMaxSoundId
    // Rutas peligrosas o que no son .wav
    for (const char* bad : {"../../etc/passwd.wav", "/abs/x.wav", "a/../b.wav", "a//b.wav", "a\\\\b.wav", "c:/x.wav",
                            "sounds/x.ogg", "", "./a.wav"}) {
        CHECK(!parseKsnd(shader("5", bad)));
    }
    CHECK(!parseKsnd("type = 5\nwavefile = sounds/x.wav\n"));  // sin comillas
    CHECK(!parseKsnd(std::string(audio::kMaxKsndBytes + 1, ' ') + shader("5", "a/b.wav")));
}

TEST(banco_alterna_variantes_y_carga_perezosa) {
    MemFs fs;
    fs.text("s/a.ksnd", shader("512", "w/a.wav"));
    fs.text("s/b.ksnd", shader("512", "w/b.wav"));
    fs.text("s/c.ksnd", shader("410", "w/missing.wav"));
    fs.text("s/bad.ksnd", "basura");
    fs.files["w/a.wav"] = tinyWav(1);
    fs.files["w/b.wav"] = tinyWav(2);
    SoundBank bank(fs.reader());
    std::string err;
    CHECK(bank.addShader("s/a.ksnd") && bank.addShader("s/b.ksnd") && bank.addShader("s/c.ksnd"));
    CHECK(!bank.addShader("s/bad.ksnd", &err) && !err.empty());
    CHECK(!bank.addShader("s/missing.ksnd", &err));
    CHECK(bank.size() == 2 && bank.has(512) && !bank.has(1));
    CHECK(fs.reads["w/a.wav"] == 0);  // nada se carga hasta pedirlo
    const auto p1 = bank.get(512), p2 = bank.get(512), p3 = bank.get(512);
    CHECK(p1 && p2 && p3 && p1 != p2 && p1 == p3);  // alterna a → b → a
    if (p1 && p2) CHECK(p1->samples[0] == 1 && p2->samples[0] == 2);
    CHECK(fs.reads["w/a.wav"] == 1);                 // caché
    CHECK(!bank.get(410) && !bank.get(410));         // el fallo se recuerda
    CHECK(fs.reads["w/missing.wav"] == 1);
    CHECK(!bank.get(999));
}

TEST(banco_limita_variantes) {
    MemFs fs;
    fs.text("s/x.ksnd", shader("7", "w/x.wav"));
    SoundBank bank(fs.reader());
    for (std::size_t i = 0; i < SoundBank::kMaxVariants; ++i) CHECK(bank.addShader("s/x.ksnd"));
    CHECK(!bank.addShader("s/x.ksnd"));
}
