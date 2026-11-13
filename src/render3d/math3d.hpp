// Matemática 3D mínima para el renderer: vectores, matrices 4x4 column-major (las que espera
// glUniformMatrix4fv con transpose = GL_FALSE) y las convenciones del mundo de Turok 2:
//   - Z hacia arriba, unidades del mapa, ángulos en radianes;
//   - rotación de instancia (r0, r1, r2) = Rz(-r0) · Ry(-r2) · Rx(-r1), verificada contra las
//     bbox guardadas en los .map (docs/FORMATS.md).
#pragma once

#include <cmath>

namespace render3d {

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float length(Vec3 a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(Vec3 a) {
    const float l = length(a);
    return l > 1e-20f ? a * (1.0f / l) : Vec3{0, 0, 1};
}

// Matriz 3x3 fila-mayor (solo para transformar vértices en CPU)
struct Mat3 {
    float m[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
};

inline Mat3 mul(const Mat3& a, const Mat3& b) {
    Mat3 r;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
    return r;
}
inline Vec3 mul(const Mat3& a, Vec3 v) {
    return {a.m[0][0] * v.x + a.m[0][1] * v.y + a.m[0][2] * v.z, a.m[1][0] * v.x + a.m[1][1] * v.y + a.m[1][2] * v.z,
            a.m[2][0] * v.x + a.m[2][1] * v.y + a.m[2][2] * v.z};
}
inline Mat3 rotX(float a) {
    const float c = std::cos(a), s = std::sin(a);
    return {{{1, 0, 0}, {0, c, -s}, {0, s, c}}};
}
inline Mat3 rotY(float a) {
    const float c = std::cos(a), s = std::sin(a);
    return {{{c, 0, s}, {0, 1, 0}, {-s, 0, c}}};
}
inline Mat3 rotZ(float a) {
    const float c = std::cos(a), s = std::sin(a);
    return {{{c, -s, 0}, {s, c, 0}, {0, 0, 1}}};
}

// Rotación de una instancia del .map a partir de sus tres ángulos guardados
inline Mat3 instanceRotation(Vec3 rot) { return mul(mul(rotZ(-rot.x), rotY(-rot.z)), rotX(-rot.y)); }

// Transformación de instancia: p' = pos + R · (escala ⊙ p);  n' = normalize(R · (n / escala))
struct InstanceTransform {
    Mat3 rot;
    Vec3 pos, scale{1, 1, 1};

    Vec3 point(Vec3 p) const { return pos + mul(rot, Vec3{p.x * scale.x, p.y * scale.y, p.z * scale.z}); }
    Vec3 normal(Vec3 n) const { return normalize(mul(rot, Vec3{n.x / scale.x, n.y / scale.y, n.z / scale.z})); }
    // Escala con un número impar de ejes negativos: invierte el sentido de los triángulos
    bool mirrors() const { return scale.x * scale.y * scale.z < 0.0f; }
};

// ---------------------------------------------------------------------------- cuaterniones

// Cuaternión (x, y, z, w) como en los .anim; se usa normalizado
struct Quat {
    float x = 0, y = 0, z = 0, w = 1;
};

inline float dot(Quat a, Quat b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }

// Normalizado; un cuaternión casi nulo o con longitud no finita se convierte en la identidad
inline Quat normalize(Quat q) {
    const float l = std::sqrt(dot(q, q));
    if (!(l > 1e-6f) || !std::isfinite(l)) return {};
    const float k = 1.0f / l;
    return {q.x * k, q.y * k, q.z * k, q.w * k};
}

// Interpolación lineal normalizada por el camino corto (suficiente entre frames contiguos)
inline Quat nlerp(Quat a, Quat b, float t) {
    const float s = dot(a, b) < 0.0f ? -1.0f : 1.0f;
    return normalize(Quat{a.x + (s * b.x - a.x) * t, a.y + (s * b.y - a.y) * t, a.z + (s * b.z - a.z) * t,
                          a.w + (s * b.w - a.w) * t});
}

inline Mat3 toMat3(Quat q) {
    const float x = q.x, y = q.y, z = q.z, w = q.w;
    return {{{1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)},
             {2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)},
             {2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)}}};
}

inline Vec3 lerp(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }

// ---------------------------------------------------------------------------- Mat4 (GL)

struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};  // column-major
    float& at(int row, int col) { return m[col * 4 + row]; }
    float at(int row, int col) const { return m[col * 4 + row]; }
};

inline Mat4 mul(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a.at(row, k) * b.at(k, col);
            r.at(row, col) = s;
        }
    return r;
}

// Matriz de modelo de una transformación de instancia: p' = pos + R · (escala ⊙ p)
inline Mat4 modelMatrix(const InstanceTransform& xf) {
    Mat4 r;
    const float s[3] = {xf.scale.x, xf.scale.y, xf.scale.z};
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col) r.at(row, col) = xf.rot.m[row][col] * s[col];
    r.at(0, 3) = xf.pos.x;
    r.at(1, 3) = xf.pos.y;
    r.at(2, 3) = xf.pos.z;
    return r;
}

// Punto p transformado por una matriz afín (w = 1)
inline Vec3 transformPoint(const Mat4& m, Vec3 p) {
    return {m.at(0, 0) * p.x + m.at(0, 1) * p.y + m.at(0, 2) * p.z + m.at(0, 3),
            m.at(1, 0) * p.x + m.at(1, 1) * p.y + m.at(1, 2) * p.z + m.at(1, 3),
            m.at(2, 0) * p.x + m.at(2, 1) * p.y + m.at(2, 2) * p.z + m.at(2, 3)};
}

// Proyección en perspectiva estilo glFrustum (clip z en [-w, w]); fovY en radianes
inline Mat4 perspective(float fovY, float aspect, float zNear, float zFar) {
    Mat4 r;
    const float f = 1.0f / std::tan(fovY * 0.5f);
    r.at(0, 0) = f / aspect;
    r.at(1, 1) = f;
    r.at(2, 2) = (zFar + zNear) / (zNear - zFar);
    r.at(2, 3) = 2.0f * zFar * zNear / (zNear - zFar);
    r.at(3, 2) = -1.0f;
    r.at(3, 3) = 0.0f;
    return r;
}

// Dirección de vista para yaw (alrededor de Z, 0 = +X, positivo = giro a la izquierda) y pitch
inline Vec3 viewForward(float yaw, float pitch) {
    const float cp = std::cos(pitch);
    return {cp * std::cos(yaw), cp * std::sin(yaw), std::sin(pitch)};
}

// Matriz de vista con Z del mundo hacia arriba (cámara GL: mira a -Z, Y arriba)
inline Mat4 lookDir(Vec3 eye, Vec3 forward) {
    const Vec3 f = normalize(forward);
    const Vec3 s = normalize(cross(f, Vec3{0, 0, 1}));  // derecha
    const Vec3 u = cross(s, f);
    Mat4 r;
    r.at(0, 0) = s.x, r.at(0, 1) = s.y, r.at(0, 2) = s.z;
    r.at(1, 0) = u.x, r.at(1, 1) = u.y, r.at(1, 2) = u.z;
    r.at(2, 0) = -f.x, r.at(2, 1) = -f.y, r.at(2, 2) = -f.z;
    r.at(0, 3) = -dot(s, eye);
    r.at(1, 3) = -dot(u, eye);
    r.at(2, 3) = dot(f, eye);
    return r;
}

}  // namespace render3d
