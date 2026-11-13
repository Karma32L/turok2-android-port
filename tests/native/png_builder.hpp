// Generador de PNG sintéticos para los tests (ningún dato del juego).
// Construye cada chunk a mano para poder fabricar archivos inválidos a propósito.
#pragma once

#include <miniz.h>

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace pb {

using Bytes = std::vector<std::uint8_t>;

inline void put32be(Bytes& b, std::uint32_t v) {
    b.push_back(static_cast<std::uint8_t>(v >> 24));
    b.push_back(static_cast<std::uint8_t>(v >> 16));
    b.push_back(static_cast<std::uint8_t>(v >> 8));
    b.push_back(static_cast<std::uint8_t>(v));
}

inline Bytes signature() { return {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'}; }

// Chunk con CRC correcta (o forzada a `badCrc` si se pide)
inline Bytes chunk(const char type[4], const Bytes& data, bool badCrc = false) {
    Bytes c;
    put32be(c, static_cast<std::uint32_t>(data.size()));
    c.insert(c.end(), type, type + 4);
    c.insert(c.end(), data.begin(), data.end());
    auto crc = static_cast<std::uint32_t>(mz_crc32(MZ_CRC32_INIT, c.data() + 4, data.size() + 4));
    if (badCrc) crc ^= 1;
    put32be(c, crc);
    return c;
}

inline Bytes ihdr(std::uint32_t w, std::uint32_t h, std::uint8_t colorType, std::uint8_t depth = 8,
                  std::uint8_t interlace = 0) {
    Bytes d;
    put32be(d, w);
    put32be(d, h);
    d.insert(d.end(), {depth, colorType, 0, 0, interlace});
    return d;
}

inline Bytes zlib(const Bytes& in) {
    size_t outLen = 0;
    const int flags = static_cast<int>(tdefl_create_comp_flags_from_zip_params(6, 15, MZ_DEFAULT_STRATEGY));
    void* p = tdefl_compress_mem_to_heap(in.data(), in.size(), &outLen, flags);
    Bytes out(static_cast<const std::uint8_t*>(p), static_cast<const std::uint8_t*>(p) + outLen);
    mz_free(p);
    return out;
}

inline std::uint8_t paeth(int a, int b, int c) {
    const int p = a + b - c;
    const int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return static_cast<std::uint8_t>(a);
    return static_cast<std::uint8_t>(pb <= pc ? b : c);
}

// Aplica el filtro PNG `filterOf(y)` a cada fila (codificador de referencia independiente).
template <class FilterOf>
Bytes filterRows(const Bytes& px, std::uint32_t w, std::uint32_t h, std::uint32_t bpp, FilterOf filterOf) {
    const std::size_t stride = std::size_t{w} * bpp;
    Bytes out;
    for (std::uint32_t y = 0; y < h; ++y) {
        const std::uint8_t f = filterOf(y);
        out.push_back(f);
        const std::uint8_t* cur = px.data() + y * stride;
        const std::uint8_t* prev = y ? cur - stride : nullptr;
        for (std::size_t i = 0; i < stride; ++i) {
            const int a = i >= bpp ? cur[i - bpp] : 0;
            const int b = prev ? prev[i] : 0;
            const int c = (prev && i >= bpp) ? prev[i - bpp] : 0;
            int pred = 0;
            if (f == 1) pred = a;
            else if (f == 2) pred = b;
            else if (f == 3) pred = (a + b) / 2;
            else if (f == 4) pred = paeth(a, b, c);
            out.push_back(static_cast<std::uint8_t>(cur[i] - pred));
        }
    }
    return out;
}

// Píxeles deterministas con bastante variación (ejercita todos los predictores)
inline Bytes pattern(std::size_t n, std::uint32_t seed = 1) {
    Bytes b(n);
    std::uint32_t s = seed;
    for (auto& v : b) {
        s = s * 1103515245u + 12345u;
        v = static_cast<std::uint8_t>(s >> 16);
    }
    return b;
}

inline Bytes concat(std::initializer_list<Bytes> parts) {
    Bytes out;
    for (const Bytes& p : parts) out.insert(out.end(), p.begin(), p.end());
    return out;
}

// PNG válido completo: firma + IHDR + IDAT + IEND
inline Bytes makePng(std::uint32_t w, std::uint32_t h, std::uint8_t colorType, const Bytes& px,
                     std::uint8_t filter = 0) {
    const std::uint32_t bpp = colorType == 6 ? 4 : colorType == 2 ? 3 : 1;
    const Bytes raw = filterRows(px, w, h, bpp, [filter](std::uint32_t) { return filter; });
    return concat({signature(), chunk("IHDR", ihdr(w, h, colorType)), chunk("IDAT", zlib(raw)),
                   chunk("IEND", {})});
}

}  // namespace pb
