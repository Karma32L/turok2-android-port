#include "fly_camera.hpp"

#include <algorithm>
#include <cmath>

namespace render3d {
namespace {

constexpr float kPi = 3.14159265358979f;

float clampUnit(float v) { return std::clamp(v, -1.0f, 1.0f); }

float wrapAngle(float a) {
    a = std::fmod(a + kPi, 2.0f * kPi);
    if (a < 0.0f) a += 2.0f * kPi;
    return a - kPi;
}

bool finite(const FlyInput& in) {
    return std::isfinite(in.strafe) && std::isfinite(in.forward) && std::isfinite(in.up) &&
           std::isfinite(in.yawDelta) && std::isfinite(in.pitchDelta);
}

}  // namespace

void FlyCamera::place(Vec3 pos, float yaw, float pitch) {
    pos_ = pos;
    yaw_ = wrapAngle(std::isfinite(yaw) ? yaw : 0.0f);
    pitch_ = std::clamp(std::isfinite(pitch) ? pitch : 0.0f, -kMaxPitch, kMaxPitch);
}

void FlyCamera::update(const FlyInput& in, float dt) {
    if (!finite(in) || !std::isfinite(dt) || dt <= 0.0f) return;
    dt = std::min(dt, kMaxDt);

    // Primero la orientación, para que el movimiento siga a donde se mira en este frame
    yaw_ = wrapAngle(yaw_ + in.yawDelta);
    pitch_ = std::clamp(pitch_ + in.pitchDelta, -kMaxPitch, kMaxPitch);

    const Vec3 fwd = viewForward(yaw_, pitch_);
    const Vec3 right{std::sin(yaw_), -std::cos(yaw_), 0.0f};
    Vec3 move = fwd * clampUnit(in.forward) + right * clampUnit(in.strafe) + Vec3{0, 0, clampUnit(in.up)};
    // Diagonales no más rápidas que los ejes
    const float len = length(move);
    if (len > 1.0f) move = move * (1.0f / len);
    pos_ = pos_ + move * (speed * (in.fast ? fastFactor : 1.0f) * dt);
}

}  // namespace render3d
