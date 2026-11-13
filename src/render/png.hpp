// Decodificador PNG mínimo y estricto para las texturas de game.kpf.
//
// Subconjunto soportado (cubre 7695 de los 7696 PNG del juego, ver docs/FORMATS.md):
//   - profundidad de 8 bits, tipos de color 0 (gris), 2 (RGB) y 6 (RGBA);
//   - sin entrelazado (Adam7 se rechaza con un error explícito).
// Todo el archivo se trata como no confiable: cada longitud de chunk se valida contra los
// bytes restantes, se verifica la CRC de cada chunk, las dimensiones y el tamaño
// descomprimido se acotan antes de reservar memoria, y el flujo zlib debe producir
// exactamente el número de bytes esperado (ni más ni menos).
// Independiente de GL/SDL para poder probarse en el host con ASan/UBSan.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace render {

// Píxeles de 8 bits por canal, filas de arriba a abajo, sin relleno entre filas.
struct Image {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint8_t channels = 0;  // 3 (RGB) o 4 (RGBA); el gris se expande a RGB
    std::vector<std::uint8_t> pixels;
};

struct PngLimits {
    std::uint32_t maxDim = 8192;              // ancho/alto máximos
    std::uint64_t maxDecodedBytes = 64ull << 20;  // datos filtrados y píxeles de salida
    std::uint64_t maxCompressedBytes = 64ull << 20;  // suma de todos los IDAT
};

// Devuelve nullopt (y el motivo en *err si no es nulo) ante cualquier dato inválido.
std::optional<Image> decodePng(const std::uint8_t* data, std::size_t size, std::string* err = nullptr,
                               const PngLimits& limits = {});

}  // namespace render
