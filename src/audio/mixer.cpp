#include "mixer.hpp"

#include <algorithm>

namespace audio {
namespace {

std::int16_t saturate(std::int32_t v) {
    return static_cast<std::int16_t>(std::clamp<std::int32_t>(v, INT16_MIN, INT16_MAX));
}

void setErr(std::string* err, const char* msg) {
    if (err) *err = msg;
}

}  // namespace

std::int32_t Mixer::gainFor(int v) {
    v = std::clamp(v, 0, kMaxVolume);
    return v * v * 256 / (kMaxVolume * kMaxVolume);
}

bool Mixer::setMusic(std::unique_ptr<PcmSource> src, std::string* err) {
    if (!src) {
        setErr(err, "Mixer: fuente de música nula");
        return false;
    }
    if (src->rate() != kOutRate) {
        setErr(err, "Mixer: la música debe estar a 44100 Hz");
        return false;
    }
    if (src->channels() < 1 || src->channels() > kMaxChannels) {
        setErr(err, "Mixer: número de canales de música no admitido");
        return false;
    }
    music_ = std::move(src);
    return true;
}

bool Mixer::play(std::shared_ptr<const Pcm> sound, std::string* err) {
    if (!sound || sound->frames() == 0) {
        setErr(err, "Mixer: sonido vacío");
        return false;
    }
    if (sound->channels < 1 || sound->channels > kMaxChannels || sound->rate < kMinRate || sound->rate > kMaxRate ||
        sound->samples.size() % static_cast<std::size_t>(sound->channels) != 0) {
        setErr(err, "Mixer: formato de sonido no admitido");
        return false;
    }
    // Voz libre o, si no hay, la más antigua (menor serial)
    Voice* slot = &voices_[0];
    for (Voice& v : voices_) {
        if (!v.pcm) {
            slot = &v;
            break;
        }
        if (v.serial < slot->serial) slot = &v;
    }
    slot->pcm = std::move(sound);
    slot->pos = 0;
    slot->step = (static_cast<std::uint64_t>(slot->pcm->rate) << 16) / kOutRate;
    slot->serial = nextSerial_++;
    return true;
}

std::size_t Mixer::activeVoices() const {
    return static_cast<std::size_t>(std::count_if(voices_.begin(), voices_.end(), [](const Voice& v) { return v.pcm != nullptr; }));
}

void Mixer::mix(std::int16_t* out, std::size_t frames) {
    while (frames > 0) {
        const std::size_t n = std::min(frames, kChunk);
        mixChunk(out, n);
        out += n * kOutChannels;
        frames -= n;
    }
}

void Mixer::mixChunk(std::int16_t* out, std::size_t frames) {
    std::fill_n(acc_.begin(), frames * kOutChannels, 0);

    if (music_) {
        const int ch = music_->channels();
        const std::size_t got = music_->read(scratch_.data(), frames);  // ch <= 2: cabe en scratch_
        for (std::size_t i = 0; i < got; ++i) {
            const std::int32_t l = scratch_[i * static_cast<std::size_t>(ch)];
            const std::int32_t r = ch == 2 ? scratch_[i * 2 + 1] : l;
            acc_[i * 2] += l * musicGain_ / 256;
            acc_[i * 2 + 1] += r * musicGain_ / 256;
        }
        if (got < frames) music_.reset();  // fin sin bucle o error de decodificación
    }

    for (Voice& v : voices_) {
        if (!v.pcm) continue;
        const Pcm& p = *v.pcm;
        const std::size_t total = p.frames();
        const std::size_t ch = static_cast<std::size_t>(p.channels);
        for (std::size_t i = 0; i < frames; ++i) {
            const std::size_t idx = static_cast<std::size_t>(v.pos >> 16);
            if (idx >= total) {
                v.pcm.reset();
                break;
            }
            // Interpolación lineal con el siguiente frame (o el mismo en el último)
            const std::size_t nxt = idx + 1 < total ? idx + 1 : idx;
            const std::int64_t frac = static_cast<std::int64_t>(v.pos & 0xFFFF);
            for (std::size_t c = 0; c < static_cast<std::size_t>(kOutChannels); ++c) {
                const std::size_t sc = ch == 2 ? c : 0;  // mono -> ambos canales
                const std::int32_t a = p.samples[idx * ch + sc];
                const std::int32_t b = p.samples[nxt * ch + sc];
                // (b - a) * frac llega a ~2^32: en 64 bits para no desbordar
                const std::int32_t s = a + static_cast<std::int32_t>(((b - a) * frac) / 65536);
                acc_[i * 2 + c] += s * sfxGain_ / 256;
            }
            v.pos += v.step;
        }
    }

    for (std::size_t i = 0; i < frames * kOutChannels; ++i) out[i] = saturate(acc_[i]);
}

}  // namespace audio
