// Arranque: ventana GL ES 3.0 a pantalla completa, montaje del VFS, localización, audio,
// menú de título interactivo (táctil, mando y teclado) y, con NUEVO JUEGO, exploración en
// cámara libre del primer nivel 3D, todo con gráficos y sonidos de game.kpf.
// Si algo falla, se muestra el color de diagnóstico (verde = VFS y localización OK pero sin
// gráficos, rojo = fallo de datos).
#include <SDL.h>
#include <SDL_main.h>
#include <GLES3/gl3.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "audio/audio_sdl.hpp"
#include "audio/ogg_music.hpp"
#include "audio/wav.hpp"
#include "input/input_sdl.hpp"
#include "loc/localization.hpp"
#include "log.hpp"
#include "title_screen.hpp"
#include "ui/menu_controller.hpp"
#include "ui/settings.hpp"
#include "vfs/vfs.hpp"
#include "vfs/vfs_sdl.hpp"
#include "world_scene.hpp"

namespace {

// Color de fondo como diagnóstico: verde = VFS y localización OK, rojo = fallo
constexpr float kOkR = 0.08f, kOkG = 0.22f, kOkB = 0.10f;
constexpr float kErrR = 0.35f, kErrG = 0.05f, kErrB = 0.05f;

constexpr std::size_t kLocMaxBytes = 4u << 20;  // límite de cordura para el archivo de texto
constexpr std::size_t kLocPreview = 8;           // cadenas mostradas de cada tipo

// La música no está dentro de game.kpf: son .ogg sueltos junto a él (ver docs/FORMATS.md)
constexpr const char* kTitleMusic = "game_data/music/n64/title.ogg";
// SFX del menú: rutas de los .wav referenciados por sounds/shaders/Menu *.ksnd
constexpr const char* kSfxMove = "sounds/waves/MENU04.wav";          // Menu Move Cursor
constexpr const char* kSfxSelect = "sounds/waves/MENUDST10FLT.wav";  // Menu Option Select
constexpr const char* kSfxChange = "sounds/waves/MENUNEW03.wav";     // Menu Change Option

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

// Monta el VFS y lo resume en logcat. true si la capa base (game.kpf) está montada.
bool mountVfs(vfs::Vfs& fs, const std::string& extDir) {
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
    }
    return rep.baseOk;
}

// Carga localization/loc_<idioma>.txt y lo resume en logcat. nullopt si no hay cadenas.
std::optional<loc::Table> loadStrings(const vfs::Vfs& fs, ui::Language lang) {
    const std::string path = std::string("localization/loc_") + ui::languageInfo(lang).code + ".txt";
    auto data = fs.read(path);
    if (!data || data->size() > kLocMaxBytes) {
        LOGE("LOC: no se pudo leer %s", path.c_str());
        return std::nullopt;
    }
    const std::string* from = fs.origin(path);
    loc::Table t = loc::parse(std::string_view(reinterpret_cast<const char*>(data->data()), data->size()));
    LOGI("LOC: %s (%s): %zu indexadas, %zu con clave, %zu mal formadas, %zu duplicadas", path.c_str(),
         from ? from->c_str() : "?", t.indexed().size(), t.keyed().size(), t.malformed(), t.duplicates());
    for (std::size_t i = 0; i < t.indexed().size() && i < kLocPreview; ++i) {
        LOGI("LOC [%zu] %s", i, oneLine(t.indexed()[i]).c_str());
    }
    for (std::size_t i = 0; i < t.keyed().size() && i < kLocPreview; ++i) {
        LOGI("LOC %s = %s", t.keyed()[i].first.c_str(), oneLine(t.keyed()[i].second).c_str());
    }
    if (t.indexed().empty() && t.keyed().empty()) return std::nullopt;
    return t;
}

// Lee una fuente completa con tope de tamaño (los datos del usuario no son confiables)
std::optional<std::vector<std::uint8_t>> readAll(vfs::ByteSource& src, std::size_t maxBytes) {
    const std::uint64_t size = src.size();
    if (size == 0 || size > maxBytes) return std::nullopt;
    std::vector<std::uint8_t> out(static_cast<std::size_t>(size));
    if (!src.readAt(0, out.data(), out.size())) return std::nullopt;
    return out;
}

// Música del título: assets del APK (install --mode local) o <ext>/game_data (--mode adb)
std::unique_ptr<audio::OggMusic> loadTitleMusic(const std::string& extDir) {
    std::string err;
    std::unique_ptr<vfs::ByteSource> src = vfs::openSdlSource(kTitleMusic, &err);
    if (!src && !extDir.empty()) src = vfs::openFileSource(extDir + "/" + kTitleMusic, &err);
    if (!src) {
        LOGE("AUDIO: no se encontró %s (%s)", kTitleMusic, err.c_str());
        return nullptr;
    }
    auto bytes = readAll(*src, audio::kMaxOggBytes);
    if (!bytes) {
        LOGE("AUDIO: %s vacío, demasiado grande o ilegible", kTitleMusic);
        return nullptr;
    }
    auto music = audio::OggMusic::open(std::move(*bytes), true, &err);
    if (!music) {
        LOGE("AUDIO: %s: %s", kTitleMusic, err.c_str());
        return nullptr;
    }
    LOGI("AUDIO: %s %u Hz, %d canales (bucle)", kTitleMusic, music->rate(), music->channels());
    return music;
}

std::shared_ptr<const audio::Pcm> loadSfx(const vfs::Vfs& fs, const char* path) {
    auto data = fs.read(path);
    if (!data) {
        LOGE("AUDIO: no se pudo leer %s", path);
        return nullptr;
    }
    std::string err;
    auto pcm = audio::parseWav(data->data(), data->size(), &err);
    if (!pcm) {
        LOGE("AUDIO: %s: %s", path, err.c_str());
        return nullptr;
    }
    return std::make_shared<const audio::Pcm>(std::move(*pcm));
}

struct MenuSounds {
    std::shared_ptr<const audio::Pcm> move, select, change;
    std::shared_ptr<const audio::Pcm> get(ui::Sfx s) const {
        switch (s) {
        case ui::Sfx::Move: return move;
        case ui::Sfx::Select: return select;
        case ui::Sfx::Change: return change;
        }
        return nullptr;
    }
};

// Ruta del archivo de ajustes en el directorio privado de la app ("" si no hay)
std::string settingsPath() {
    char* pref = SDL_GetPrefPath("turok2port", "turok2");
    if (!pref) {
        LOGE("SETTINGS: SDL_GetPrefPath falló: %s", SDL_GetError());
        return {};
    }
    std::string p = std::string(pref) + "settings.cfg";
    SDL_free(pref);
    return p;
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
    // Atrás (botón o gesto) llega como SDL_SCANCODE_AC_BACK en vez de cerrar la actividad:
    // el menú lo usa para volver de Opciones / del stub
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    // Los toques llegan como SDL_FINGER*: sin ratón sintético que duplique la entrada
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");

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

    const char* ext = SDL_AndroidGetExternalStoragePath();
    const std::string extDir = ext ? ext : "";
    if (!ext) LOGE("VFS: sin almacenamiento externo: %s", SDL_GetError());

    // Ajustes persistentes (volúmenes e idioma)
    const std::string cfgPath = settingsPath();
    const ui::Settings initial = cfgPath.empty() ? ui::Settings{} : ui::loadSettingsFile(cfgPath);
    LOGI("SETTINGS: música %d, sfx %d, idioma %s", initial.musicVolume, initial.sfxVolume,
         ui::languageInfo(initial.language).code);

    vfs::Vfs fs;
    loc::Table strings;
    bool vfsOk = mountVfs(fs, extDir);
    if (vfsOk) {
        auto t = loadStrings(fs, initial.language);
        if (!t && initial.language != ui::Language::Spanish) t = loadStrings(fs, ui::Language::Spanish);
        if (t) strings = std::move(*t);
        else vfsOk = false;
    }
    const float clearR = vfsOk ? kOkR : kErrR;
    const float clearG = vfsOk ? kOkG : kErrG;
    const float clearB = vfsOk ? kOkB : kErrB;

    ui::MenuController menu(initial);
    menu.setStrings(&strings);  // `strings` vive hasta el final de main()

    // Audio: si falla, el menú funciona igual en silencio
    audio::AudioDevice audioDev;
    MenuSounds sounds;
    {
        std::string err;
        if (!audioDev.open(&err)) {
            LOGE("AUDIO: no se pudo abrir el dispositivo: %s", err.c_str());
        } else {
            audioDev.setVolumes(initial.musicVolume, initial.sfxVolume);
            if (vfsOk) {
                sounds = {loadSfx(fs, kSfxMove), loadSfx(fs, kSfxSelect), loadSfx(fs, kSfxChange)};
            }
            if (auto music = loadTitleMusic(extDir)) {
                if (!audioDev.setMusic(std::move(music), &err)) LOGE("AUDIO: %s", err.c_str());
            }
            audioDev.resume();
        }
    }

    // Los recursos GL deben destruirse antes que el contexto: vida acotada con optional
    std::optional<TitleScreen> title(std::in_place);
    bool titleOk = vfsOk && title->load(fs);
    if (vfsOk && !titleOk) LOGE("TITLE: sin pantalla de título; se mantiene el color de diagnóstico");
    std::optional<WorldScene> world(std::in_place);  // nivel 3D: se carga al entrar en InGame

    // Ejecuta los efectos que pide el menú: SFX, volúmenes, idioma y guardado
    auto applyOutput = [&](const ui::MenuOutput& out) {
        for (ui::Sfx s : out.sfx) audioDev.playSfx(sounds.get(s));
        if (out.languageChanged) {
            if (auto t = loadStrings(fs, menu.settings().language)) strings = std::move(*t);
            else LOGE("LOC: se mantiene el idioma anterior");
        }
        if (out.settingsChanged) {
            const ui::Settings& st = menu.settings();
            audioDev.setVolumes(st.musicVolume, st.sfxVolume);
            std::string err;
            if (!cfgPath.empty() && !ui::saveSettingsFile(cfgPath, st, &err)) LOGE("SETTINGS: %s", err.c_str());
        }
    };

    std::vector<SDL_GameController*> controllers;
    input::StickPoller stick;
    std::vector<input::MenuAction> stickActions;
    SDL_FingerID activeFinger = 0;
    bool fingerDown = false;  // solo se sigue un dedo a la vez
    bool running = true;
    bool paused = false;
    GameState lastState = menu.state();

    Uint64 lastCounter = SDL_GetPerformanceCounter();
    const double counterFreq = static_cast<double>(SDL_GetPerformanceFrequency());
    auto inWorld = [&] { return menu.state() == GameState::InGame; };

    // Fila bajo un toque (coordenadas normalizadas de SDL -> píxeles del drawable)
    auto touchRow = [&](float nx, float ny, float* relX) {
        const float px = nx * static_cast<float>(viewW), py = ny * static_cast<float>(viewH);
        const auto rects = TitleScreen::rowRects(viewW, viewH, menu.rows().size());
        const int row = ui::hitTest(rects, px, py);
        if (relX) *relX = row >= 0 ? (px - rects[static_cast<std::size_t>(row)].x) / rects[static_cast<std::size_t>(row)].w : 0.5f;
        return row;
    };

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (auto a = input::menuActionFromEvent(ev)) applyOutput(menu.onAction(*a));

            switch (ev.type) {
            case SDL_QUIT:
                running = false;
                break;
            case SDL_APP_WILLENTERBACKGROUND:
                paused = true;  // no renderizar sin superficie
                audioDev.pause();
                fingerDown = false;
                world->cancelTouches();
                LOGI("App en segundo plano: audio en pausa");
                break;
            case SDL_APP_DIDENTERFOREGROUND:
                paused = false;
                audioDev.resume();
                LOGI("App en primer plano: audio reanudado");
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
                world->abandonGl();
                updateViewport(window, viewW, viewH);
                titleOk = vfsOk && title->load(fs);
                if (inWorld() && !world->reloadGl(fs)) LOGE("WORLD: no se pudo recargar tras perder el contexto");
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
            case SDL_FINGERDOWN:
                if (inWorld()) {
                    world->fingerDown(ev.tfinger.fingerId, ev.tfinger.x, ev.tfinger.y);
                } else if (!fingerDown) {
                    fingerDown = true;
                    activeFinger = ev.tfinger.fingerId;
                    applyOutput(menu.onTouchDown(touchRow(ev.tfinger.x, ev.tfinger.y, nullptr)));
                }
                break;
            case SDL_FINGERMOTION:
                if (inWorld()) world->fingerMotion(ev.tfinger.fingerId, ev.tfinger.x, ev.tfinger.y);
                break;
            case SDL_FINGERUP:
                if (inWorld()) {
                    // Botón de salida táctil; si el nivel no cargó, cualquier toque vuelve
                    const bool exit = world->fingerUp(ev.tfinger.fingerId, ev.tfinger.x, ev.tfinger.y);
                    if (exit || !world->loaded()) applyOutput(menu.onAction(input::MenuAction::Back));
                } else if (fingerDown && ev.tfinger.fingerId == activeFinger) {
                    fingerDown = false;
                    float relX = 0.5f;
                    const int row = touchRow(ev.tfinger.x, ev.tfinger.y, &relX);
                    applyOutput(menu.onTouchUp(row, relX));
                }
                break;
            default:
                break;
            }
        }

        stickActions.clear();
        stick.poll(controllers, SDL_GetTicks(), stickActions);
        for (input::MenuAction a : stickActions) applyOutput(menu.onAction(a));

        if (menu.state() != lastState) {
            LOGI("Escena: %d -> %d", static_cast<int>(lastState), static_cast<int>(menu.state()));
            if (lastState == GameState::InGame) world->unload();  // libera GPU y memoria del nivel
            lastState = menu.state();
            fingerDown = false;  // un toque que cambia de página no debe activar la siguiente
            if (inWorld() && !paused) {
                // Un frame con el mensaje de carga antes de bloquear leyendo el nivel
                if (titleOk) {
                    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                    title->drawMessage(viewW, viewH, menu.heading());
                    SDL_GL_SwapWindow(window);
                }
                if (!vfsOk || !world->load(fs)) LOGE("WORLD: no se pudo cargar el nivel; toca para volver");
                lastCounter = SDL_GetPerformanceCounter();  // la carga no cuenta como dt
            }
        }
        if (menu.state() == GameState::Quit) {
            LOGI("SALIR seleccionado: cerrando");
            running = false;
            continue;
        }

        const Uint64 nowCounter = SDL_GetPerformanceCounter();
        const auto dt = static_cast<float>(static_cast<double>(nowCounter - lastCounter) / counterFreq);
        lastCounter = nowCounter;

        if (paused) {
            SDL_Delay(16);  // evita bucle activo mientras está en background
            continue;
        }

        if (inWorld() && world->loaded()) {
            world->update(dt, controllers);
            world->draw(viewW, viewH);
            SDL_GL_SwapWindow(window);
            continue;
        }

        if (titleOk) glClearColor(0.0f, 0.0f, 0.0f, 1.0f);  // bandas negras fuera del área 4:3
        else glClearColor(clearR, clearG, clearB, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (titleOk) {
            if (inWorld()) {
                // El nivel no cargó (ver logcat): mensaje y vuelta con un toque o Atrás
                title->drawMessage(viewW, viewH, menu.heading());
            } else {
                const float t = static_cast<float>(SDL_GetTicks()) / 1000.0f;
                title->drawMenu(viewW, viewH, menu.heading(), menu.rows(), menu.selected(), t);
            }
        }
        SDL_GL_SwapWindow(window);
    }

    audioDev.close();  // detiene el hilo de audio antes de liberar nada más
    for (SDL_GameController* gc : controllers) {
        SDL_GameControllerClose(gc);
    }
    world.reset();  // glDelete* con el contexto aún vivo
    title.reset();
    // Tras SDL_RENDER_DEVICE_RESET el contexto actual ya no es `context`
    SDL_GL_DeleteContext(SDL_GL_GetCurrentContext());
    SDL_DestroyWindow(window);
    SDL_Quit();
    LOGI("SDL_main finalizado");
    return 0;
}
