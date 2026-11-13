// Primitivas geométricas de la física (funciones puras, sin estado ni asignaciones).
// Todas toleran entradas degeneradas o no finitas: devuelven "sin resultado" en vez de NaN.
#pragma once

#include <cmath>
#include <optional>

#include "math3d.hpp"

namespace physics {

using render3d::Vec3;

struct Vec2 {
    float x = 0, y = 0;
};

inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2 operator*(Vec2 a, float s) { return {a.x * s, a.y * s}; }
inline float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline float cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }  // > 0: b a la izquierda de a
inline float length(Vec2 a) { return std::sqrt(dot(a, a)); }
inline bool finite(Vec2 a) { return std::isfinite(a.x) && std::isfinite(a.y); }
inline bool finite(Vec3 a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }

// Rayo origin + t·dir contra el triángulo (a, b, c), por las dos caras (Möller–Trumbore).
// Devuelve t en [0, maxT] del primer impacto; nullopt si no corta, si el rayo es paralelo
// al plano, si el triángulo es degenerado o si hay valores no finitos. dir no necesita
// estar normalizado (t se mide en unidades de dir).
std::optional<float> rayTriangle(Vec3 origin, Vec3 dir, Vec3 a, Vec3 b, Vec3 c, float maxT);

// ¿Está p dentro del triángulo (a, b, c) en XY? Acepta ambos sentidos de giro. eps > 0
// ensancha el triángulo (en unidades de distancia) para que los bordes compartidos no dejen huecos.
bool pointInTriangle2D(Vec2 p, Vec2 a, Vec2 b, Vec2 c, float eps = 0.0f);

// Z del plano que pasa por a, b, c en el punto (x, y). Triángulo degenerado en XY: media de
// las tres alturas (no hay plano único).
float planeZ(Vec3 a, Vec3 b, Vec3 c, Vec2 p);

// Punto del segmento [a, b] más cercano a p (a si el segmento es un punto)
Vec2 closestPointOnSegment2D(Vec2 p, Vec2 a, Vec2 b);

// Separa un círculo (centro p, radio r) del segmento [a, b]: si lo solapa, devuelve el
// desplazamiento mínimo que lo deja tangente; si no, (0, 0). Con el centro exactamente sobre
// el segmento empuja hacia la izquierda de a→b (el interior en un triángulo CCW).
Vec2 pushCircleOutOfSegment(Vec2 p, float r, Vec2 a, Vec2 b);

// Intersección de los segmentos p→q y a→b. Devuelve (t, u) con p + t(q-p) = a + u(b-a), ambos
// en [0, 1]; nullopt si no se cortan o son paralelos.
struct SegHit {
    float t = 0, u = 0;
};
std::optional<SegHit> segmentIntersect2D(Vec2 p, Vec2 q, Vec2 a, Vec2 b);

}  // namespace physics
