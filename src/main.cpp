// Arranque: ventana GL ES 3.0 a pantalla completa, montaje del VFS, localización y
// pantalla de título con gráficos reales de game.kpf. Si algo falla, se muestra el color de
// diagnóstico (verde = VFS y localización OK pero sin gráficos, rojo = fallo de datos).
#include <SDL.h>
#include <SDL_main.h>
#include <GLES3/gl3.h>

#include <optional>
#include <string>
#include <vector>

#include "loc/localization.hpp"
#include "log.hpp"
#include "title_screen.hpp"
#include "vfs/vfs.hpp"
#include "vfs/vfs_sdl.hpp"

namespace {

// Color de fondo como diagnóstico: verde = VFS y localización OK, rojo = fallo
constexpr float kOkR = 0.08f, kOkG = 0.22f, kOkB = 0.10f;
constexpr float kErrR = 0.35f, kErrG = 0.05f, kErrB = 0.05f;

constexpr const char* kLocPath = "localization/loc_spanish.txt";
constexpr std::size_t kLocMaxBytes = 4u << 20;  // límite de cordura para el archivo de texto
constexpr std::size_t kLocPreview = 8;           // cadenas mostradas de cada tipo

// Una cadena por línea en logcat: escapa saltos de línea del texto traducido
std::string oneLine(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '\n') out += "\\n";
        else if (c == '\r' || c == '\t') out += ' ';
        else out += c;
    }
    return out;
}

// Monta el VFS, carga loc_spanish.txt en `table` y lo resume en logcat. true si todo fue bien.
bool runVfsDiagnostics(vfs::Vfs& fs, loc::Table& table) {
    const char* ext = SDL_AndroidGetExternalStoragePath();
    const std::string extDir = ext ? ext : "";
    if (!ext) LOGE("VFS: sin almacenamiento externo: %s", SDL_GetError());

    // Base: assets del APK (o almacenamiento interno); si no hay, mountStandardLayers
    // intenta <ext>/game_data/game.kpf
    std::string err;
    auto base = vfs::openSdlSource("game_data/game.kpf", &err);
    if (!base) LOGI("VFS: game.kpf no está en el APK (%s); se busca en %s", err.c_str(), extDir.c_str());

    const vfs::MountReport rep = vfs::mountStandardLayers(fs, std::move(base), "apk:game_data/game.kpf", extDir);
    for (const std::string& e : rep.errors) LOGE("VFS: %s", e.c_str());

    const vfs::Stats st = fs.stats();
    for (const vfs::LayerStats& l : st.layers) {
        LOGI("VFS capa %-28s %6zu archivos, %zu rechazados", l.name.c_str(), l.files, l.rejected);
    }
    LOGI("VFS: %zu capas, %zu archivos montados (únicos)", st.layers.size(), st.uniqueFiles);
    if (!rep.baseOk) {
        LOGE("VFS: no se pudo montar game.kpf. Ejecuta: python3 tools/prepare_assets.py install --mode adb");
        return false;
    }

    auto data = fs.read(kLocPath);
    if (!data || data->size() > kLocMaxBytes) {
        LOGE("LOC: no se pudo leer %s", kLocPath);
        return false;
    }
    const std::string* from = fs.origin(kLocPath);
    table = loc::parse(std::string_view(reinterpret_cast<const char*>(data->data()), data->size()));
    const loc::Table& t = table;
    LOGI("LOC: %s (%s): %zu indexadas, %zu con clave, %zu mal formadas, %zu duplicadas",
         kLocPath, from ? from->c_str() : "?", t.indexed().size(), t.keyed().size(), t.malformed(),
         t.duplicates());
    for (std::size_t i = 0; i < t.indexed().size() && i < kLocPreview; ++i) {
        LOGI("LOC [%zu] %s", i, oneLine(t.indexed()[i]).c_str());
    }
    for (std::size_t i = 0; i < t.keyed().size() && i < kLocPreview; ++i) {
        LOGI("LOC %s = %s", t.keyed()[i].first.c_str(), oneLine(t.keyed()[i].second).c_str());
    }
    return !t.indexed().empty() || !t.keyed().empty();
}

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

void updateViewport(SDL_Window* window, int& w, int& h) {
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
    int viewW = 0, viewH = 0;
    updateViewport(window, viewW, viewH);

    vfs::Vfs fs;
    loc::Table strings;
    const bool vfsOk = runVfsDiagnostics(fs, strings);
    const float clearR = vfsOk ? kOkR : kErrR;
    const float clearG = vfsOk ? kOkG : kErrG;
    const float clearB = vfsOk ? kOkB : kErrB;

    // Los recursos GL deben destruirse antes que el contexto: vida acotada con optional
    std::optional<TitleScreen> title(std::in_place);
    bool titleOk = vfsOk && title->load(fs, &strings);
    if (vfsOk && !titleOk) LOGE("TITLE: sin pantalla de título; se mantiene el color de diagnóstico");

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
                    updateViewport(window, viewW, viewH);
                }
                break;
            case SDL_RENDER_DEVICE_RESET:
                // SDL creó un contexto EGL nuevo al reanudar: los IDs GL antiguos ya no valen
                LOGI("Contexto GL perdido: recargando recursos");
                title->abandonGl();
                updateViewport(window, viewW, viewH);
                titleOk = vfsOk && title->load(fs, &strings);
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

        if (titleOk) glClearColor(0.0f, 0.0f, 0.0f, 1.0f);  // bandas negras fuera del área 4:3
        else glClearColor(clearR, clearG, clearB, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (titleOk) title->draw(viewW, viewH);
        SDL_GL_SwapWindow(window);
    }

    for (SDL_GameController* gc : controllers) {
        SDL_GameControllerClose(gc);
    }
    title.reset();  // glDelete* con el contexto aún vivo
    // Tras SDL_RENDER_DEVICE_RESET el contexto actual ya no es `context`
    SDL_GL_DeleteContext(SDL_GL_GetCurrentContext());
    SDL_DestroyWindow(window);
    SDL_Quit();
    LOGI("SDL_main finalizado");
    return 0;
}
