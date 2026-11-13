// Ajustes del usuario (volúmenes e idioma), persistidos como texto `clave=valor`.
//
// El archivo vive en el directorio privado de la app (SDL_GetPrefPath), pero se parsea como
// no confiable: tamaño acotado, solo claves conocidas, valores en rango. Cualquier línea
// inválida se ignora y se conserva el valor por defecto.
#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace ui {

enum class Language { Spanish, English, French, German, Italian };
constexpr std::size_t kLanguageCount = 5;

struct LanguageInfo {
    Language id;
    const char* code;    // sufijo de localization/loc_<code>.txt y valor en el archivo
    const char* locKey;  // clave del nombre del idioma en las tablas de localización
};
extern const std::array<LanguageInfo, kLanguageCount> kLanguages;

const LanguageInfo& languageInfo(Language l);
std::optional<Language> languageFromCode(std::string_view code);
Language cycleLanguage(Language l, int delta);

constexpr int kVolumeMax = 10;

struct Settings {
    int musicVolume = 8;
    int sfxVolume = 10;
    Language language = Language::Spanish;
    bool operator==(const Settings& o) const {
        return musicVolume == o.musicVolume && sfxVolume == o.sfxVolume && language == o.language;
    }
};

constexpr std::size_t kMaxSettingsBytes = 4096;

// `rejected` (opcional): número de líneas no vacías ignoradas
Settings parseSettings(std::string_view text, std::size_t* rejected = nullptr);
std::string serializeSettings(const Settings& s);

// E/S con stdio. load: si el archivo no existe, es demasiado grande o no se lee, devuelve
// los valores por defecto. save: escribe en `<path>.tmp` y renombra (atómico en POSIX).
Settings loadSettingsFile(const std::string& path);
bool saveSettingsFile(const std::string& path, const Settings& s, std::string* err);

}  // namespace ui
