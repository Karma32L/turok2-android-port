#include "weapon_view.hpp"

#include <algorithm>
#include <cmath>

namespace game {
namespace {

constexpr float kTwoPi = 6.28318531f;
constexpr float kMaxDt = 0.1f;

// Aproximación exponencial de `cur` a `target` con tasa `rate` (1/s)
float approach(float cur, float target, float rate, float dt) { return target + (cur - target) * std::exp(-rate * dt); }

}  // namespace

WeaponView::WeaponView(const render3d::AnimSet* anims, int ammo, WeaponTuning tuning)
    : anims_(anims), t_(tuning), ammo_(std::max(ammo, 0)) {
    enter(WeaponState::Enter);
}

const render3d::AnimClip* WeaponView::clipFor(WeaponState s) const {
    if (!anims_ || anims_->clips.empty()) return nullptr;
    std::uint32_t id = kAnimWpnIdle;
    switch (s) {
    case WeaponState::Enter: id = kAnimWpnEnter; break;
    case WeaponState::Idle: id = kAnimWpnIdle; break;
    case WeaponState::Run: id = kAnimWpnRun; break;
    case WeaponState::Charge: id = kAnimWpnCharge; break;
    case WeaponState::Fire: id = kAnimWpnAttack1; break;
    }
    if (const render3d::AnimClip* c = anims_->find(id)) return c;
    if (const render3d::AnimClip* c = anims_->find(kAnimWpnIdle)) return c;
    return &anims_->clips.front();
}

void WeaponView::enter(WeaponState s) {
    state_ = s;
    clip_ = clipFor(s);
    time_ = 0.0f;
    loops_ = s == WeaponState::Idle || s == WeaponState::Run;  // Charge se queda en el último frame
}

bool WeaponView::clipFinished() const { return !clip_ || time_ >= clip_->duration(); }

WeaponState WeaponView::moveState() const {
    return onGround_ && speed_ > t_.runSpeed ? WeaponState::Run : WeaponState::Idle;
}

void WeaponView::update(const WeaponInput& in, float dt) {
    fired_ = false;
    events_.clear();
    if (!std::isfinite(dt) || dt <= 0.0f) return;
    dt = std::min(dt, kMaxDt);
    speed_ = std::isfinite(in.speed) ? std::max(in.speed, 0.0f) : 0.0f;
    onGround_ = in.onGround;

    // Eventos del tramo [time_, time_ + dt) del clip actual (los del frame 0 de un clip recién
    // empezado salen en el update siguiente al cambio de estado)
    if (clip_) render3d::collectEvents(*clip_, time_, dt, loops_, events_);
    time_ += dt;
    if (clip_) {
        // En bucle se envuelve; sin bucle basta con no crecer sin límite pasado el final
        time_ = loops_ ? std::fmod(time_, clip_->duration()) : std::min(time_, clip_->duration() + 1.0f);
    }

    switch (state_) {
    case WeaponState::Enter:
        if (clipFinished()) enter(moveState());
        break;
    case WeaponState::Idle:
    case WeaponState::Run:
        if (in.fireHeld && ammo_ > 0) {
            charge_ = 0.0f;
            enter(WeaponState::Charge);
        } else if (moveState() != state_) {
            const float keep = time_;  // al alternar idle/run se conserva la fase si cabe
            enter(moveState());
            if (clip_) time_ = std::fmod(keep, clip_->duration());
        }
        break;
    case WeaponState::Charge:
        charge_ += dt;
        if (!in.fireHeld) {
            --ammo_;
            fired_ = true;
            lastCharge_ = charge_;
            enter(WeaponState::Fire);
        }
        break;
    case WeaponState::Fire:
        if (clipFinished()) enter(moveState());
        break;
    }

    // Balanceo: peso hacia 1 al moverse por el suelo; la fase avanza con la distancia
    const bool moving = onGround_ && speed_ > t_.runSpeed;
    bobWeight_ = approach(bobWeight_, moving ? 1.0f : 0.0f, t_.bobBlendRate, dt);
    if (onGround_) bobPhase_ = std::fmod(bobPhase_ + speed_ * dt / t_.bobStride * kTwoPi, kTwoPi);

    // Inercia: el arma se retrasa respecto al giro (girar a la izquierda la desplaza a la derecha)
    const float yawRate = std::isfinite(in.yawRate) ? in.yawRate : 0.0f;
    const float pitchRate = std::isfinite(in.pitchRate) ? in.pitchRate : 0.0f;
    const float tx = std::clamp(yawRate * t_.swayPerRad, -t_.swayMax, t_.swayMax);
    const float ty = std::clamp(-pitchRate * t_.swayPerRad, -t_.swayMax, t_.swayMax);
    swayX_ = approach(swayX_, tx, t_.swayRate, dt);
    swayY_ = approach(swayY_, ty, t_.swayRate, dt);
}

render3d::Vec3 WeaponView::offset() const {
    // Lateral a la frecuencia del paso; vertical al doble (cada pisada) y siempre hacia abajo
    const float side = std::sin(bobPhase_) * t_.bobSide * bobWeight_;
    const float up = -std::fabs(std::sin(bobPhase_)) * t_.bobUp * bobWeight_;
    return {side + swayX_, up + swayY_, 0.0f};
}

}  // namespace game
