// Parser de niveles .map (raíz de 9 secciones, ver docs/FORMATS.md).
//
// Extrae lo que necesitan el renderer y la física:
//   - sección 1: material del cielo y capas de cielo (opcional);
//   - sección 2: malla de colisión por sectores (opcional; si es inválida se descarta);
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

// Capa de cielo (sección 1 [1], 44 B; idéntica a un bloque SkyLayer_* de defs/skyInfo.txt)
struct SkyLayer {
    std::string material;  // ruta normalizada "skies/skymaterials/<nombre>"
    std::uint8_t white[4] = {255, 255, 255, 255};  // RGBA: color de las zonas claras de la textura
    std::uint8_t black[4] = {0, 0, 0, 255};        // RGBA: color de las zonas oscuras
    float animSpeed = 1.0f, cameraTrack = 0.5f, speedScalar = 1.0f;
    float heightOffset = 1536.0f;  // altura sobre la cámara
    float fadeThroughDist = 0.0f, tile = 1.0f;
    std::uint8_t opacity = 255, flags = 0;
};

// ---------------------------------------------------------------- colisión (sección 2)
// Malla 2.5D de sectores triangulares. Cada punto lleva la altura del suelo y del techo; un
// sector apunta a sus 3 puntos (CCW en XY), a 3 vecinos y a un área (agua, niebla).
constexpr std::uint16_t kNoLink = 0xFFFF;

struct CollisionPoint {
    float x = 0, y = 0;
    float floorZ = 0, ceilZ = 0;  // ceilZ <= floorZ: sin techo (aparece en mapas abiertos)
};

struct CollisionSector {
    std::uint16_t area = 0, flags = 0;
    std::uint16_t p[3] = {0, 0, 0};
    // link[e] = vecino al otro lado de la arista (p[(e+2)%3], p[e]); kNoLink = pared
    std::uint16_t link[3] = {kNoLink, kNoLink, kNoLink};
    std::uint16_t region = 0;
};

struct CollisionArea {
    static constexpr std::uint32_t kWater = 1u << 0;
    std::uint32_t flags = 0;
    std::uint8_t fogColor[4] = {0, 0, 0, 255};
    std::uint8_t waterColor[4] = {0, 0, 0, 255};
    float fogEnd = 0, fogStart = 0;  // @16 y @24 (≈ 0,73·@16)
    float waterZ = 0;                // solo con kWater

    bool water() const { return (flags & kWater) != 0; }
};

struct MapCollision {
    std::vector<CollisionPoint> points;
    std::vector<CollisionArea> areas;
    std::vector<CollisionSector> sectors;
};

struct MapData {
    std::vector<MeshInstance> instances;
    std::string skyMaterial;           // "" si no hay
    std::vector<SkyLayer> skyLayers;   // capas con material (se omiten las "_default")
    std::optional<MapCollision> collision;
    std::string collisionError;        // motivo si la sección 2 existía pero era inválida
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
    std::size_t maxCollisionPoints = 1u << 16;   // índices u16
    std::size_t maxCollisionSectors = 0xFFFF;    // 0xFFFF reservado para kNoLink
    std::size_t maxCollisionAreas = 1u << 16;
};

constexpr std::uint32_t kMapSections = 9;
// Instancia: 88 B en la campaña, 84 B en levels/n64/; ambas empiezan por los 64 B que se leen
constexpr std::uint32_t kInstanceMinStride = 64;
constexpr std::uint32_t kActorHeaderStride = 48;
constexpr std::uint32_t kActorSpStart = 100;
constexpr std::uint32_t kSkyLayerStride = 44;
constexpr std::uint32_t kCollisionPointStride = 16;
constexpr std::uint32_t kCollisionSectorStride = 18;
// Área: 96 B en la campaña, 92 B en levels/n64/; los primeros 36 B (los que se leen) coinciden
constexpr std::uint32_t kCollisionAreaMinStride = 36;

std::optional<MapData> parseMap(ByteView bytes, std::string* err = nullptr, const MapLimits& limits = {});

}  // namespace render3d
