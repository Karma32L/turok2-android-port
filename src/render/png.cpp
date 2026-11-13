#include "png.hpp"

#include <miniz.h>

#include <cstdlib>
#include <cstring>

namespace render {
namespace {

constexpr std::uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
constexpr std::uint32_t kMaxChunkLen = 0x7FFFFFFFu;  // límite de la especificación PNG

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t{p[0]} << 24) | (std::uint32_t{p[1]} << 16) | (std::uint32_t{p[2]} << 8) | p[3];
}

bool fail(std::string* err, const char* msg) {
    if (err) *err = msg;
    return false;
}

bool isAsciiLetter(std::uint8_t c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }

struct Header {
    std::uint32_t width = 0, height = 0;
    std::uint8_t colorType = 0;
    std::uint32_t bpp = 0;  // bytes por píxel en los datos filtrados
};

bool parseIhdr(const std::uint8_t* d, std::uint32_t len, const PngLimits& lim, Header& h, std::string* err) {
    if (len != 13) return fail(err, "IHDR con longitud distinta de 13");
    h.width = be32(d);
    h.height = be32(d + 4);
    const std::uint8_t depth = d[8], compression = d[10], filter = d[11], interlace = d[12];
    h.colorType = d[9];
    if (h.width == 0 || h.height == 0) return fail(err, "dimensiones nulas");
    if (h.width > lim.maxDim || h.height > lim.maxDim) return fail(err, "dimensiones por encima del límite");
    if (compression != 0 || filter != 0) return fail(err, "método de compresión/filtro desconocido");
    if (interlace == 1) return fail(err, "PNG entrelazado (Adam7) no soportado");
    if (interlace != 0) return fail(err, "método de entrelazado inválido");
    if (depth != 8) return fail(err, "solo se soporta profundidad de 8 bits");
    switch (h.colorType) {
    case 0: h.bpp = 1; break;
    case 2: h.bpp = 3; break;
    case 6: h.bpp = 4; break;
    default: return fail(err, "tipo de color no soportado (solo gris, RGB y RGBA)");
    }
    return true;
}

std::uint8_t paeth(int a, int b, int c) {
    const int p = a + b - c;
    const int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return static_cast<std::uint8_t>(a);
    if (pb <= pc) return static_cast<std::uint8_t>(b);
    return static_cast<std::uint8_t>(c);
}

// Deshace los filtros por fila in situ. `raw` mide exactamente height * (1 + stride).
bool unfilter(std::uint8_t* raw, std::size_t stride, std::uint32_t height, std::size_t bpp, std::string* err) {
    const std::uint8_t* prev = nullptr;  // fila anterior ya reconstruida (nullptr = ceros)
    for (std::uint32_t y = 0; y < height; ++y) {
        std::uint8_t* row = raw + std::size_t{y} * (stride + 1);
        const std::uint8_t type = row[0];
        std::uint8_t* cur = row + 1;
        for (std::size_t i = 0; i < stride; ++i) {
            const int a = i >= bpp ? cur[i - bpp] : 0;
            const int b = prev ? prev[i] : 0;
            const int c = (prev && i >= bpp) ? prev[i - bpp] : 0;
            int pred;
            switch (type) {
            case 0: pred = 0; break;
            case 1: pred = a; break;
            case 2: pred = b; break;
            case 3: pred = (a + b) / 2; break;
            case 4: pred = paeth(a, b, c); break;
            default: return fail(err, "tipo de filtro de fila inválido");
            }
            cur[i] = static_cast<std::uint8_t>(cur[i] + pred);
        }
        prev = cur;
    }
    return true;
}

}  // namespace

std::optional<Image> decodePng(const std::uint8_t* data, std::size_t size, std::string* err,
                               const PngLimits& limits) {
    if (!data || size < sizeof(kSignature) || std::memcmp(data, kSignature, sizeof(kSignature)) != 0) {
        fail(err, "firma PNG inválida");
        return std::nullopt;
    }

    Header hdr;
    bool haveHeader = false, haveEnd = false;
    std::vector<std::uint8_t> idat;
    std::size_t p = sizeof(kSignature);

    while (!haveEnd) {
        // Invariante: p <= size. Cada chunk necesita longitud(4) + tipo(4) + CRC(4).
        if (size - p < 12) {
            fail(err, "PNG truncado (falta IEND)");
            return std::nullopt;
        }
        const std::uint32_t len = be32(data + p);
        const std::uint8_t* type = data + p + 4;
        if (len > kMaxChunkLen || len > size - p - 12) {
            fail(err, "longitud de chunk fuera del archivo");
            return std::nullopt;
        }
        for (int i = 0; i < 4; ++i) {
            if (!isAsciiLetter(type[i])) {
                fail(err, "tipo de chunk inválido");
                return std::nullopt;
            }
        }
        const std::uint8_t* body = type + 4;
        const auto crc = static_cast<std::uint32_t>(mz_crc32(MZ_CRC32_INIT, type, std::size_t{len} + 4));
        if (crc != be32(body + len)) {
            fail(err, "CRC de chunk incorrecta");
            return std::nullopt;
        }
        p += std::size_t{len} + 12;

        const bool isIhdr = std::memcmp(type, "IHDR", 4) == 0;
        if (!haveHeader && !isIhdr) {
            fail(err, "el primer chunk no es IHDR");
            return std::nullopt;
        }
        if (isIhdr) {
            if (haveHeader) {
                fail(err, "IHDR duplicado");
                return std::nullopt;
            }
            if (!parseIhdr(body, len, limits, hdr, err)) return std::nullopt;
            haveHeader = true;
        } else if (std::memcmp(type, "IDAT", 4) == 0) {
            if (len > limits.maxCompressedBytes - idat.size()) {
                fail(err, "IDAT por encima del límite");
                return std::nullopt;
            }
            idat.insert(idat.end(), body, body + len);
        } else if (std::memcmp(type, "IEND", 4) == 0) {
            haveEnd = true;
        } else if ((type[0] & 0x20) == 0 && std::memcmp(type, "PLTE", 4) != 0) {
            // Bit 5 del primer byte a 0 = chunk crítico: no se puede ignorar sin entenderlo.
            // PLTE es opcional (solo sugerencia) en RGB/RGBA, así que se ignora.
            fail(err, "chunk crítico desconocido");
            return std::nullopt;
        }
        // Resto de chunks auxiliares (gAMA, iCCP, tEXt, pHYs...) se ignoran.
    }
    if (idat.empty()) {
        fail(err, "PNG sin datos IDAT");
        return std::nullopt;
    }

    // Tamaños calculados en 64 bits y acotados antes de reservar (w, h <= maxDim <= 2^32).
    const std::uint64_t stride = std::uint64_t{hdr.width} * hdr.bpp;
    const std::uint64_t rawSize = std::uint64_t{hdr.height} * (stride + 1);
    const std::uint8_t outCh = hdr.colorType == 6 ? 4 : 3;
    const std::uint64_t outSize = std::uint64_t{hdr.width} * hdr.height * outCh;
    if (rawSize > limits.maxDecodedBytes || outSize > limits.maxDecodedBytes) {
        fail(err, "imagen descomprimida por encima del límite");
        return std::nullopt;
    }

    std::vector<std::uint8_t> raw(static_cast<std::size_t>(rawSize));
    // Buffer de salida de tamaño exacto: si el flujo produce más datos, tinfl falla; si
    // produce menos, se detecta comparando. PARSE_ZLIB_HEADER verifica también el Adler-32.
    const std::size_t got = tinfl_decompress_mem_to_mem(raw.data(), raw.size(), idat.data(), idat.size(),
                                                        TINFL_FLAG_PARSE_ZLIB_HEADER);
    if (got == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED) {
        fail(err, "flujo zlib inválido o con datos de más");
        return std::nullopt;
    }
    if (got != raw.size()) {
        fail(err, "flujo zlib con datos de menos");
        return std::nullopt;
    }
    if (!unfilter(raw.data(), static_cast<std::size_t>(stride), hdr.height, hdr.bpp, err)) return std::nullopt;

    Image img;
    img.width = hdr.width;
    img.height = hdr.height;
    img.channels = outCh;
    img.pixels.resize(static_cast<std::size_t>(outSize));
    const std::size_t rowOut = std::size_t{hdr.width} * outCh;
    for (std::uint32_t y = 0; y < hdr.height; ++y) {
        const std::uint8_t* src = raw.data() + std::size_t{y} * (stride + 1) + 1;
        std::uint8_t* dst = img.pixels.data() + std::size_t{y} * rowOut;
        if (hdr.colorType == 0) {
            for (std::uint32_t x = 0; x < hdr.width; ++x) dst[3 * x] = dst[3 * x + 1] = dst[3 * x + 2] = src[x];
        } else {
            std::memcpy(dst, src, rowOut);
        }
    }
    return img;
}

}  // namespace render
