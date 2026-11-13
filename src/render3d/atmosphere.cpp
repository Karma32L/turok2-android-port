#include "atmosphere.hpp"

#include <cmath>

namespace render3d {
namespace {

void rgb(const std::uint8_t in[4], float out[3]) {
    for (int i = 0; i < 3; ++i) out[i] = static_cast<float>(in[i]) / 255.0f;
}

}  // namespace

Fog defaultFog() { return Fog{}; }

Fog fogForArea(const CollisionArea& area, bool eyeUnderwater) {
    Fog f;
    if (eyeUnderwater && area.water()) {
        rgb(area.waterColor, f.color);
        f.start = 0.0f;
        f.end = kUnderwaterFogEnd;
        return f;
    }
    if (!(area.fogEnd > 0.0f)) return f;  // área sin niebla (el parser neutralizó valores raros)
    rgb(area.fogColor, f.color);
    f.end = area.fogEnd;
    f.start = (area.fogStart >= 0.0f && area.fogStart < area.fogEnd) ? area.fogStart : 0.0f;
    return f;
}

Fog approachFog(const Fog& current, const Fog& target, float dt, float tau) {
    if (!std::isfinite(dt) || dt <= 0.0f || !std::isfinite(tau)) return current;
    const float k = tau > 0.0f ? 1.0f - std::exp(-dt / tau) : 1.0f;
    Fog r;
    for (int i = 0; i < 3; ++i) r.color[i] = current.color[i] + (target.color[i] - current.color[i]) * k;
    r.start = current.start + (target.start - current.start) * k;
    r.end = current.end + (target.end - current.end) * k;
    return r;
}

void fogRange(const Fog& f, float out[2]) {
    if (std::isfinite(f.start) && std::isfinite(f.end) && f.end > f.start) {
        out[0] = f.start;
        out[1] = 1.0f / (f.end - f.start);
    } else {
        out[0] = 1.0e30f;
        out[1] = 0.0f;
    }
}

}  // namespace render3d
