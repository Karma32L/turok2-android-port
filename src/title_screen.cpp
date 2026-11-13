#include "title_screen.hpp"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <optional>

#include "log.hpp"
#include "png.hpp"
#include "text_layout.hpp"

namespace {

constexpr const char* kBackground = "gfx/titleback.png";
constexpr const char* kLogo = "gfx/turok2Logo.png";
constexpr const char* kCursor = "gfx/cursor.png";
constexpr const char* kFont = "fonts/bigfont.kfont";

constexpr float kTextHeight = 0.06f;     // altura del texto de las filas (fracción del área 4:3)
constexpr float kHeadingHeight = 0.08f;  // título de página ("Opciones")
constexpr float kHeadingTop = 0.38f;

constexpr render::gl::Color kNormal{235, 235, 235, 255};
constexpr render::gl::Color kSelected{255, 196, 64, 255};
constexpr render::gl::Color kDisabled{120, 120, 120, 200};
constexpr render::gl::Color kShadow{0, 0, 0, 170};

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

bool TitleScreen::load(const vfs::Vfs& fs) {
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

    // Logo, cursor y texto son opcionales: si fallan, se dibuja lo que haya
    if (auto logoImg = loadImage(fs, kLogo)) {
        logoUv_ = alphaBounds<UvRect>(*logoImg);
        if (auto t = upload(*logoImg, kLogo, true, true)) logo_ = std::move(*t);
    }
    if (auto curImg = loadImage(fs, kCursor)) {
        if (auto t = upload(*curImg, kCursor, true, false)) cursor_ = std::move(*t);
    }

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
                fontLineHeight_ = render::layoutText(font_, "A", fontAtlas_.width(), fontAtlas_.height()).height;
                LOGI("TITLE: fuente %s: %zu glifos (%zu fuera del atlas, %zu mal formados)", kFont,
                     font_.glyphCount, dropped, font_.malformed);
            }
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
    cursor_.abandon();
    ready_ = false;
}

std::vector<ui::Rect> TitleScreen::rowRects(int viewportW, int viewportH, std::size_t count) {
    return ui::menuRows(ui::letterbox(static_cast<float>(viewportW), static_cast<float>(viewportH)), count);
}

float TitleScreen::drawCentered(const std::string& text, float cx, float top, float textHeight, render::gl::Color c) {
    if (!fontAtlas_.id() || fontLineHeight_ <= 0.0f || text.empty()) return 0.0f;
    const float scale = textHeight / fontLineHeight_;
    const render::TextLayout t = render::layoutText(font_, text, fontAtlas_.width(), fontAtlas_.height(), scale);
    const float x = cx - t.width * 0.5f;
    const float shadow = std::max(1.0f, textHeight * 0.05f);
    batch_.drawText(fontAtlas_, t, x + shadow, top + shadow, kShadow);
    batch_.drawText(fontAtlas_, t, x, top, c);
    return t.width;
}

void TitleScreen::drawMenu(int viewportW, int viewportH, const std::string& heading,
                           const std::vector<ui::MenuRowView>& rows, int selected, float timeSec) {
    if (!ready_) return;
    const ui::Rect area = ui::letterbox(static_cast<float>(viewportW), static_cast<float>(viewportH));
    if (area.w <= 0.0f) return;

    batch_.begin(viewportW, viewportH);
    batch_.drawImage(background_, area.x, area.y, area.w, area.h);

    const float cx = area.x + area.w * 0.5f;
    if (heading.empty() && logo_.id()) {
        // Logo recortado a su contenido: 70 % del ancho del área como máximo, en el tercio superior
        const float cw = (logoUv_.x1 - logoUv_.x0) * static_cast<float>(logo_.width());
        const float ch = (logoUv_.y1 - logoUv_.y0) * static_cast<float>(logo_.height());
        const float scale = std::min(area.w * 0.70f / cw, area.h * 0.40f / ch);
        const float w = cw * scale, h = ch * scale;
        const float x = area.x + (area.w - w) * 0.5f, y = area.y + area.h * 0.06f;
        batch_.draw(logo_, render::Quad{x, y, x + w, y + h, logoUv_.x0, logoUv_.y0, logoUv_.x1, logoUv_.y1});
    } else if (!heading.empty()) {
        drawCentered(heading, cx, area.y + area.h * kHeadingTop, area.h * kHeadingHeight, kSelected);
    }

    const std::vector<ui::Rect> boxes = ui::menuRows(area, rows.size());
    const float baseH = area.h * kTextHeight;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const bool sel = static_cast<int>(i) == selected;
        // Seleccionada: más grande con un pulso suave (1,06–1,12)
        const float s = sel ? 1.09f + 0.03f * std::sin(timeSec * 4.0f) : 1.0f;
        const float h = baseH * s;
        const float top = boxes[i].y + (boxes[i].h - h) * 0.5f;
        const render::gl::Color c = !rows[i].enabled ? kDisabled : (sel ? kSelected : kNormal);
        const float textW = drawCentered(rows[i].text, cx, top, h, c);
        if (sel && cursor_.id()) {
            const float size = h * 0.9f;
            const float x = cx - textW * 0.5f - size * 1.4f;
            batch_.drawImage(cursor_, x, top + (h - size) * 0.5f, size, size, kSelected);
        }
    }
    batch_.end();
}

void TitleScreen::drawMessage(int viewportW, int viewportH, const std::string& text) {
    if (!ready_) return;
    const ui::Rect area = ui::letterbox(static_cast<float>(viewportW), static_cast<float>(viewportH));
    if (area.w <= 0.0f) return;
    batch_.begin(viewportW, viewportH);
    const float h = area.h * kTextHeight;
    drawCentered(text, area.x + area.w * 0.5f, area.y + (area.h - h) * 0.5f, h, kNormal);
    batch_.end();
}
