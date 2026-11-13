// Mundo de colisión: la malla 2.5D de sectores del .map (sección 2, ver docs/FORMATS.md) con
// una rejilla 2D uniforme para localizar sectores y consultas de altura, agua y recorrido.
//
// Un sector es un triángulo CCW en XY con suelo (y techo opcional) interpolados entre sus 3
// puntos. La arista e va de p[(e+2)%3] a p[e]; link[e] es el vecino al otro lado (o pared).
// Varios sectores pueden solaparse en XY (puentes, pisos): se desambigua con la altura.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "geometry.hpp"
#include "map_file.hpp"

namespace physics {

using render3d::CollisionArea;
using render3d::MapCollision;

constexpr int kNoSector = -1;

struct SectorLimits {
    float cellSize = 256.0f;
    std::size_t maxCells = 1u << 18;  // si el nivel es enorme se agranda la celda
};

class SectorWorld {
public:
    // nullopt si la colisión está vacía o sus límites no son finitos
    static std::optional<SectorWorld> build(const MapCollision& col, std::string* err = nullptr,
                                            const SectorLimits& limits = {});

    std::size_t sectorCount() const { return sectors_.size(); }

    // Sector bajo (x, y) para un cuerpo con los pies en z: el de suelo más alto que no quede
    // por encima de z + stepUp; si todos lo están, el de suelo más bajo. kNoSector si ninguno
    // contiene (x, y).
    int locate(Vec2 p, float z, float stepUp) const;

    bool contains(int s, Vec2 p, float eps = 0.01f) const;
    float floorZ(int s, Vec2 p) const;
    // nullopt si algún punto del sector no tiene techo (techo <= suelo)
    std::optional<float> ceilZ(int s, Vec2 p) const;
    const CollisionArea& area(int s) const { return areas_[sectors_[static_cast<std::size_t>(s)].area]; }
    int areaIndex(int s) const { return sectors_[static_cast<std::size_t>(s)].area; }
    bool isWater(int s) const { return area(s).water(); }
    float waterZ(int s) const { return area(s).waterZ; }

    // Arista e del sector s (a → b, interior a la izquierda) y su vecino (kNoSector = pared)
    void edge(int s, int e, Vec2& a, Vec2& b) const;
    int link(int s, int e) const;

    // Primer impacto de un rayo contra los suelos de los sectores (t en unidades de dir)
    struct RayHit {
        int sector = kNoSector;
        float t = 0;
        Vec3 point;
    };
    std::optional<RayHit> raycastFloor(Vec3 origin, Vec3 dir, float maxT) const;

    // Recorre el segmento from → to desde el sector s, cruzando aristas mientras
    // pass(desde, hacia, punto) lo permita. Si se topa con una pared se detiene justo
    // dentro del último sector y devuelve la arista que lo bloqueó.
    using PassFn = std::function<bool(int from, int to, Vec2 at)>;
    struct Trace {
        Vec2 end;
        int sector = kNoSector;
        bool blocked = false;
        int wallEdge = -1;  // arista de `sector` que bloqueó (-1 si no hay)
    };
    Trace trace(int s, Vec2 from, Vec2 to, const PassFn& pass) const;

    // Sectores cuya caja XY toca el círculo (p, r); sin duplicados
    void sectorsNear(Vec2 p, float r, std::vector<int>& out) const;

private:
    struct Sector {
        Vec3 v[3];  // (x, y, suelo)
        float ceil[3] = {0, 0, 0};
        bool hasCeil = false;
        int link[3] = {kNoSector, kNoSector, kNoSector};
        std::uint16_t area = 0;
        Vec2 bmin, bmax;
    };

    void cellRange(Vec2 bmin, Vec2 bmax, int& x0, int& y0, int& x1, int& y1) const;

    std::vector<Sector> sectors_;
    std::vector<CollisionArea> areas_;
    Vec2 origin_;
    float cell_ = 256.0f;
    int nx_ = 0, ny_ = 0;
    std::vector<std::vector<int>> grid_;
};

}  // namespace physics
