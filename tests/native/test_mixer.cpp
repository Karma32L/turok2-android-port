// Tests del mezclador con fuentes sintéticas.
#include "test.hpp"

#include <mixer.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace {

// Fuente de música sintética: `frames` frames de valor constante (o rampa)
class FakeSource : public audio::PcmSource {
public:
    FakeSource(int ch, std::uint32_t rate, std::size_t frames, std::int16_t value)
        : ch_(ch), rate_(rate), left_(frames), value_(value) {}
    std::uint32_t rate() const override { return rate_; }
    int channels() const override { return ch_; }
    std::size_t read(std::int16_t* out, std::size_t frames) override {
        const std::size_t n = frames < left_ ? frames : left_;
        for (std::size_t i = 0; i < n * static_cast<std::size_t>(ch_); ++i) {
            out[i] = (ch_ == 2 && (i & 1u)) ? static_cast<std::int16_t>(-value_) : value_;
        }
        left_ -= n;
        return n;
    }

private:
    int ch_;
    std::uint32_t rate_;
    std::size_t left_;
    std::int16_t value_;
};

std::shared_ptr<const audio::Pcm> tone(int ch, std::uint32_t rate, std::size_t frames, std::int16_t v) {
    auto p = std::make_shared<audio::Pcm>();
    p->channels = ch;
    p->rate = rate;
    p->samples.assign(frames * static_cast<std::size_t>(ch), v);
    return p;
}

std::vector<std::int16_t> run(audio::Mixer& m, std::size_t frames) {
    std::vector<std::int16_t> out(frames * 2, 12345);  // valor basura: mix() debe sobrescribirlo
    m.mix(out.data(), frames);
    return out;
}

}  // namespace

TEST(mixer_silencio_sin_fuentes) {
    audio::Mixer m;
    for (std::int16_t s : run(m, 1500)) CHECK(s == 0);
}

TEST(mixer_musica_mono_a_estereo_y_fin) {
    audio::Mixer m;
    CHECK(m.setMusic(std::make_unique<FakeSource>(1, 44100, 700, 1000), nullptr));
    const auto out = run(m, 1000);  // cruza el bloque interno de 512
    CHECK(out[0] == 1000 && out[1] == 1000);
    CHECK(out[699 * 2] == 1000 && out[699 * 2 + 1] == 1000);
    CHECK(out[700 * 2] == 0);  // tras el fin de la fuente, silencio
    CHECK(!m.hasMusic());
}

TEST(mixer_musica_estereo_conserva_canales) {
    audio::Mixer m;
    CHECK(m.setMusic(std::make_unique<FakeSource>(2, 44100, 10, 500), nullptr));
    const auto out = run(m, 4);
    CHECK(out[0] == 500 && out[1] == -500);
}

TEST(mixer_rechaza_fuentes_invalidas) {
    audio::Mixer m;
    std::string err;
    CHECK(!m.setMusic(nullptr, &err));
    CHECK(!m.setMusic(std::make_unique<FakeSource>(2, 22050, 10, 1), &err));
    CHECK(!m.setMusic(std::make_unique<FakeSource>(3, 44100, 10, 1), &err));
    CHECK(!m.play(nullptr, &err));
    CHECK(!m.play(tone(1, 44100, 0, 1), &err));
    CHECK(!m.play(tone(0, 44100, 4, 1), &err));
    CHECK(!m.play(tone(1, 100, 4, 1), &err));
    auto odd = std::make_shared<audio::Pcm>();
    odd->channels = 2;
    odd->rate = 44100;
    odd->samples = {1, 2, 3};
    CHECK(!m.play(odd, &err));
    CHECK(m.activeVoices() == 0);
}

TEST(mixer_saturacion) {
    audio::Mixer m;
    CHECK(m.setMusic(std::make_unique<FakeSource>(1, 44100, 100, 30000), nullptr));
    CHECK(m.play(tone(1, 44100, 100, 30000), nullptr));
    CHECK(m.play(tone(1, 44100, 100, -32768), nullptr));
    CHECK(m.play(tone(1, 44100, 100, -32768), nullptr));
    auto out = run(m, 10);
    // La suma se hace en int32 antes de recortar: 30000 + 30000 - 32768 - 32768 = -5536
    CHECK(out[0] == -5536);
    audio::Mixer p;
    for (int i = 0; i < 3; ++i) CHECK(p.play(tone(1, 44100, 10, 30000), nullptr));
    out = run(p, 2);
    CHECK(out[0] == 32767 && out[1] == 32767);
}

TEST(mixer_volumenes) {
    audio::Mixer m;
    m.setMusicVolume(0);
    m.setSfxVolume(5);  // (5/10)^2 = 0,25
    CHECK(m.setMusic(std::make_unique<FakeSource>(1, 44100, 10, 20000), nullptr));
    CHECK(m.play(tone(1, 44100, 10, 4000), nullptr));
    auto out = run(m, 2);
    CHECK(out[0] == 1000);
    m.setSfxVolume(99);  // se recorta a 10
    CHECK(m.play(tone(1, 44100, 10, 4000), nullptr));
    m.setSfxVolume(-3);  // se recorta a 0
    out = run(m, 2);
    CHECK(out[0] == 0);
}

TEST(mixer_voces_maximas_reemplaza_la_mas_antigua) {
    audio::Mixer m;
    for (std::size_t i = 0; i < audio::Mixer::kMaxVoices; ++i) CHECK(m.play(tone(1, 44100, 1000, 1), nullptr));
    CHECK(m.activeVoices() == audio::Mixer::kMaxVoices);
    CHECK(m.play(tone(1, 44100, 1000, 100), nullptr));
    CHECK(m.activeVoices() == audio::Mixer::kMaxVoices);
    const auto out = run(m, 1);
    CHECK(out[0] == 7 + 100);  // 7 voces de 1 + la nueva de 100
}

TEST(mixer_sfx_termina_y_libera_voz) {
    audio::Mixer m;
    CHECK(m.play(tone(1, 44100, 3, 50), nullptr));
    const auto out = run(m, 5);
    CHECK(out[4] == 50 && out[5] == 50);  // frame 2 suena (L y R)
    CHECK(out[6] == 0 && out[7] == 0);    // frame 3 ya no
    CHECK(m.activeVoices() == 0);
}

TEST(mixer_remuestreo_11025) {
    // 11025 -> 44100: cada frame de origen ocupa 4 de salida, con interpolación lineal
    auto p = std::make_shared<audio::Pcm>();
    p->channels = 1;
    p->rate = 11025;
    p->samples = {0, 400, 400};
    audio::Mixer m;
    CHECK(m.play(p, nullptr));
    const auto out = run(m, 14);
    CHECK(out[0] == 0);
    CHECK(out[2 * 2] == 200);  // mitad entre 0 y 400
    CHECK(out[4 * 2] == 400);
    CHECK(out[11 * 2] == 400);
    CHECK(out[12 * 2] == 0);  // 3 frames * 4 = 12 frames de salida
    CHECK(m.activeVoices() == 0);
}

TEST(mixer_remuestreo_extremos_sin_desbordar) {
    // Salto -32768 -> 32767 a 8 kHz: el producto de interpolación excede int32 (UBSan vigila)
    auto p = std::make_shared<audio::Pcm>();
    p->channels = 2;
    p->rate = 8000;
    p->samples = {-32768, 32767, 32767, -32768};
    audio::Mixer m;
    CHECK(m.play(p, nullptr));
    const auto out = run(m, 12);
    CHECK(out[0] == -32768 && out[1] == 32767);
    // Frame 1: pos = step = (8000 << 16) / 44100 = 11888 -> fracción 11888/65536
    // L: -32768 + 65535 * 11888 / 65536 = -20881 ; R: 32767 - 11887 = 20880
    CHECK(out[2] == -20881 && out[3] == 20880);
}
