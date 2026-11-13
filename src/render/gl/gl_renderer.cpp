#include "gl_renderer.hpp"

#include <cstddef>
#include <utility>

namespace render::gl {
namespace {

// Shaders propios GLSL ES 3.00 (los .shader del juego usan un macro-lenguaje HLSL/D3D).
constexpr const char* kVertexSrc = R"(#version 300 es
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
uniform vec2 uScale;  // (2/ancho, -2/alto): píxeles -> NDC con y hacia abajo
out vec2 vUV;
out vec4 vColor;
void main() {
    vUV = aUV;
    vColor = aColor;
    gl_Position = vec4(aPos * uScale + vec2(-1.0, 1.0), 0.0, 1.0);
}
)";

constexpr const char* kFragmentSrc = R"(#version 300 es
precision mediump float;
uniform sampler2D uTex;
in vec2 vUV;
in vec4 vColor;
out vec4 oColor;
void main() {
    oColor = texture(uTex, vUV) * vColor;
}
)";

std::string infoLog(GLuint obj, bool isProgram) {
    GLint len = 0;
    if (isProgram) glGetProgramiv(obj, GL_INFO_LOG_LENGTH, &len);
    else glGetShaderiv(obj, GL_INFO_LOG_LENGTH, &len);
    if (len <= 1) return "(sin log)";
    std::string log(static_cast<std::size_t>(len), '\0');
    if (isProgram) glGetProgramInfoLog(obj, len, nullptr, log.data());
    else glGetShaderInfoLog(obj, len, nullptr, log.data());
    log.resize(static_cast<std::size_t>(len - 1));
    return log;
}

GLuint compile(GLenum type, const char* src, std::string* err) {
    const GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok = GL_FALSE;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        if (err) *err = "compilación de shader: " + infoLog(sh, false);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

// Descarta errores GL previos para no atribuirlos a la operación siguiente. Acotado: con el
// contexto perdido algunos drivers devuelven error en cada llamada.
void clearGlErrors() {
    for (int i = 0; i < 16 && glGetError() != GL_NO_ERROR; ++i) {
    }
}

}  // namespace

// ------------------------------------------------------------------------- Texture

Texture::~Texture() {
    if (id_) glDeleteTextures(1, &id_);
}

Texture::Texture(Texture&& o) noexcept
    : id_(std::exchange(o.id_, 0)), w_(o.w_), h_(o.h_) {}

Texture& Texture::operator=(Texture&& o) noexcept {
    if (this != &o) {
        if (id_) glDeleteTextures(1, &id_);
        id_ = std::exchange(o.id_, 0);
        w_ = o.w_;
        h_ = o.h_;
    }
    return *this;
}

std::optional<Texture> Texture::create(const Image& img, bool linear, bool mipmaps, std::string* err) {
    if ((img.channels != 3 && img.channels != 4) || img.width == 0 || img.height == 0 ||
        img.pixels.size() != std::size_t{img.width} * img.height * img.channels) {
        if (err) *err = "imagen inconsistente";
        return std::nullopt;
    }
    GLint maxSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxSize);
    if (maxSize <= 0 || img.width > static_cast<std::uint32_t>(maxSize) ||
        img.height > static_cast<std::uint32_t>(maxSize)) {
        if (err) *err = "textura mayor que GL_MAX_TEXTURE_SIZE (" + std::to_string(maxSize) + ")";
        return std::nullopt;
    }

    clearGlErrors();
    Texture t;
    t.w_ = img.width;
    t.h_ = img.height;
    glGenTextures(1, &t.id_);
    glBindTexture(GL_TEXTURE_2D, t.id_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);  // filas RGB sin relleno a 4 bytes
    const bool rgba = img.channels == 4;
    glTexImage2D(GL_TEXTURE_2D, 0, rgba ? GL_RGBA8 : GL_RGB8, static_cast<GLsizei>(img.width),
                 static_cast<GLsizei>(img.height), 0, rgba ? GL_RGBA : GL_RGB, GL_UNSIGNED_BYTE, img.pixels.data());
    const GLenum magFilter = linear ? GL_LINEAR : GL_NEAREST;
    GLenum minFilter = magFilter;
    if (mipmaps) {
        glGenerateMipmap(GL_TEXTURE_2D);
        minFilter = linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(minFilter));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(magFilter));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    const GLenum glErr = glGetError();
    if (glErr != GL_NO_ERROR) {
        if (err) *err = "glTexImage2D falló (GL error " + std::to_string(glErr) + ")";
        return std::nullopt;  // el destructor de t libera el ID
    }
    return t;
}

// ------------------------------------------------------------------------- SpriteBatch

SpriteBatch::~SpriteBatch() { release(); }

void SpriteBatch::release() {
    if (ibo_) glDeleteBuffers(1, &ibo_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (program_) glDeleteProgram(program_);
    abandon();
}

void SpriteBatch::abandon() {
    program_ = vao_ = vbo_ = ibo_ = 0;
    curTex_ = 0;
    verts_.clear();
}

bool SpriteBatch::init(std::string* err) {
    release();
    clearGlErrors();
    const GLuint vs = compile(GL_VERTEX_SHADER, kVertexSrc, err);
    if (!vs) return false;
    const GLuint fs = compile(GL_FRAGMENT_SHADER, kFragmentSrc, err);
    if (!fs) {
        glDeleteShader(vs);
        return false;
    }
    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);
    glDeleteShader(vs);  // quedan referenciados por el programa
    glDeleteShader(fs);
    GLint ok = GL_FALSE;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (!ok) {
        if (err) *err = "enlace de programa: " + infoLog(program_, true);
        release();
        return false;
    }
    uScale_ = glGetUniformLocation(program_, "uScale");
    uTex_ = glGetUniformLocation(program_, "uTex");

    // Índices estáticos: dos triángulos por quad (0,1,2 / 2,3,0)
    std::vector<GLushort> idx(kMaxQuads * 6);
    for (std::size_t q = 0; q < kMaxQuads; ++q) {
        const auto b = static_cast<GLushort>(q * 4);
        const GLushort tri[6] = {b, static_cast<GLushort>(b + 1), static_cast<GLushort>(b + 2),
                                 static_cast<GLushort>(b + 2), static_cast<GLushort>(b + 3), b};
        for (int k = 0; k < 6; ++k) idx[q * 6 + static_cast<std::size_t>(k)] = tri[k];
    }

    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(kMaxQuads * 4 * sizeof(Vertex)), nullptr, GL_DYNAMIC_DRAW);
    glGenBuffers(1, &ibo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);  // queda asociado al VAO
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(idx.size() * sizeof(GLushort)), idx.data(),
                 GL_STATIC_DRAW);
    const auto stride = static_cast<GLsizei>(sizeof(Vertex));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(Vertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(Vertex, u)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, reinterpret_cast<const void*>(offsetof(Vertex, r)));
    glBindVertexArray(0);

    verts_.reserve(kMaxQuads * 4);
    const GLenum glErr = glGetError();
    if (glErr != GL_NO_ERROR) {
        if (err) *err = "inicialización del sprite batch (GL error " + std::to_string(glErr) + ")";
        release();
        return false;
    }
    return true;
}

void SpriteBatch::begin(int viewportW, int viewportH) {
    glUseProgram(program_);
    glUniform2f(uScale_, 2.0f / static_cast<float>(viewportW > 0 ? viewportW : 1),
                -2.0f / static_cast<float>(viewportH > 0 ? viewportH : 1));
    glUniform1i(uTex_, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(vao_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    curTex_ = 0;
    verts_.clear();
}

void SpriteBatch::draw(const Texture& tex, const Quad& q, Color c) {
    if (!program_ || !tex.id()) return;
    if (tex.id() != curTex_ || verts_.size() >= kMaxQuads * 4) {
        flush();
        curTex_ = tex.id();
    }
    verts_.push_back({q.x0, q.y0, q.u0, q.v0, c.r, c.g, c.b, c.a});
    verts_.push_back({q.x1, q.y0, q.u1, q.v0, c.r, c.g, c.b, c.a});
    verts_.push_back({q.x1, q.y1, q.u1, q.v1, c.r, c.g, c.b, c.a});
    verts_.push_back({q.x0, q.y1, q.u0, q.v1, c.r, c.g, c.b, c.a});
}

void SpriteBatch::drawImage(const Texture& tex, float x, float y, float w, float h, Color c) {
    draw(tex, Quad{x, y, x + w, y + h, 0.0f, 0.0f, 1.0f, 1.0f}, c);
}

void SpriteBatch::drawText(const Texture& atlas, const TextLayout& text, float x, float y, Color c) {
    for (Quad q : text.quads) {
        q.x0 += x;
        q.x1 += x;
        q.y0 += y;
        q.y1 += y;
        draw(atlas, q, c);
    }
}

void SpriteBatch::flush() {
    if (verts_.empty() || !curTex_) {
        verts_.clear();
        return;
    }
    glBindTexture(GL_TEXTURE_2D, curTex_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(verts_.size() * sizeof(Vertex)), verts_.data());
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(verts_.size() / 4 * 6), GL_UNSIGNED_SHORT, nullptr);
    verts_.clear();
}

void SpriteBatch::end() {
    flush();
    glBindVertexArray(0);
}

}  // namespace render::gl
