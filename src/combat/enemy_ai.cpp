#include "enemy_ai.hpp"

#include <algorithm>
#include <cmath>

#include "projectile.hpp"

namespace combat {
namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kEyeFactor = 0.8f;  // altura de los ojos / altura del cuerpo (línea de visión)

float wrapAngle(float a) {
    a = std::fmod(a + kPi, 2.0f * kPi);
    if (a < 0.0f) a += 2.0f * kPi;
    return a - kPi;
}

float dist2D(Vec3 a, Vec3 b) { return std::hypot(a.x - b.x, a.y - b.y); }

// Avance XY de la raíz (hueso 0) en el clip, en u/s del mundo (escala de la instancia)
float rootSpeed(const render3d::ActorSet& set, const render3d::ActorInstance& inst, std::uint32_t clipId) {
    const render3d::ActorModel& m = set.models[inst.model];
    if (!m.anims) return 0.0f;
    const render3d::AnimClip* c = m.anims->find(clipId);
    if (!c || c->frames < 2 || c->transTrack.empty() || c->transTrack[0] == render3d::kNoTrack) return 0.0f;
    const std::size_t base = std::size_t{c->transTrack[0]} * c->frames;
    const Vec3 a = c->trans[base], b = c->trans[base + c->frames - 1];
    const float speed = std::hypot(b.x - a.x, b.y - a.y) / c->duration() * std::fabs(inst.xf.scale.x);
    return std::isfinite(speed) && speed < 5000.0f ? speed : 0.0f;
}

}  // namespace

const char* aiStateName(AiState s) {
    switch (s) {
    case AiState::Idle: return "reposo";
    case AiState::Alert: return "alerta";
    case AiState::Chase: return "persecución";
    case AiState::Attack: return "ataque";
    case AiState::Return: return "regreso";
    case AiState::Dead: return "muerto";
    }
    return "?";
}

EnemyAI::EnemyAI(std::size_t inst, std::int32_t tid, int health, float radius, float height, render3d::ActorSet& set,
                 const physics::SectorWorld* world, const std::vector<physics::Obstacle>* obstacles, int owner,
                 EnemyTuning tuning)
    : inst_(inst), tid_(tid), health_(std::max(health, 1)), radius_(radius), height_(height), set_(set), t_(tuning) {
    render3d::ActorInstance& ai = set_.instances[inst_];
    ai.stripRootMotion = true;
    home_ = pos_ = ai.xf.pos;
    // El modelo avanza por −Y local (la raíz de andar/correr se mueve hacia −Y)
    yaw_ = std::atan2(-ai.xf.rot.m[1][1], -ai.xf.rot.m[0][1]);
    const float walk = rootSpeed(set_, ai, kAnimWalk), run = rootSpeed(set_, ai, kAnimRun);
    walkSpeed_ = t_.walkSpeed > 0.0f ? t_.walkSpeed : (walk > 1.0f ? walk : 120.0f);
    runSpeed_ = t_.runSpeed > 0.0f ? t_.runSpeed : (run > 1.0f ? run : 300.0f);
    if (world) {
        physics::PlayerTuning bt;
        bt.radius = radius_;
        bt.standHeight = bt.crouchHeight = height_;
        bt.walkSpeed = std::max(runSpeed_, walkSpeed_);
        body_.emplace(world, bt);
        body_->spawn(pos_, yaw_);
        if (body_->mode() != physics::MoveMode::Walk) body_.reset();  // fuera de la malla: quieto
        else body_->setObstacles(obstacles, owner);
    }
    enter(AiState::Idle);
    syncInstance();
}

Vec3 EnemyAI::feet() const { return body_ ? body_->feet() : pos_; }

void EnemyAI::enter(AiState s) {
    state_ = s;
    render3d::ActorInstance& ai = set_.instances[inst_];
    auto play = [&](std::uint32_t id, bool loop) {
        if (!set_.play(ai, id, loop)) return 1.0f;  // sin clip: el estado dura 1 s
        return set_.clipOf(ai)->duration();
    };
    switch (s) {
    case AiState::Idle: play(kAnimIdle, true); break;
    case AiState::Alert: timer_ = std::max(t_.alertSeconds, play(kAnimAlert, false)); break;
    case AiState::Chase: play(kAnimRun, true); break;
    case AiState::Attack: timer_ = play(kAnimAttack, false); break;
    case AiState::Return: play(kAnimWalk, true); break;
    case AiState::Dead: play(kAnimDeath, false); break;
    }
}

void EnemyAI::wake() {
    if (state_ == AiState::Idle || state_ == AiState::Return) enter(AiState::Alert);
}

bool EnemyAI::damage(int amount) {
    if (state_ == AiState::Dead || amount <= 0) return false;
    health_ -= amount;
    if (health_ <= 0) {
        health_ = 0;
        enter(AiState::Dead);
        return true;
    }
    wake();
    return false;
}

bool EnemyAI::canSee(const AiContext& ctx) const {
    if (!ctx.playerAlive) return false;
    const Vec3 me = feet();
    if (dist2D(me, ctx.player) > t_.sightRadius || std::fabs(ctx.player.z - me.z) > t_.sightRadius) return false;
    const Vec3 eye = me + Vec3{0, 0, height_ * kEyeFactor};
    const Vec3 target = ctx.player + Vec3{0, 0, ctx.playerHeight * kEyeFactor};
    // Paredes: la línea solo cruza a sectores en los que queda entre suelo y techo
    if (ctx.world && body_) {
        const physics::Vec2 from{eye.x, eye.y}, to{target.x, target.y};
        const float len = physics::length(to - from);
        const physics::SectorWorld::PassFn pass = [&](int, int next, physics::Vec2 at) {
            const float t = len > 1e-6f ? physics::length(at - from) / len : 1.0f;
            const float z = eye.z + (target.z - eye.z) * t;
            if (z < ctx.world->floorZ(next, at)) return false;
            const auto c = ctx.world->ceilZ(next, at);
            return !c || z <= *c;
        };
        if (ctx.world->trace(body_->sector(), from, to, pass).blocked) return false;
    }
    // Puertas cerradas y barriles tapan la vista
    if (ctx.obstacles) {
        for (const physics::Obstacle& o : *ctx.obstacles)
            if (o.owner == physics::kObstacleStatic && segmentObstacle(eye, target, o)) return false;
    }
    return true;
}

void EnemyAI::onEvent(const render3d::AnimEvent& e, const AiContext& ctx, int& playerDamage) {
    if (e.type != render3d::kEvMeleeDamage || state_ != AiState::Attack || !ctx.playerAlive) return;
    const Vec3 me = feet();
    const float d = dist2D(me, ctx.player);
    if (d > t_.attackRadius * 1.25f || std::fabs(ctx.player.z - me.z) > height_) return;
    const float facing = d > 1e-3f ? ((ctx.player.x - me.x) * std::cos(yaw_) + (ctx.player.y - me.y) * std::sin(yaw_)) / d
                                   : 1.0f;
    if (facing >= t_.attackCone) playerDamage += t_.meleeDamage;
}

void EnemyAI::steer(Vec3 target, float speed, float dt) {
    const Vec3 me = feet();
    const float desired = std::atan2(target.y - me.y, target.x - me.x);
    const float diff = wrapAngle(desired - yaw_);
    const float turn = std::clamp(diff, -t_.turnRate * dt, t_.turnRate * dt);
    const bool ahead = std::cos(diff - turn) >= t_.attackCone;
    if (!body_) {
        yaw_ = wrapAngle(yaw_ + turn);
        return;
    }
    physics::PlayerInput in;
    in.yawDelta = turn;
    in.forward = ahead ? std::clamp(speed / body_->tuning().walkSpeed, 0.0f, 1.0f) : 0.0f;
    body_->update(in, dt);
    yaw_ = body_->yaw();
}

void EnemyAI::stand(float dt, std::optional<Vec3> face) {
    float turn = 0.0f;
    if (face) {
        const Vec3 me = feet();
        const float diff = wrapAngle(std::atan2(face->y - me.y, face->x - me.x) - yaw_);
        turn = std::clamp(diff, -t_.turnRate * dt, t_.turnRate * dt);
    }
    if (!body_) {
        yaw_ = wrapAngle(yaw_ + turn);
        return;
    }
    physics::PlayerInput in;  // sin avanzar: frena y sigue la gravedad
    in.yawDelta = turn;
    body_->update(in, dt);
    yaw_ = body_->yaw();
}

void EnemyAI::update(float dt, const AiContext& ctx) {
    if (!std::isfinite(dt) || dt <= 0.0f) return;
    dt = std::min(dt, 0.1f);
    const Vec3 me = feet();
    const float toPlayer = dist2D(me, ctx.player), toHome = dist2D(me, home_);
    switch (state_) {
    case AiState::Dead: stand(dt, std::nullopt); break;
    case AiState::Idle:
        if (canSee(ctx)) wake();
        stand(dt, std::nullopt);
        break;
    case AiState::Alert:
        timer_ -= dt;
        stand(dt, ctx.player);
        if (timer_ <= 0.0f) enter(ctx.playerAlive ? AiState::Chase : AiState::Return);
        break;
    case AiState::Chase:
        if (!ctx.playerAlive || toHome > t_.leashRadius) enter(AiState::Return);
        else if (toPlayer <= t_.attackRadius) enter(AiState::Attack);
        else steer(ctx.player, runSpeed_, dt);
        break;
    case AiState::Attack:
        timer_ -= dt;
        stand(dt, ctx.player);
        if (timer_ <= 0.0f) enter(AiState::Chase);
        break;
    case AiState::Return:
        if (canSee(ctx) && dist2D(ctx.player, home_) < t_.leashRadius * 0.75f) enter(AiState::Chase);
        else if (toHome <= t_.homeRadius) enter(AiState::Idle);
        else steer(home_, walkSpeed_, dt);
        break;
    }
    syncInstance();
}

void EnemyAI::syncInstance() {
    render3d::ActorInstance& ai = set_.instances[inst_];
    ai.xf.pos = feet();
    // Avance (cos yaw, sin yaw) = Rz(yaw + π/2) · (0, −1): el −Y del modelo
    ai.xf.rot = render3d::rotZ(yaw_ + kPi * 0.5f);
}

}  // namespace combat
