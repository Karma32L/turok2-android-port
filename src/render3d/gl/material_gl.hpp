// Piezas GL compartidas por los renderers 3D (solo Android): samplers por material (wrap ×
// filtrado, con anisotropía si la extensión existe), textura blanca de reserva, carga de PNG del
// VFS con caché por ruta y los parámetros fijos de iluminación.
#pragma once

#include <GLES3/gl3.h>

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "kmat.hpp"
#include "render/gl/gl_renderer.hpp"

namespace vfs {
class Vfs;
}

namespace render3d::gl {

// Iluminación fija hasta interpretar kexSunLightComponent de la sección 5 del .map
constexpr float kSunDir[3] = {0.40f, 0.30f, 0.866f};
constexpr float kSunColor[3] = {0.70f, 0.68f, 0.62f};
constexpr float kAmbient[3] = {0.42f, 0.44f, 0.50f};

// Fragment shader GLSL ES 3.00 común (textura × color difuso, alpha test, sol + ambiente, niebla
// lineal). Espera del vertex shader: vUV, vNormal (mundo) y vWorld (posición en mundo).
// Uniforms: uTex, uDiffuse, uAlphaRef, uFullbright, uSunDir, uSunColor, uAmbient, uCamPos,
// uFogColor, uFogRange.
extern const char* const kLitFragmentSrc;

// PNG del VFS → textura con mipmaps (nullopt y log si falta o es inválido)
std::optional<render::gl::Texture> loadVfsTexture(const vfs::Vfs& fs, const std::string& path);

class SamplerSet {
public:
    ~SamplerSet() { release(); }
    void init();
    void release();  // glDeleteSamplers (contexto vivo)
    void abandon();  // contexto perdido
    GLuint forMaterial(const Material& m) const;

private:
    GLuint samplers_[3][2] = {};  // [WrapMode][lineal]
};

// Texturas de un conjunto de materiales: [0] blanca + una por ruta distinta (caché por ruta)
class MaterialTextures {
public:
    // false solo si no se pudo crear la blanca; las ausentes usan la blanca y se cuentan
    bool init(const std::vector<Material>& materials, const vfs::Vfs& fs, std::string* err);
    void release() { textures_.clear(); }  // ~Texture llama a glDeleteTextures
    void abandon();
    GLuint forMaterial(std::size_t material) const;
    std::size_t loaded() const { return loaded_; }
    std::size_t failed() const { return failed_; }

private:
    std::vector<render::gl::Texture> textures_;
    std::vector<std::size_t> ofMaterial_;
    std::size_t loaded_ = 0, failed_ = 0;
};

}  // namespace render3d::gl
