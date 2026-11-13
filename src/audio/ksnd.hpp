// Shaders de sonido .ksnd (texto, sounds/shaders/*.ksnd) y banco de sonidos por id.
//
//   sounds[N] =
//   {
//       volume_min = 100 | volume_max = 100 | type = 453 | weight = 33
//       { wavefile = "sounds/waves/XPLOMED2.WAV" ... }      × N (secuencia con retardos)
//   }
//
// El id que usan los eventos de animación y los scripts es `type`; varios .ksnd pueden compartir
// el mismo (variantes que el juego elige al azar por `weight`). El port toma el primer wavefile de
// cada .ksnd y alterna entre las variantes de un id. Datos no confiables: tamaño, id y ruta acotados.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "pcm.hpp"

namespace audio {

constexpr std::size_t kMaxKsndBytes = 64u << 10;  // máximo real: ~10 KB
constexpr std::uint32_t kMaxSoundId = 1u << 20;

struct KsndInfo {
    std::uint32_t id = 0;  // campo `type`
    std::string wav;       // primer wavefile (ruta relativa sin "..", termina en .wav)
};

std::optional<KsndInfo> parseKsnd(std::string_view text, std::string* err = nullptr);

class SoundBank {
public:
    using Bytes = std::vector<std::uint8_t>;
    using Reader = std::function<std::optional<Bytes>(const std::string&)>;

    static constexpr std::size_t kMaxIds = 1024;
    static constexpr std::size_t kMaxVariants = 16;

    explicit SoundBank(Reader read) : read_(std::move(read)) {}

    // Registra un .ksnd del lector. false (con motivo) si no se lee, no es válido o no cabe.
    bool addShader(const std::string& path, std::string* err = nullptr);

    // PCM del id (carga perezosa del .wav; los fallos se recuerdan). Alterna entre variantes.
    // nullptr si el id no está registrado o ninguna variante se puede cargar.
    std::shared_ptr<const Pcm> get(std::uint32_t id);

    bool has(std::uint32_t id) const { return byId_.count(id) != 0; }
    std::size_t size() const { return byId_.size(); }

private:
    struct Variant {
        std::string wav;
        std::shared_ptr<const Pcm> pcm;
        bool failed = false;
    };
    struct Entry {
        std::vector<Variant> variants;
        std::size_t next = 0;
    };

    Reader read_;
    std::unordered_map<std::uint32_t, Entry> byId_;
};

}  // namespace audio
