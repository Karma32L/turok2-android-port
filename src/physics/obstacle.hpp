// Obstáculos dinámicos que no forman parte de la malla de sectores: puertas cerradas, barriles,
// enemigos y el propio jugador. Cada uno es una cápsula 2D (segmento a–b engordado `radius`; con
// a == b, un círculo) extruida entre zMin y zMax. Los mantiene la lógica del nivel; la física solo
// los consulta.
#pragma once

#include "geometry.hpp"

namespace physics {

constexpr int kObstacleStatic = -1;  // puertas, barriles…: afectan a todos
constexpr int kObstaclePlayer = 0;   // el jugador; los enemigos usan 1 + índice

struct Obstacle {
    Vec2 a, b;
    float radius = 0.0f;
    float zMin = 0.0f, zMax = 0.0f;
    int owner = kObstacleStatic;
};

// ¿Se solapa con un cuerpo que ocupa [z0, z1]?
inline bool overlapsZ(const Obstacle& o, float z0, float z1) { return z0 < o.zMax && z1 > o.zMin; }

// ¿Afecta al cuerpo `self`? (nadie choca con su propio obstáculo)
inline bool affects(const Obstacle& o, int self) { return o.owner == kObstacleStatic || o.owner != self; }

}  // namespace physics
