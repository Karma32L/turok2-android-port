// Parser de niveles .map (raíz de 9 secciones, ver docs/FORMATS.md).
//
// Solo extrae lo que necesita el renderer de geometría estática:
//   - sección 1: material del cielo (opcional);
//   - sección 4: instancias de .staticmesh por área (posición, escala, rotación, bbox);
//   - sección 5: actor de inicio del jugador (tipo 100 = kActor_SPStart, opcional).
// Errores estructurales en la raíz o en la sección 4 invalidan el mapa; una instancia con
// valores absurdos o ruta insegura se omite y se cuenta. Los actores mal formados se ignoran.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "byte_view.hpp"
#include "math3d.hpp"

namespace render3d {

struct MeshInstance {
    std::string model;  // ruta normalizada del VFS (.staticmesh)
    Vec3 pos, scale, rot, boundsMin, boundsMax;
    std::uint32_t flags = 0;
};

struct SpawnPoint {
    Vec3 pos;
    float yaw = 0.0f;  // radianes, convención del .map (ver instanceRotation)
};

struct MapData {
    std::vector<MeshInstance> instances;
    std::string skyMaterial;           // "" si no hay
    std::optional<SpawnPoint> start;   // actor kActor_SPStart
    std::size_t areas = 0;
    std::size_t skippedInstances = 0;  // valores no finitos/absurdos o ruta inválida
};

struct MapLimits {
    std::size_t maxBytes = 64u << 20;
    std::size_t maxInstances = 1u << 16;
    float maxCoord = 1.0e6f;   // |posición| y |bbox|
    float maxScale = 1.0e3f;   // |escala| por eje (y > minScale)
    float minScale = 1.0e-4f;
    float maxAngle = 64.0f;    // |ángulo| en radianes
};

constexpr std::uint32_t kMapSections = 9;
// Instancia: 88 B en la campaña, 84 B en levels/n64/; ambas empiezan por los 64 B que se leen
constexpr std::uint32_t kInstanceMinStride = 64;
constexpr std::uint32_t kActorHeaderStride = 48;
constexpr std::uint32_t kActorSpStart = 100;

std::optional<MapData> parseMap(ByteView bytes, std::string* err = nullptr, const MapLimits& limits = {});

}  // namespace render3d
