// Flechas con trayectoria balística (sin GL ni SDL; se prueban en el host).
//
// Valores de particles/Arrow.particle (kParticle_Arrow = 86): velocity 3072 u/s y gravity −1632
// u/s²; scripts/weapons.txt (TurokBow) multiplica la velocidad por clamp(carga, 0,5, 1,5).
// Cada flecha avanza en sub-pasos de 1/120 s; en cada uno se busca el primer impacto del segmento
// recorrido contra:
//   - objetivos (cilindros verticales: barriles, enemigos) → se consume;
//   - obstáculos (cápsulas: puertas cerradas), paredes, suelo y techo de la malla de sectores →
//     se clava y desaparece a los stuckSeconds.
#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "math3d.hpp"
#include "obstacle.hpp"
#include "sector_world.hpp"

namespace combat {

using render3d::Vec3;

// Daño de una flecha normal. Hipótesis sin confirmar: el impacto de particles/Arrow.particle es
// SEvent_Damage {1280, 3840} y 3840 / 256 = 15 (el barril de 5 cae de un tiro, el Endtrail de 80 de 6)
constexpr int kArrowDamage = 15;

struct ArrowTuning {
    float speed = 3072.0f;      // Arrow.particle velocity
    float gravity = 1632.0f;    // Arrow.particle gravity (hacia −Z)
    float minCharge = 0.5f, maxCharge = 1.5f;  // TurokBow: escala de velocidad por tiempo tensado
    float maxFlight = 4.0f;     // s en vuelo antes de descartarla
    float stuckSeconds = 8.0f;  // s clavada antes de desaparecer
};

// Cilindro vertical que recibe impactos: base en `base`, de altura `height`
struct Target {
    Vec3 base;
    float radius = 0.0f, height = 0.0f;
    int id = 0;  // lo asigna el dueño de la lista
};

struct ProjectileHit {
    bool target = false;  // true: objetivo `id`; false: mundo (pared, suelo, techo u obstáculo)
    int id = -1;
    Vec3 point;
};

struct Arrow {
    Vec3 pos, vel;
    int sector = physics::kNoSector;
    float age = 0.0f;
    bool stuck = false;
};

// Primer t ∈ [0, 1] en que el segmento p0 → p1 entra en el cilindro (también si empieza dentro)
std::optional<float> segmentCylinder(Vec3 p0, Vec3 p1, Vec3 base, float radius, float height);
// t ∈ [0, 1] del punto de máximo acercamiento (XY) del segmento a la cápsula del obstáculo si la
// toca a una altura dentro de [zMin, zMax]; exacto para cualquier longitud de segmento
std::optional<float> segmentObstacle(Vec3 p0, Vec3 p1, const physics::Obstacle& o);

class Projectiles {
public:
    static constexpr std::size_t kMaxArrows = 32;  // al pasar, se descarta la más antigua
    static constexpr float kStep = 1.0f / 120.0f;

    explicit Projectiles(ArrowTuning tuning = {}) : t_(tuning) {}

    // Dispara desde origin en la dirección dir (no hace falta normalizarla); charge = s tensando
    void fire(Vec3 origin, Vec3 dir, float charge, const physics::SectorWorld* world);
    void update(float dt, const physics::SectorWorld* world, const std::vector<Target>& targets,
                const std::vector<physics::Obstacle>& obstacles, std::vector<ProjectileHit>& hits);

    const std::vector<Arrow>& arrows() const { return arrows_; }
    const ArrowTuning& tuning() const { return t_; }

private:
    // Avanza una flecha un sub-paso; true si sigue viva
    bool step(Arrow& a, float h, const physics::SectorWorld* world, const std::vector<Target>& targets,
              const std::vector<physics::Obstacle>& obstacles, std::vector<ProjectileHit>& hits);

    ArrowTuning t_;
    std::vector<Arrow> arrows_;
};

}  // namespace combat
