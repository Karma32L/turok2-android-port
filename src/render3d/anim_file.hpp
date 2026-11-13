// Parser de animaciones .anim v2 (secuencial, little-endian; ver docs/FORMATS.md):
//
//   u32 version (= 2) | u32 numClips
//   numClips × {
//     u32 id | u32 numFrames | u32 numBones | u32 numBones | u32 numTrans | u32 numRot | u32 numEvents
//     f32 | u32 flags | u16 | f32 fps | u16
//     numBones × { u16 pistaTraslación, u16 pistaRotación }       0xFFFF = pose fija
//     numBones × { f32 t[3], f32 q[4] }                           pose fija (28 B)
//     numFrames × u32                                             sin identificar
//     numTrans × numFrames × f32[3] | numRot × numFrames × f32[4] | numEvents × 50 B
//   }
// Todos los clips de un archivo tienen los mismos huesos. Floats finitos; los cuaterniones se
// normalizan al cargar. Tamaños comprobados en 64 bits antes de reservar.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "byte_view.hpp"
#include "math3d.hpp"

namespace render3d {

constexpr std::uint16_t kNoTrack = 0xFFFF;

// Evento de un clip (50 B en disco): u16 frame | u16 tipo | i16 hueso | f32 pos[3] | f32 params[8].
// Tipos = turokEventActions de scripts/actions.txt; los que usa el port:
enum AnimEventType : std::uint16_t {
    kEvPlaySfx = 0,           // params[0] = id de sonido (campo `type` de sounds/shaders/*.ksnd)
    kEvParticle = 1,          // params[0] = id de partícula (sin implementar)
    kEvMeleeDamage = 3,       // MDAMAGE: golpe cuerpo a cuerpo (params sin descifrar)
    kEvFireProjectile = 11,   // params[0] = id de partícula proyectil (86 = flecha)
    kEvRumble = 21,
    kEvSfxOnModel = 23,       // como kEvPlaySfx, desde el modelo
    kEvFootstepSfx = 29,      // sonido según material del suelo (sin implementar)
};

struct AnimEvent {
    std::uint16_t frame = 0, type = 0;
    float params[8] = {0, 0, 0, 0, 0, 0, 0, 0};  // los no finitos se guardan como 0

    std::uint32_t soundId() const { return params[0] >= 0.0f && params[0] < 1.0e6f ? static_cast<std::uint32_t>(params[0]) : 0; }
};

struct AnimClip {
    std::uint32_t id = 0;  // ANIM_* de scripts/animations.txt
    std::uint32_t frames = 0;  // >= 1
    float fps = 15.0f;         // > 0
    std::vector<std::uint16_t> transTrack, rotTrack;  // por hueso: índice de pista o kNoTrack
    std::vector<Vec3> fixedPos;                        // por hueso (huesos sin pista)
    std::vector<Quat> fixedRot;
    std::vector<Vec3> trans;  // numTrans × frames (pista p, frame f → [p * frames + f])
    std::vector<Quat> rots;   // numRot × frames
    std::vector<AnimEvent> events;  // en el orden del archivo (no siempre ordenados por frame)

    float duration() const { return static_cast<float>(frames) / fps; }
};

// Eventos cuyo instante (frame / fps) cae en [t0, t0 + dt). Con bucle, el intervalo que pasa del
// final continúa desde 0 (como mucho una vuelta); sin bucle se corta en duration(). dt <= 0 o no
// finito: ninguno.
void collectEvents(const AnimClip& clip, float t0, float dt, bool loop, std::vector<const AnimEvent*>& out);

struct AnimSet {
    std::uint32_t bones = 0;
    std::vector<AnimClip> clips;

    const AnimClip* find(std::uint32_t id) const;
};

struct AnimLimits {
    std::size_t maxBytes = 16u << 20;
    std::uint32_t maxClips = 256;    // máximo real: 65
    std::uint32_t maxBones = 128;
    std::uint32_t maxFrames = 4096;  // máximo real: 500
    std::uint32_t maxEvents = 1024;  // máximo real: 79
    float maxCoord = 1.0e6f;
    float maxFps = 240.0f;
};

constexpr std::uint32_t kAnimVersion = 2;

std::optional<AnimSet> parseAnimSet(ByteView bytes, std::string* err = nullptr, const AnimLimits& limits = {});

}  // namespace render3d
