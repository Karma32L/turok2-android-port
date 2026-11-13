// Música Ogg Vorbis en streaming sobre stb_vorbis.
//
// El archivo comprimido se guarda entero en memoria (title.ogg ≈ 4,5 MB) y se decodifica
// bajo demanda en bloques pequeños: nunca se expande el tema completo a PCM (≈ 50 MB).
// Un handle no es thread-safe: usarlo desde un único hilo a la vez.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "pcm.hpp"

struct stb_vorbis;

namespace audio {

constexpr std::size_t kMaxOggBytes = 64u << 20;

class OggMusic final : public PcmSource {
public:
    // Valida cabeceras, canales (1–2) y frecuencia. nullptr + *err si falla.
    static std::unique_ptr<OggMusic> open(std::vector<std::uint8_t> data, bool loop, std::string* err);
    ~OggMusic() override;
    OggMusic(const OggMusic&) = delete;
    OggMusic& operator=(const OggMusic&) = delete;

    std::uint32_t rate() const override { return rate_; }
    int channels() const override { return channels_; }
    // Al llegar al final vuelve al principio si `loop`; 0 = fin (o error de decodificación).
    std::size_t read(std::int16_t* out, std::size_t frames) override;

private:
    OggMusic() = default;

    std::vector<std::uint8_t> data_;  // debe sobrevivir al decoder (lee de este buffer)
    stb_vorbis* vorbis_ = nullptr;
    std::uint32_t rate_ = 0;
    int channels_ = 0;
    bool loop_ = false;
};

}  // namespace audio
