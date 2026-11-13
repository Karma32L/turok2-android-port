// Mallas de colisión sintéticas para los tests de física (nunca datos del juego).
// Un mapa de baldosas: cada baldosa no sólida aporta 2 sectores CCW con sus propios 4 puntos
// (así dos baldosas vecinas pueden tener suelos distintos = escalón). Los vecinos se enlazan
// por coincidencia de coordenadas XY de las aristas, con la convención del .map:
// link[e] cruza la arista (p[(e+2)%3], p[e]).
#pragma once

#include <map_file.hpp>

#include <functional>
#include <vector>

namespace cb {

using render3d::CollisionArea;
using render3d::CollisionPoint;
using render3d::CollisionSector;
using render3d::MapCollision;

struct Tile {
    bool solid = false;
    float floor = 0.0f;
    float ceil = 1000.0f;  // <= floor: sin techo
    std::uint16_t area = 0;
    float slopeX = 0.0f;       // suelo += slopeX · x (en coordenadas de mundo): rampas continuas
    bool diagonalWall = false;  // omite el triángulo inferior derecho: pared en la diagonal
};

inline void linkByXY(MapCollision& col) {
    auto P = [&](std::uint16_t i) { return col.points[i]; };
    auto same = [&](std::uint16_t i, std::uint16_t j) { return P(i).x == P(j).x && P(i).y == P(j).y; };
    for (std::size_t s = 0; s < col.sectors.size(); ++s) {
        for (int e = 0; e < 3; ++e) {
            const std::uint16_t a = col.sectors[s].p[(e + 2) % 3], b = col.sectors[s].p[e];
            for (std::size_t n = 0; n < col.sectors.size(); ++n) {
                if (n == s) continue;
                for (int f = 0; f < 3; ++f) {
                    const std::uint16_t c = col.sectors[n].p[(f + 2) % 3], d = col.sectors[n].p[f];
                    if (same(a, d) && same(b, c)) col.sectors[s].link[e] = static_cast<std::uint16_t>(n);
                }
            }
        }
    }
}

// w × h baldosas de lado `size` con origen en (0, 0)
inline MapCollision tiles(int w, int h, float size, const std::function<Tile(int, int)>& at,
                          std::vector<CollisionArea> areas = {CollisionArea{}}) {
    MapCollision col;
    col.areas = std::move(areas);
    for (int ty = 0; ty < h; ++ty) {
        for (int tx = 0; tx < w; ++tx) {
            const Tile t = at(tx, ty);
            if (t.solid) continue;
            const float x0 = static_cast<float>(tx) * size, y0 = static_cast<float>(ty) * size;
            const auto base = static_cast<std::uint16_t>(col.points.size());
            for (auto [x, y] : {std::pair{x0, y0}, {x0 + size, y0}, {x0 + size, y0 + size}, {x0, y0 + size}}) {
                const float f = t.floor + t.slopeX * x;
                col.points.push_back({x, y, f, t.ceil > t.floor ? t.ceil + t.slopeX * x : f});
            }
            const auto P = [&](int k) { return static_cast<std::uint16_t>(base + k); };
            CollisionSector a, b;
            a.area = b.area = t.area;
            a.p[0] = P(0), a.p[1] = P(1), a.p[2] = P(2);  // inferior derecho
            b.p[0] = P(0), b.p[1] = P(2), b.p[2] = P(3);  // superior izquierdo
            if (!t.diagonalWall) col.sectors.push_back(a);
            col.sectors.push_back(b);
        }
    }
    linkByXY(col);
    return col;
}

inline CollisionArea waterArea(float waterZ) {
    CollisionArea a;
    a.flags = CollisionArea::kWater;
    a.waterZ = waterZ;
    return a;
}

}  // namespace cb
