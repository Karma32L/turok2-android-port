#include "wav.hpp"

#include <algorithm>
#include <cstring>

namespace audio {
namespace {

std::uint16_t le16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }
std::uint32_t le32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::optional<Pcm> fail(std::string* err, const char* msg) {
    if (err) *err = msg;
    return std::nullopt;
}

}  // namespace

std::optional<Pcm> parseWav(const std::uint8_t* data, std::size_t size, std::string* err) {
    if (!data || size < 12) return fail(err, "WAV: archivo demasiado corto");
    if (size > kMaxWavBytes) return fail(err, "WAV: archivo demasiado grande");
    if (std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "WAVE", 4) != 0)
        return fail(err, "WAV: no es RIFF/WAVE");

    // size <= 16 MiB, así que 8 + riff cabe en uint64 sin desbordar
    const std::size_t end = static_cast<std::size_t>(std::min<std::uint64_t>(size, 8ull + le32(data + 4)));
    // Un tamaño RIFF < 4 deja `end` antes del primer chunk: sin esta comprobación,
    // `end - off` desborda (size_t) y el bucle leería fuera del buffer
    if (end < 12) return fail(err, "WAV: tamaño RIFF inválido");

    bool haveFmt = false;
    std::uint16_t channels = 0, blockAlign = 0;
    std::uint32_t rate = 0;
    const std::uint8_t* pcm = nullptr;
    std::size_t pcmBytes = 0;

    std::size_t off = 12;
    while (end - off >= 8) {  // invariante: off <= end
        const std::uint8_t* hdr = data + off;
        const std::uint32_t len = le32(hdr + 4);
        if (len > end - off - 8) return fail(err, "WAV: chunk truncado");
        const std::uint8_t* body = hdr + 8;

        if (std::memcmp(hdr, "fmt ", 4) == 0) {
            if (haveFmt) return fail(err, "WAV: chunk fmt duplicado");
            if (len < 16) return fail(err, "WAV: chunk fmt demasiado corto");
            const std::uint16_t tag = le16(body);
            channels = le16(body + 2);
            rate = le32(body + 4);
            const std::uint32_t byteRate = le32(body + 8);
            blockAlign = le16(body + 12);
            const std::uint16_t bits = le16(body + 14);
            if (tag != 1) return fail(err, "WAV: solo se admite PCM sin comprimir");
            if (bits != 16) return fail(err, "WAV: solo se admite PCM de 16 bits");
            if (channels < 1 || channels > kMaxChannels) return fail(err, "WAV: número de canales no admitido");
            if (rate < kMinRate || rate > kMaxRate) return fail(err, "WAV: frecuencia fuera de rango");
            if (blockAlign != channels * 2u) return fail(err, "WAV: blockAlign inconsistente");
            if (byteRate != rate * blockAlign) return fail(err, "WAV: byteRate inconsistente");
            haveFmt = true;
        } else if (std::memcmp(hdr, "data", 4) == 0) {
            if (pcm) return fail(err, "WAV: chunk data duplicado");
            pcm = body;
            pcmBytes = len;
        }
        // Avance con padding a par; si el padding cae fuera, termina el recorrido
        const std::size_t step = 8u + len + (len & 1u);
        if (step > end - off) break;
        off += step;
    }

    if (!haveFmt) return fail(err, "WAV: falta el chunk fmt");
    if (!pcm) return fail(err, "WAV: falta el chunk data");
    if (pcmBytes == 0) return fail(err, "WAV: sin muestras");
    if (pcmBytes % blockAlign != 0) return fail(err, "WAV: data no es múltiplo de blockAlign");

    Pcm out;
    out.rate = rate;
    out.channels = channels;
    out.samples.resize(pcmBytes / 2);
    for (std::size_t i = 0; i < out.samples.size(); ++i) {
        out.samples[i] = static_cast<std::int16_t>(le16(pcm + i * 2));
    }
    return out;
}

}  // namespace audio
