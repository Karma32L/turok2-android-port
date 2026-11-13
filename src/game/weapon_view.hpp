// Arma en primera persona (view-model) sin GL ni SDL: se prueba en el host.
//
// Máquina de estados del arco (kWpn_Bow, ver docs/FORMATS.md):
//   Enter (ANIM_WPN_ENTER, una vez) → Idle (ANIM_WPN_IDLE) ⇄ Run (ANIM_WPN_RUN) según la velocidad;
//   pulsar disparo con munición → Charge (ANIM_WPN_CHARGE, se mantiene tensado mientras se pulse);
//   soltar → Fire (ANIM_WPN_ATK1, una vez, gasta 1 flecha) → Idle/Run.
// Si un clip no existe en el .anim, el estado usa el de reposo. Sin proyectiles todavía.
//
// Balanceo (bob): fase proporcional a la distancia recorrida en el suelo, con un peso que sube y
// baja suavemente con la velocidad. Inercia (sway): el arma se retrasa al girar la cámara.
// El offset resultante está en el espacio de la vista: x derecha, y arriba, z hacia delante.
#pragma once

#include <cstdint>

#include "anim_file.hpp"
#include "math3d.hpp"

namespace game {

// IDs de scripts/animations.txt
constexpr std::uint32_t kAnimWpnEnter = 20000;
constexpr std::uint32_t kAnimWpnIdle = 20100;
constexpr std::uint32_t kAnimWpnRun = 20110;
constexpr std::uint32_t kAnimWpnCharge = 20200;
constexpr std::uint32_t kAnimWpnAttack1 = 20210;

enum class WeaponState { Enter, Idle, Run, Charge, Fire };

struct WeaponInput {
    float speed = 0.0f;     // velocidad horizontal (u/s)
    bool onGround = true;
    bool fireHeld = false;  // botón de disparo / RT mantenido
    float yawRate = 0.0f;   // rad/s (positivo = izquierda)
    float pitchRate = 0.0f; // rad/s (positivo = arriba)
};

struct WeaponTuning {
    float runSpeed = 120.0f;      // por encima: animación de correr y balanceo
    float bobStride = 260.0f;     // u recorridas por ciclo completo de balanceo
    float bobSide = 0.9f;         // amplitud lateral (u de vista)
    float bobUp = 0.6f;           // amplitud vertical
    float bobBlendRate = 6.0f;    // 1/s: rapidez con la que el peso del bob sigue a la velocidad
    float swayPerRad = 1.6f;      // u de desplazamiento por rad/s de giro
    float swayMax = 1.2f;
    float swayRate = 8.0f;        // 1/s
};

class WeaponView {
public:
    // anims puede ser nulo (sin animaciones: se dibuja en pose fija). Debe vivir más que el arma.
    WeaponView(const render3d::AnimSet* anims, int ammo, WeaponTuning tuning = {});

    void update(const WeaponInput& in, float dt);

    WeaponState state() const { return state_; }
    int ammo() const { return ammo_; }
    // true solo en el update en el que se soltó la cuerda (para sonido/proyectil)
    bool firedThisUpdate() const { return fired_; }

    // Clip y tiempo a reproducir (nullptr si no hay animaciones)
    const render3d::AnimClip* clip() const { return clip_; }
    float clipTime() const { return time_; }
    bool clipLoops() const { return loops_; }

    render3d::Vec3 offset() const;  // bob + sway en espacio de vista

private:
    void enter(WeaponState s);
    const render3d::AnimClip* clipFor(WeaponState s) const;
    bool clipFinished() const;
    WeaponState moveState() const;

    const render3d::AnimSet* anims_;
    WeaponTuning t_;
    WeaponState state_ = WeaponState::Enter;
    const render3d::AnimClip* clip_ = nullptr;
    float time_ = 0.0f;
    bool loops_ = false;
    int ammo_ = 0;
    bool fired_ = false;
    float speed_ = 0.0f;
    bool onGround_ = true;
    float bobPhase_ = 0.0f, bobWeight_ = 0.0f;
    float swayX_ = 0.0f, swayY_ = 0.0f;
};

}  // namespace game
