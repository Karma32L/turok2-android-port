// Renderer GLES 3.0 de modelos animados con skinning rígido en GPU (solo Android).
//
// Sube los modelos de un ActorSet (actores del nivel o el arma) a un VAO/VBO/IBO por modelo
// (índices u16) y dibuja cada instancia con su matriz de modelo y su paleta de huesos:
//   - atributo `aBone` entero (glVertexAttribIPointer, u16 → uint);
//   - paleta en un UBO std140 de kMaxBones × 3 vec4 (filas de [R|t], 6 KiB < 16 KiB garantizados
//     por GLES 3.0; 115 huesos × 3 vec4 no caben en uniforms sueltos);
//   - el índice se acota en el shader a la paleta subida, aunque la CPU ya lo validó.
// Mismo fragment shader (luz y niebla) que la geometría del nivel. Por modelo se dibujan sus
// superficies en orden opaco → alpha test → translúcido.
#pragma once

#include <GLES3/gl3.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "atmosphere.hpp"
#include "level_actors.hpp"
#include "material_gl.hpp"
#include "math3d.hpp"
#include "skeleton.hpp"

namespace vfs {
class Vfs;
}

namespace render3d::gl {

constexpr std::size_t kMaxBones = 128;  // = SkinnedLimits::maxBones

class SkinnedRenderer {
public:
    SkinnedRenderer() = default;
    ~SkinnedRenderer();
    SkinnedRenderer(const SkinnedRenderer&) = delete;
    SkinnedRenderer& operator=(const SkinnedRenderer&) = delete;

    // Modelos y materiales de `set` (las instancias se pasan al dibujar). Vacío no es error.
    bool init(const ActorSet& set, const vfs::Vfs& fs, std::string* err);
    void release();
    void abandon();
    bool ready() const { return program_ != 0; }
    std::size_t texturesLoaded() const { return textures_.loaded(); }
    std::size_t texturesFailed() const { return textures_.failed(); }

    // begin → drawModel* → end. Deja el estado GL como lo espera SpriteBatch.
    void begin(const Mat4& viewProj, Vec3 camPos, const Fog& fog);
    void drawModel(std::size_t model, const Mat4& modelMatrix, const std::vector<BoneMatrix>& palette);
    void end();

private:
    enum Pass { Opaque, Masked, Translucent };
    struct Draw {
        Pass pass = Opaque;
        std::size_t material = 0;
        GLuint sampler = 0;
        bool cull = true, depthWrite = true, fullbright = false;
        float alphaRef = -1.0f;
        float diffuse[4] = {1, 1, 1, 1};
        std::uint32_t firstIndex = 0, indexCount = 0;
    };
    struct Model {
        GLuint vao = 0, vbo = 0, ibo = 0;
        std::size_t bones = 0;
        std::vector<Draw> draws;  // ordenadas por pasada
    };

    bool initProgram(std::string* err);

    GLuint program_ = 0, ubo_ = 0;
    GLint uViewProj_ = -1, uModel_ = -1, uBoneMax_ = -1, uTex_ = -1, uDiffuse_ = -1, uAlphaRef_ = -1;
    GLint uFullbright_ = -1, uSunDir_ = -1, uSunColor_ = -1, uAmbient_ = -1, uCamPos_ = -1, uFogColor_ = -1;
    GLint uFogRange_ = -1;
    SamplerSet samplers_;
    MaterialTextures textures_;
    std::vector<Model> models_;
    std::vector<float> uboScratch_;
};

}  // namespace render3d::gl
