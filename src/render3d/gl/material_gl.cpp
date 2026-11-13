#include "material_gl.hpp"

#include <algorithm>
#include <cstring>

#include "log.hpp"
#include "png.hpp"
#include "vfs.hpp"

namespace render3d::gl {
namespace {

constexpr GLenum kTexMaxAnisotropy = 0x84FE;     // GL_TEXTURE_MAX_ANISOTROPY_EXT
constexpr GLenum kMaxTexMaxAnisotropy = 0x84FF;  // GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
constexpr float kAnisotropy = 4.0f;
constexpr std::size_t kMaxTextureBytes = 32u << 20;  // por PNG comprimido

bool hasExtension(const char* name) {
    GLint n = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    for (GLint i = 0; i < n; ++i) {
        const auto* e = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
        if (e && std::strcmp(e, name) == 0) return true;
    }
    return false;
}

render::Image whiteImage() {
    render::Image img;
    img.width = img.height = 1;
    img.channels = 4;
    img.pixels = {255, 255, 255, 255};
    return img;
}

}  // namespace

// highp: las UV de texturas repetidas en mallas grandes salen del rango útil de mediump
const char* const kLitFragmentSrc = R"(#version 300 es
precision highp float;
uniform sampler2D uTex;
uniform vec4 uDiffuse;
uniform float uAlphaRef;    // < 0: sin alpha test
uniform float uFullbright;  // 1 = sin iluminación
uniform vec3 uSunDir;       // hacia el sol, normalizado
uniform vec3 uSunColor;
uniform vec3 uAmbient;
uniform vec3 uCamPos;
uniform vec3 uFogColor;
uniform vec2 uFogRange;     // (inicio, 1 / (fin - inicio)); sin niebla: inicio enorme
in vec2 vUV;
in vec3 vNormal;
in vec3 vWorld;
out vec4 oColor;
void main() {
    vec4 c = texture(uTex, vUV) * uDiffuse;
    if (c.a < uAlphaRef) discard;
    // Caras sin culling vistas por detrás: usar la normal del lado visible
    vec3 n = normalize(gl_FrontFacing ? vNormal : -vNormal);
    vec3 light = uAmbient + uSunColor * max(dot(n, uSunDir), 0.0);
    c.rgb *= mix(light, vec3(1.0), uFullbright);
    // Niebla lineal por distancia a la cámara (por área del nivel, ver FogParams)
    float fog = clamp((distance(vWorld, uCamPos) - uFogRange.x) * uFogRange.y, 0.0, 1.0);
    c.rgb = mix(c.rgb, uFogColor, fog);
    oColor = c;
}
)";

std::optional<render::gl::Texture> loadVfsTexture(const vfs::Vfs& fs, const std::string& path) {
    auto data = fs.read(path);
    if (!data || data->size() > kMaxTextureBytes) {
        LOGE("WORLD: textura %s ausente o demasiado grande", path.c_str());
        return std::nullopt;
    }
    std::string err;
    auto img = render::decodePng(data->data(), data->size(), &err);
    if (!img) {
        LOGE("WORLD: %s: %s", path.c_str(), err.c_str());
        return std::nullopt;
    }
    auto tex = render::gl::Texture::create(*img, true, true, &err);
    if (!tex) LOGE("WORLD: textura %s: %s", path.c_str(), err.c_str());
    return tex;
}

// ------------------------------------------------------------------ SamplerSet

void SamplerSet::init() {
    release();
    float aniso = 0.0f;
    if (hasExtension("GL_EXT_texture_filter_anisotropic")) {
        glGetFloatv(kMaxTexMaxAnisotropy, &aniso);
        aniso = std::min(aniso, kAnisotropy);
    }
    const GLenum wraps[3] = {GL_REPEAT, GL_MIRRORED_REPEAT, GL_CLAMP_TO_EDGE};
    for (int w = 0; w < 3; ++w) {
        for (int linear = 0; linear < 2; ++linear) {
            GLuint& s = samplers_[w][linear];
            glGenSamplers(1, &s);
            glSamplerParameteri(s, GL_TEXTURE_WRAP_S, static_cast<GLint>(wraps[w]));
            glSamplerParameteri(s, GL_TEXTURE_WRAP_T, static_cast<GLint>(wraps[w]));
            glSamplerParameteri(s, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST);
            glSamplerParameteri(s, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
            if (linear && aniso > 1.0f) glSamplerParameterf(s, kTexMaxAnisotropy, aniso);
        }
    }
}

void SamplerSet::release() {
    for (auto& row : samplers_)
        for (GLuint& s : row)
            if (s) glDeleteSamplers(1, &s);
    abandon();
}

void SamplerSet::abandon() {
    for (auto& row : samplers_)
        for (GLuint& s : row) s = 0;
}

GLuint SamplerSet::forMaterial(const Material& m) const {
    const int w = m.wrap == WrapMode::Mirrored ? 1 : m.wrap == WrapMode::Clamp ? 2 : 0;
    return samplers_[w][m.linearFilter ? 1 : 0];
}

// ------------------------------------------------------------------ MaterialTextures

bool MaterialTextures::init(const std::vector<Material>& materials, const vfs::Vfs& fs, std::string* err) {
    release();
    loaded_ = failed_ = 0;
    ofMaterial_.assign(materials.size(), 0);
    std::string texErr;
    auto white = render::gl::Texture::create(whiteImage(), false, false, &texErr);
    if (!white) {
        if (err) *err = "textura blanca: " + texErr;
        return false;
    }
    textures_.push_back(std::move(*white));
    std::unordered_map<std::string, std::size_t> byPath;
    for (std::size_t mi = 0; mi < materials.size(); ++mi) {
        const std::string& path = materials[mi].texture;
        if (path.empty()) continue;
        auto it = byPath.find(path);
        if (it == byPath.end()) {
            std::size_t idx = 0;
            if (auto tex = loadVfsTexture(fs, path)) {
                textures_.push_back(std::move(*tex));
                idx = textures_.size() - 1;
                ++loaded_;
            } else {
                ++failed_;
            }
            it = byPath.emplace(path, idx).first;
        }
        ofMaterial_[mi] = it->second;
    }
    return true;
}

void MaterialTextures::abandon() {
    for (auto& t : textures_) t.abandon();
    textures_.clear();
}

GLuint MaterialTextures::forMaterial(std::size_t material) const {
    const std::size_t i = material < ofMaterial_.size() ? ofMaterial_[material] : 0;
    return i < textures_.size() ? textures_[i].id() : 0;
}

}  // namespace render3d::gl
