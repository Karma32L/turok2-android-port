# stb_vorbis (vendored)

- Versión: **v1.23** (`// Ogg Vorbis audio decoder - v1.23`), dominio público / MIT (ver `LICENSE`).
- Origen: repositorio oficial `https://github.com/nothings/stb`, commit fijado
  `48fb1c9709f973041a7e6992d720ac625266cdd3` (2026-11-09, "update version numbers").
- URLs exactas:
  - `https://raw.githubusercontent.com/nothings/stb/48fb1c9709f973041a7e6992d720ac625266cdd3/stb_vorbis.c`
  - `https://raw.githubusercontent.com/nothings/stb/48fb1c9709f973041a7e6992d720ac625266cdd3/LICENSE`
- Archivos copiados sin modificar.

| Archivo        | SHA-256 |
|----------------|---------|
| `stb_vorbis.c` | `54ac11ef3821d05b544fa83cb60764cdb82fa4408a1680a8c11a047f84ce5ea8` |
| `LICENSE`      | `eb36e45c704dd0b59730115523e089b051852625b4db0a6fe76c991900af20e7` |

## Por qué v1.23 y no v1.22

La v1.22 (2021) no incluye la tanda de correcciones de octubre/noviembre de 2026
(comprobaciones de desbordamiento de tamaños, `lookup1_values`, UB en el recorte de muestras,
fugas en `start_decoder`, aserciones sobre límites de página). La v1.24 se publicó el mismo día
del vendoring y solo añade arreglos de warnings, así que se descartó por ser demasiado reciente.

## Configuración (`CMakeLists.txt`)

- `STB_VORBIS_NO_STDIO`: sin acceso a archivos; solo decodificamos desde memoria (los bytes
  llegan del VFS / `ByteSource`, ya acotados en tamaño).
- `STB_VORBIS_NO_PUSHDATA_API`: API no usada, menos superficie de ataque.
- `STB_VORBIS_NO_COMMENTS`: no se parsean los comentarios Vorbis (metadatos no confiables).
- Las aserciones internas se mantienen: ante datos imposibles, abortar es preferible a UB.

## Uso desde C++

Las declaraciones se obtienen con `#define STB_VORBIS_HEADER_ONLY` + `#include "stb_vorbis.c"`
(solo `src/audio/ogg_music.cpp`). Los handles no son thread-safe: cada `OggMusic` se usa desde
un único hilo a la vez (el callback de audio, bajo `SDL_LockAudioDevice`).

## Actualizar

Descargar el nuevo commit en un directorio aislado, revisar el diff y el changelog de
`stb_vorbis.c`, verificar, copiar ambos archivos y actualizar esta tabla. Ejecutar los tests
nativos con ASan/UBSan (`test_ogg`).
