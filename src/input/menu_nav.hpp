// Lógica de navegación de menús independiente del dispositivo (táctil, mando, teclado).
// La traducción desde SDL_Event vive en input_sdl.*; aquí solo hay estado puro testeable.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace input {

enum class MenuAction { Up, Down, Left, Right, Accept, Back };

struct NavEvent {
    enum Kind { None, Moved, Activated, Adjusted, Back };
    Kind kind = None;
    int index = -1;  // fila afectada
    int delta = 0;   // Adjusted: -1 / +1
};

struct MenuItem {
    bool enabled = true;      // deshabilitada: se dibuja atenuada y la navegación la salta
    bool adjustable = false;  // fila de valor (volumen, idioma): izq./der. ajustan
};

class MenuNav {
public:
    // Selecciona `selected` si es válida y está habilitada; si no, la primera habilitada.
    void setItems(std::vector<MenuItem> items, int selected = 0);
    int selected() const { return selected_; }
    const std::vector<MenuItem>& items() const { return items_; }

    NavEvent handle(MenuAction a);

    // Táctil: `hit` = fila bajo el dedo (-1 si ninguna). touchDown selecciona; touchUp activa
    // solo si el dedo se levanta sobre la misma fila en la que bajó (deslizar no activa).
    // `relX` (0..1) es la posición horizontal dentro de la fila: en filas ajustables la mitad
    // izquierda resta y la derecha suma.
    NavEvent touchDown(int hit);
    NavEvent touchUp(int hit, float relX);
    void touchCancel() { pressed_ = -1; }

private:
    bool usable(int i) const;
    NavEvent step(int dir);

    std::vector<MenuItem> items_;
    int selected_ = -1;
    int pressed_ = -1;
};

// Convierte un eje analógico (-1..1) en pulsaciones discretas con zona muerta, histéresis y
// autorepetición. El tiempo se inyecta (ms) para poder probarlo sin reloj real.
class StickRepeater {
public:
    static constexpr float kPress = 0.5f;     // umbral para considerar la dirección pulsada
    static constexpr float kRelease = 0.35f;  // umbral para soltarla (histéresis)
    static constexpr std::uint32_t kInitialDelayMs = 400;
    static constexpr std::uint32_t kRepeatMs = 120;

    // Devuelve -1/+1 cuando toca emitir un paso, 0 si no.
    int update(float value, std::uint32_t nowMs);

private:
    int dir_ = 0;
    std::uint32_t next_ = 0;
};

}  // namespace input
