// Dispositivo de audio SDL que alimenta al Mixer desde el hilo de audio.
//
// Toda llamada que toca el Mixer desde el hilo principal se hace bajo SDL_LockAudioDevice,
// así que el callback nunca ve el estado a medias. Solo se compila en la app (no en host).
#pragma once

#include <SDL.h>

#include <memory>
#include <string>

#include "mixer.hpp"

namespace audio {

class AudioDevice {
public:
    AudioDevice() = default;
    ~AudioDevice() { close(); }
    AudioDevice(const AudioDevice&) = delete;
    AudioDevice& operator=(const AudioDevice&) = delete;

    // Abre la salida S16 estéreo a 44,1 kHz (SDL convierte si el hardware usa otro formato).
    // Arranca en pausa: llamar a resume() cuando haya algo que reproducir.
    bool open(std::string* err);
    void close();
    bool isOpen() const { return dev_ != 0; }

    bool setMusic(std::unique_ptr<PcmSource> src, std::string* err);
    void playSfx(std::shared_ptr<const Pcm> sound);
    void setVolumes(int music, int sfx);

    // Idempotentes; seguras con el dispositivo cerrado
    void pause();
    void resume();

private:
    static void SDLCALL callback(void* user, Uint8* stream, int len);

    SDL_AudioDeviceID dev_ = 0;
    Mixer mixer_;
};

}  // namespace audio
