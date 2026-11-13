// Niebla del nivel a partir del área de colisión en la que está la cámara (sin GL: se
// prueba en el host). El renderer la aplica como niebla lineal por distancia.
//   - fuera del agua: color de niebla del área (@8) de fogStart (@24) a fogEnd (@16);
//   - con los ojos bajo el agua: color del agua del área (@12) y distancia corta;
//   - sin colisión o sin niebla: "niebla" muy lejana con el color de cielo por defecto.
// Al cambiar de área no salta: approachFog interpola exponencialmente.
#pragma once

#include "map_file.hpp"

namespace render3d {

struct Fog {
    float color[3] = {0.42f, 0.50f, 0.60f};
    float start = 1.0e5f, end = 2.0e5f;  // por defecto, más allá del plano lejano
};

constexpr float kUnderwaterFogEnd = 1024.0f;  // aproximación: el área no trae distancia bajo el agua

Fog defaultFog();
Fog fogForArea(const CollisionArea& area, bool eyeUnderwater);

// Acerca `current` a `target` con constante de tiempo tau (s). dt <= 0 o no finito: sin cambio.
Fog approachFog(const Fog& current, const Fog& target, float dt, float tau = 0.3f);

// (inicio, 1 / (fin - inicio)) para el shader; fin <= inicio → sin niebla
void fogRange(const Fog& f, float out[2]);

}  // namespace render3d
