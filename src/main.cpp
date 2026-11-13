// Prueba de integración SDL2: ventana GL ES 3.0 a pantalla completa que limpia a verde
// y registra la información del driver GL en logcat (tag "turok2").
#include <SDL.h>
#include <SDL_main.h>
#include <GLES3/gl3.h>
#include <android/log.h>

#include <vector>

#define LOG_TAG "turok2"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

// Verde oscuro "Turok" para la prueba de clear
constexpr float kClearR = 0.08f, kClearG = 0.22f, kClearB = 0.10f;

// glGetString puede devolver nullptr si no hay contexto válido: nunca pasar nullptr a %s
const char* glStr(GLenum name) {
    const GLubyte* s = glGetString(name);
    return s ? reinterpret_cast<const char*>(s) : "(desconocido)";
}

void logVersions() {
    SDL_version compiled;
    SDL_version linked;
    SDL_VERSION(&compiled);
    SDL_GetVersion(&linked);
    LOGI("SDL compilado %u.%u.%u / enlazado %u.%u.%u",
         compiled.major, compiled.minor, compiled.patch,
         linked.major, linked.minor, linked.patch);
    LOGI("GL_VENDOR:   %s", glStr(GL_VENDOR));
    LOGI("GL_RENDERER: %s", glStr(GL_RENDERER));
    LOGI("GL_VERSION:  %s", glStr(GL_VERSION));
    LOGI("GLSL:        %s", glStr(GL_SHADING_LANGUAGE_VERSION));
}

void updateViewport(SDL_Window* window) {
    int w = 0, h = 0;
    SDL_GL_GetDrawableSize(window, &w, &h);
    glViewport(0, 0, w, h);
    LOGI("Viewport %dx%d", w, h);
}

}  // namespace

int main(int /*argc*/, char* /*argv*/[]) {
    // Solo landscape (complementa screenOrientation del manifest)
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        LOGE("SDL_Init falló: %s", SDL_GetError());
        return 1;
    }

    // Contexto OpenGL ES 3.0
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    // En Android, SDL_WINDOW_FULLSCREEN activa el modo inmersivo
    SDL_Window* window = SDL_CreateWindow(
        "Turok2 Port", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 0, 0,
        SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_ALLOW_HIGHDPI);
    if (window == nullptr) {
        LOGE("SDL_CreateWindow falló: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (context == nullptr) {
        LOGE("SDL_GL_CreateContext falló: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    if (SDL_GL_SetSwapInterval(1) != 0) {
        LOGI("VSync no disponible: %s", SDL_GetError());
    }

    logVersions();
    updateViewport(window);

    std::vector<SDL_GameController*> controllers;
    bool running = true;
    bool paused = false;

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_QUIT:
                running = false;
                break;
            case SDL_APP_WILLENTERBACKGROUND:
                paused = true;  // no renderizar sin superficie
                LOGI("App en segundo plano");
                break;
            case SDL_APP_DIDENTERFOREGROUND:
                paused = false;
                LOGI("App en primer plano");
                break;
            case SDL_WINDOWEVENT:
                if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    updateViewport(window);
                }
                break;
            case SDL_CONTROLLERDEVICEADDED:
                if (SDL_GameController* gc = SDL_GameControllerOpen(ev.cdevice.which)) {
                    controllers.push_back(gc);
                    const char* name = SDL_GameControllerName(gc);
                    LOGI("Mando conectado: %s", name ? name : "(sin nombre)");
                }
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
                // En REMOVED, 'which' es el instance id del joystick
                for (auto it = controllers.begin(); it != controllers.end(); ++it) {
                    SDL_Joystick* js = SDL_GameControllerGetJoystick(*it);
                    if (js && SDL_JoystickInstanceID(js) == ev.cdevice.which) {
                        SDL_GameControllerClose(*it);
                        controllers.erase(it);
                        LOGI("Mando desconectado");
                        break;
                    }
                }
                break;
            case SDL_CONTROLLERBUTTONDOWN:
                LOGI("Botón de mando: %s",
                     SDL_GameControllerGetStringForButton(
                         static_cast<SDL_GameControllerButton>(ev.cbutton.button)));
                break;
            case SDL_FINGERDOWN:
                LOGI("Toque en (%.2f, %.2f)", ev.tfinger.x, ev.tfinger.y);
                break;
            default:
                break;
            }
        }

        if (paused) {
            SDL_Delay(16);  // evita bucle activo mientras está en background
            continue;
        }

        glClearColor(kClearR, kClearG, kClearB, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        SDL_GL_SwapWindow(window);
    }

    for (SDL_GameController* gc : controllers) {
        SDL_GameControllerClose(gc);
    }
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    LOGI("SDL_main finalizado");
    return 0;
}
