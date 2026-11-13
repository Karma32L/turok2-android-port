#include "geometry.hpp"

#include <algorithm>
#include <cmath>

namespace physics {

std::optional<float> rayTriangle(Vec3 origin, Vec3 dir, Vec3 a, Vec3 b, Vec3 c, float maxT) {
    if (!finite(origin) || !finite(dir) || !finite(a) || !finite(b) || !finite(c) || !std::isfinite(maxT)) {
        return std::nullopt;
    }
    using render3d::cross;
    using render3d::dot;
    const Vec3 e1 = b - a, e2 = c - a;
    const Vec3 pv = cross(dir, e2);
    const float det = dot(e1, pv);
    // Escala del épsilon con el tamaño del problema: det ~ |dir|·|e1|·|e2|
    const float scale = render3d::length(dir) * render3d::length(e1) * render3d::length(e2);
    if (!(scale > 0.0f) || std::fabs(det) <= 1e-7f * scale) return std::nullopt;  // paralelo o degenerado
    const float inv = 1.0f / det;
    const Vec3 tv = origin - a;
    const float u = dot(tv, pv) * inv;
    constexpr float kEdge = 1e-6f;  // incluye los bordes: los triángulos vecinos no dejan rendijas
    if (u < -kEdge || u > 1.0f + kEdge) return std::nullopt;
    const Vec3 qv = cross(tv, e1);
    const float v = dot(dir, qv) * inv;
    if (v < -kEdge || u + v > 1.0f + kEdge) return std::nullopt;
    const float t = dot(e2, qv) * inv;
    if (!std::isfinite(t) || t < 0.0f || t > maxT) return std::nullopt;
    return t;
}

bool pointInTriangle2D(Vec2 p, Vec2 a, Vec2 b, Vec2 c, float eps) {
    if (!finite(p) || !finite(a) || !finite(b) || !finite(c)) return false;
    const float area2 = cross(b - a, c - a);
    if (area2 == 0.0f) return false;
    const float s = area2 > 0.0f ? 1.0f : -1.0f;  // normaliza a CCW
    // Distancia con signo de p a cada arista (positiva = dentro)
    const Vec2 v[3] = {a, b, c};
    for (int i = 0; i < 3; ++i) {
        const Vec2 e = v[(i + 1) % 3] - v[i];
        const float len = length(e);
        if (len <= 0.0f) return false;
        if (s * cross(e, p - v[i]) / len < -eps) return false;
    }
    return true;
}

float planeZ(Vec3 a, Vec3 b, Vec3 c, Vec2 p) {
    const float det = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
    if (!std::isfinite(det) || std::fabs(det) < 1e-12f) return (a.z + b.z + c.z) / 3.0f;
    // Coordenadas baricéntricas de p (pueden salir de [0,1]: extrapola el plano)
    const float l1 = ((p.x - a.x) * (c.y - a.y) - (c.x - a.x) * (p.y - a.y)) / det;
    const float l2 = ((b.x - a.x) * (p.y - a.y) - (p.x - a.x) * (b.y - a.y)) / det;
    return a.z + l1 * (b.z - a.z) + l2 * (c.z - a.z);
}

Vec2 closestPointOnSegment2D(Vec2 p, Vec2 a, Vec2 b) {
    const Vec2 ab = b - a;
    const float len2 = dot(ab, ab);
    if (!(len2 > 0.0f)) return a;
    const float t = std::clamp(dot(p - a, ab) / len2, 0.0f, 1.0f);
    return a + ab * t;
}

Vec2 pushCircleOutOfSegment(Vec2 p, float r, Vec2 a, Vec2 b) {
    if (!finite(p) || !finite(a) || !finite(b) || !(r > 0.0f)) return {};
    const Vec2 c = closestPointOnSegment2D(p, a, b);
    const Vec2 d = p - c;
    const float dist = length(d);
    if (dist >= r) return {};
    if (dist > 1e-6f) return d * ((r - dist) / dist);
    // Centro sobre el segmento: normal izquierda de a→b
    const Vec2 e = b - a;
    const float len = length(e);
    if (!(len > 0.0f)) return {r, 0.0f};
    return Vec2{-e.y / len, e.x / len} * r;
}

std::optional<SegHit> segmentIntersect2D(Vec2 p, Vec2 q, Vec2 a, Vec2 b) {
    const Vec2 r = q - p, s = b - a;
    const float den = cross(r, s);
    if (!std::isfinite(den) || std::fabs(den) < 1e-12f) return std::nullopt;
    const Vec2 ap = a - p;
    const float t = cross(ap, s) / den;
    const float u = cross(ap, r) / den;
    if (!std::isfinite(t) || !std::isfinite(u) || t < 0.0f || t > 1.0f || u < 0.0f || u > 1.0f) return std::nullopt;
    return SegHit{t, u};
}

}  // namespace physics
