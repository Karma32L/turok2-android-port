// Evaluación de poses: muestreo de frames, jerarquía e interpolación.
#include "test.hpp"

#include <skeleton.hpp>

#include <cmath>
#include <limits>

#include "skinned_builder.hpp"

using namespace render3d;

namespace {

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
bool near(Vec3 a, Vec3 b, float eps = 1e-4f) { return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps); }

AnimClip parseOne(const sb::Clip& c) {
    const auto a = parseAnimSet(ByteView(sb::anim({c})));
    CHECK(a.has_value());
    return a ? a->clips[0] : AnimClip{};
}

}  // namespace

TEST(skeleton_muestreo_de_frames) {
    AnimClip c;
    c.frames = 4;
    c.fps = 10.0f;
    FrameSample s = sampleFrame(c, 0.15f, true);  // frame 1,5
    CHECK(s.f0 == 1 && s.f1 == 2 && near(s.alpha, 0.5f));
    s = sampleFrame(c, 0.35f, true);  // 3,5: interpola del último al primero
    CHECK(s.f0 == 3 && s.f1 == 0 && near(s.alpha, 0.5f));
    s = sampleFrame(c, 0.45f, true);  // envuelve: 4,5 → 0,5
    CHECK(s.f0 == 0 && s.f1 == 1 && near(s.alpha, 0.5f));
    s = sampleFrame(c, -0.05f, true);  // negativo con bucle: 3,5
    CHECK(s.f0 == 3 && near(s.alpha, 0.5f));
    s = sampleFrame(c, 10.0f, false);  // sin bucle se queda en el último
    CHECK(s.f0 == 3 && s.f1 == 3 && s.alpha == 0.0f);
    s = sampleFrame(c, -1.0f, false);
    CHECK(s.f0 == 0 && s.f1 == 0);
    s = sampleFrame(c, std::numeric_limits<float>::quiet_NaN(), true);
    CHECK(s.f0 == 0 && s.f1 == 0 && s.alpha == 0.0f);
    c.frames = 1;
    s = sampleFrame(c, 5.0f, true);
    CHECK(s.f0 == 0 && s.f1 == 0);
}

TEST(skeleton_jerarquia_de_traslaciones) {
    // 3 huesos en cadena, cada hijo desplazado (0, 1, 0) respecto al padre
    const AnimClip c = parseOne(sb::fixedClip(3));
    std::vector<BoneMatrix> pal;
    CHECK(evaluatePose(c, {-1, 0, 1}, 0.0f, true, pal));
    CHECK(pal.size() == 3);
    CHECK(near(pal[2].apply({0, 0, 0}), {0, 2, 0}));
    CHECK(near(pal[1].apply({1, 0, 0}), {1, 1, 0}));
}

TEST(skeleton_rotacion_del_padre_afecta_al_hijo) {
    sb::Clip k = sb::fixedClip(2);
    // Raíz girada 90° sobre Z: q = (0, 0, sin45, cos45)
    const float h = std::sqrt(0.5f);
    k.fixed[0].q[2] = h;
    k.fixed[0].q[3] = h;
    const AnimClip c = parseOne(k);
    std::vector<BoneMatrix> pal;
    CHECK(evaluatePose(c, {-1, 0}, 0.0f, true, pal));
    // El hijo está en (0,1,0) local → (-1,0,0) tras girar; su punto local (1,0,0) → (-1,1,0)
    CHECK(near(pal[1].apply({0, 0, 0}), {-1, 0, 0}));
    CHECK(near(pal[1].apply({1, 0, 0}), {-1, 1, 0}));
}

TEST(skeleton_interpola_pistas) {
    sb::Clip k = sb::fixedClip(2);
    k.frames = 3;
    k.numTrans = 1;
    k.transTrack[1] = 0;
    k.trans = {{0, 0, 0}, {0, 0, 2}, {0, 0, 4}};
    k.numRot = 1;
    k.rotTrack[0] = 0;
    const float h = std::sqrt(0.5f);
    k.rots = {{0, 0, 0, 1}, {0, 0, h, h}, {0, 0, 1, 0}};
    const AnimClip c = parseOne(k);
    std::vector<BoneMatrix> pal;
    // t = 0,5 frames → traslación z = 1; rotación a mitad de 0° → 90° = 45°
    CHECK(evaluatePose(c, {-1, 0}, 0.5f / 15.0f, false, pal));
    CHECK(near(pal[1].apply({0, 0, 0}), {0, 0, 1}));
    const Vec3 x = pal[0].apply({1, 0, 0});
    CHECK(near(x, {h, h, 0}, 1e-3f));
}

TEST(skeleton_huesos_incoherentes_dan_identidad) {
    const AnimClip c = parseOne(sb::fixedClip(3));
    std::vector<BoneMatrix> pal;
    CHECK(!evaluatePose(c, {-1, 0}, 0.0f, true, pal));  // la malla tiene 2 huesos, el clip 3
    CHECK(pal.size() == 2 && pal[1].row[1][3] == 0.0f && pal[1].row[0][0] == 1.0f);
    CHECK(!evaluatePose(c, {-1, 2, 0}, 0.0f, true, pal));  // jerarquía no topológica
    CHECK(pal.size() == 3 && pal[1].row[1][3] == 0.0f);
}
