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

struct AnimClip {
    std::uint32_t id = 0;  // ANIM_* de scripts/animations.txt
    std::uint32_t frames = 0;  // >= 1
    float fps = 15.0f;         // > 0
    std::vector<std::uint16_t> transTrack, rotTrack;  // por hueso: índice de pista o kNoTrack
    std::vector<Vec3> fixedPos;                        // por hueso (huesos sin pista)
    std::vector<Quat> fixedRot;
    std::vector<Vec3> trans;  // numTrans × frames (pista p, frame f → [p * frames + f])
    std::vector<Quat> rots;   // numRot × frames

    float duration() const { return static_cast<float>(frames) / fps; }
};

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
