// Niebla por área: selección (seco / bajo el agua / sin niebla), transición y rango del shader.
#include "test.hpp"

#include <atmosphere.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace render3d;

namespace {

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

CollisionArea portArea() {
    CollisionArea a;  // valores de Port_of_Adia_1
    const std::uint8_t fog[4] = {110, 110, 120, 255}, water[4] = {74, 99, 113, 165};
    std::copy(fog, fog + 4, a.fogColor);
    std::copy(water, water + 4, a.waterColor);
    a.fogEnd = 5120.0f;
    a.fogStart = 3750.0f;
    return a;
}

}  // namespace

TEST(niebla_del_area_seca) {
    const Fog f = fogForArea(portArea(), false);
    CHECK(near(f.color[0], 110.0f / 255) && near(f.color[2], 120.0f / 255));
    CHECK(f.start == 3750.0f && f.end == 5120.0f);
    CHECK(fogForArea(portArea(), true).end == 5120.0f);  // ojos "bajo el agua" en un área seca: niebla normal
}

TEST(niebla_bajo_el_agua) {
    CollisionArea a = portArea();
    a.flags = CollisionArea::kWater;
    const Fog f = fogForArea(a, true);
    CHECK(near(f.color[1], 99.0f / 255) && f.start == 0.0f && f.end == kUnderwaterFogEnd);
    CHECK(fogForArea(a, false).end == 5120.0f);  // en la superficie: la del área
}

TEST(niebla_valores_raros) {
    CollisionArea a = portArea();
    a.fogEnd = 0.0f;
    CHECK(fogForArea(a, false).end == defaultFog().end);  // sin niebla
    a = portArea();
    a.fogStart = 9000.0f;  // inicio tras el fin: empieza en 0
    CHECK(fogForArea(a, false).start == 0.0f);
}

TEST(niebla_transicion_suave) {
    const Fog from = defaultFog(), to = fogForArea(portArea(), false);
    Fog f = approachFog(from, to, 0.0f);
    CHECK(f.end == from.end);  // dt 0: sin cambio
    f = approachFog(from, to, std::numeric_limits<float>::quiet_NaN());
    CHECK(f.end == from.end);
    f = approachFog(from, to, 0.1f);
    CHECK(f.end < from.end && f.end > to.end);
    for (int i = 0; i < 600; ++i) f = approachFog(f, to, 1.0f / 60.0f);  // 10 s
    CHECK(near(f.end, to.end, 1.0f) && near(f.color[0], to.color[0], 1e-3f));
    f = approachFog(from, to, 0.1f, 0.0f);  // tau 0: salto inmediato
    CHECK(f.end == to.end);
}

TEST(niebla_rango_del_shader) {
    float r[2];
    Fog f;
    f.start = 1000;
    f.end = 3000;
    fogRange(f, r);
    CHECK(r[0] == 1000.0f && near(r[1], 1.0f / 2000));
    f.end = 1000;
    fogRange(f, r);
    CHECK(r[1] == 0.0f && r[0] > 1e29f);
    f.end = std::numeric_limits<float>::quiet_NaN();
    fogRange(f, r);
    CHECK(r[1] == 0.0f);
}
