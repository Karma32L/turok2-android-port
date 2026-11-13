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

## Arquitectura del esqueleto
- `android/app/src/main/AndroidManifest.xml`: `NativeActivity` sin código Java (`hasCode=false`), sin permisos.
- `src/CMakeLists.txt`: compila `libturok2.so` + `native_app_glue` del NDK.
- `src/main.cpp`: `android_main()` con bucle de eventos y log "Hola Mundo".
- Pendiente: integrar SDL2 (sustituirá a `native_app_glue` como capa de plataforma).
