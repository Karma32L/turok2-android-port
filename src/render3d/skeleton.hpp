// Evaluación de la pose de un esqueleto (sin GL; se prueba en el host).
//
// Local de cada hueso = T(t) · R(q), con t y q interpolados entre los dos frames más cercanos
// (lerp y nlerp) o tomados de la pose fija del clip si el hueso no tiene pista.
// Global = Global(padre) · Local, en una sola pasada gracias al orden topológico (padre < hijo).
// Como los vértices de .skinnedmesh están en espacio local de su hueso, la paleta es directamente
// la transformación global (no hay inversa de bind).
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "anim_file.hpp"

namespace render3d {

// Filas de [R | t] (3×4): el layout de 3 vec4 por hueso del UBO std140 del shader
struct BoneMatrix {
    float row[3][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}};

    Vec3 apply(Vec3 p) const {
        return {row[0][0] * p.x + row[0][1] * p.y + row[0][2] * p.z + row[0][3],
                row[1][0] * p.x + row[1][1] * p.y + row[1][2] * p.z + row[1][3],
                row[2][0] * p.x + row[2][1] * p.y + row[2][2] * p.z + row[2][3]};
    }
};

// Frame continuo de un clip en el instante `time` (s): con bucle se envuelve; sin bucle se queda
// en el último frame. Tiempo no finito o negativo sin bucle → 0.
struct FrameSample {
    std::uint32_t f0 = 0, f1 = 0;
    float alpha = 0.0f;
};
FrameSample sampleFrame(const AnimClip& clip, float time, bool loop);

// Paleta de la pose. false (y paleta identidad del tamaño de `parents`) si el clip no tiene los
// mismos huesos que la malla o la jerarquía no está en orden topológico.
bool evaluatePose(const AnimClip& clip, const std::vector<std::int16_t>& parents, float time, bool loop,
                  std::vector<BoneMatrix>& out);

void identityPose(std::size_t bones, std::vector<BoneMatrix>& out);

}  // namespace render3d
