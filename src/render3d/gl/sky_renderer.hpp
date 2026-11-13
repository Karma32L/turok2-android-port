// Cielo por capas (solo Android, GLES 3.0). Cada SkyLayer del .map es un plano horizontal
// enorme centrado en la cámara a cam.z + heightOffset, con la textura del material repetida
// (tile), desplazada con el tiempo (animSpeed · speedScalar) y con parallax (cameraTrack).
// El color se interpola entre blackColor y whiteColor según la textura; el alfa es opacity.
// Hacia el horizonte se funde con la niebla: el fondo (glClear) es el color de la niebla.
// Se dibuja antes que el mundo, sin profundidad. Los factores de escala son aproximaciones
// visuales: el motor original no documenta las unidades de tile/animSpeed.
#pragma once

#include <GLES3/gl3.h>

#include <string>
#include <vector>

#include "level_builder.hpp"
#include "math3d.hpp"
#include "render/gl/gl_renderer.hpp"
#include "world_renderer.hpp"

namespace vfs {
class Vfs;
}

namespace render3d::gl {

class SkyRenderer {
public:
    SkyRenderer() = default;
    ~SkyRenderer();
    SkyRenderer(const SkyRenderer&) = delete;
    SkyRenderer& operator=(const SkyRenderer&) = delete;

    // Sin capas no es un error: ready() == false y draw() no hace nada
    bool init(const std::vector<SkyLayer>& layers, const std::vector<Material>& materials, const vfs::Vfs& fs,
              std::string* err);
    void release();
    void abandon();
    bool ready() const { return program_ != 0 && !layers_.empty(); }

    // time en segundos (para el desplazamiento de las nubes)
    void draw(const Mat4& viewProj, Vec3 camPos, float time, const FogParams& fog);

private:
    struct Layer {
        render::gl::Texture texture;
        float white[3], black[3], opacity, diffuse[3];
        float height, track, tile, scroll;
    };

    GLuint program_ = 0, vao_ = 0, vbo_ = 0;
    GLint uViewProj_ = -1, uCenter_ = -1, uRadius_ = -1, uUVOffset_ = -1, uUVScale_ = -1, uTex_ = -1;
    GLint uWhite_ = -1, uBlack_ = -1, uOpacity_ = -1, uFogColor_ = -1;
    std::vector<Layer> layers_;
};

}  // namespace render3d::gl
