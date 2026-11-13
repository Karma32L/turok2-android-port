// Contenedor binario genérico de los .map (ver docs/FORMATS.md, "Árbol de nodos").
//
// Un nodo es un rango de bytes que, según el esquema que espera el llamador, se interpreta como:
//   - Contenedor: u32 n; u32 off[n+1]; hijos i = [off[i], off[i+1]) relativos al nodo.
//   - Hoja:       u32 stride; u32 count; bytes data[stride*count] (+ relleno).
// El formato no lleva etiquetas de tipo: es el esquema (map_file.cpp) quien decide qué
// interpretación aplicar en cada nivel, así que la navegación no es recursiva ni heurística.
// Todo offset y tamaño se valida contra el nodo padre antes de usarse.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "byte_view.hpp"

namespace render3d::kex {

constexpr std::uint32_t kMaxChildren = 1u << 20;

// Hijos del contenedor, ya validados: cabecera completa, off[0] >= tamaño de cabecera,
// offsets no decrecientes, off[n] <= tamaño del nodo. nullopt si el nodo no es un contenedor válido.
std::optional<std::vector<ByteView>> asContainer(ByteView node, std::string* err = nullptr);

struct Leaf {
    std::uint32_t stride = 0;
    std::uint32_t count = 0;
    ByteView data;  // exactamente stride*count bytes

    ByteView element(std::uint32_t i) const { return data.sub(std::size_t{i} * stride, stride); }
};

// Hoja validada: stride*count (en 64 bits) cabe en el nodo.
std::optional<Leaf> asLeaf(ByteView node, std::string* err = nullptr);

// Hoja de una sola cadena (count == 1, stride = longitud con NUL). Devuelve el texto hasta
// el primer NUL, que debe existir dentro de la hoja; solo ASCII imprimible y <= maxLen.
std::optional<std::string> leafString(ByteView node, std::size_t maxLen = 256);

// true si el nodo es una hoja vacía (stride*count == 0): las secciones o áreas sin datos.
bool isEmptyLeaf(ByteView node);

}  // namespace render3d::kex
