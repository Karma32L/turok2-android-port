#include "world_renderer.hpp"

#include <SDL.h>

#include <algorithm>
#include <cstring>
#include <unordered_map>

#include "log.hpp"
#include "png.hpp"
#include "vfs.hpp"

namespace render3d::gl {
namespace {

// GLSL ES 3.00 propio. Los vértices ya están en espacio de mundo (level_builder).
constexpr const char* kVertexSrc = R"(#version 300 es
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec3 aNormal;
uniform mat4 uViewProj;
out vec2 vUV;
out vec3 vNormal;
void main() {
    vUV = aUV;
    vNormal = aNormal;
    gl_Position = uViewProj * vec4(aPos, 1.0);
}
)";

// highp: las UV de texturas repetidas en mallas grandes salen del rango útil de mediump
constexpr const char* kFragmentSrc = R"(#version 300 es
precision highp float;
uniform sampler2D uTex;
uniform vec4 uDiffuse;
uniform float uAlphaRef;    // < 0: sin alpha test
uniform float uFullbright;  // 1 = sin iluminación
uniform vec3 uSunDir;       // hacia el sol, normalizado
uniform vec3 uSunColor;
uniform vec3 uAmbient;
in vec2 vUV;
in vec3 vNormal;
out vec4 oColor;
void main() {
    vec4 c = texture(uTex, vUV) * uDiffuse;
    if (c.a < uAlphaRef) discard;
    // Caras sin culling vistas por detrás: usar la normal del lado visible
    vec3 n = normalize(gl_FrontFacing ? vNormal : -vNormal);
    vec3 light = uAmbient + uSunColor * max(dot(n, uSunDir), 0.0);
    c.rgb *= mix(light, vec3(1.0), uFullbright);
    oColor = c;
}
)";

// Iluminación fija hasta interpretar kexSunLightComponent de la sección 5 del .map
constexpr float kSunDir[3] = {0.40f, 0.30f, 0.866f};
constexpr float kSunColor[3] = {0.70f, 0.68f, 0.62f};
constexpr float kAmbient[3] = {0.42f, 0.44f, 0.50f};

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

std::optional<render::gl::Texture> loadTexture(const vfs::Vfs& fs, const std::string& path) {
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

}  // namespace

WorldRenderer::~WorldRenderer() { release(); }

void WorldRenderer::release() {
    if (ibo_) glDeleteBuffers(1, &ibo_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (program_) glDeleteProgram(program_);
    for (auto& row : samplers_)
        for (GLuint& s : row)
            if (s) glDeleteSamplers(1, &s);
    textures_.clear();  // ~Texture llama a glDeleteTextures
    abandon();
}

void WorldRenderer::abandon() {
    program_ = vao_ = vbo_ = ibo_ = 0;
    for (auto& row : samplers_)
        for (GLuint& s : row) s = 0;
    for (auto& t : textures_) t.abandon();
    textures_.clear();
    draws_.clear();
}

bool WorldRenderer::initProgram(std::string* err) {
    program_ = render::gl::linkProgram(kVertexSrc, kFragmentSrc, err);
    if (!program_) return false;
    uViewProj_ = glGetUniformLocation(program_, "uViewProj");
    uTex_ = glGetUniformLocation(program_, "uTex");
    uDiffuse_ = glGetUniformLocation(program_, "uDiffuse");
    uAlphaRef_ = glGetUniformLocation(program_, "uAlphaRef");
    uFullbright_ = glGetUniformLocation(program_, "uFullbright");
    uSunDir_ = glGetUniformLocation(program_, "uSunDir");
    uSunColor_ = glGetUniformLocation(program_, "uSunColor");
    uAmbient_ = glGetUniformLocation(program_, "uAmbient");
    return true;
}

void WorldRenderer::initSamplers() {
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

GLuint WorldRenderer::samplerFor(const Material& m) const {
    const int w = m.wrap == WrapMode::Mirrored ? 1 : m.wrap == WrapMode::Clamp ? 2 : 0;
    return samplers_[w][m.linearFilter ? 1 : 0];
}

bool WorldRenderer::init(const LevelGeometry& geo, const vfs::Vfs& fs, UploadStats* stats, std::string* err) {
    release();
    const Uint64 t0 = SDL_GetPerformanceCounter();
    UploadStats st;
    if (geo.vertices.empty() || geo.indices.empty()) {
        if (err) *err = "el nivel no tiene geometría";
        return false;
    }
    render::gl::clearGlErrors();
    if (!initProgram(err)) return false;
    initSamplers();

    // Texturas: [0] blanca de reserva + una por ruta distinta
    std::string texErr;
    auto white = render::gl::Texture::create(whiteImage(), false, false, &texErr);
    if (!white) {
        if (err) *err = "textura blanca: " + texErr;
        release();
        return false;
    }
    textures_.push_back(std::move(*white));
    std::unordered_map<std::string, std::size_t> byPath;
    std::vector<std::size_t> texOfMaterial(geo.materials.size(), 0);
    for (std::size_t mi = 0; mi < geo.materials.size(); ++mi) {
        const std::string& path = geo.materials[mi].texture;
        if (path.empty()) continue;
        auto it = byPath.find(path);
        if (it == byPath.end()) {
            std::size_t idx = 0;
            if (auto tex = loadTexture(fs, path)) {
                textures_.push_back(std::move(*tex));
                idx = textures_.size() - 1;
                ++st.textures;
            } else {
                ++st.texturesFailed;
            }
            it = byPath.emplace(path, idx).first;
        }
        texOfMaterial[mi] = it->second;
    }

    // Lotes → llamadas de dibujo ordenadas por pasada (estable: conserva el orden del nivel)
    for (const LevelBatch& b : geo.batches) {
        const Material& m = geo.materials[b.material];
        Draw d;
        d.pass = m.translucent() ? Translucent : m.masked() ? Masked : Opaque;
        d.texture = texOfMaterial[b.material];
        d.sampler = samplerFor(m);
        d.cull = m.cullBack;
        d.depthWrite = m.depthWrite && d.pass != Translucent;
        d.fullbright = m.fullbright;
        d.alphaRef = d.pass == Masked ? m.alphaRef : -1.0f;
        std::copy(std::begin(m.diffuse), std::end(m.diffuse), std::begin(d.diffuse));
        d.firstIndex = b.firstIndex;
        d.indexCount = b.indexCount;
        draws_.push_back(d);
    }
    std::stable_sort(draws_.begin(), draws_.end(), [](const Draw& a, const Draw& b) { return a.pass < b.pass; });

    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(geo.vertices.size() * sizeof(LevelVertex)),
                 geo.vertices.data(), GL_STATIC_DRAW);
    glGenBuffers(1, &ibo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);  // queda asociado al VAO
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(geo.indices.size() * sizeof(std::uint32_t)),
                 geo.indices.data(), GL_STATIC_DRAW);
    const auto stride = static_cast<GLsizei>(sizeof(LevelVertex));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(LevelVertex, pos)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(LevelVertex, uv)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void*>(offsetof(LevelVertex, normal)));
    glBindVertexArray(0);

    const GLenum glErr = glGetError();
    if (glErr != GL_NO_ERROR) {
        if (err) *err = "subida de la geometría (GL error " + std::to_string(glErr) + ")";
        release();
        return false;
    }
    st.ms = 1000.0 * static_cast<double>(SDL_GetPerformanceCounter() - t0) /
            static_cast<double>(SDL_GetPerformanceFrequency());
    if (stats) *stats = st;
    return true;
}

void WorldRenderer::draw(const Mat4& viewProj) {
    if (!ready()) return;
    glUseProgram(program_);
    glUniformMatrix4fv(uViewProj_, 1, GL_FALSE, viewProj.m);
    glUniform1i(uTex_, 0);
    glUniform3fv(uSunDir_, 1, kSunDir);
    glUniform3fv(uSunColor_, 1, kSunColor);
    glUniform3fv(uAmbient_, 1, kAmbient);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(vao_);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glFrontFace(GL_CCW);
    glCullFace(GL_BACK);
    glDisable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    for (const Draw& d : draws_) {
        if (d.pass == Translucent) glEnable(GL_BLEND);
        if (d.cull) glEnable(GL_CULL_FACE);
        else glDisable(GL_CULL_FACE);
        glDepthMask(d.depthWrite ? GL_TRUE : GL_FALSE);
        glBindTexture(GL_TEXTURE_2D, textures_[d.texture].id());
        glBindSampler(0, d.sampler);
        glUniform4fv(uDiffuse_, 1, d.diffuse);
        glUniform1f(uAlphaRef_, d.alphaRef);
        glUniform1f(uFullbright_, d.fullbright ? 1.0f : 0.0f);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(d.indexCount), GL_UNSIGNED_INT,
                       reinterpret_cast<const void*>(std::uintptr_t{d.firstIndex} * sizeof(std::uint32_t)));
    }

    // Estado que espera el resto (SpriteBatch del HUD)
    glBindSampler(0, 0);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
}

}  // namespace render3d::gl
