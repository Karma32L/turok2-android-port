// ByteSource sobre SDL_RWops (solo en el binario de la app; los tests no dependen de SDL).
// En Android, SDL_RWFromFile con ruta relativa busca primero en el almacenamiento interno
// de la app y después en los assets del APK (AAssetManager).
#pragma once

#include "vfs.hpp"

namespace vfs {

std::unique_ptr<ByteSource> openSdlSource(const char* path, std::string* err = nullptr);

}  // namespace vfs
