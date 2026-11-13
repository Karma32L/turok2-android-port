# Contexto del Proyecto
Port nativo a Android (ARM64 / `arm64-v8a`) escrito en C/C++.
Toolchain: Android NDK 26.3.11579264, CMake 3.22.1, Gradle 8.7 + AGP 8.5.1, JDK 17, SDL2 (pendiente de integrar).
Entorno de desarrollo: Ubuntu Linux. SDK en `~/Android/Sdk` (ruta definida en `local.properties`, nunca versionado).

# Estructura
- `android/` — proyecto Gradle de la app (manifest, `build.gradle.kts`).
- `src/` — código nativo C/C++ y su `CMakeLists.txt` (independiente de Android cuando sea posible).
- `tools/` — utilidades host (extractores/convertidores de datos del juego).
- `docs/` — documentación técnica.

# Comandos Clave
- Compilar APK debug: `./gradlew assembleDebug`
- Compilar APK release: `./gradlew assembleRelease`
- Limpiar: `./gradlew clean`
- Verificar ABIs del APK: `unzip -l android/app/build/outputs/apk/debug/app-debug.apk | grep lib/`
- Instalar en dispositivo: `adb install -r android/app/build/outputs/apk/debug/app-debug.apk`
- Logs nativos: `adb logcat -s turok2:*`

# Regla de Assets (ESTRICTA — sin excepciones)
**CERO assets con copyright en Git.** Prohibido versionar archivos de datos del juego original:
`.kpf`, `.exe`, `.dll`, `.z64` ni ningún otro binario, textura, audio, modelo o nivel extraído.
- El usuario aporta sus propios datos legítimos en tiempo de ejecución (almacenamiento de la app), nunca dentro del APK ni del repo.
- Nunca empaquetes datos del juego en `assets/` o `res/`.
- Antes de cada commit, revisa `git status` y `git diff --cached --stat`; si aparece cualquier binario sospechoso, aborta.
- Los tests deben usar datos sintéticos generados por el propio código.

# Reglas de Seguridad (ESTRICTAS)
1. Cero secretos hardcodeados (keystores, contraseñas de firma, tokens). Firma release vía variables de entorno o `~/.gradle/gradle.properties`.
2. C/C++: comprueba límites en todo parseo de datos externos (cabeceras, offsets, tamaños). Nada de `strcpy`/`sprintf`/`gets`; usa `snprintf`, `std::span`, `std::vector`, `std::string_view`.
3. Extractores: normaliza rutas y rechaza `..`, rutas absolutas y symlinks (path traversal / zip-slip).
4. Compila debug con `-Wall -Wextra` y, al depurar memoria, con ASan/HWASan (`-fsanitize=hwaddress` en ARM64).
5. AndroidManifest mínimo: sin permisos innecesarios, `allowBackup="false"`, `exported` explícito.
6. Antes de modificar más de 3 archivos, presenta un plan y espera confirmación.

# Git
- Usa Conventional Commits (`feat:`, `fix:`, `sec:`, `docs:`, `build:`, `chore:`).
- Nunca hagas push directo a `main`.
