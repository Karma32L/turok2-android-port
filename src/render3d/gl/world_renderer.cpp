#include "world_renderer.hpp"

#include <SDL.h>

#include <algorithm>

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
out vec3 vWorld;
void main() {
    vUV = aUV;
    vNormal = aNormal;
    vWorld = aPos;
    gl_Position = uViewProj * vec4(aPos, 1.0);
}
)";


}  // namespace

WorldRenderer::~WorldRenderer() { release(); }

void WorldRenderer::release() {
    if (ibo_) glDeleteBuffers(1, &ibo_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (program_) glDeleteProgram(program_);
    samplers_.release();
    textures_.release();
    abandon();
}

void WorldRenderer::abandon() {
    program_ = vao_ = vbo_ = ibo_ = 0;
    samplers_.abandon();
    textures_.abandon();
    draws_.clear();
}

bool WorldRenderer::initProgram(std::string* err) {
    program_ = render::gl::linkProgram(kVertexSrc, kLitFragmentSrc, err);
    if (!program_) return false;
    uViewProj_ = glGetUniformLocation(program_, "uViewProj");
    uTex_ = glGetUniformLocation(program_, "uTex");
    uDiffuse_ = glGetUniformLocation(program_, "uDiffuse");
    uAlphaRef_ = glGetUniformLocation(program_, "uAlphaRef");
    uFullbright_ = glGetUniformLocation(program_, "uFullbright");
    uSunDir_ = glGetUniformLocation(program_, "uSunDir");
    uSunColor_ = glGetUniformLocation(program_, "uSunColor");
    uAmbient_ = glGetUniformLocation(program_, "uAmbient");
    uCamPos_ = glGetUniformLocation(program_, "uCamPos");
    uFogColor_ = glGetUniformLocation(program_, "uFogColor");
    uFogRange_ = glGetUniformLocation(program_, "uFogRange");
    return true;
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
    samplers_.init();
    if (!textures_.init(geo.materials, fs, err)) {
        release();
        return false;
    }
    st.textures = textures_.loaded();
    st.texturesFailed = textures_.failed();

    // Lotes → llamadas de dibujo ordenadas por pasada (estable: conserva el orden del nivel)
    for (const LevelBatch& b : geo.batches) {
        const Material& m = geo.materials[b.material];
        Draw d;
        d.pass = m.translucent() ? Translucent : m.masked() ? Masked : Opaque;
        d.material = b.material;
        d.sampler = samplers_.forMaterial(m);
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

void WorldRenderer::draw(const Mat4& viewProj, Vec3 camPos, const FogParams& fog) {
    if (!ready()) return;
    glUseProgram(program_);
    glUniformMatrix4fv(uViewProj_, 1, GL_FALSE, viewProj.m);
    float range[2];
    fogRange(fog, range);
    glUniform3f(uCamPos_, camPos.x, camPos.y, camPos.z);
    glUniform3fv(uFogColor_, 1, fog.color);
    glUniform2fv(uFogRange_, 1, range);
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
        glBindTexture(GL_TEXTURE_2D, textures_.forMaterial(d.material));
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
