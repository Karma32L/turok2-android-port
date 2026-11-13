#include "sky_renderer.hpp"

#include <cmath>

#include "log.hpp"
#include "vfs.hpp"

namespace render3d::gl {
namespace {

constexpr float kRadius = 12000.0f;       // semilado del plano (< kFar de la escena)
constexpr float kWorldPerRepeat = 1024.0f;  // unidades del mapa por repetición de textura con tile = 1
constexpr float kScrollPerSecond = 0.01f;   // repeticiones por segundo con animSpeed·speedScalar = 1

constexpr const char* kVertexSrc = R"(#version 300 es
layout(location = 0) in vec2 aPos;    // cuadrado unidad [-1, 1]²
uniform mat4 uViewProj;
uniform vec3 uCenter;
uniform float uRadius;
uniform vec2 uUVOffset;
uniform float uUVScale;
out vec2 vUV;
out float vEdge;
void main() {
    vec2 local = aPos * uRadius;
    vUV = local * uUVScale + uUVOffset;
    vEdge = length(aPos);
    gl_Position = uViewProj * vec4(uCenter + vec3(local, 0.0), 1.0);
}
)";

constexpr const char* kFragmentSrc = R"(#version 300 es
precision highp float;
uniform sampler2D uTex;
uniform vec3 uWhite;
uniform vec3 uBlack;
uniform float uOpacity;
uniform vec3 uFogColor;
in vec2 vUV;
in float vEdge;
out vec4 oColor;
void main() {
    vec4 t = texture(uTex, vUV);
    vec3 c = mix(uBlack, uWhite, t.rgb);
    // Hacia el borde del plano: color de la niebla y transparente (fondo = niebla)
    float horizon = smoothstep(0.25, 1.0, vEdge);
    c = mix(c, uFogColor, horizon);
    oColor = vec4(c, t.a * uOpacity * (1.0 - horizon));
}
)";

void rgb(const std::uint8_t in[4], float out[3]) {
    for (int i = 0; i < 3; ++i) out[i] = static_cast<float>(in[i]) / 255.0f;
}

}  // namespace

SkyRenderer::~SkyRenderer() { release(); }

void SkyRenderer::release() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (program_) glDeleteProgram(program_);
    layers_.clear();  // ~Texture borra las texturas
    abandon();
}

void SkyRenderer::abandon() {
    program_ = vao_ = vbo_ = 0;
    for (auto& l : layers_) l.texture.abandon();
    layers_.clear();
}

bool SkyRenderer::init(const std::vector<SkyLayer>& layers, const std::vector<Material>& materials, const vfs::Vfs& fs,
                       std::string* err) {
    release();
    if (layers.empty()) return true;
    render::gl::clearGlErrors();
    program_ = render::gl::linkProgram(kVertexSrc, kFragmentSrc, err);
    if (!program_) return false;
    uViewProj_ = glGetUniformLocation(program_, "uViewProj");
    uCenter_ = glGetUniformLocation(program_, "uCenter");
    uRadius_ = glGetUniformLocation(program_, "uRadius");
    uUVOffset_ = glGetUniformLocation(program_, "uUVOffset");
    uUVScale_ = glGetUniformLocation(program_, "uUVScale");
    uTex_ = glGetUniformLocation(program_, "uTex");
    uWhite_ = glGetUniformLocation(program_, "uWhite");
    uBlack_ = glGetUniformLocation(program_, "uBlack");
    uOpacity_ = glGetUniformLocation(program_, "uOpacity");
    uFogColor_ = glGetUniformLocation(program_, "uFogColor");

    for (std::size_t i = 0; i < layers.size() && i < materials.size(); ++i) {
        const SkyLayer& s = layers[i];
        const Material& m = materials[i];
        if (m.noDraw || m.texture.empty()) continue;
        auto tex = loadVfsTexture(fs, m.texture);
        if (!tex) continue;
        // Texture::create deja CLAMP_TO_EDGE; las nubes se repiten
        glBindTexture(GL_TEXTURE_2D, tex->id());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glBindTexture(GL_TEXTURE_2D, 0);
        Layer l;
        l.texture = std::move(*tex);
        rgb(s.white, l.white);
        rgb(s.black, l.black);
        // color_diffuse del material tiñe la capa (sky4/sky3 son amarillentas)
        for (int k = 0; k < 3; ++k) {
            l.white[k] *= m.diffuse[k];
            l.black[k] *= m.diffuse[k];
        }
        l.opacity = static_cast<float>(s.opacity) / 255.0f;
        l.height = s.heightOffset;
        l.track = s.cameraTrack;
        l.tile = s.tile / kWorldPerRepeat;
        l.scroll = s.animSpeed * s.speedScalar * kScrollPerSecond;
        layers_.push_back(std::move(l));
    }
    LOGI("WORLD: cielo con %zu de %zu capas", layers_.size(), layers.size());
    if (layers_.empty()) return true;

    static const float quad[] = {-1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1};
    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);
    const GLenum e = glGetError();
    if (e != GL_NO_ERROR) {
        if (err) *err = "cielo (GL error " + std::to_string(e) + ")";
        release();
        return false;
    }
    return true;
}

void SkyRenderer::draw(const Mat4& viewProj, Vec3 camPos, float time, const FogParams& fog) {
    if (!ready()) return;
    glUseProgram(program_);
    glUniformMatrix4fv(uViewProj_, 1, GL_FALSE, viewProj.m);
    glUniform1f(uRadius_, kRadius);
    glUniform1i(uTex_, 0);
    glUniform3fv(uFogColor_, 1, fog.color);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(vao_);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);  // se ve desde abajo
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    for (const Layer& l : layers_) {
        glUniform3f(uCenter_, camPos.x, camPos.y, camPos.z + l.height);
        // Parallax: las nubes se desplazan cameraTrack veces lo que la cámara
        const float u = std::fmod(camPos.x * l.track * l.tile + time * l.scroll, 1.0f);
        const float v = std::fmod(camPos.y * l.track * l.tile, 1.0f);
        glUniform2f(uUVOffset_, u, v);
        glUniform1f(uUVScale_, l.tile);
        glUniform3fv(uWhite_, 1, l.white);
        glUniform3fv(uBlack_, 1, l.black);
        glUniform1f(uOpacity_, l.opacity);
        glBindTexture(GL_TEXTURE_2D, l.texture.id());
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }
    glBindVertexArray(0);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
}

}  // namespace render3d::gl
