// Escena activa de la aplicación.
#pragma once

enum class GameState {
    Title,       // menú principal
    Options,     // submenú de opciones (volúmenes e idioma)
    InGame,      // exploración del primer mapa 3D (cámara libre)
    Quit,        // el bucle principal debe terminar y liberar recursos
};
