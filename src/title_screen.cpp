#include "title_screen.hpp"

#include <SDL.h>

#include <algorithm>
#include <optional>

#include "log.hpp"
#include "png.hpp"
#include "text_layout.hpp"

namespace {

constexpr const char* kBackground = "gfx/titleback.png";
constexpr const char* kLogo = "gfx/turok2Logo.png";
constexpr const char* kFont = "fonts/bigfont.kfont";
constexpr const char* kMenuKeys[] = {"mainmenu_newgame", "mainmenu_loadgame", "mainmenu_options",
                                     "mainmenu_extras", "mainmenu_quit"};
constexpr float kAspect = 4.0f / 3.0f;  // los fondos del juego son 4:3 (2048x1536)

std::optional<render::Image> loadImage(const vfs::Vfs& fs, const char* path) {
    const Uint64 t0 = SDL_GetPerformanceCounter();
    auto data = fs.read(path);
    if (!data) {
        LOGE("TITLE: no se pudo leer %s", path);
        return std::nullopt;
    }
    std::string err;
    auto img = render::decodePng(data->data(), data->size(), &err);
    const double ms = 1000.0 * static_cast<double>(SDL_GetPerformanceCounter() - t0) /
                      static_cast<double>(SDL_GetPerformanceFrequency());
    if (!img) {
        LOGE("TITLE: %s: %s", path, err.c_str());
        return std::nullopt;
    }
    LOGI("TITLE: %s %ux%u (%u canales) leído+decodificado en %.1f ms", path, img->width, img->height,
         img->channels, ms);
    return img;
}

std::optional<render::gl::Texture> upload(const render::Image& img, const char* path, bool linear, bool mipmaps) {
    std::string err;
    auto tex = render::gl::Texture::create(img, linear, mipmaps, &err);
    if (!tex) LOGE("TITLE: textura %s: %s", path, err.c_str());
    return tex;
}

// Caja envolvente (en UV) de los píxeles con alfa > umbral; toda la imagen si no hay alfa.
// Permite colocar el logo por su contenido y no por su lienzo transparente.
template <class Rect>
Rect alphaBounds(const render::Image& img, std::uint8_t threshold = 8) {
    if (img.channels != 4) return Rect{};
    std::uint32_t minX = img.width, minY = img.height, maxX = 0, maxY = 0;
    for (std::uint32_t y = 0; y < img.height; ++y) {
        const std::uint8_t* row = img.pixels.data() + std::size_t{y} * img.width * 4;
        for (std::uint32_t x = 0; x < img.width; ++x) {
            if (row[x * 4 + 3] > threshold) {
                minX = std::min(minX, x);
                maxX = std::max(maxX, x);
                minY = std::min(minY, y);
                maxY = std::max(maxY, y);
            }
        }
    }
    if (minX > maxX || minY > maxY) return Rect{};
    const float w = static_cast<float>(img.width), h = static_cast<float>(img.height);
    return Rect{static_cast<float>(minX) / w, static_cast<float>(minY) / h, static_cast<float>(maxX + 1) / w,
                static_cast<float>(maxY + 1) / h};
}

}  // namespace

bool TitleScreen::load(const vfs::Vfs& fs, const loc::Table* strings) {
    ready_ = false;
    std::string err;
    if (!batch_.init(&err)) {
        LOGE("TITLE: %s", err.c_str());
        return false;
    }

    auto bgImg = loadImage(fs, kBackground);
    if (!bgImg) return false;
    auto bg = upload(*bgImg, kBackground, true, false);
    bgImg.reset();  // liberar ~9 MiB de RAM en cuanto está en la GPU
    if (!bg) return false;
    background_ = std::move(*bg);

    // Logo y texto son opcionales: si fallan, se dibuja lo que haya
    if (auto logoImg = loadImage(fs, kLogo)) {
        logoUv_ = alphaBounds<Rect>(*logoImg);
        if (auto t = upload(*logoImg, kLogo, true, true)) logo_ = std::move(*t);
    }

    menu_.clear();
    auto fontData = fs.read(kFont);
    std::optional<render::Font> font;
    if (fontData) {  // parseKfont rechaza archivos de más de kMaxKfontBytes
        font = render::parseKfont(
            std::string_view(reinterpret_cast<const char*>(fontData->data()), fontData->size()), &err);
        if (!font) LOGE("TITLE: %s: %s", kFont, err.c_str());
    } else {
        LOGE("TITLE: no se pudo leer %s", kFont);
    }
    if (font) {
        if (auto atlasImg = loadImage(fs, font->texture.c_str())) {
            const std::size_t dropped = render::dropGlyphsOutside(*font, atlasImg->width, atlasImg->height);
            if (auto t = upload(*atlasImg, font->texture.c_str(), font->linearFilter, false)) {
                fontAtlas_ = std::move(*t);
                font_ = std::move(*font);
                LOGI("TITLE: fuente %s: %zu glifos (%zu fuera del atlas, %zu mal formados)", kFont,
                     font_.glyphCount, dropped, font_.malformed);
            }
        }
    }
    if (strings && fontAtlas_.id()) {
        for (const char* key : kMenuKeys) {
            if (const std::string* s = strings->get(key)) menu_.push_back(*s);
            else LOGE("TITLE: falta la cadena %s", key);
        }
    }

    ready_ = true;
    return true;
}

void TitleScreen::abandonGl() {
    batch_.abandon();
    background_.abandon();
    logo_.abandon();
    fontAtlas_.abandon();
    ready_ = false;
}

void TitleScreen::draw(int viewportW, int viewportH) {
    if (!ready_ || viewportW <= 0 || viewportH <= 0) return;
    const float vw = static_cast<float>(viewportW), vh = static_cast<float>(viewportH);

    // Área 4:3 centrada (letterbox/pillarbox); el resto queda del color de limpieza (negro)
    float aw = vw, ah = vw / kAspect;
    if (ah > vh) {
        ah = vh;
        aw = vh * kAspect;
    }
    const float ax = (vw - aw) * 0.5f, ay = (vh - ah) * 0.5f;

    batch_.begin(viewportW, viewportH);
    batch_.drawImage(background_, ax, ay, aw, ah);

    if (logo_.id()) {
        // Logo recortado a su contenido: 70 % del ancho del área como máximo, en el tercio superior
        const float cw = (logoUv_.x1 - logoUv_.x0) * static_cast<float>(logo_.width());
        const float ch = (logoUv_.y1 - logoUv_.y0) * static_cast<float>(logo_.height());
        const float scale = std::min(aw * 0.70f / cw, ah * 0.40f / ch);
        const float w = cw * scale, h = ch * scale;
        const float x = ax + (aw - w) * 0.5f, y = ay + ah * 0.06f;
        batch_.draw(logo_, render::Quad{x, y, x + w, y + h, logoUv_.x0, logoUv_.y0, logoUv_.x1, logoUv_.y1});
    }

    if (!menu_.empty()) {
        // Altura de línea ≈ 6 % del área, independiente de la resolución del dispositivo
        const render::TextLayout probe = render::layoutText(font_, "A", fontAtlas_.width(), fontAtlas_.height());
        const float pixelScale = probe.height > 0.0f ? ah * 0.06f / probe.height : 1.0f;
        float y = ay + ah * 0.55f;
        for (std::size_t i = 0; i < menu_.size(); ++i) {
            const render::TextLayout t =
                render::layoutText(font_, menu_[i], fontAtlas_.width(), fontAtlas_.height(), pixelScale);
            const float x = ax + (aw - t.width) * 0.5f;
            const float shadow = std::max(1.0f, ah * 0.003f);
            batch_.drawText(fontAtlas_, t, x + shadow, y + shadow, {0, 0, 0, 170});
            // Primera opción resaltada (seleccionada por defecto)
            const render::gl::Color c = i == 0 ? render::gl::Color{255, 196, 64, 255} : render::gl::Color{235, 235, 235, 255};
            batch_.drawText(fontAtlas_, t, x, y, c);
            y += t.height * 1.25f;
        }
    }
    batch_.end();
}
