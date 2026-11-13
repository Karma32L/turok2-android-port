# Compilación

## Requisitos
| Componente | Versión |
|---|---|
| JDK | 17 |
| Gradle | 8.7 (wrapper incluido) |
| Android Gradle Plugin | 8.5.1 |
| Android SDK Platform | 34 |
| NDK | 26.3.11579264 |
| CMake (del SDK) | 3.22.1 |

## Código fuente
SDL2 es un submódulo Git fijado a `release-2.32.10` (`5d24957`):
```bash
git clone --recursive <repo>          # clon nuevo
git submodule update --init           # clon existente
```

## Configuración
Crea `local.properties` en la raíz (no se versiona):
```
sdk.dir=/home/<usuario>/Android/Sdk
```

## Compilar
```bash
./gradlew assembleDebug
# APK: android/app/build/outputs/apk/debug/app-debug.apk
unzip -l android/app/build/outputs/apk/debug/app-debug.apk | grep lib/   # debe mostrar solo arm64-v8a
```

## Ejecutar
```bash
adb install -r android/app/build/outputs/apk/debug/app-debug.apk
adb logcat -s turok2:*
```

## Arquitectura
- `android/app/src/main/AndroidManifest.xml`: `MainActivity` (landscape, sin permisos, `allowBackup=false`).
- `android/app/src/main/java/com/turok2port/app/MainActivity.java`: extiende `SDLActivity`; carga `libSDL2.so` y `libturok2.so`.
- La capa Java de SDL (`org.libsdl.app`) se compila directamente desde `third_party/SDL2/android-project/` (no se copia).
- `src/CMakeLists.txt`: `add_subdirectory` de SDL2 → `libSDL2.so` + `libturok2.so` (enlaza `SDL2::SDL2`, `GLESv3`, `log`).
- `src/main.cpp`: `SDL_main` crea una ventana GL ES 3.0 a pantalla completa (inmersiva), limpia a verde
  y registra `GL_VENDOR`/`GL_RENDERER`/`GL_VERSION` en logcat. Registra también toques y mandos.

Logs: `adb logcat -s turok2:* SDL:*`
