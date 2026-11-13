// Escena activa de la aplicación.
#pragma once

enum class GameState {
    Title,       // menú principal
    Options,     // submenú de opciones (volúmenes e idioma)
    InGameStub,  // marcador de posición: aquí se cargará el primer mapa 3D
    Quit,        // el bucle principal debe terminar y liberar recursos
};
