// Cámara libre (noclip) sin dependencias de plataforma: integra la entrada ya normalizada
// (sticks táctiles o del mando) en posición y orientación. Z del mundo hacia arriba.
#pragma once

#include "math3d.hpp"

namespace render3d {

struct FlyInput {
    float strafe = 0.0f;    // [-1, 1], positivo = derecha
    float forward = 0.0f;   // [-1, 1], positivo = hacia donde se mira (incluido el pitch)
    float up = 0.0f;        // [-1, 1], positivo = +Z del mundo
    float yawDelta = 0.0f;  // radianes ya escalados por el llamador; positivo = girar a la izquierda
    float pitchDelta = 0.0f;  // radianes; positivo = mirar arriba
    bool fast = false;
};

class FlyCamera {
public:
    static constexpr float kMaxPitch = 1.553343f;  // 89°
    static constexpr float kMaxDt = 0.1f;          // un tirón (pausa, carga) no teletransporta

    float speed = 600.0f;      // unidades del mapa por segundo
    float fastFactor = 3.0f;

    void place(Vec3 pos, float yaw, float pitch = 0.0f);
    // Valores no finitos en la entrada o en dt se ignoran (la cámara no queda en NaN)
    void update(const FlyInput& in, float dt);

    Vec3 position() const { return pos_; }
    float yaw() const { return yaw_; }
    float pitch() const { return pitch_; }
    Vec3 forward() const { return viewForward(yaw_, pitch_); }
    Mat4 view() const { return lookDir(pos_, forward()); }

private:
    Vec3 pos_;
    float yaw_ = 0.0f, pitch_ = 0.0f;
};

}  // namespace render3d
