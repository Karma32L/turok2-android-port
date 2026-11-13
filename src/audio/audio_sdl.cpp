#include "audio_sdl.hpp"

#include <cstring>

#include "log.hpp"

namespace audio {

namespace {

// RAII para SDL_LockAudioDevice: imposible olvidar el unlock en un return temprano
class DeviceLock {
public:
    explicit DeviceLock(SDL_AudioDeviceID d) : d_(d) {
        if (d_) SDL_LockAudioDevice(d_);
    }
    ~DeviceLock() {
        if (d_) SDL_UnlockAudioDevice(d_);
    }
    DeviceLock(const DeviceLock&) = delete;
    DeviceLock& operator=(const DeviceLock&) = delete;

private:
    SDL_AudioDeviceID d_;
};

}  // namespace

bool AudioDevice::open(std::string* err) {
    if (dev_) return true;
    SDL_AudioSpec want{};
    want.freq = static_cast<int>(kOutRate);
    want.format = AUDIO_S16SYS;
    want.channels = kOutChannels;
    want.samples = 1024;  // ≈ 23 ms: latencia razonable para SFX de menú
    want.callback = &AudioDevice::callback;
    want.userdata = this;
    SDL_AudioSpec have{};
    // allowed_changes = 0: SDL garantiza exactamente el formato pedido y convierte por dentro
    dev_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (!dev_) {
        if (err) *err = SDL_GetError();
        return false;
    }
    LOGI("AUDIO: dispositivo abierto %d Hz, %u canales, búfer %u frames", have.freq, have.channels, have.samples);
    return true;
}

void AudioDevice::close() {
    if (!dev_) return;
    SDL_CloseAudioDevice(dev_);  // espera a que termine el callback en curso
    dev_ = 0;
}

void SDLCALL AudioDevice::callback(void* user, Uint8* stream, int len) {
    auto* self = static_cast<AudioDevice*>(user);
    if (len <= 0) return;
    const std::size_t bytes = static_cast<std::size_t>(len);
    constexpr std::size_t kFrameBytes = sizeof(std::int16_t) * kOutChannels;
    const std::size_t frames = bytes / kFrameBytes;
    // SDL ya tiene el dispositivo bloqueado durante el callback
    self->mixer_.mix(reinterpret_cast<std::int16_t*>(stream), frames);
    if (const std::size_t rest = bytes - frames * kFrameBytes) std::memset(stream + frames * kFrameBytes, 0, rest);
}

bool AudioDevice::setMusic(std::unique_ptr<PcmSource> src, std::string* err) {
    DeviceLock lock(dev_);
    return mixer_.setMusic(std::move(src), err);
}

void AudioDevice::playSfx(std::shared_ptr<const Pcm> sound) {
    if (!sound) return;
    std::string err;
    DeviceLock lock(dev_);
    if (!mixer_.play(std::move(sound), &err)) LOGE("AUDIO: %s", err.c_str());
}

void AudioDevice::setVolumes(int music, int sfx) {
    DeviceLock lock(dev_);
    mixer_.setMusicVolume(music);
    mixer_.setSfxVolume(sfx);
}

void AudioDevice::pause() {
    if (dev_) SDL_PauseAudioDevice(dev_, 1);
}

void AudioDevice::resume() {
    if (dev_) SDL_PauseAudioDevice(dev_, 0);
}

}  // namespace audio
