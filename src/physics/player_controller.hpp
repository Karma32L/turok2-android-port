// Controlador del jugador sobre el SectorWorld: un cilindro vertical (radio, altura) con
// tres modos:
//   - Walk: gravedad, suelo y rampas interpolados, escalones de hasta stepUp, deslizamiento
//           contra paredes, techo, salto y agacharse;
//   - Swim: en áreas de agua más profundas que swimDepth; flota en la superficie, sube con
//           saltar y bucea con agacharse (o mirando abajo); sale trepando a orillas bajas;
//   - Fly:  cámara libre de depuración sin colisión (render3d::FlyCamera).
// Paso fijo de 1/60 s (independiente de los FPS) y dt acotado: un tirón no atraviesa paredes.
// Sin GL ni SDL: testeable en el host con mapas sintéticos.
#pragma once

#include <vector>

#include "fly_camera.hpp"
#include "obstacle.hpp"
#include "sector_world.hpp"

namespace physics {

enum class MoveMode { Walk, Swim, Fly };

struct PlayerInput {
    float strafe = 0.0f;      // [-1, 1], positivo = derecha
    float forward = 0.0f;     // [-1, 1], positivo = adelante
    float yawDelta = 0.0f;    // radianes; positivo = girar a la izquierda (como FlyCamera)
    float pitchDelta = 0.0f;  // radianes; positivo = mirar arriba
    bool jump = false;        // mantenido: saltar / nadar arriba
    bool crouch = false;      // mantenido: agacharse / bucear
    float flyUp = 0.0f;       // [-1, 1], solo en Fly
    bool fast = false;        // solo en Fly
};

// Valores en unidades del mapa. Las alturas de ojos son las de defs/postureInfo.txt; el
// resto son aproximaciones a ajustar jugando en el dispositivo.
struct PlayerTuning {
    float radius = 30.0f;
    float standHeight = 120.0f, crouchHeight = 70.0f;
    float standEye = 100.0f, crouchEye = 50.0f;  // viewHeight de Player_Standing / _Crouching
    float stepUp = 40.0f;     // escalón máximo que se sube andando
    float stepDown = 40.0f;   // al bajar rampas/escaleras se pega al suelo hasta esta caída
    float walkSpeed = 480.0f, crouchSpeed = 200.0f, swimSpeed = 260.0f;
    float groundAccel = 3000.0f, airAccel = 700.0f, swimAccel = 900.0f;
    float gravity = 2000.0f, maxFallSpeed = 4000.0f;
    float jumpSpeed = 560.0f;    // ≈ 78 u de altura de salto
    float riseSpeed = 60.0f;     // flotabilidad: sin entrada vertical se sube despacio a la superficie
    float swimDepth = 60.0f;     // los pies a esta profundidad (o más) → nadar
    float floatDepth = 72.0f;    // pies bajo la superficie al flotar (ojos 28 u por encima)
    float climbOut = 200.0f;     // altura sobre el agua de una orilla a la que se trepa nadando
};

class PlayerController {
public:
    static constexpr float kStep = 1.0f / 60.0f;
    static constexpr float kMaxDt = 0.1f;

    // world puede ser nulo (nivel sin colisión): solo hay modo Fly
    explicit PlayerController(const SectorWorld* world, PlayerTuning tuning = {});

    // Coloca los pies en `feet` (se pega al suelo si está a menos de stepDown). Sin sector
    // válido debajo queda en modo Fly.
    void spawn(Vec3 feet, float yaw, float pitch = 0.0f);
    void update(const PlayerInput& in, float dt);

    // Obstáculos dinámicos (puertas, barriles, otros cuerpos). La lista la mantiene el llamador y
    // debe vivir más que el controlador (o desconectarse con nullptr); se ignoran los de `self`.
    void setObstacles(const std::vector<Obstacle>* obstacles, int self = kObstaclePlayer) {
        obstacles_ = obstacles;
        self_ = self;
    }

    // Cambiar a Fly siempre funciona; volver de Fly exige un sector bajo la cámara.
    bool setFly(bool fly);
    MoveMode mode() const { return mode_; }

    Vec3 feet() const { return feet_; }
    Vec3 eye() const;
    float yaw() const;
    float pitch() const;
    render3d::Mat4 view() const;
    Vec3 velocity() const { return vel_; }
    bool onGround() const { return onGround_; }
    bool crouched() const { return crouched_; }
    int sector() const { return sector_; }
    int areaIndex() const;  // -1 sin sector
    bool eyeUnderwater() const;
    const PlayerTuning& tuning() const { return t_; }

private:
    void step(const PlayerInput& in);
    void stepWalk(const PlayerInput& in);
    void stepSwim(const PlayerInput& in);
    float height() const { return crouched_ ? t_.crouchHeight : t_.standHeight; }
    bool atSurface() const;
    // Movimiento horizontal con deslizamiento y separación de paredes; devuelve el desplazamiento real
    Vec2 moveHorizontal(Vec2 delta);
    void pushOutOfWalls();
    void pushOutOfObstacles();
    bool passable(int from, int to, Vec2 at) const;
    void groundCheck();
    void updateWaterMode();
    void relocate();

    const SectorWorld* world_;
    PlayerTuning t_;
    MoveMode mode_ = MoveMode::Fly;
    render3d::FlyCamera fly_;
    Vec3 feet_, vel_, lastValid_;
    float yaw_ = 0.0f, pitch_ = 0.0f;
    float eyeHeight_ = 100.0f;  // suavizada al agacharse/levantarse
    float accum_ = 0.0f;
    int sector_ = kNoSector;
    bool onGround_ = false, crouched_ = false;
    std::vector<int> scratch_, visited_;
    const std::vector<Obstacle>* obstacles_ = nullptr;
    int self_ = kObstaclePlayer;
};

}  // namespace physics
