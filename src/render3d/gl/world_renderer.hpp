// Renderer GLES 3.0 de la geometría estática de un nivel (solo Android).
//
// Sube LevelGeometry a un único VAO/VBO/IBO (índices u32), carga las texturas de los
// materiales desde el VFS y dibuja en tres pasadas: opacos → alpha test → translúcidos
// (blend, sin escritura de profundidad). Iluminación básica: sol direccional + ambiente con
// las normales del modelo (el juego no trae lightmaps ni colores por vértice).
#pragma once

#include <GLES3/gl3.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "level_builder.hpp"
#include "math3d.hpp"
#include "render/gl/gl_renderer.hpp"

namespace vfs {
class Vfs;
}

namespace render3d::gl {

struct UploadStats {
    std::size_t textures = 0;        // texturas distintas subidas
    std::size_t texturesFailed = 0;  // ausentes o inválidas (se usa blanco)
    double ms = 0.0;
};

class WorldRenderer {
public:
    WorldRenderer() = default;
    ~WorldRenderer();
    WorldRenderer(const WorldRenderer&) = delete;
    WorldRenderer& operator=(const WorldRenderer&) = delete;

    // Crea los objetos GL. Si falla, el renderer queda vacío (ready() == false).
    bool init(const LevelGeometry& geo, const vfs::Vfs& fs, UploadStats* stats, std::string* err);
    void release();  // glDelete* (contexto vivo)
    void abandon();  // contexto perdido: olvida los IDs sin borrarlos
    bool ready() const { return program_ != 0; }

    // Deja el estado GL como lo espera SpriteBatch (sin depth test ni culling, sin sampler)
    void draw(const Mat4& viewProj);

private:
    enum Pass { Opaque, Masked, Translucent };
    struct Draw {
        Pass pass = Opaque;
        std::size_t texture = 0;  // índice en textures_ (0 = blanca)
        GLuint sampler = 0;
        bool cull = true, depthWrite = true, fullbright = false;
        float alphaRef = -1.0f;  // < 0: sin alpha test
        float diffuse[4] = {1, 1, 1, 1};
        std::uint32_t firstIndex = 0, indexCount = 0;
    };

    bool initProgram(std::string* err);
    void initSamplers();
    GLuint samplerFor(const Material& m) const;

    GLuint program_ = 0, vao_ = 0, vbo_ = 0, ibo_ = 0;
    GLuint samplers_[3][2] = {};  // [WrapMode][lineal]
    GLint uViewProj_ = -1, uTex_ = -1, uDiffuse_ = -1, uAlphaRef_ = -1, uFullbright_ = -1;
    GLint uSunDir_ = -1, uSunColor_ = -1, uAmbient_ = -1;
    std::vector<render::gl::Texture> textures_;
    std::vector<Draw> draws_;  // ordenados por pasada
};

}  // namespace render3d::gl
