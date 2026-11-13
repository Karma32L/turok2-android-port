// IA básica de un enemigo de tierra (pensada para el Endtrail, kActor_AI_Endtrail = 201) sin GL ni
// SDL: se prueba en el host.
//
//   Idle (GROUND_IDLE1, bucle) ──ve al jugador / mensaje 1 / recibe daño──▶ Alert (GROUND_ALERT1 una
//   vez, mínimo alertSeconds) ─▶ Chase (GROUND_MOVE2 = correr, bucle) ─a menos de attackRadius─▶
//   Attack (GROUND_ATTACK_COMBAT1 una vez; daña al jugador en sus eventos MDAMAGE) ─▶ Chase …
//   Lejos de casa (leashRadius) o sin jugador ─▶ Return (GROUND_MOVE_SLOW1 = andar) ─▶ Idle.
//   Vida a 0 ─▶ Dead (GROUND_DEATH_NORMAL2 una vez; deja de ser sólido y objetivo).
//
// El cuerpo es un physics::PlayerController (cilindro radius × height del kexWorldComponent) que
// recorre los sectores de la malla 2.5D: paredes, escalones, gravedad y obstáculos (puertas
// cerradas, el jugador). Persigue en línea recta con deslizamiento contra paredes (sin A*).
// Las velocidades salen del avance de la raíz en los clips de andar/correr (que se anula al dibujar).
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "level_actors.hpp"
#include "obstacle.hpp"
#include "player_controller.hpp"
#include "sector_world.hpp"

namespace combat {

using render3d::Vec3;

// IDs de scripts/animations.txt presentes en anims/Endtrail_0100.anim
constexpr std::uint32_t kAnimIdle = 1;          // ANIM_GROUND_IDLE1
constexpr std::uint32_t kAnimAlert = 200;       // ANIM_GROUND_ALERT1
constexpr std::uint32_t kAnimWalk = 2000;       // ANIM_GROUND_MOVE_SLOW1
constexpr std::uint32_t kAnimRun = 2011;        // ANIM_GROUND_MOVE2
constexpr std::uint32_t kAnimAttack = 4000;     // ANIM_GROUND_ATTACK_COMBAT1
constexpr std::uint32_t kAnimDeath = 7001;      // ANIM_GROUND_DEATH_NORMAL2

enum class AiState { Idle, Alert, Chase, Attack, Return, Dead };

const char* aiStateName(AiState s);

struct EnemyTuning {
    float sightRadius = 1024.0f;    // decisión del port (el .map no trae radio de visión)
    float attackRadius = 163.84f;   // attackCombatRadius (defs/actors/enemies.txt)
    float leashRadius = 2048.0f;    // leashRadius
    float homeRadius = 64.0f;       // al volver, distancia a casa para quedarse quieto
    float turnRate = 6.0f;          // rad/s
    float attackCone = 0.5f;        // cos del ángulo máximo para avanzar o golpear de frente
    float alertSeconds = 0.6f;      // GROUND_ALERT1 dura 0,13 s: se alarga hasta esto
    int meleeDamage = 9;            // hipótesis: MDAMAGE {2304, …} → 2304 / 256 = 9
    float walkSpeed = 0.0f, runSpeed = 0.0f;  // 0 = del avance de la raíz del clip (si no, 120 / 300)
};

// Lo que la IA necesita saber del mundo en cada update
struct AiContext {
    const physics::SectorWorld* world = nullptr;
    Vec3 player;          // pies
    float playerRadius = 30.0f, playerHeight = 120.0f;
    bool playerAlive = true;
    const std::vector<physics::Obstacle>* obstacles = nullptr;  // tapan la línea de visión (los fijos)
};

class EnemyAI {
public:
    // inst: instancia del ActorSet que se mueve y anima. owner: su dueño en la lista de obstáculos.
    EnemyAI(std::size_t inst, std::int32_t tid, int health, float radius, float height, render3d::ActorSet& set,
            const physics::SectorWorld* world, const std::vector<physics::Obstacle>* obstacles, int owner,
            EnemyTuning tuning = {});

    // Mensaje 1 (TRIGGER) del EventLink o daño: se despierta si estaba quieto o volviendo
    void wake();
    // Resta vida; true si muere con este golpe
    bool damage(int amount);
    // Evento de animación de su instancia (golpe MDAMAGE: daña al jugador si está al alcance)
    void onEvent(const render3d::AnimEvent& e, const AiContext& ctx, int& playerDamage);
    void update(float dt, const AiContext& ctx);

    AiState state() const { return state_; }
    int health() const { return health_; }
    bool alive() const { return state_ != AiState::Dead; }
    std::size_t instance() const { return inst_; }
    std::int32_t tid() const { return tid_; }
    Vec3 feet() const;
    float radius() const { return radius_; }
    float height() const { return height_; }
    float runSpeed() const { return runSpeed_; }
    float walkSpeed() const { return walkSpeed_; }
    bool canSee(const AiContext& ctx) const;

private:
    void enter(AiState s);
    // Gira hacia `target` y avanza a `speed` u/s si lo tiene delante
    void steer(Vec3 target, float speed, float dt);
    void stand(float dt, std::optional<Vec3> face);
    void syncInstance();

    std::size_t inst_;
    std::int32_t tid_;
    int health_;
    float radius_, height_;
    render3d::ActorSet& set_;
    EnemyTuning t_;
    std::optional<physics::PlayerController> body_;  // nullopt: sin malla de sectores (no se mueve)
    Vec3 home_, pos_;   // pos_: posición sin cuerpo
    float yaw_ = 0.0f;  // dirección de avance en el mundo (rad)
    float walkSpeed_ = 120.0f, runSpeed_ = 300.0f;
    AiState state_ = AiState::Idle;
    float timer_ = 0.0f;
};

}  // namespace combat
