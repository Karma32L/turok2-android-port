// Tipos básicos de audio PCM compartidos por decodificadores y mezclador.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace audio {

// Límites aceptados para cualquier fuente (WAV u OGG)
constexpr std::uint32_t kMinRate = 8000;
constexpr std::uint32_t kMaxRate = 192000;
constexpr int kMaxChannels = 2;

// Sonido completo en memoria (SFX): PCM16 entrelazado.
struct Pcm {
    std::uint32_t rate = 0;
    int channels = 0;
    std::vector<std::int16_t> samples;  // frames * channels

    std::size_t frames() const { return channels > 0 ? samples.size() / static_cast<std::size_t>(channels) : 0; }
};

// Fuente en streaming (música). read() devuelve los frames escritos; 0 = fin de la fuente.
class PcmSource {
public:
    virtual ~PcmSource() = default;
    virtual std::uint32_t rate() const = 0;
    virtual int channels() const = 0;
    // `out` tiene espacio para frames * channels() muestras.
    virtual std::size_t read(std::int16_t* out, std::size_t frames) = 0;
};

}  // namespace audio
