#include "player_controller.hpp"

#include <algorithm>
#include <cmath>

namespace physics {
namespace {

constexpr float kPi = 3.14159265358979f;
constexpr int kMaxSlides = 3;       // iteraciones de deslizamiento por paso
constexpr int kMaxWallSectors = 48;  // sectores examinados al separar el cilindro de las paredes
constexpr float kEyeLerp = 300.0f;   // u/s al agacharse/levantarse
constexpr float kSurfaceBand = 8.0f;

float clampUnit(float v) { return std::isfinite(v) ? std::clamp(v, -1.0f, 1.0f) : 0.0f; }

float wrapAngle(float a) {
    a = std::fmod(a + kPi, 2.0f * kPi);
    if (a < 0.0f) a += 2.0f * kPi;
    return a - kPi;
}

// Acerca v a target como mucho maxDelta (en módulo)
Vec2 approach(Vec2 v, Vec2 target, float maxDelta) {
    const Vec2 d = target - v;
    const float len = length(d);
    if (len <= maxDelta || len <= 0.0f) return target;
    return v + d * (maxDelta / len);
}

float approach(float v, float target, float maxDelta) {
    if (v < target) return std::min(v + maxDelta, target);
    return std::max(v - maxDelta, target);
}

Vec2 xy(Vec3 v) { return {v.x, v.y}; }

}  // namespace

PlayerController::PlayerController(const SectorWorld* world, PlayerTuning tuning) : world_(world), t_(tuning) {
    eyeHeight_ = t_.standEye;
}

float PlayerController::yaw() const { return mode_ == MoveMode::Fly ? fly_.yaw() : yaw_; }
float PlayerController::pitch() const { return mode_ == MoveMode::Fly ? fly_.pitch() : pitch_; }

Vec3 PlayerController::eye() const {
    if (mode_ == MoveMode::Fly) return fly_.position();
    return feet_ + Vec3{0, 0, eyeHeight_};
}

render3d::Mat4 PlayerController::view() const {
    if (mode_ == MoveMode::Fly) return fly_.view();
    return render3d::lookDir(eye(), render3d::viewForward(yaw_, pitch_));
}

int PlayerController::areaIndex() const {
    if (!world_ || sector_ == kNoSector) return -1;
    return world_->areaIndex(sector_);
}

bool PlayerController::eyeUnderwater() const {
    if (!world_) return false;
    const Vec3 e = eye();
    // En Fly el sector no se sigue: se busca el de debajo de la cámara
    const int s = mode_ == MoveMode::Fly ? world_->locate(xy(e), e.z, 0.0f) : sector_;
    return s != kNoSector && world_->isWater(s) && e.z < world_->waterZ(s);
}

void PlayerController::spawn(Vec3 feet, float yaw, float pitch) {
    if (!finite(feet)) feet = {};
    yaw_ = wrapAngle(std::isfinite(yaw) ? yaw : 0.0f);
    pitch_ = std::clamp(std::isfinite(pitch) ? pitch : 0.0f, -render3d::FlyCamera::kMaxPitch,
                        render3d::FlyCamera::kMaxPitch);
    vel_ = {};
    accum_ = 0.0f;
    crouched_ = false;
    eyeHeight_ = t_.standEye;
    feet_ = feet;
    sector_ = world_ ? world_->locate(xy(feet), feet.z, t_.stepUp) : kNoSector;
    if (sector_ == kNoSector) {
        mode_ = MoveMode::Fly;
        fly_.place(feet + Vec3{0, 0, t_.standEye}, yaw_, pitch_);
        return;
    }
    mode_ = MoveMode::Walk;
    const float f = world_->floorZ(sector_, xy(feet_));
    if (feet_.z - f <= t_.stepDown) feet_.z = f;  // el inicio del .map está ~10 u sobre el suelo
    lastValid_ = feet_;
    groundCheck();
    updateWaterMode();
}

bool PlayerController::setFly(bool fly) {
    if (fly) {
        if (mode_ != MoveMode::Fly) fly_.place(eye(), yaw_, pitch_);
        mode_ = MoveMode::Fly;
        return true;
    }
    if (mode_ != MoveMode::Fly) return true;
    if (!world_) return false;
    const Vec3 f = fly_.position() - Vec3{0, 0, t_.standEye};
    const int s = world_->locate(xy(f), f.z, t_.stepUp);
    if (s == kNoSector) return false;
    const float y = fly_.yaw(), p = fly_.pitch();
    spawn(f, y, p);
    return mode_ != MoveMode::Fly;
}

void PlayerController::update(const PlayerInput& in, float dt) {
    if (!std::isfinite(dt) || dt <= 0.0f) return;
    dt = std::min(dt, kMaxDt);
    if (mode_ == MoveMode::Fly) {
        render3d::FlyInput fi;
        fi.strafe = in.strafe;
        fi.forward = in.forward;
        fi.up = in.flyUp;
        fi.yawDelta = in.yawDelta;
        fi.pitchDelta = in.pitchDelta;
        fi.fast = in.fast;
        fly_.update(fi, dt);
        return;
    }
    // Orientación una vez por frame (respuesta inmediata); la física, a paso fijo
    if (std::isfinite(in.yawDelta)) yaw_ = wrapAngle(yaw_ + in.yawDelta);
    if (std::isfinite(in.pitchDelta)) {
        pitch_ = std::clamp(pitch_ + in.pitchDelta, -render3d::FlyCamera::kMaxPitch, render3d::FlyCamera::kMaxPitch);
    }
    accum_ += dt;
    while (accum_ >= kStep) {
        accum_ -= kStep;
        step(in);
    }
    const float targetEye = crouched_ ? t_.crouchEye : t_.standEye;
    eyeHeight_ = approach(eyeHeight_, targetEye, kEyeLerp * dt);
}

void PlayerController::step(const PlayerInput& in) {
    if (sector_ == kNoSector) relocate();
    if (sector_ == kNoSector) return;
    if (mode_ == MoveMode::Swim) stepSwim(in);
    else stepWalk(in);
    if (!finite(feet_) || !finite(vel_)) {  // nunca debería pasar: se vuelve al último punto bueno
        feet_ = lastValid_;
        vel_ = {};
        relocate();
    } else {
        lastValid_ = feet_;
    }
}

void PlayerController::relocate() {
    sector_ = world_ ? world_->locate(xy(feet_), feet_.z, t_.stepUp) : kNoSector;
    if (sector_ == kNoSector && world_) {
        feet_ = lastValid_;
        sector_ = world_->locate(xy(feet_), feet_.z, t_.stepUp);
    }
}

bool PlayerController::passable(int from, int to, Vec2 at) const {
    (void)from;
    const float f = world_->floorZ(to, at);
    bool ok = f <= feet_.z + t_.stepUp;
    // Nadando en la superficie se trepa a orillas bajas
    if (!ok && mode_ == MoveMode::Swim && atSurface() && world_->isWater(from)) {
        ok = f <= world_->waterZ(from) + t_.climbOut;
    }
    if (!ok) return false;
    if (const auto c = world_->ceilZ(to, at)) {
        if (*c - std::max(f, feet_.z) < height()) return false;
    }
    return true;
}

Vec2 PlayerController::moveHorizontal(Vec2 delta) {
    const Vec2 start = xy(feet_);
    if (!(length(delta) > 0.0f)) {
        pushOutOfWalls();
        pushOutOfObstacles();
        return xy(feet_) - start;
    }
    const SectorWorld::PassFn pass = [this](int a, int b, Vec2 at) { return passable(a, b, at); };
    // Sub-pasos más cortos que medio radio: no se cruza una esquina fina en un solo paso
    const int sub = std::clamp(static_cast<int>(std::ceil(length(delta) / (t_.radius * 0.5f))), 1, 16);
    const Vec2 part = delta * (1.0f / static_cast<float>(sub));
    for (int i = 0; i < sub; ++i) {
        Vec2 remaining = part;
        for (int k = 0; k < kMaxSlides && length(remaining) > 1e-4f; ++k) {
            const Vec2 from = xy(feet_);
            const auto tr = world_->trace(sector_, from, from + remaining, pass);
            feet_.x = tr.end.x;
            feet_.y = tr.end.y;
            sector_ = tr.sector;
            if (!tr.blocked) break;
            if (tr.wallEdge < 0) break;
            // Deslizamiento: lo que falta, proyectado sobre la dirección de la pared
            Vec2 a, b;
            world_->edge(sector_, tr.wallEdge, a, b);
            const Vec2 dir = b - a;
            const float len = length(dir);
            if (!(len > 0.0f)) break;
            const Vec2 u = dir * (1.0f / len);
            const Vec2 left = (from + remaining) - tr.end;
            remaining = u * dot(left, u);
        }
        // Al subir un escalón los pies se colocan en el nuevo suelo
        const float f = world_->floorZ(sector_, xy(feet_));
        if (feet_.z < f) feet_.z = f;
        pushOutOfWalls();
        pushOutOfObstacles();
    }
    return xy(feet_) - start;
}

void PlayerController::pushOutOfObstacles() {
    if (!obstacles_ || obstacles_->empty()) return;
    // Sub-pasos de medio radio (moveHorizontal): el cilindro no puede saltar al otro lado de una
    // cápsula, así que el empuje siempre lo devuelve a su lado
    Vec2 target = xy(feet_);
    const float top = feet_.z + height();
    for (const Obstacle& o : *obstacles_) {
        if (!affects(o, self_) || !overlapsZ(o, feet_.z, top)) continue;
        target = target + pushCircleOutOfSegment(target, t_.radius + o.radius, o.a, o.b);
    }
    const Vec2 d = target - xy(feet_);
    if (!(length(d) > 1e-4f)) return;
    // El empuje respeta las paredes (no saca el cilindro de la malla)
    const auto tr = world_->trace(sector_, xy(feet_), target,
                                  [this](int a, int b, Vec2 at) { return passable(a, b, at); });
    feet_.x = tr.end.x;
    feet_.y = tr.end.y;
    sector_ = tr.sector;
}

void PlayerController::pushOutOfWalls() {
    // Sectores alcanzables desde el actual por aristas transitables que tocan el círculo
    visited_.assign(1, sector_);
    for (std::size_t qi = 0; qi < visited_.size() && visited_.size() < kMaxWallSectors; ++qi) {
        const int s = visited_[qi];
        for (int e = 0; e < 3; ++e) {
            const int n = world_->link(s, e);
            if (n == kNoSector || std::find(visited_.begin(), visited_.end(), n) != visited_.end()) continue;
            Vec2 a, b;
            world_->edge(s, e, a, b);
            const Vec2 c = closestPointOnSegment2D(xy(feet_), a, b);
            if (length(c - xy(feet_)) >= t_.radius || !passable(s, n, c)) continue;
            visited_.push_back(n);
        }
    }
    Vec2 target = xy(feet_);
    for (int s : visited_) {
        for (int e = 0; e < 3; ++e) {
            Vec2 a, b;
            world_->edge(s, e, a, b);
            const int n = world_->link(s, e);
            const Vec2 c = closestPointOnSegment2D(target, a, b);
            if (length(c - target) >= t_.radius) continue;
            if (n != kNoSector && passable(s, n, c)) continue;
            const Vec2 push = pushCircleOutOfSegment(target, t_.radius, a, b);
            // Solo hacia el interior del sector dueño de la pared (a la izquierda de a→b)
            if (cross(b - a, push) <= 0.0f) continue;
            target = target + push;
        }
    }
    const Vec2 d = target - xy(feet_);
    if (!(length(d) > 1e-4f)) return;
    // La separación también respeta las paredes (sin deslizar)
    const auto tr = world_->trace(sector_, xy(feet_), target,
                                  [this](int a, int b, Vec2 at) { return passable(a, b, at); });
    feet_.x = tr.end.x;
    feet_.y = tr.end.y;
    sector_ = tr.sector;
}

void PlayerController::groundCheck() {
    const Vec2 p = xy(feet_);
    const float f = world_->floorZ(sector_, p);
    if (feet_.z <= f) {
        feet_.z = f;
        if (vel_.z < 0.0f) vel_.z = 0.0f;
        onGround_ = true;
    } else if (onGround_ && vel_.z <= 0.0f && feet_.z - f <= t_.stepDown) {
        feet_.z = f;  // bajando una rampa o un escalón: pegado al suelo
        vel_.z = 0.0f;
    } else {
        onGround_ = false;
    }
    if (const auto c = world_->ceilZ(sector_, p)) {
        if (feet_.z + height() > *c) {
            feet_.z = std::max(f, *c - height());
            if (vel_.z > 0.0f) vel_.z = 0.0f;
        }
    }
}

bool PlayerController::atSurface() const {
    return sector_ != kNoSector && world_->isWater(sector_) &&
           feet_.z >= world_->waterZ(sector_) - t_.floatDepth - kSurfaceBand;
}

void PlayerController::updateWaterMode() {
    const bool water = world_->isWater(sector_);
    const float depth = water ? world_->waterZ(sector_) - feet_.z : 0.0f;
    if (mode_ == MoveMode::Walk && water && depth > t_.swimDepth) {
        mode_ = MoveMode::Swim;
        crouched_ = false;
        onGround_ = false;
        vel_.z *= 0.3f;  // el agua frena la caída
    } else if (mode_ == MoveMode::Swim && (!water || depth < t_.swimDepth - 10.0f)) {
        mode_ = MoveMode::Walk;
        groundCheck();
    }
}

void PlayerController::stepWalk(const PlayerInput& in) {
    // Agacharse; al soltar solo se levanta si cabe bajo el techo
    if (in.crouch) {
        crouched_ = true;
    } else if (crouched_) {
        const auto c = world_->ceilZ(sector_, xy(feet_));
        if (!c || *c - feet_.z >= t_.standHeight) crouched_ = false;
    }
    const Vec2 fwd{std::cos(yaw_), std::sin(yaw_)};
    const Vec2 right{std::sin(yaw_), -std::cos(yaw_)};
    Vec2 wish = fwd * clampUnit(in.forward) + right * clampUnit(in.strafe);
    if (length(wish) > 1.0f) wish = wish * (1.0f / length(wish));
    const float speed = crouched_ ? t_.crouchSpeed : t_.walkSpeed;
    Vec2 v{vel_.x, vel_.y};
    v = approach(v, wish * speed, (onGround_ ? t_.groundAccel : t_.airAccel) * kStep);

    if (onGround_ && in.jump && !crouched_) {
        vel_.z = t_.jumpSpeed;
        onGround_ = false;
    } else if (!onGround_) {
        vel_.z = std::max(vel_.z - t_.gravity * kStep, -t_.maxFallSpeed);
    }

    const Vec2 moved = moveHorizontal(v * kStep);
    // Contra una pared la velocidad no se acumula: queda la que realmente se movió
    const Vec2 wanted = v * kStep;
    if (length(moved - wanted) > 1e-3f) v = moved * (1.0f / kStep);
    vel_.x = v.x;
    vel_.y = v.y;

    feet_.z += vel_.z * kStep;
    groundCheck();
    updateWaterMode();
}

void PlayerController::stepSwim(const PlayerInput& in) {
    const float wz = world_->waterZ(sector_);
    const bool under = feet_.z + eyeHeight_ < wz;
    // Bajo el agua se nada hacia donde se mira (incluido el pitch); en la superficie, en horizontal
    const Vec3 fwd = under ? render3d::viewForward(yaw_, pitch_) : Vec3{std::cos(yaw_), std::sin(yaw_), 0.0f};
    const Vec3 right{std::sin(yaw_), -std::cos(yaw_), 0.0f};
    const float upIn = (in.jump ? 1.0f : 0.0f) - (in.crouch ? 1.0f : 0.0f);
    Vec3 wish = fwd * clampUnit(in.forward) + right * clampUnit(in.strafe) + Vec3{0, 0, upIn};
    const float len = render3d::length(wish);
    if (len > 1.0f) wish = wish * (1.0f / len);
    const Vec3 target = wish * t_.swimSpeed;
    const float dv = t_.swimAccel * kStep;
    Vec2 v = approach(Vec2{vel_.x, vel_.y}, Vec2{target.x, target.y}, dv);
    const bool vertical = std::fabs(target.z) > 1e-3f;
    vel_.z = approach(vel_.z, vertical ? target.z : t_.riseSpeed, dv);

    const Vec2 moved = moveHorizontal(v * kStep);
    if (length(moved - v * kStep) > 1e-3f) v = moved * (1.0f / kStep);
    vel_.x = v.x;
    vel_.y = v.y;

    // Puede haber trepado a la orilla (otro sector): el agua es la del sector actual
    if (!world_->isWater(sector_)) {
        updateWaterMode();
        return;
    }
    feet_.z += vel_.z * kStep;
    const float surface = world_->waterZ(sector_) - t_.floatDepth;
    if (feet_.z > surface) {  // flota: no sale del agua nadando hacia arriba
        feet_.z = surface;
        if (vel_.z > 0.0f) vel_.z = 0.0f;
    }
    const Vec2 p = xy(feet_);
    const float f = world_->floorZ(sector_, p);
    if (feet_.z < f) {
        feet_.z = f;
        if (vel_.z < 0.0f) vel_.z = 0.0f;
    }
    if (const auto c = world_->ceilZ(sector_, p)) {
        if (feet_.z + height() > *c) {
            feet_.z = std::max(f, *c - height());
            if (vel_.z > 0.0f) vel_.z = 0.0f;
        }
    }
    updateWaterMode();
}

}  // namespace physics
