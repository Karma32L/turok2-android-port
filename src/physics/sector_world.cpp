#include "sector_world.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace physics {
namespace {

std::optional<SectorWorld> fail(std::string* err, const char* msg) {
    if (err) *err = msg;
    return std::nullopt;
}

Vec2 xy(Vec3 v) { return {v.x, v.y}; }

constexpr int kMaxCrossings = 256;  // aristas cruzadas por llamada a trace (anti-bucle)
constexpr float kInset = 0.05f;      // al chocar, la posición queda así de dentro del sector

}  // namespace

std::optional<SectorWorld> SectorWorld::build(const MapCollision& col, std::string* err, const SectorLimits& limits) {
    if (col.sectors.empty() || col.areas.empty()) return fail(err, "colisión vacía");
    SectorWorld w;
    w.areas_ = col.areas;
    w.sectors_.resize(col.sectors.size());
    Vec2 lo{std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    Vec2 hi{-lo.x, -lo.y};
    for (std::size_t i = 0; i < col.sectors.size(); ++i) {
        const auto& src = col.sectors[i];
        Sector& s = w.sectors_[i];
        s.area = src.area;
        s.hasCeil = true;
        s.bmin = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
        s.bmax = {-s.bmin.x, -s.bmin.y};
        for (int k = 0; k < 3; ++k) {
            // El parser ya validó índices y floats; se comprueba otra vez por si la colisión
            // se construyó a mano (tests, futuros formatos)
            if (src.p[k] >= col.points.size() || src.area >= col.areas.size()) return fail(err, "índice inválido");
            const auto& p = col.points[src.p[k]];
            s.v[k] = {p.x, p.y, p.floorZ};
            s.ceil[k] = p.ceilZ;
            if (!(p.ceilZ > p.floorZ)) s.hasCeil = false;
            const std::uint16_t l = src.link[k];
            s.link[k] = (l == render3d::kNoLink || l >= col.sectors.size()) ? kNoSector : static_cast<int>(l);
            s.bmin = {std::min(s.bmin.x, p.x), std::min(s.bmin.y, p.y)};
            s.bmax = {std::max(s.bmax.x, p.x), std::max(s.bmax.y, p.y)};
        }
        if (!finite(s.bmin) || !finite(s.bmax)) return fail(err, "coordenadas no finitas");
        lo = {std::min(lo.x, s.bmin.x), std::min(lo.y, s.bmin.y)};
        hi = {std::max(hi.x, s.bmax.x), std::max(hi.y, s.bmax.y)};
    }

    // Rejilla: celda mínima limits.cellSize, agrandada hasta no superar maxCells
    float cell = limits.cellSize > 1.0f ? limits.cellSize : 256.0f;
    const double w0 = static_cast<double>(hi.x) - lo.x, h0 = static_cast<double>(hi.y) - lo.y;
    for (int guard = 0; guard < 64; ++guard) {
        const double cells = (std::floor(w0 / cell) + 1.0) * (std::floor(h0 / cell) + 1.0);
        if (cells <= static_cast<double>(limits.maxCells)) break;
        cell *= 2.0f;
    }
    w.cell_ = cell;
    w.origin_ = lo;
    w.nx_ = static_cast<int>(std::floor(w0 / cell)) + 1;
    w.ny_ = static_cast<int>(std::floor(h0 / cell)) + 1;
    if (static_cast<std::size_t>(w.nx_) * static_cast<std::size_t>(w.ny_) > limits.maxCells) {
        return fail(err, "nivel demasiado grande para la rejilla");
    }
    w.grid_.resize(static_cast<std::size_t>(w.nx_) * static_cast<std::size_t>(w.ny_));
    for (std::size_t i = 0; i < w.sectors_.size(); ++i) {
        int x0, y0, x1, y1;
        w.cellRange(w.sectors_[i].bmin, w.sectors_[i].bmax, x0, y0, x1, y1);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                w.grid_[static_cast<std::size_t>(y) * static_cast<std::size_t>(w.nx_) + static_cast<std::size_t>(x)]
                    .push_back(static_cast<int>(i));
    }
    return w;
}

void SectorWorld::cellRange(Vec2 bmin, Vec2 bmax, int& x0, int& y0, int& x1, int& y1) const {
    auto cx = [&](float v) {
        const float c = std::floor((v - origin_.x) / cell_);
        return static_cast<int>(std::clamp(c, 0.0f, static_cast<float>(nx_ - 1)));
    };
    auto cy = [&](float v) {
        const float c = std::floor((v - origin_.y) / cell_);
        return static_cast<int>(std::clamp(c, 0.0f, static_cast<float>(ny_ - 1)));
    };
    x0 = cx(bmin.x), x1 = cx(bmax.x), y0 = cy(bmin.y), y1 = cy(bmax.y);
}

bool SectorWorld::contains(int s, Vec2 p, float eps) const {
    if (s < 0 || static_cast<std::size_t>(s) >= sectors_.size()) return false;
    const Sector& q = sectors_[static_cast<std::size_t>(s)];
    return pointInTriangle2D(p, xy(q.v[0]), xy(q.v[1]), xy(q.v[2]), eps);
}

float SectorWorld::floorZ(int s, Vec2 p) const {
    const Sector& q = sectors_[static_cast<std::size_t>(s)];
    return planeZ(q.v[0], q.v[1], q.v[2], p);
}

std::optional<float> SectorWorld::ceilZ(int s, Vec2 p) const {
    const Sector& q = sectors_[static_cast<std::size_t>(s)];
    if (!q.hasCeil) return std::nullopt;
    return planeZ({q.v[0].x, q.v[0].y, q.ceil[0]}, {q.v[1].x, q.v[1].y, q.ceil[1]}, {q.v[2].x, q.v[2].y, q.ceil[2]}, p);
}

void SectorWorld::edge(int s, int e, Vec2& a, Vec2& b) const {
    const Sector& q = sectors_[static_cast<std::size_t>(s)];
    a = xy(q.v[(e + 2) % 3]);
    b = xy(q.v[e]);
}

int SectorWorld::link(int s, int e) const { return sectors_[static_cast<std::size_t>(s)].link[e]; }

int SectorWorld::locate(Vec2 p, float z, float stepUp) const {
    if (!finite(p) || !std::isfinite(z) || grid_.empty()) return kNoSector;
    if (p.x < origin_.x || p.y < origin_.y) return kNoSector;
    const float fx = std::floor((p.x - origin_.x) / cell_), fy = std::floor((p.y - origin_.y) / cell_);
    if (fx >= static_cast<float>(nx_) || fy >= static_cast<float>(ny_)) return kNoSector;
    const auto& cell = grid_[static_cast<std::size_t>(fy) * static_cast<std::size_t>(nx_) + static_cast<std::size_t>(fx)];
    int best = kNoSector, lowest = kNoSector;
    float bestZ = -std::numeric_limits<float>::max(), lowestZ = std::numeric_limits<float>::max();
    for (int s : cell) {
        if (!contains(s, p)) continue;
        const float f = floorZ(s, p);
        if (f <= z + stepUp && f > bestZ) best = s, bestZ = f;
        if (f < lowestZ) lowest = s, lowestZ = f;
    }
    return best != kNoSector ? best : lowest;
}

void SectorWorld::sectorsNear(Vec2 p, float r, std::vector<int>& out) const {
    out.clear();
    if (!finite(p) || !(r >= 0.0f) || grid_.empty()) return;
    int x0, y0, x1, y1;
    cellRange({p.x - r, p.y - r}, {p.x + r, p.y + r}, x0, y0, x1, y1);
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            for (int s : grid_[static_cast<std::size_t>(y) * static_cast<std::size_t>(nx_) + static_cast<std::size_t>(x)]) {
                const Sector& q = sectors_[static_cast<std::size_t>(s)];
                if (q.bmax.x < p.x - r || q.bmin.x > p.x + r || q.bmax.y < p.y - r || q.bmin.y > p.y + r) continue;
                out.push_back(s);
            }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
}

std::optional<SectorWorld::RayHit> SectorWorld::raycastFloor(Vec3 origin, Vec3 dir, float maxT) const {
    if (!finite(origin) || !finite(dir) || !std::isfinite(maxT) || maxT < 0.0f || grid_.empty()) return std::nullopt;
    const Vec3 end = origin + dir * maxT;
    int x0, y0, x1, y1;
    cellRange({std::min(origin.x, end.x), std::min(origin.y, end.y)}, {std::max(origin.x, end.x), std::max(origin.y, end.y)},
              x0, y0, x1, y1);
    std::optional<RayHit> best;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            for (int s : grid_[static_cast<std::size_t>(y) * static_cast<std::size_t>(nx_) + static_cast<std::size_t>(x)]) {
                const Sector& q = sectors_[static_cast<std::size_t>(s)];
                const auto t = rayTriangle(origin, dir, q.v[0], q.v[1], q.v[2], best ? best->t : maxT);
                if (t && (!best || *t < best->t)) best = RayHit{s, *t, origin + dir * *t};
            }
    return best;
}

SectorWorld::Trace SectorWorld::trace(int s, Vec2 from, Vec2 to, const PassFn& pass) const {
    Trace r;
    r.end = from;
    r.sector = s;
    if (s < 0 || static_cast<std::size_t>(s) >= sectors_.size() || !finite(from) || !finite(to)) {
        r.blocked = true;
        return r;
    }
    Vec2 p = from;
    for (int step = 0; step < kMaxCrossings; ++step) {
        if (contains(s, to)) {
            r.end = to;
            r.sector = s;
            return r;
        }
        // Arista por la que sale el segmento p → to: la de menor t entre las que `to` deja
        // fuera (en un triángulo convexo con p dentro solo hay una, salvo esquinas exactas)
        int exitEdge = -1;
        float exitT = 2.0f;
        float worst = 0.0f;
        int worstEdge = -1;
        for (int e = 0; e < 3; ++e) {
            Vec2 a, b;
            edge(s, e, a, b);
            const Vec2 ab = b - a;
            const float len = length(ab);
            if (!(len > 0.0f)) continue;
            const float side = cross(ab, to - a) / len;  // < 0: to fuera de esta arista
            if (side >= 0.0f) continue;
            if (side < worst) worst = side, worstEdge = e;
            if (auto h = segmentIntersect2D(p, to, a, b); h && h->t < exitT) exitT = h->t, exitEdge = e;
        }
        Vec2 hit = p;
        if (exitEdge < 0) {
            // p ya estaba (numéricamente) fuera o en una esquina: se usa la arista que más deja
            // fuera a `to`, sin avanzar
            if (worstEdge < 0) {  // contains() dijo que no pero ninguna arista lo excluye: degenerado
                r.blocked = true;
                r.end = p;
                r.sector = s;
                return r;
            }
            exitEdge = worstEdge;
        } else {
            hit = p + (to - p) * exitT;
        }
        const int n = link(s, exitEdge);
        if (n != kNoSector && pass(s, n, hit)) {
            s = n;
            p = hit;
            continue;
        }
        // Pared: se queda un poco hacia dentro de la arista
        Vec2 a, b;
        edge(s, exitEdge, a, b);
        const Vec2 ab = b - a;
        const float len = length(ab);
        Vec2 inward = len > 0.0f ? Vec2{-ab.y / len, ab.x / len} : Vec2{};
        Vec2 end = hit + inward * kInset;
        if (!contains(s, end)) end = contains(s, p) ? p : end;
        r.end = end;
        r.sector = s;
        r.blocked = true;
        r.wallEdge = exitEdge;
        return r;
    }
    r.end = p;
    r.sector = s;
    r.blocked = true;
    return r;
}

}  // namespace physics
