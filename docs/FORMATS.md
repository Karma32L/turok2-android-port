# Formatos de `game.kpf`

Reconocimiento hecho **en solo lectura y en memoria** sobre la copia del usuario (Steam,
BuildID 22373491). Este documento describe **estructura**, nunca contenido del juego.
Para reproducirlo:

```bash
python3 tools/prepare_assets.py formats
```

`game.kpf` es un ZIP estándar (deflate) montado por el VFS (ver [VFS.md](VFS.md)).
Tiene 22 646 entradas: 22 562 archivos y 84 entradas de directorio.

## Inventario por carpeta

| Carpeta | Archivos | Contenido principal |
|---|---:|---|
| `textures/` | 7476 | `.png` de mundo y personajes |
| `models/` | 5344 | `.staticmesh` |
| `materials/` | 3923 | `.kmat` |
| `sounds/` | 3182 | `.wav` + `.ksnd` (definiciones) |
| `char/` | 641 | `.skinnedmesh` |
| `anims/` | 479 | `.anim` |
| `particles/` | 383 | `.particle` |
| `cinemas/` | 203 | `.txt` (guiones de cinemáticas) |
| `levels/` | 177 | `.map` |
| `localized/` | 160 | `.wav` de voces por idioma |
| `sprites/` | 156 | `.png` + `.txt` |
| `gfx/` | 112 | HUD, fondos de menú, logo (`.png`) |
| `trails/`, `progs/`, `defs/`, `editor/`, `quickwarps/`, `scripts/`, `fonts/`, … | < 100 c/u | ver tabla de formatos |

## Formatos

| Extensión | Nº | Magic / primeros bytes | Tipo | Estado |
|---|---:|---|---|---|
| `.png` | 7696 | `89 50 4E 47 0D 0A 1A 0A` + `IHDR` | Imagen estándar | **Soportado** (`src/render/png.*`) |
| `.kfont` | 4 | texto (`filter`, `texture`, …) | Fuente bitmap | **Soportado** (`src/render/kfont.*`) |
| `.staticmesh` | 5344 | `02 00 00 00` (u32 LE, versión) | Binario | Pendiente |
| `.skinnedmesh` | 641 | `03 00 00 00` + floats (¿bbox?) | Binario | Pendiente |
| `.anim` | 479 | `02 00 00 00` + u32 + … | Binario | Pendiente |
| `.map` | 177 | `09 00 00 00` + tabla de offsets u32 (`0x30`, `0x38`, …) | Binario | Pendiente |
| `.wav` | 2075 | `RIFF....WAVEfmt ` | Audio estándar | **Soportado** (`src/audio/wav.*`) |
| `.kmat` | 3926 | texto (`material …`, `//`) | Material | Pendiente |
| `.ksnd`, `.particle`, `.kafx`, `.klf` | ~1650 | texto con bloques `nombre[i] = { … }` | Definiciones | Pendiente |
| `.txt` (`defs/`, `cinemas/`, `scripts/`, …) | 449 | texto `clave = valor` | Configuración | `localization/` soportado |
| `.cfg` | 5 | texto (`bind`, `seta`) | Consola/config | Pendiente |
| `.shader` / `.inc` | 77 | texto, macros propias sobre HLSL/GLSL | Shaders | Solo referencia |
| `progs/dxShaders*.bin` | 2 | `03 00 00 00 18 00 00 00 …` | Shaders D3D precompilados | No aplica en GLES |
| `.tga` | 1 | TGA tipo 2 (true color sin RLE) | Atlas de `confont` | Pendiente |
| `.ttf` | 6 | TrueType (`00 01 00 00`) | Fuentes vectoriales | Pendiente |
| `.bmp` | 25 | `BM` | Iconos del editor | No necesario |

## PNG

Distribución real (profundidad, tipo de color, entrelazado):

| Variante | Nº | Soporte |
|---|---:|---|
| 8 bits, RGBA (6), sin entrelazar | 6570 | Sí |
| 8 bits, RGB (2), sin entrelazar | 1124 | Sí |
| 8 bits, gris (0), sin entrelazar | 1 | Sí (se expande a RGB) |
| 8 bits, RGBA, **Adam7** | 1 (`gfx/crosshair.png`) | Se rechaza con error explícito |

El decodificador (`render::decodePng`) usa `tinfl` de miniz y es estricto: CRC por chunk,
longitudes validadas contra el archivo, dimensiones ≤ 8192, salida ≤ 64 MiB calculada en
64 bits **antes** de reservar, y el flujo zlib (con Adler-32) debe producir exactamente
`alto × (1 + ancho × bpp)` bytes. Los chunks auxiliares (`gAMA`, `iCCP`, `tEXt`, `pHYs`, …)
se ignoran, así que no se aplica corrección de gamma ni perfil ICC.

Validación con los datos reales: 7695 PNG decodificados **bit a bit idénticos** a Pillow
y cero informes de ASan/UBSan.

## Audio

### `.wav` (dentro de `game.kpf`)

Recuento sobre los 2075 archivos:

| Variante | Nº |
|---|---:|
| PCM (tag 1), 16 bits, mono, **11 025 Hz** | 1267 |
| PCM (tag 1), 16 bits, mono, 44 100 Hz | 808 |
| Chunks `fmt ` + `data` (+ `LIST`, + `id3 `) | 429 / 965 / 681 |
| Bytes extra después del tamaño declarado en RIFF | 500 |

Todos tienen `data` completo y múltiplo de `blockAlign`. `audio::parseWav` acepta solo PCM
de 16 bits con 1–2 canales a 8–192 kHz, valida cada chunk contra el tamaño real, rechaza
`fmt`/`data` duplicados y campos incoherentes (`blockAlign`, `byteRate`), y tolera los
bytes de cola recorriendo solo `min(tamaño, 8 + tamaño RIFF)`. El mezclador remuestrea los
SFX a 44 100 Hz con interpolación lineal en punto fijo.

Los `.ksnd` (`sounds/shaders/`) son definiciones de texto que apuntan al `.wav`
(`wavefile = "sounds/waves/X.WAV"`, en mayúsculas; el VFS no distingue mayúsculas). Los
sonidos del menú usados por el port:

| Definición | WAV |
|---|---|
| `Menu Move Cursor.ksnd` | `sounds/waves/MENU04.wav` |
| `Menu Option Select.ksnd` | `sounds/waves/MENUDST10FLT.wav` |
| `Menu Change Option.ksnd` | `sounds/waves/MENUNEW03.wav` |

### Música `.ogg` (**fuera** de `game.kpf`)

La banda sonora no está dentro del `.kpf`: son 20 archivos Ogg Vorbis sueltos en
`music/n64/` junto a él (p. ej. `title.ogg`, 44 100 Hz estéreo, ≈ 4,5 MB).
`prepare_assets.py install` los copia a `game_data/music/n64/`, y la app los busca primero en
los assets del APK y después en `<almacenamiento externo>/game_data/music/n64/`.

`audio::OggMusic` usa stb_vorbis v1.23 (ver `third_party/stb/VENDOR.md`): el archivo
comprimido se guarda en memoria (≤ 64 MiB) y se decodifica bajo demanda en bloques
pequeños, con vuelta al principio para el bucle. Solo se aceptan 1–2 canales a 8–192 kHz;
el mezclador exige 44 100 Hz para la música.

Validación con los datos reales: `title.ogg` decodificado y en bucle, más 40 mutaciones del
stream, sin informes de ASan/UBSan (`TURK2_OGG_SAMPLE`, ver `tests/native/test_ogg.cpp`).

## `.kfont`

Texto ASCII con CRLF. Directivas de nivel superior:

| Directiva | Ejemplo | Significado |
|---|---|---|
| `texture "<ruta>"` | `texture "fonts/x.png"` | Atlas en el VFS (obligatoria; puede ser `.png` o `.tga`) |
| `filter <modo>` | `filter linear` | Filtrado del atlas |
| `scale <n>` | `scale 0.5` | Escala de dibujo |
| `padwidth <n>` | `padwidth -8.0` | Ajuste del avance horizontal en px del atlas |
| `nosdf` | | El atlas no es un campo de distancia |
| `mapchar { … }` | | Tabla de glifos; `{` en la misma línea o en la siguiente |

Cada línea del bloque `mapchar` tiene la forma `código x y ancho alto desplazamiento_y`
(seguida opcionalmente de un comentario `//`):

- `código` es **Latin-1** (0–255): á = 225, ñ = 241, ¿ = 191, ¡ = 161.
- `loc_*.txt` está en UTF-8, así que el layout convierte UTF-8 → codepoint y usa
  directamente los ≤ 255.
- En las 4 fuentes del juego, `desplazamiento_y` vale siempre 0.

Avance horizontal usado por el port: `(ancho + padwidth) × scale`.

| Fuente | Atlas | Glifos | Notas |
|---|---|---:|---|
| `bigfont` | PNG 512×256 RGBA | 112 | Cubre todos los caracteres de `loc_spanish.txt` (en otros idiomas, los caracteres sin glifo se dibujan como `?`) |
| `smallfont01` | PNG 256×128 RGBA | 79 | 1 código duplicado en el original (gana el primero) |
| `numbers` | PNG 512×256 RGBA | 10 | `nosdf` |
| `confont` | TGA 256×256 | 256 | Consola; requiere lector TGA |

Aunque solo `numbers` declara `nosdf`, `bigfont` tampoco se comporta como un SDF en la
práctica (≈ 4 % de píxeles con alfa intermedio): basta con alpha blending y filtrado lineal.

## Pendiente de análisis

Los formatos binarios (`.staticmesh`, `.skinnedmesh`, `.anim`, `.map`) solo tienen
identificada la cabecera. Cada parser nuevo seguirá el mismo patrón que el decodificador PNG:
- núcleo independiente de GL en `src/render/` (o en su propio módulo);
- límites comprobados en cada offset y tamaño;
- tests host con datos sintéticos en `tests/native/`.
