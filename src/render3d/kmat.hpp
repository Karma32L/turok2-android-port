// Parser de materiales .kmat (texto, ver docs/FORMATS.md).
//
//   material <nombre> {                 '{' en la misma línea o en la siguiente
//       cull back|none
//       sort default|masked|translucent|water
//       blend | alphatest | alphamask <umbral> | nodepthmask | fullbright | nodraw
//       color_diffuse "r g b a"
//       sampler "diffuse" <n> "<textura.png>" { filter <modo>  wrap repeat|mirrored|clamp }
//       ... (param, surface_type, depthtest, color_environment, reflectivePower: se ignoran)
//   }
// Directivas desconocidas o mal formadas se cuentan y se ignoran; los errores estructurales
// (bloques sin cerrar, rutas de textura inseguras, límites superados) invalidan el archivo.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace render3d {

enum class SortMode { Default, Masked, Translucent, Water };
enum class WrapMode { Repeat, Mirrored, Clamp };

struct Material {
    std::string name;     // en minúsculas
    std::string texture;  // ruta normalizada del VFS ("" = sin textura → blanco)
    bool cullBack = true;
    SortMode sort = SortMode::Default;
    bool blend = false;
    bool alphaTest = false;
    float alphaRef = 0.5f;  // umbral de alphatest (alphamask)
    bool depthWrite = true;
    bool fullbright = false;
    bool noDraw = false;
    bool linearFilter = true;
    WrapMode wrap = WrapMode::Repeat;
    float diffuse[4] = {1, 1, 1, 1};

    bool translucent() const { return blend || sort == SortMode::Translucent || sort == SortMode::Water; }
    bool masked() const { return !translucent() && (alphaTest || sort == SortMode::Masked); }
};

struct MaterialFile {
    std::vector<Material> materials;
    std::size_t malformed = 0;
    std::size_t duplicates = 0;  // nombres repetidos: gana el primero

    const Material* find(std::string_view name) const;  // sin distinguir mayúsculas
};

struct KmatLimits {
    std::size_t maxBytes = 1u << 20;
    std::size_t maxMaterials = 4096;
};

std::optional<MaterialFile> parseKmat(std::string_view text, std::string* err = nullptr, const KmatLimits& limits = {});

// Referencia de superficie de un .staticmesh → (archivo .kmat normalizado, nombre en minúsculas).
// "materials/harbor/mat_harbor2185/surf_2185_00_00" → ("materials/harbor/mat_harbor2185.kmat", "surf_2185_00_00")
std::optional<std::pair<std::string, std::string>> splitMaterialRef(std::string_view ref);

}  // namespace render3d
