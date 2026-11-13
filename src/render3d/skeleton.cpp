#include "skeleton.hpp"

#include <cmath>

namespace render3d {
namespace {

struct Xform {
    Mat3 r;
    Vec3 t;
};

Vec3 bonePos(const AnimClip& clip, std::size_t b, const FrameSample& s) {
    const std::uint16_t tr = clip.transTrack[b];
    const std::size_t base = std::size_t{tr} * clip.frames;
    // Defensivo: un clip construido a mano con pistas incoherentes usa la pose fija
    if (tr == kNoTrack || base + clip.frames > clip.trans.size()) return clip.fixedPos[b];
    return lerp(clip.trans[base + s.f0], clip.trans[base + s.f1], s.alpha);
}

Quat boneRot(const AnimClip& clip, std::size_t b, const FrameSample& s) {
    const std::uint16_t rt = clip.rotTrack[b];
    const std::size_t base = std::size_t{rt} * clip.frames;
    if (rt == kNoTrack || base + clip.frames > clip.rots.size()) return clip.fixedRot[b];
    return nlerp(clip.rots[base + s.f0], clip.rots[base + s.f1], s.alpha);
}

}  // namespace

FrameSample sampleFrame(const AnimClip& clip, float time, bool loop) {
    FrameSample s;
    if (clip.frames <= 1 || !std::isfinite(time) || !(clip.fps > 0.0f)) return s;
    const float n = static_cast<float>(clip.frames);
    float pos = time * clip.fps;
    if (loop) {
        pos = std::fmod(pos, n);
        if (pos < 0.0f) pos += n;
    } else if (pos <= 0.0f) {
        return s;
    } else if (pos >= n - 1.0f) {
        s.f0 = s.f1 = clip.frames - 1;
        return s;
    }
    auto f0 = static_cast<std::uint32_t>(pos);
    if (f0 >= clip.frames) f0 = clip.frames - 1;  // redondeo de fmod en el borde
    s.f0 = f0;
    s.f1 = f0 + 1 < clip.frames ? f0 + 1 : (loop ? 0 : f0);
    s.alpha = pos - static_cast<float>(f0);
    if (s.alpha < 0.0f) s.alpha = 0.0f;
    if (s.alpha > 1.0f) s.alpha = 1.0f;
    return s;
}

void identityPose(std::size_t bones, std::vector<BoneMatrix>& out) { out.assign(bones, BoneMatrix{}); }

bool evaluatePose(const AnimClip& clip, const std::vector<std::int16_t>& parents, float time, bool loop,
                  std::vector<BoneMatrix>& out) {
    const std::size_t n = parents.size();
    if (clip.transTrack.size() != n || clip.rotTrack.size() != n || clip.fixedPos.size() != n ||
        clip.fixedRot.size() != n) {
        identityPose(n, out);
        return false;
    }
    const FrameSample s = sampleFrame(clip, time, loop);
    std::vector<Xform> global(n);
    for (std::size_t b = 0; b < n; ++b) {
        const Xform local{toMat3(boneRot(clip, b, s)), bonePos(clip, b, s)};
        const std::int16_t p = parents[b];
        if (p < 0) {
            global[b] = local;
        } else if (static_cast<std::size_t>(p) < b) {
            const Xform& g = global[static_cast<std::size_t>(p)];
            global[b] = {mul(g.r, local.r), g.t + mul(g.r, local.t)};
        } else {
            identityPose(n, out);
            return false;
        }
    }
    out.resize(n);
    for (std::size_t b = 0; b < n; ++b) {
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) out[b].row[i][j] = global[b].r.m[i][j];
        }
        out[b].row[0][3] = global[b].t.x;
        out[b].row[1][3] = global[b].t.y;
        out[b].row[2][3] = global[b].t.z;
    }
    return true;
}

}  // namespace render3d
