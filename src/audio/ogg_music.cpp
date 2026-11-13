#include "ogg_music.hpp"

#include <algorithm>
#include <climits>

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

namespace audio {

std::unique_ptr<OggMusic> OggMusic::open(std::vector<std::uint8_t> data, bool loop, std::string* err) {
    auto setErr = [err](std::string msg) {
        if (err) *err = std::move(msg);
    };
    if (data.empty()) {
        setErr("OGG: archivo vacío");
        return nullptr;
    }
    static_assert(kMaxOggBytes <= static_cast<std::size_t>(INT_MAX), "stb_vorbis usa int para la longitud");
    if (data.size() > kMaxOggBytes) {
        setErr("OGG: archivo demasiado grande");
        return nullptr;
    }

    std::unique_ptr<OggMusic> m(new OggMusic());
    m->data_ = std::move(data);
    m->loop_ = loop;
    int error = 0;
    m->vorbis_ = stb_vorbis_open_memory(m->data_.data(), static_cast<int>(m->data_.size()), &error, nullptr);
    if (!m->vorbis_) {
        setErr("OGG: cabecera Vorbis inválida (stb_vorbis error " + std::to_string(error) + ")");
        return nullptr;
    }
    const stb_vorbis_info info = stb_vorbis_get_info(m->vorbis_);
    if (info.channels < 1 || info.channels > kMaxChannels) {
        setErr("OGG: número de canales no admitido (" + std::to_string(info.channels) + ")");
        return nullptr;  // el destructor cierra el decoder
    }
    if (info.sample_rate < kMinRate || info.sample_rate > kMaxRate) {
        setErr("OGG: frecuencia fuera de rango (" + std::to_string(info.sample_rate) + ")");
        return nullptr;
    }
    m->channels_ = info.channels;
    m->rate_ = info.sample_rate;
    return m;
}

OggMusic::~OggMusic() {
    if (vorbis_) stb_vorbis_close(vorbis_);
}

std::size_t OggMusic::read(std::int16_t* out, std::size_t frames) {
    if (!vorbis_ || !out) return 0;
    // num_shorts es int: se trocea la petición para no desbordarlo
    constexpr std::size_t kMaxFramesPerCall = 1u << 16;
    std::size_t done = 0;
    bool rewound = false;  // evita un bucle infinito en streams sin audio decodificable
    while (done < frames) {
        const std::size_t want = std::min(frames - done, kMaxFramesPerCall);
        const int got = stb_vorbis_get_samples_short_interleaved(
            vorbis_, channels_, out + done * static_cast<std::size_t>(channels_),
            static_cast<int>(want * static_cast<std::size_t>(channels_)));
        if (got > 0) {
            done += static_cast<std::size_t>(got);
            rewound = false;
            continue;
        }
        if (!loop_ || rewound || !stb_vorbis_seek_start(vorbis_)) break;
        rewound = true;
    }
    return done;
}

}  // namespace audio
