#include "skinned_renderer.hpp"

#include <algorithm>
#include <cstring>

#include "vfs.hpp"

namespace render3d::gl {
namespace {

constexpr GLuint kBonesBinding = 0;
constexpr std::size_t kFloatsPerBone = 12;  // 3 vec4

// Skinning rígido: cada vértice va con un único hueso, en el espacio local de ese hueso.
// Normal: rotación del hueso y la parte 3×3 del modelo (las escalas de actor son casi uniformes).
constexpr const char* kVertexSrc = R"(#version 300 es
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec3 aNormal;
layout(location = 3) in uint aBone;
layout(std140) uniform Bones {
    vec4 uBones[384];  // 128 huesos × 3 filas de [R | t]
};
uniform mat4 uViewProj;
uniform mat4 uModel;
uniform uint uBoneMax;  // último índice válido de la paleta subida
out vec2 vUV;
out vec3 vNormal;
out vec3 vWorld;
void main() {
    int b = int(min(aBone, uBoneMax)) * 3;
    vec4 p = vec4(aPos, 1.0);
    vec3 local = vec3(dot(uBones[b], p), dot(uBones[b + 1], p), dot(uBones[b + 2], p));
    vec3 n = vec3(dot(uBones[b].xyz, aNormal), dot(uBones[b + 1].xyz, aNormal), dot(uBones[b + 2].xyz, aNormal));
    vec4 world = uModel * vec4(local, 1.0);
    vUV = aUV;
    vNormal = normalize(mat3(uModel) * n);
    vWorld = world.xyz;
    gl_Position = uViewProj * world;
}
)";

float det3(const Mat4& m) {
    return m.at(0, 0) * (m.at(1, 1) * m.at(2, 2) - m.at(1, 2) * m.at(2, 1)) -
           m.at(0, 1) * (m.at(1, 0) * m.at(2, 2) - m.at(1, 2) * m.at(2, 0)) +
           m.at(0, 2) * (m.at(1, 0) * m.at(2, 1) - m.at(1, 1) * m.at(2, 0));
}

}  // namespace

SkinnedRenderer::~SkinnedRenderer() { release(); }

void SkinnedRenderer::release() {
    for (Model& m : models_) {
        if (m.ibo) glDeleteBuffers(1, &m.ibo);
        if (m.vbo) glDeleteBuffers(1, &m.vbo);
        if (m.vao) glDeleteVertexArrays(1, &m.vao);
    }
    if (ubo_) glDeleteBuffers(1, &ubo_);
    if (program_) glDeleteProgram(program_);
    samplers_.release();
    textures_.release();
    abandon();
}

void SkinnedRenderer::abandon() {
    program_ = ubo_ = 0;
    models_.clear();
    samplers_.abandon();
    textures_.abandon();
}

bool SkinnedRenderer::initProgram(std::string* err) {
    program_ = render::gl::linkProgram(kVertexSrc, kLitFragmentSrc, err);
    if (!program_) return false;
    const GLuint block = glGetUniformBlockIndex(program_, "Bones");
    if (block == GL_INVALID_INDEX) {
        if (err) *err = "el shader no tiene el bloque Bones";
        return false;
    }
    glUniformBlockBinding(program_, block, kBonesBinding);
    uViewProj_ = glGetUniformLocation(program_, "uViewProj");
    uModel_ = glGetUniformLocation(program_, "uModel");
    uBoneMax_ = glGetUniformLocation(program_, "uBoneMax");
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

bool SkinnedRenderer::init(const ActorSet& set, const vfs::Vfs& fs, std::string* err) {
    release();
    render::gl::clearGlErrors();
    if (!initProgram(err)) {
        release();
        return false;
    }
    samplers_.init();
    if (!textures_.init(set.materials, fs, err)) {
        release();
        return false;
    }
    glGenBuffers(1, &ubo_);
    glBindBuffer(GL_UNIFORM_BUFFER, ubo_);
    glBufferData(GL_UNIFORM_BUFFER, static_cast<GLsizeiptr>(kMaxBones * kFloatsPerBone * sizeof(float)), nullptr,
                 GL_DYNAMIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    models_.resize(set.models.size());
    for (std::size_t mi = 0; mi < set.models.size(); ++mi) {
        const ActorModel& src = set.models[mi];
        Model& dst = models_[mi];
        dst.bones = std::min(src.mesh.boneCount(), kMaxBones);
        std::vector<std::uint16_t> indices;
        for (std::size_t si = 0; si < src.mesh.surfaces.size(); ++si) {
            const MeshSurface& surf = src.mesh.surfaces[si];
            const Material& m = set.materials[src.surfaceMaterial[si]];
            if (surf.indices.empty() || m.noDraw) continue;
            Draw d;
            d.pass = m.translucent() ? Translucent : m.masked() ? Masked : Opaque;
            d.material = src.surfaceMaterial[si];
            d.sampler = samplers_.forMaterial(m);
            d.cull = m.cullBack;
            d.depthWrite = m.depthWrite && d.pass != Translucent;
            d.fullbright = m.fullbright;
            d.alphaRef = d.pass == Masked ? m.alphaRef : -1.0f;
            std::copy(std::begin(m.diffuse), std::end(m.diffuse), std::begin(d.diffuse));
            d.firstIndex = static_cast<std::uint32_t>(indices.size());
            d.indexCount = static_cast<std::uint32_t>(surf.indices.size());
            indices.insert(indices.end(), surf.indices.begin(), surf.indices.end());
            dst.draws.push_back(d);
        }
        std::stable_sort(dst.draws.begin(), dst.draws.end(), [](const Draw& a, const Draw& b) { return a.pass < b.pass; });
        if (src.mesh.vertices.empty() || indices.empty()) continue;

        glGenVertexArrays(1, &dst.vao);
        glBindVertexArray(dst.vao);
        glGenBuffers(1, &dst.vbo);
        glBindBuffer(GL_ARRAY_BUFFER, dst.vbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(src.mesh.vertices.size() * sizeof(SkinnedVertex)),
                     src.mesh.vertices.data(), GL_STATIC_DRAW);
        glGenBuffers(1, &dst.ibo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, dst.ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint16_t)),
                     indices.data(), GL_STATIC_DRAW);
        const auto stride = static_cast<GLsizei>(sizeof(SkinnedVertex));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(SkinnedVertex, pos)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(SkinnedVertex, uv)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<const void*>(offsetof(SkinnedVertex, normal)));
        glEnableVertexAttribArray(3);
        glVertexAttribIPointer(3, 1, GL_UNSIGNED_SHORT, stride, reinterpret_cast<const void*>(offsetof(SkinnedVertex, bone)));
        glBindVertexArray(0);
    }

    const GLenum glErr = glGetError();
    if (glErr != GL_NO_ERROR) {
        if (err) *err = "subida de los modelos (GL error " + std::to_string(glErr) + ")";
        release();
        return false;
    }
    return true;
}

void SkinnedRenderer::begin(const Mat4& viewProj, Vec3 camPos, const Fog& fog) {
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
    glBindBufferBase(GL_UNIFORM_BUFFER, kBonesBinding, ubo_);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glCullFace(GL_BACK);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void SkinnedRenderer::drawModel(std::size_t model, const Mat4& modelMatrix, const std::vector<BoneMatrix>& palette) {
    if (!ready() || model >= models_.size()) return;
    const Model& m = models_[model];
    if (!m.vao || palette.empty()) return;

    // Paleta → UBO (como mucho los huesos del modelo; el shader acota el índice a lo subido)
    const std::size_t n = std::min({palette.size(), m.bones, kMaxBones});
    uboScratch_.resize(n * kFloatsPerBone);
    for (std::size_t b = 0; b < n; ++b) std::memcpy(&uboScratch_[b * kFloatsPerBone], palette[b].row, sizeof palette[b].row);
    glBindBuffer(GL_UNIFORM_BUFFER, ubo_);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, static_cast<GLsizeiptr>(uboScratch_.size() * sizeof(float)), uboScratch_.data());
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
    glUniform1ui(uBoneMax_, static_cast<GLuint>(n - 1));
    glUniformMatrix4fv(uModel_, 1, GL_FALSE, modelMatrix.m);
    glFrontFace(det3(modelMatrix) < 0.0f ? GL_CW : GL_CCW);  // escala espejo: conserva el CCW
    glBindVertexArray(m.vao);
    for (const Draw& d : m.draws) {
        if (d.pass == Translucent) glEnable(GL_BLEND);
        else glDisable(GL_BLEND);
        if (d.cull) glEnable(GL_CULL_FACE);
        else glDisable(GL_CULL_FACE);
        glDepthMask(d.depthWrite ? GL_TRUE : GL_FALSE);
        glBindTexture(GL_TEXTURE_2D, textures_.forMaterial(d.material));
        glBindSampler(0, d.sampler);
        glUniform4fv(uDiffuse_, 1, d.diffuse);
        glUniform1f(uAlphaRef_, d.alphaRef);
        glUniform1f(uFullbright_, d.fullbright ? 1.0f : 0.0f);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(d.indexCount), GL_UNSIGNED_SHORT,
                       reinterpret_cast<const void*>(std::uintptr_t{d.firstIndex} * sizeof(std::uint16_t)));
    }
}

void SkinnedRenderer::end() {
    if (!ready()) return;
    glBindSampler(0, 0);
    glBindVertexArray(0);
    glBindBufferBase(GL_UNIFORM_BUFFER, kBonesBinding, 0);
    glFrontFace(GL_CCW);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
}

}  // namespace render3d::gl
