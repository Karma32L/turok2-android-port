#include "projectile.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace combat {
namespace {

using physics::Vec2;

Vec2 xy(Vec3 v) { return {v.x, v.y}; }
bool finite3(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

}  // namespace

std::optional<float> segmentCylinder(Vec3 p0, Vec3 p1, Vec3 base, float radius, float height) {
    if (!(radius > 0.0f) || !(height > 0.0f) || !finite3(p0) || !finite3(p1) || !finite3(base)) return std::nullopt;
    const Vec3 d = p1 - p0;
    float lo = 0.0f, hi = 1.0f;
    // XY: |p0 + t·d − c|² <= r²
    const float ox = p0.x - base.x, oy = p0.y - base.y;
    const float a = d.x * d.x + d.y * d.y;
    const float b = 2.0f * (ox * d.x + oy * d.y);
    const float c = ox * ox + oy * oy - radius * radius;
    if (a < 1e-12f) {
        if (c > 0.0f) return std::nullopt;  // vertical y fuera del círculo
    } else {
        const float disc = b * b - 4.0f * a * c;
        if (disc < 0.0f) return std::nullopt;
        const float s = std::sqrt(disc);
        lo = std::max(lo, (-b - s) / (2.0f * a));
        hi = std::min(hi, (-b + s) / (2.0f * a));
    }
    // Z: entre la base y la tapa
    const float zLo = base.z, zHi = base.z + height;
    if (std::fabs(d.z) < 1e-12f) {
        if (p0.z < zLo || p0.z > zHi) return std::nullopt;
    } else {
        float ta = (zLo - p0.z) / d.z, tb = (zHi - p0.z) / d.z;
        if (ta > tb) std::swap(ta, tb);
        lo = std::max(lo, ta);
        hi = std::min(hi, tb);
    }
    if (lo > hi) return std::nullopt;
    return lo;
}

std::optional<float> segmentObstacle(Vec3 p0, Vec3 p1, const physics::Obstacle& o) {
    if (!finite3(p0) || !finite3(p1) || !physics::finite(o.a) || !physics::finite(o.b)) return std::nullopt;
    // Punto de máximo acercamiento en XY entre p0→p1 y el eje a→b de la cápsula (exacto para
    // cualquier longitud: sirve igual para un sub-paso de flecha que para una línea de visión)
    const Vec2 q0 = xy(p0), q1 = xy(p1), d = q1 - q0;
    const float len2 = physics::dot(d, d);
    auto paramOn = [&](Vec2 p) {
        return len2 > 1e-12f ? std::clamp(physics::dot(p - q0, d) / len2, 0.0f, 1.0f) : 0.0f;
    };
    float t = 0.0f, dist = 0.0f;
    if (const auto hit = physics::segmentIntersect2D(q0, q1, o.a, o.b)) {
        t = hit->t;
    } else {
        const float ta = paramOn(o.a), tb = paramOn(o.b);
        const struct {
            float t, d;
        } c[4] = {{0.0f, physics::length(q0 - physics::closestPointOnSegment2D(q0, o.a, o.b))},
                  {1.0f, physics::length(q1 - physics::closestPointOnSegment2D(q1, o.a, o.b))},
                  {ta, physics::length(o.a - (q0 + d * ta))},
                  {tb, physics::length(o.b - (q0 + d * tb))}};
        t = c[0].t;
        dist = c[0].d;
        for (const auto& k : c) {
            if (k.d < dist) {
                dist = k.d;
                t = k.t;
            }
        }
    }
    if (dist > o.radius) return std::nullopt;
    const float z = p0.z + (p1.z - p0.z) * t;
    if (z < o.zMin || z > o.zMax) return std::nullopt;
    return t;
}

void Projectiles::fire(Vec3 origin, Vec3 dir, float charge, const physics::SectorWorld* world) {
    const float len = render3d::length(dir);
    if (!finite3(origin) || !(len > 1e-6f) || !std::isfinite(len)) return;
    if (!std::isfinite(charge)) charge = t_.minCharge;
    Arrow a;
    a.pos = origin;
    a.vel = dir * (t_.speed * std::clamp(charge, t_.minCharge, t_.maxCharge) / len);
    a.sector = world ? world->locate(xy(origin), origin.z, 0.0f) : physics::kNoSector;
    if (arrows_.size() >= kMaxArrows) arrows_.erase(arrows_.begin());
    arrows_.push_back(a);
}

void Projectiles::update(float dt, const physics::SectorWorld* world, const std::vector<Target>& targets,
                         const std::vector<physics::Obstacle>& obstacles, std::vector<ProjectileHit>& hits) {
    if (!std::isfinite(dt) || dt <= 0.0f) return;
    dt = std::min(dt, 0.1f);
    const int n = std::max(1, static_cast<int>(std::ceil(dt / kStep)));
    const float h = dt / static_cast<float>(n);
    std::size_t keep = 0;
    for (std::size_t i = 0; i < arrows_.size(); ++i) {
        Arrow a = arrows_[i];
        a.age += dt;
        bool alive = a.stuck ? a.age <= t_.stuckSeconds : a.age <= t_.maxFlight;
        for (int s = 0; alive && !a.stuck && s < n; ++s) alive = step(a, h, world, targets, obstacles, hits);
        if (alive) arrows_[keep++] = a;
    }
    arrows_.resize(keep);
}

bool Projectiles::step(Arrow& a, float h, const physics::SectorWorld* world, const std::vector<Target>& targets,
                       const std::vector<physics::Obstacle>& obstacles, std::vector<ProjectileHit>& hits) {
    const Vec3 v1 = a.vel - Vec3{0, 0, t_.gravity * h};
    const Vec3 p0 = a.pos, p1 = a.pos + (a.vel + v1) * (0.5f * h);
    a.vel = v1;
    if (!finite3(p1) || !finite3(v1)) return false;

    float best = 2.0f;
    ProjectileHit hit;
    for (const Target& t : targets) {
        const auto ht = segmentCylinder(p0, p1, t.base, t.radius, t.height);
        if (ht && *ht < best) {
            best = *ht;
            hit = {true, t.id, {}};
        }
    }
    // Solo obstáculos fijos (puertas, barriles); en empate gana el objetivo (comparación estricta)
    for (const physics::Obstacle& o : obstacles) {
        if (o.owner != physics::kObstacleStatic) continue;
        const auto ht = segmentObstacle(p0, p1, o);
        if (ht && *ht < best) {
            best = *ht;
            hit = {false, -1, {}};
        }
    }

    int sector = a.sector;
    if (world && sector != physics::kNoSector) {
        const Vec2 from = xy(p0), to = xy(p1);
        const float len = physics::length(to - from);
        auto zAt = [&](Vec2 at) {
            const float t = len > 1e-6f ? physics::length(at - from) / len : 1.0f;
            return p0.z + (p1.z - p0.z) * t;
        };
        // Cruza a otro sector si a esa altura queda entre su suelo y su techo
        const physics::SectorWorld::PassFn pass = [&](int, int next, Vec2 at) {
            const float z = zAt(at);
            if (z < world->floorZ(next, at)) return false;
            const auto c = world->ceilZ(next, at);
            return !c || z <= *c;
        };
        const auto tr = world->trace(sector, from, to, pass);
        if (tr.blocked) {
            const float tw = len > 1e-6f ? physics::length(tr.end - from) / len : 0.0f;
            if (tw < best) {
                best = tw;
                hit = {false, -1, {}};
            }
        } else {
            sector = tr.sector;
            const float f = world->floorZ(sector, to);
            const auto c = world->ceilZ(sector, to);
            float tz = 2.0f;
            if (p1.z <= f) tz = p0.z > f ? (p0.z - f) / (p0.z - p1.z) : 0.0f;
            else if (c && p1.z >= *c) tz = p0.z < *c ? (*c - p0.z) / (p1.z - p0.z) : 0.0f;
            if (tz < best) {
                best = std::clamp(tz, 0.0f, 1.0f);
                hit = {false, -1, {}};
            }
        }
    }

    if (best > 1.0f) {
        a.pos = p1;
        a.sector = sector;
        return true;
    }
    hit.point = p0 + (p1 - p0) * best;
    hits.push_back(hit);
    if (hit.target) return false;  // se queda en el objetivo: desaparece
    a.pos = hit.point;
    a.stuck = true;
    a.age = 0.0f;
    return true;
}

}  // namespace combat
