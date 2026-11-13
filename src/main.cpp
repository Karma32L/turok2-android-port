// Esqueleto mínimo: verifica que la cadena NDK -> CMake -> Gradle produce un APK ARM64 funcional.
#include <android/log.h>
#include <android_native_app_glue.h>

#define LOG_TAG "turok2"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

// Punto de entrada llamado por native_app_glue en su propio hilo
void android_main(android_app* app) {
    LOGI("Hola Mundo desde C++ nativo (ABI: %s)",
#if defined(__aarch64__)
         "arm64-v8a"
#else
         "desconocido"
#endif
    );

    // Bucle de eventos: procesa eventos del sistema hasta que se pida destruir la actividad
    while (!app->destroyRequested) {
        int events = 0;
        android_poll_source* source = nullptr;
        // Timeout -1: bloquea hasta que haya un evento (no hay render todavía)
        if (ALooper_pollOnce(-1, nullptr, &events, reinterpret_cast<void**>(&source)) >= 0 &&
            source != nullptr) {
            source->process(app, source);
        }
    }

    LOGI("android_main finalizado");
}
