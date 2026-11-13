// Vista de bytes no propietaria y lector secuencial con comprobación de límites.
// Base de todos los parsers binarios de render3d: ningún acceso sale de [data, data+size).
// Little-endian explícito (no depende del endianness del host). C++17: sin std::span.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace render3d {

struct ByteView {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;

    ByteView() = default;
    ByteView(const std::uint8_t* d, std::size_t n) : data(d), size(n) {}
    explicit ByteView(const std::vector<std::uint8_t>& v) : data(v.data()), size(v.size()) {}

    // Subrango [off, off+len). Vista vacía si no cabe (el llamador debe comprobar antes).
    ByteView sub(std::size_t off, std::size_t len) const {
        if (off > size || len > size - off) return {};
        return {data + off, len};
    }

    bool u32At(std::size_t off, std::uint32_t& out) const {
        if (off > size || size - off < 4) return false;
        const std::uint8_t* p = data + off;
        out = std::uint32_t{p[0]} | (std::uint32_t{p[1]} << 8) | (std::uint32_t{p[2]} << 16) |
              (std::uint32_t{p[3]} << 24);
        return true;
    }

    bool f32At(std::size_t off, float& out) const {
        std::uint32_t bits = 0;
        if (!u32At(off, bits)) return false;
        std::memcpy(&out, &bits, sizeof out);
        return true;
    }
};

// Lector secuencial: cada operación falla (false) sin avanzar si no quedan bytes suficientes.
class Cursor {
public:
    explicit Cursor(ByteView v) : v_(v) {}

    std::size_t pos() const { return pos_; }
    std::size_t remaining() const { return v_.size - pos_; }

    bool u16(std::uint16_t& out) {
        if (remaining() < 2) return false;
        const std::uint8_t* p = v_.data + pos_;
        out = static_cast<std::uint16_t>(p[0] | (p[1] << 8));
        pos_ += 2;
        return true;
    }

    bool u32(std::uint32_t& out) {
        if (!v_.u32At(pos_, out)) return false;
        pos_ += 4;
        return true;
    }

    // Float finito (rechaza NaN/Inf: un NaN en una posición contamina toda la transformación)
    bool f32(float& out) {
        if (!v_.f32At(pos_, out) || !std::isfinite(out)) return false;
        pos_ += 4;
        return true;
    }

    // Cadena terminada en NUL de como mucho maxLen bytes (sin contar el NUL), solo ASCII
    // imprimible. Falla si no hay NUL dentro del límite o si aparece un byte de control.
    bool cstring(std::size_t maxLen, std::string& out) {
        const std::size_t avail = remaining();
        for (std::size_t i = 0; i < avail && i <= maxLen; ++i) {
            const std::uint8_t c = v_.data[pos_ + i];
            if (c == 0) {
                out.assign(reinterpret_cast<const char*>(v_.data + pos_), i);
                pos_ += i + 1;
                return true;
            }
            if (c < 0x20 || c >= 0x7F) return false;
        }
        return false;
    }

    // Avanza n bytes devolviendo su vista
    bool take(std::size_t n, ByteView& out) {
        if (remaining() < n) return false;
        out = v_.sub(pos_, n);
        pos_ += n;
        return true;
    }

private:
    ByteView v_;
    std::size_t pos_ = 0;
};

}  // namespace render3d
