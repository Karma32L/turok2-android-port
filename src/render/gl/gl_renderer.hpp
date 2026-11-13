// Capa mínima OpenGL ES 3.0: texturas RAII y un sprite batch 2D.
// Solo se compila para Android (depende de GLESv3); la lógica de formatos vive en
// el núcleo testeable (png, kfont, text_layout).
#pragma once

#include <GLES3/gl3.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "png.hpp"
#include "text_layout.hpp"

namespace render::gl {

// Compila y enlaza un programa GLSL ES. 0 (y el log en *err) si falla.
GLuint linkProgram(const char* vertexSrc, const char* fragmentSrc, std::string* err);

// Descarta errores GL previos para no atribuirlos a la operación siguiente
void clearGlErrors();

struct Color {
    std::uint8_t r = 255, g = 255, b = 255, a = 255;
};

class Texture {
public:
    Texture() = default;
    ~Texture();
    Texture(Texture&& o) noexcept;
    Texture& operator=(Texture&& o) noexcept;
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    // Sube una imagen RGB8/RGBA8. `mipmaps` genera la cadena de mipmaps (para texturas
    // que se dibujan reducidas, como el logo de 2048 px en pantallas pequeñas).
    static std::optional<Texture> create(const Image& img, bool linear, bool mipmaps, std::string* err);

    // Olvida el ID sin glDeleteTextures: usar cuando el contexto GL se ha perdido
    // (en el contexto nuevo ese nombre podría pertenecer a otro objeto).
    void abandon() { id_ = 0; }

    GLuint id() const { return id_; }
    std::uint32_t width() const { return w_; }
    std::uint32_t height() const { return h_; }

private:
    GLuint id_ = 0;
    std::uint32_t w_ = 0, h_ = 0;
};

class SpriteBatch {
public:
    SpriteBatch() = default;
    ~SpriteBatch();
    SpriteBatch(const SpriteBatch&) = delete;
    SpriteBatch& operator=(const SpriteBatch&) = delete;

    bool init(std::string* err);
    void abandon();  // ver Texture::abandon

    // Coordenadas en píxeles del framebuffer, origen arriba-izquierda.
    void begin(int viewportW, int viewportH);
    void draw(const Texture& tex, const Quad& q, Color c = {});
    void drawImage(const Texture& tex, float x, float y, float w, float h, Color c = {});
    void drawText(const Texture& atlas, const TextLayout& text, float x, float y, Color c = {});
    void end();

private:
    struct Vertex {
        float x, y, u, v;
        std::uint8_t r, g, b, a;
    };
    static constexpr std::size_t kMaxQuads = 4096;  // índices uint16: 4 * 4096 < 65536

    void flush();
    void release();

    GLuint program_ = 0, vao_ = 0, vbo_ = 0, ibo_ = 0;
    GLint uScale_ = -1, uTex_ = -1;
    GLuint curTex_ = 0;
    std::vector<Vertex> verts_;
};

}  // namespace render::gl
