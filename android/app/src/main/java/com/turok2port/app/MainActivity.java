package com.turok2port.app;

import org.libsdl.app.SDLActivity;

/**
 * Actividad principal: SDLActivity gestiona ciclo de vida, superficie, entrada y audio.
 * SDL busca el símbolo SDL_main en la ÚLTIMA librería de la lista (libturok2.so).
 */
public class MainActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {
            "SDL2",
            "turok2",
        };
    }
}
