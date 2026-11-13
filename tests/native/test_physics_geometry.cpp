// Primitivas de intersección de la física (rayo/triángulo, punto en triángulo, círculo/segmento).
#include "test.hpp"

#include <geometry.hpp>

#include <cmath>
#include <limits>

using namespace physics;

namespace {

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
const float kNan = std::numeric_limits<float>::quiet_NaN();
const float kInf = std::numeric_limits<float>::infinity();

// Triángulo en el plano z = 5
const Vec3 A{0, 0, 5}, B{10, 0, 5}, C{0, 10, 5};

}  // namespace

TEST(rayo_triangulo_impacto_por_ambas_caras) {
    auto t = rayTriangle({2, 2, 20}, {0, 0, -1}, A, B, C, 100.0f);
    CHECK(t && near(*t, 15.0f));
    t = rayTriangle({2, 2, -20}, {0, 0, 1}, A, B, C, 100.0f);  // por debajo (sin culling)
    CHECK(t && near(*t, 25.0f));
    t = rayTriangle({2, 2, 20}, {0, 0, -2}, A, B, C, 100.0f);  // dir no normalizado: t en unidades de dir
    CHECK(t && near(*t, 7.5f));
}

TEST(rayo_triangulo_fallos) {
    CHECK(!rayTriangle({20, 20, 20}, {0, 0, -1}, A, B, C, 100.0f));  // fuera
    CHECK(!rayTriangle({2, 2, 20}, {0, 0, 1}, A, B, C, 100.0f));    // se aleja
    CHECK(!rayTriangle({2, 2, 20}, {0, 0, -1}, A, B, C, 10.0f));    // más allá de maxT
    CHECK(!rayTriangle({2, 2, 20}, {1, 0, 0}, A, B, C, 100.0f));    // paralelo
    CHECK(!rayTriangle({2, 2, 5}, {1, 1, 0}, A, B, C, 100.0f));     // paralelo y en el plano
    CHECK(!rayTriangle({2, 2, 20}, {0, 0, 0}, A, B, C, 100.0f));    // dirección nula
    CHECK(!rayTriangle({2, 2, 20}, {0, 0, -1}, A, A, C, 100.0f));   // degenerado
    CHECK(!rayTriangle({0, 0, 20}, {0, 0, -1}, {0, 0, 0}, {5, 5, 0}, {10, 10, 0}, 100.0f));  // colineal
}

TEST(rayo_triangulo_no_finitos) {
    CHECK(!rayTriangle({kNan, 2, 20}, {0, 0, -1}, A, B, C, 100.0f));
    CHECK(!rayTriangle({2, 2, 20}, {0, kInf, -1}, A, B, C, 100.0f));
    CHECK(!rayTriangle({2, 2, 20}, {0, 0, -1}, {kNan, 0, 0}, B, C, 100.0f));
    CHECK(!rayTriangle({2, 2, 20}, {0, 0, -1}, A, B, C, kInf));
}

TEST(rayo_triangulo_bordes_y_vertices) {
    // Arista compartida y vértice: cuentan como impacto (sin rendijas entre vecinos)
    CHECK(rayTriangle({5, 0, 20}, {0, 0, -1}, A, B, C, 100.0f));
    CHECK(rayTriangle({5, 5, 20}, {0, 0, -1}, A, B, C, 100.0f));
    CHECK(rayTriangle({0, 0, 20}, {0, 0, -1}, A, B, C, 100.0f));
    CHECK(!rayTriangle({5.01f, 5.01f, 20}, {0, 0, -1}, A, B, C, 100.0f));
    // Rayo oblicuo contra un plano inclinado
    const auto t = rayTriangle({0, 0, 0}, {1, 1, 1}, {10, 0, 0}, {0, 10, 0}, {0, 0, 10}, 100.0f);
    CHECK(t && near(*t, 10.0f / 3.0f));
}

TEST(punto_en_triangulo_2d) {
    const Vec2 a{0, 0}, b{10, 0}, c{0, 10};
    CHECK(pointInTriangle2D({1, 1}, a, b, c));
    CHECK(pointInTriangle2D({1, 1}, a, c, b));  // sentido horario también
    CHECK(pointInTriangle2D({5, 0}, a, b, c));  // en el borde
    CHECK(!pointInTriangle2D({6, 6}, a, b, c));
    CHECK(!pointInTriangle2D({-0.5f, 1}, a, b, c));
    CHECK(pointInTriangle2D({-0.5f, 1}, a, b, c, 1.0f));  // ensanchado
    CHECK(!pointInTriangle2D({1, 1}, a, a, c));             // degenerado
    CHECK(!pointInTriangle2D({kNan, 1}, a, b, c));
}

TEST(altura_del_plano) {
    const Vec3 a{0, 0, 0}, b{100, 0, 50}, c{0, 100, 0};  // rampa en X: z = x/2
    CHECK(near(planeZ(a, b, c, {40, 10}), 20.0f));
    CHECK(near(planeZ(a, b, c, {200, 0}), 100.0f));  // extrapola fuera del triángulo
    CHECK(near(planeZ({0, 0, 3}, {0, 0, 6}, {0, 0, 9}, {1, 1}), 6.0f));  // degenerado: media
}

TEST(punto_mas_cercano_en_segmento) {
    const Vec2 a{0, 0}, b{10, 0};
    Vec2 c = closestPointOnSegment2D({5, 5}, a, b);
    CHECK(near(c.x, 5) && near(c.y, 0));
    c = closestPointOnSegment2D({-5, 5}, a, b);
    CHECK(near(c.x, 0) && near(c.y, 0));
    c = closestPointOnSegment2D({15, -1}, a, b);
    CHECK(near(c.x, 10));
    c = closestPointOnSegment2D({3, 3}, a, a);
    CHECK(c.x == 0 && c.y == 0);
}

TEST(circulo_contra_segmento) {
    const Vec2 a{0, 0}, b{100, 0};
    Vec2 d = pushCircleOutOfSegment({50, 10}, 30, a, b);
    CHECK(near(d.x, 0) && near(d.y, 20));  // queda tangente
    d = pushCircleOutOfSegment({50, 40}, 30, a, b);
    CHECK(d.x == 0 && d.y == 0);  // no solapa
    d = pushCircleOutOfSegment({50, 0}, 30, a, b);
    CHECK(near(d.y, 30));  // centro sobre la pared: hacia la izquierda de a→b
    d = pushCircleOutOfSegment({50, 0}, 30, b, a);
    CHECK(near(d.y, -30));
    d = pushCircleOutOfSegment({-10, 10}, 30, a, b);  // contra el extremo: radial
    CHECK(near(std::sqrt(d.x * d.x + d.y * d.y) + std::sqrt(200.0f), 30.0f, 1e-3f));
    d = pushCircleOutOfSegment({kNan, 0}, 30, a, b);
    CHECK(d.x == 0 && d.y == 0);
    d = pushCircleOutOfSegment({50, 10}, kNan, a, b);
    CHECK(d.x == 0 && d.y == 0);
}

TEST(interseccion_de_segmentos) {
    auto h = segmentIntersect2D({0, -5}, {0, 5}, {-10, 0}, {10, 0});
    CHECK(h && near(h->t, 0.5f) && near(h->u, 0.5f));
    CHECK(!segmentIntersect2D({0, 1}, {0, 5}, {-10, 0}, {10, 0}));
    CHECK(!segmentIntersect2D({0, 0}, {10, 0}, {0, 1}, {10, 1}));  // paralelos
    CHECK(!segmentIntersect2D({kNan, 0}, {0, 5}, {-10, 0}, {10, 0}));
}
