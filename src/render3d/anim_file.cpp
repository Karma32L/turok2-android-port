#include "anim_file.hpp"

#include <algorithm>
#include <cmath>

namespace render3d {
namespace {

std::optional<AnimSet> fail(std::string* err, const std::string& msg) {
    if (err) *err = msg;
    return std::nullopt;
}

constexpr std::size_t kFixedPose = 28;   // f32 t[3] + f32 q[4]
constexpr std::size_t kTransKey = 12;
constexpr std::size_t kRotKey = 16;
constexpr std::size_t kEvent = 50;
constexpr std::size_t kFrameWord = 4;    // u32 por frame sin identificar

bool readVec3(Cursor& c, Vec3& v, float limit) {
    return c.f32(v.x) && c.f32(v.y) && c.f32(v.z) && std::fabs(v.x) <= limit && std::fabs(v.y) <= limit &&
           std::fabs(v.z) <= limit;
}

// Cuaternión finito, no nulo y de componentes acotadas (los del juego son unitarios; el tope
// evita que dot(q, q) desborde a infinito y la normalización lo convierta en nulo), normalizado
constexpr float kMaxQuatComponent = 16.0f;

bool readQuat(Cursor& c, Quat& q) {
    if (!c.f32(q.x) || !c.f32(q.y) || !c.f32(q.z) || !c.f32(q.w)) return false;
    for (float v : {q.x, q.y, q.z, q.w})
        if (std::fabs(v) > kMaxQuatComponent) return false;
    if (!(dot(q, q) > 1e-6f)) return false;
    q = normalize(q);
    return true;
}

// Tabla de pistas por hueso: cada índice < numTracks o kNoTrack, y cada pista usada por un hueso
// como mucho (así lo hace el juego en los 1 865 clips; evita pistas compartidas inesperadas).
bool validTracks(const std::vector<std::uint16_t>& table, std::uint32_t numTracks) {
    std::vector<bool> used(numTracks, false);
    for (std::uint16_t t : table) {
        if (t == kNoTrack) continue;
        if (t >= numTracks || used[t]) return false;
        used[t] = true;
    }
    return true;
}

}  // namespace

void collectEvents(const AnimClip& clip, float t0, float dt, bool loop, std::vector<const AnimEvent*>& out) {
    if (!std::isfinite(dt) || dt <= 0.0f || !std::isfinite(t0) || clip.events.empty()) return;
    const float dur = clip.duration();
    t0 = std::max(t0, 0.0f);
    auto range = [&](float a, float b) {
        for (const AnimEvent& ev : clip.events) {
            const float t = static_cast<float>(ev.frame) / clip.fps;
            if (t >= a && t < b) out.push_back(&ev);
        }
    };
    if (!loop) {
        if (t0 < dur) range(t0, std::min(t0 + dt, dur));
        return;
    }
    t0 = std::fmod(t0, dur);
    const float t1 = t0 + std::min(dt, dur);  // como mucho una vuelta
    range(t0, std::min(t1, dur));
    if (t1 > dur) range(0.0f, t1 - dur);
}

const AnimClip* AnimSet::find(std::uint32_t id) const {
    for (const AnimClip& c : clips)
        if (c.id == id) return &c;
    return nullptr;
}

std::optional<AnimSet> parseAnimSet(ByteView bytes, std::string* err, const AnimLimits& limits) {
    if (bytes.size > limits.maxBytes) return fail(err, ".anim demasiado grande");
    Cursor c(bytes);
    std::uint32_t version = 0, numClips = 0;
    if (!c.u32(version) || !c.u32(numClips)) return fail(err, "cabecera truncada");
    if (version != kAnimVersion) return fail(err, "versión de .anim no soportada");
    if (numClips == 0 || numClips > limits.maxClips) return fail(err, "número de clips fuera de rango");

    AnimSet set;
    set.clips.resize(numClips);
    for (std::uint32_t ci = 0; ci < numClips; ++ci) {
        AnimClip& clip = set.clips[ci];
        const std::string where = "clip " + std::to_string(ci) + ": ";
        std::uint32_t bones = 0, bones2 = 0, numTrans = 0, numRot = 0, numEvents = 0, flags = 0;
        std::uint16_t h0 = 0, h1 = 0;
        ByteView unk;  // f32 sin identificar: se salta sin exigir finitud (no se usa)
        if (!c.u32(clip.id) || !c.u32(clip.frames) || !c.u32(bones) || !c.u32(bones2) || !c.u32(numTrans) ||
            !c.u32(numRot) || !c.u32(numEvents) || !c.take(4, unk) || !c.u32(flags) || !c.u16(h0) ||
            !c.f32(clip.fps) || !c.u16(h1)) {
            return fail(err, where + "cabecera truncada o con valores no finitos");
        }
        if (bones == 0 || bones > limits.maxBones || bones != bones2) return fail(err, where + "huesos fuera de rango");
        if (ci == 0) set.bones = bones;
        if (bones != set.bones) return fail(err, where + "huesos distintos entre clips");
        if (clip.frames == 0 || clip.frames > limits.maxFrames) return fail(err, where + "frames fuera de rango");
        if (!(clip.fps > 0.0f) || clip.fps > limits.maxFps) return fail(err, where + "fps fuera de rango");
        if (numTrans > bones || numRot > bones || numEvents > limits.maxEvents) {
            return fail(err, where + "número de pistas fuera de rango");
        }

        // Tamaño del resto del clip en 64 bits ANTES de reservar nada
        const std::uint64_t need = std::uint64_t{bones} * (4 + kFixedPose) + std::uint64_t{clip.frames} * kFrameWord +
                                   std::uint64_t{numTrans} * clip.frames * kTransKey +
                                   std::uint64_t{numRot} * clip.frames * kRotKey + std::uint64_t{numEvents} * kEvent;
        if (need > c.remaining()) return fail(err, where + "datos truncados");

        clip.transTrack.resize(bones);
        clip.rotTrack.resize(bones);
        for (std::uint32_t b = 0; b < bones; ++b) {
            c.u16(clip.transTrack[b]);  // no pueden fallar: el tamaño ya se comprobó
            c.u16(clip.rotTrack[b]);
        }
        if (!validTracks(clip.transTrack, numTrans) || !validTracks(clip.rotTrack, numRot)) {
            return fail(err, where + "índice de pista inválido");
        }
        clip.fixedPos.resize(bones);
        clip.fixedRot.resize(bones);
        for (std::uint32_t b = 0; b < bones; ++b) {
            if (!readVec3(c, clip.fixedPos[b], limits.maxCoord) || !readQuat(c, clip.fixedRot[b])) {
                return fail(err, where + "pose fija inválida");
            }
        }
        ByteView skip;
        c.take(std::size_t{clip.frames} * kFrameWord, skip);
        clip.trans.resize(std::size_t{numTrans} * clip.frames);
        for (Vec3& v : clip.trans)
            if (!readVec3(c, v, limits.maxCoord)) return fail(err, where + "traslación inválida");
        clip.rots.resize(std::size_t{numRot} * clip.frames);
        for (Quat& q : clip.rots)
            if (!readQuat(c, q)) return fail(err, where + "rotación inválida");
        clip.events.resize(numEvents);
        for (AnimEvent& ev : clip.events) {
            ByteView e, skipPos;
            c.take(kEvent, e);  // no puede fallar: el tamaño ya se comprobó
            Cursor ec(e);
            ec.u16(ev.frame);
            ec.u16(ev.type);
            ec.take(2 + 12, skipPos);  // hueso y posición: sin usar
            for (float& p : ev.params) {
                ByteView raw;
                ec.take(4, raw);
                float v = 0.0f;
                p = raw.f32At(0, v) && std::isfinite(v) ? v : 0.0f;
            }
        }
    }
    if (c.remaining() != 0) return fail(err, "bytes sobrantes al final");
    return set;
}

}  // namespace render3d
