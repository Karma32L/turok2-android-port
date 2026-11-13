// Matemática 3D y cámara libre.
#include "test.hpp"

#include <fly_camera.hpp>
#include <math3d.hpp>

#include <cmath>
#include <limits>

using namespace render3d;

namespace {

constexpr float kPi = 3.14159265f;

bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

// (x, y, z, 1) · M → coordenadas normalizadas
Vec3 project(const Mat4& m, Vec3 p) {
    float o[4];
    for (int r = 0; r < 4; ++r) o[r] = m.at(r, 0) * p.x + m.at(r, 1) * p.y + m.at(r, 2) * p.z + m.at(r, 3);
    return {o[0] / o[3], o[1] / o[3], o[2] / o[3]};
}

}  // namespace

TEST(math_perspectiva_mapea_near_y_far) {
    const Mat4 p = perspective(kPi / 2, 1.0f, 4.0f, 100.0f);
    CHECK(near(project(p, {0, 0, -4}).z, -1.0f));
    CHECK(near(project(p, {0, 0, -100}).z, 1.0f));
    CHECK(near(project(p, {4, 0, -4}).x, 1.0f));  // fov 90°: x = -z en el borde
}

TEST(math_vista_z_arriba) {
    // Cámara en el origen mirando a +X: un punto delante queda en -Z de cámara; +Z del mundo es +Y
    const Mat4 v = lookDir({0, 0, 0}, {1, 0, 0});
    const Vec3 ahead = project(v, {10, 0, 0});
    CHECK(near(ahead.x, 0) && near(ahead.y, 0) && near(ahead.z, -10));
    const Vec3 up = project(v, {10, 0, 5});
    CHECK(near(up.y, 5));
    const Vec3 left = project(v, {10, 3, 0});  // +Y del mundo queda a la izquierda mirando a +X
    CHECK(near(left.x, -3));
    // Traslación
    const Vec3 rel = project(lookDir({1, 2, 3}, {1, 0, 0}), {11, 2, 3});
    CHECK(near(rel.z, -10) && near(rel.x, 0) && near(rel.y, 0));
}

TEST(math_rotacion_de_instancia) {
    // Solo yaw: Rz(-yaw)
    const Mat3 r = instanceRotation({kPi / 2, 0, 0});
    const Vec3 x = mul(r, Vec3{1, 0, 0});
    CHECK(near(x.x, 0) && near(x.y, -1) && near(x.z, 0));
    // r1 rota alrededor de X (con signo invertido): (0, 1, 0) → (0, cos, -sin)
    const Vec3 y = mul(instanceRotation({0, kPi / 2, 0}), Vec3{0, 1, 0});
    CHECK(near(y.y, 0) && near(y.z, -1));
    // r2 rota alrededor de Y (con signo invertido): (1, 0, 0) → (cos, 0, sin)
    const Vec3 z = mul(instanceRotation({0, 0, kPi / 2}), Vec3{1, 0, 0});
    CHECK(near(z.x, 0) && near(z.z, 1));
    // Orden Rz · Ry · Rx: aplicar r1 y luego yaw
    const Vec3 c = mul(instanceRotation({kPi / 2, kPi / 2, 0}), Vec3{0, 1, 0});
    CHECK(near(c.x, 0) && near(c.y, 0) && near(c.z, -1));
    const InstanceTransform t{instanceRotation({0, 0, 0}), {1, 2, 3}, {2, -1, 1}};
    CHECK(t.mirrors());
    const Vec3 n = t.normal({1, 0, 0});
    CHECK(near(n.x, 1) && near(length(n), 1));
}

TEST(camara_avanza_y_gira) {
    FlyCamera cam;
    cam.speed = 100.0f;
    cam.place({0, 0, 0}, 0.0f);
    FlyInput in;
    in.forward = 1.0f;
    cam.update(in, 0.05f);
    CHECK(near(cam.position().x, 5.0f) && near(cam.position().y, 0.0f));

    in = {};
    in.strafe = 1.0f;  // derecha mirando a +X es -Y
    cam.update(in, 0.05f);
    CHECK(near(cam.position().y, -5.0f));

    in = {};
    in.yawDelta = kPi / 2;  // girar a la izquierda: ahora mira a +Y
    in.forward = 1.0f;
    cam.update(in, 0.05f);
    CHECK(near(cam.position().y, 0.0f) && near(cam.forward().y, 1.0f));

    in = {};
    in.up = 1.0f;
    in.fast = true;
    cam.update(in, 0.05f);
    CHECK(near(cam.position().z, 5.0f * cam.fastFactor));
}

TEST(camara_limites_y_entradas_invalidas) {
    FlyCamera cam;
    cam.speed = 100.0f;
    cam.place({0, 0, 0}, 0.0f);
    FlyInput in;
    in.pitchDelta = 10.0f;
    cam.update(in, 0.016f);
    CHECK(near(cam.pitch(), FlyCamera::kMaxPitch));
    in.pitchDelta = -20.0f;
    cam.update(in, 0.016f);
    CHECK(near(cam.pitch(), -FlyCamera::kMaxPitch));

    // Yaw siempre en [-π, π]
    cam.place({0, 0, 0}, 0.0f);
    in = {};
    in.yawDelta = 7.0f * kPi;
    cam.update(in, 0.016f);
    CHECK(cam.yaw() >= -kPi - 1e-4f && cam.yaw() <= kPi + 1e-4f);

    // Diagonal no más rápida; dt acotado
    cam.place({0, 0, 0}, 0.0f);
    in = {};
    in.forward = 1.0f;
    in.strafe = 1.0f;
    in.up = 1.0f;
    cam.update(in, 5.0f);  // se recorta a kMaxDt
    CHECK(near(length(cam.position()), 100.0f * FlyCamera::kMaxDt, 0.01f));

    // NaN / Inf / dt inválido: no cambia nada
    const Vec3 before = cam.position();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    in.forward = nan;
    cam.update(in, 0.016f);
    in = {};
    in.yawDelta = std::numeric_limits<float>::infinity();
    cam.update(in, 0.016f);
    in = {};
    in.forward = 1.0f;
    cam.update(in, nan);
    cam.update(in, -1.0f);
    CHECK(cam.position().x == before.x && std::isfinite(cam.yaw()));
    cam.place({0, 0, 0}, nan, nan);
    CHECK(cam.yaw() == 0.0f && cam.pitch() == 0.0f);
}
