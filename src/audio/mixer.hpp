// Mezclador software: 1 pista de música en streaming + kMaxVoices efectos en memoria.
//
// Salida fija PCM16 estéreo a kOutRate. Los SFX con otra frecuencia (el juego usa 11 025 y
// 44 100 Hz) se remuestrean con interpolación lineal en punto fijo; la música debe venir ya a
// kOutRate. No es thread-safe: el llamador serializa mix() frente al resto de métodos
// (en la app, SDL_LockAudioDevice). mix() no reserva memoria.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "pcm.hpp"

namespace audio {

constexpr std::uint32_t kOutRate = 44100;
constexpr int kOutChannels = 2;
constexpr int kMaxVolume = 10;

class Mixer {
public:
    static constexpr std::size_t kMaxVoices = 8;

    // Sustituye la música actual. Falla si la fuente no es mono/estéreo a kOutRate.
    bool setMusic(std::unique_ptr<PcmSource> src, std::string* err);
    void stopMusic() { music_.reset(); }
    bool hasMusic() const { return music_ != nullptr; }

    // Reproduce un SFX. Si no hay voces libres, reemplaza la más antigua. Falla con un Pcm
    // vacío o con parámetros fuera de rango.
    bool play(std::shared_ptr<const Pcm> sound, std::string* err);
    std::size_t activeVoices() const;

    // 0..kMaxVolume (se recorta). Curva cuadrática: más natural al oído que una lineal.
    void setMusicVolume(int v) { musicGain_ = gainFor(v); }
    void setSfxVolume(int v) { sfxGain_ = gainFor(v); }

    // Escribe `frames` frames estéreo entrelazados en `out` (siempre los rellena todos).
    void mix(std::int16_t* out, std::size_t frames);

private:
    struct Voice {
        std::shared_ptr<const Pcm> pcm;
        std::uint64_t pos = 0;   // posición en frames de origen, punto fijo 16.16
        std::uint64_t step = 0;  // avance por frame de salida, 16.16
        std::uint64_t serial = 0;
    };
    static constexpr std::size_t kChunk = 512;  // frames por bloque interno

    static std::int32_t gainFor(int v);  // Q8: 256 = ganancia unidad
    void mixChunk(std::int16_t* out, std::size_t frames);

    std::unique_ptr<PcmSource> music_;
    std::array<Voice, kMaxVoices> voices_{};
    std::uint64_t nextSerial_ = 1;
    std::int32_t musicGain_ = 256, sfxGain_ = 256;
    std::array<std::int32_t, kChunk * kOutChannels> acc_{};
    std::array<std::int16_t, kChunk * kOutChannels> scratch_{};
};

}  // namespace audio
