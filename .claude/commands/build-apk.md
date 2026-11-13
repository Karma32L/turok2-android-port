Compila el APK ARM64 del proyecto y diagnostica cualquier fallo de la cadena Gradle/NDK/CMake.

Argumento opcional: `$ARGUMENTS` (`debug` por defecto, o `release`).

Pasos:
1. Verifica el entorno: existe `local.properties` con `sdk.dir`, el NDK y la versión de CMake declarados en `android/app/build.gradle.kts` están instalados en el SDK y `java -version` es 17.
2. Ejecuta `./gradlew assemble<Debug|Release> --stacktrace` (añade `--offline` si no hay red).
3. Si falla, identifica la fase y la causa raíz antes de proponer el parche:
   - **Configuración Gradle/AGP**: incompatibilidad AGP↔Gradle↔JDK, NDK no encontrado.
   - **Configuración CMake**: revisa `android/app/.cxx/**/CMakeFiles/CMakeError.log` y `cmake_server_log.txt`.
   - **Compilación C/C++**: primer error real (ignora la cascada), warnings de `-Wall -Wextra`.
   - **Enlazado**: `undefined reference` → falta fuente en `add_library`, falta `target_link_libraries` (`android`, `log`, `GLESv3`, `EGL`, `SDL2`…), orden de librerías, `extern "C"` ausente en símbolos JNI o `ANativeActivity_onCreate` no exportado (`-u ANativeActivity_onCreate`). Usa `$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-nm -D --defined-only` / `llvm-readelf -d` sobre el `.so` para comprobar símbolos y `NEEDED`.
   - **Empaquetado**: `lib.name` del manifest distinto del nombre del target CMake.
4. Si compila, valida el APK:
   - `unzip -l` → solo `lib/arm64-v8a/*.so`, ningún otro ABI.
   - Ningún asset con copyright (`.kpf`, `.exe`, `.dll`, `.z64`) ni nada inesperado en `assets/`/`res/`.
   - Reporta tamaño del APK y del `.so`.
5. Entrega un resumen breve: resultado, causa raíz (si hubo fallo) y el cambio mínimo propuesto. No modifiques archivos sin confirmación.
