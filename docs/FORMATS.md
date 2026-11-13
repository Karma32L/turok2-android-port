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
| `.staticmesh` | 5344 | `02 00 00 00` (u32 LE, versión) | Binario | **Soportado** (`src/render3d/static_mesh.*`) |
| `.skinnedmesh` | 641 | `03 00 00 00` (u32 LE, versión) + bbox | Binario | **Soportado** (`src/render3d/skinned_mesh.*`) |
| `.anim` | 479 | `02 00 00 00` (u32 LE, versión) + nº de clips | Binario | **Soportado** (`src/render3d/anim_file.*`) |
| `.map` | 177 | `09 00 00 00` + tabla de offsets u32 (`0x30`, `0x38`, …) | Binario | **Parcial**: geometría estática, colisión, cielo y actores (`src/render3d/map_file.*`) |
| `.wav` | 2075 | `RIFF....WAVEfmt ` | Audio estándar | **Soportado** (`src/audio/wav.*`) |
| `.kmat` | 3926 | texto (`material …`, `//`) | Material | **Soportado** (`src/render3d/kmat.*`) |
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

## Niveles: mapa inicial de la campaña

`defs/gameInfo.txt` define `newGameCinematic "cinemas/Gstart-far shot.txt"`, una cinemática
que transcurre en `levels/(6)_cin_adon.map`. El primer nivel jugable es `kLevel_PortOfAdia_1`
de `defs/mapInfo.txt` → **`levels/Port_of_Adia_1.map`** (`maxZDrawDistance 4000`). El nivel
del Puerto de Adia está partido en 9 mapas (`Port_of_Adia_1..9`); el port carga solo el primero.

En el juego no hay archivos `.ksh`: los materiales son solo `.kmat`, y los `progs/*.shader`
son un macro-lenguaje propio sobre HLSL/GLSL. El port usa shaders GLSL ES 3.00 propios.

Convenciones del mundo (verificadas con los datos):
- **Z hacia arriba**, unidades del mapa; ángulos en **radianes**.
- Triángulos **CCW** = cara frontal (3 022 de 3 041 triángulos de Port_of_Adia_1 coinciden
  con su normal guardada).
- Altura de ojos del jugador: `viewHeight 100.0` (`defs/postureInfo.txt`, `Player_Standing`).

## `.map`

### Árbol de nodos

Todo el archivo es un árbol de nodos sin etiquetas de tipo; el esquema decide cómo leer
cada nivel. Un nodo es un rango de bytes que se interpreta como:

| Tipo | Disposición (u32 LE) |
|---|---|
| Contenedor | `n`, `off[n+1]`; hijo *i* = `[off[i], off[i+1])` relativo al inicio del nodo. `off[0]` = `4·(n+2)` redondeado a 8, `off[n]` = tamaño del nodo |
| Hoja | `stride`, `count`, `stride·count` bytes de datos, relleno a múltiplo de 8 |

Las cadenas son hojas con `count = 1` y `stride` = longitud + NUL. El `09 00 00 00` inicial
no es una versión aparte: es el `n` del contenedor raíz (los 177 mapas tienen 9 secciones).

### Secciones de la raíz

| Sección | Contenido | Uso |
|---|---|---|
| 0 | hoja vacía | — |
| 1 | `[0]` 16 cadenas: material de cada capa de cielo (`skies/skyMaterials/…`) o `_default`; `[1]` 16 registros de 44 B (capas de cielo) | **Sí**: capas de cielo |
| 2 | **colisión**: `{[0] puntos 16 B, [1] áreas 96 B (92 en `n64/`), [2] sectores 18 B}`; en Port_of_Adia_1 556 / 122 / 824 | **Sí**: física, agua y niebla |
| 3 | `[0]` 2 × u16; `[1]`, `[2]` una entrada de 8 B (2 × f32) por área (¿límites 2D?) | No |
| **4** | **áreas**: cada una es una hoja vacía o un contenedor `{[0] hoja de instancias, [1] contenedor de rutas .staticmesh}` con tantas rutas como instancias | **Sí** |
| 5 | actores: contenedor de `{[0] cabecera 48 B, [1] componentes}` (ver "Actores") | **Sí**: inicio del jugador y actores con modelo |
| 6–8 | vacías en los mapas inspeccionados | — |

### Instancia de malla estática

88 B en la campaña, **84 B** en `levels/n64/` (multijugador); los primeros 64 B son iguales:

| Offset | Tipo | Campo |
|---:|---|---|
| 0 | `f32[3]` | posición |
| 12 | `f32[3]` | escala por eje (negativa = espejo, 19 de 53 760 instancias) |
| 24 | `f32[3]` | rotación `(r0, r1, r2)` en radianes |
| 36 | `f32[3]` | bbox mínima en mundo |
| 48 | `f32[3]` | bbox máxima en mundo |
| 60 | `u32` | flags |
| 64 | 20–24 B | sin identificar (aparecen `61.44`, `4096.0`) |

Transformación: `p_mundo = posición + R · (escala ⊙ p_local)` con
**`R = Rz(−r0) · Ry(−r2) · Rx(−r1)`**. Se determinó comparando la bbox guardada con la de
la malla transformada: con esta fórmula encajan todas las instancias con yaw distinguible
(13 859 a favor, 0 en contra con el signo opuesto) y el 85 % de las que tienen pitch/roll;
los casos que no encajan con ninguna variante son bbox desfasadas (sobre todo en mapas de
cinemática), así que la fórmula de pitch/roll queda como la mejor hipótesis disponible. Con escala espejo se invierte el orden
de los índices para conservar el CCW.

### Actores (sección 5)

Cada actor es un contenedor `{[0] cabecera, [1] componentes}` (54 de 27 986 actores no traen `[1]`).

**Cabecera (hoja de 48 B, `count = 1`):**

| Offset | Tipo | Campo |
|---:|---|---|
| 0 | `u32` | tipo (`kActor_*` de `defs/common.txt`; `0xFFFFFFFF` = genérico, p. ej. luces) |
| 4 | `f32[3]` | posición |
| 16 | `f32[3]` | escala |
| 28 | `f32[3]` | rotación (`[0]` = yaw), misma convención que las instancias |
| 40 | `u32` | sin identificar (en los `kActor_WarpPoint` vale 11000, 11813…: ¿id de warp?) |
| 44 | 4 B | sin identificar |

**Componentes:** contenedor de componentes; cada uno es `{[0] cadena nombre, [1] contenedor de
cadenas clave, valor, clave, valor…}`. **Todo en texto**, también los números (`"40.959999"`).
Las rutas de malla vienen en el propio mapa, así que no hace falta `defs/actors/*.txt` para dibujar:

| Componente | Claves usadas por el port |
|---|---|
| `kexRenderMeshComponent` | `mesh` (`.skinnedmesh`), `anim` (`.anim`) |
| `kexStaticMeshComponent` | `mesh` (`.staticmesh`) |
| `kexWorldComponent` | `bTrackGround`, `heightOffset`, `radius`, `height` |
| `kexEnemyAIComponent` | `startAnimID` |

El resto (`kexLightComponent`, `kexRenderSpriteComponent`, `kexScriptComponent`,
`kexEventLinkComponent`, …) se ignora sin leer sus cadenas: `kexScriptComponent` trae valores
terminados en `\n` y hasta cientos de pares. 15 actores tienen escala 0 a propósito (ocultos): se
conservan pero no se dibujan. Validado en los 177 mapas: todas las cabeceras miden 48 B, todos
los componentes tienen 2 nodos y todas las claves y valores son cadenas. Máximo: 682 actores por mapa.

Port_of_Adia_1 tiene 155 actores: 1 enemigo (`kActor_AI_Endtrail`), 30 con `.skinnedmesh`
(interruptores, puertas, balizas, destructibles), 32 con `.staticmesh` (LifeForce, flechas, pistola),
3 `kActor_WarpPoint`, 1 `kActor_Misc_Portal` y luces/sprites de editor. Los Raptoids aparecen a partir de
`port_of_adia_2`.

**Inicio del jugador:** tipo **100** = `kActor_SPStart`.

### Capa de cielo (sección 1 `[1]`, 44 B)

El registro *i* va con el nombre *i* de `[0]`; los `_default` no se dibujan. Coincide byte a byte con
el bloque `SkyLayer_*` de `defs/skyInfo.txt` (Port_of_Adia_1 ↔ `SkyLayer_PortOfAdia`, verificado).

| Offset | Tipo | Campo |
|---:|---|---|
| 0 | `u8[4]` | `whiteColor` RGBA |
| 4 | `u8[4]` | `blackColor` RGBA |
| 8 | `f32` × 6 | `animSpeed`, `cameraTrack`, `speedScalar`, `heightOffset`, `fadeThroughDist`, `tile` |
| 32 | `f32` × 2 | `fadeInTime`, `fadeOutTime` (no se usan) |
| 40 | `u8` | `opacity` (0–255) |
| 41 | `u8` | `flags` |
| 42 | `u16` | sin identificar |

### Colisión (sección 2)

Es una malla 2.5D de **sectores** triangulares. El jugador camina sobre ella: no hay archivos `.kcol`.
Esquema común a los 177 mapas (máximo 2 508 puntos, 383 áreas y 3 924 sectores).

**Punto (16 B):** `f32 x, y, sueloZ, techoZ`. Si `techoZ ≤ sueloZ`, el punto no tiene techo: pasa en
2 733 puntos de otros mapas, casi siempre con `techo == suelo`. Dos puntos de `RiverOfSouls_1` traen un
techo de ~5·10⁷; el parser los trata también como "sin techo".

**Sector (18 B = 9 × u16):**

| Campo | Significado | Estado |
|---|---|---|
| `area` | índice de área | verificado (siempre < nº de áreas) |
| `flags` | 0x1, 0x8, 0x40, 0x200… | sin identificar |
| `p[3]` | índices de punto, **CCW en XY** (824/824) | verificado |
| `link[3]` | vecino al otro lado de la arista `(p[(e+2)%3], p[e])`; `0xFFFF` = pared | verificado (2 172 simétricos, 12 de un solo sentido) |
| `region` | coincide con el u32 del offset 92 del área | hipótesis |

**Área (96 B; 92 B en `levels/n64/`, iguales en los primeros 36 B):**

| Offset | Tipo | Campo | Estado |
|---:|---|---|---|
| 0 | `u32` | flags; **bit 0 = agua** (31 áreas y 303 sectores en Port_of_Adia_1) | bit 0 verificado |
| 8 | `u8[4]` | color de niebla RGBA (110,110,120 en el puerto) | hipótesis fuerte |
| 12 | `u8[4]` | color del agua RGBA (74,99,113,165 en las áreas de agua) | hipótesis |
| 16 | `f32` | fin de la niebla (5120) | hipótesis |
| 20 | `f32` | segunda distancia (1536; 3072 en áreas de agua) | sin identificar |
| 24 | `f32` | inicio de la niebla ≈ 0,73 × @16 | hipótesis |
| 28 | `f32` | ≈ 0,73 × @20 | sin identificar |
| 32 | `f32` | altura del agua (−737,28) | hipótesis fuerte |

El inicio del jugador de Port_of_Adia_1 cae en el sector 2 y queda 10 u por encima de su suelo.
`render3d::parseMap` valida todos los índices (área, punto y vecino) y los floats. Si la sección 2 es
inválida, se descarta con un mensaje (`MapData::collisionError`) sin perder el mapa, y el juego queda en
modo vuelo. Los valores raros de un área (niebla o agua no finitas) se neutralizan.

### Validación

`render3d::parseMap` rechaza raíces que no tengan 9 secciones y cualquier offset o tamaño que
se salga del nodo. Una instancia con valores no finitos, escala ~0, coordenadas absurdas o
ruta insegura (`..`, absoluta, sin `.staticmesh`) se omite y se cuenta. Con los datos reales:
177/177 mapas válidos, 53 760 instancias; Port_of_Adia_1 tiene 221 instancias de 64 mallas,
6 053 triángulos, 177 materiales y 45 texturas, todas presentes.

## `.staticmesh` (versión 2)

Secuencial, little-endian:

```
u32 version (= 2)
u32 reservado[4]            siempre 0
u32 a, u32 b                opacos: (3, 24) en 3 087 archivos, ("\x01CNR", tamaño) en 2 215
u32 numSurfaces
numSurfaces × {
    char material[]         terminado en NUL, sin prefijo de longitud
    u32  numTris
    u16  idx[numTris·3]     índices al bloque de vértices COMPARTIDO
}
u32 numVerts
numVerts × { f32 pos[3]; f32 uv[2]; f32 normal[3] }      32 B por vértice
```

Una malla vacía termina justo después de `numSurfaces = 0` (32 B, sin `numVerts`). No hay
colores por vértice ni lightmaps en ningún archivo del juego. Con los datos reales:
5 291 / 5 344 válidas (47 vacías); las rechazadas son mallas de `models/mp/` truncadas o con
bytes de sobra. `render3d::parseStaticMesh` exige índices `< numVerts ≤ 65 535`, floats
finitos, nombres ASCII ≤ 256 B con NUL, y comprueba cada tamaño en 64 bits contra lo que
queda del archivo **antes** de reservar memoria; los bytes sobrantes invalidan la malla.

## `.skinnedmesh` (versión 3)

Secuencial, little-endian. Validado en **641/641** archivos (sin bytes sobrantes):

```
u32 version (= 3)
f32 bbox[6]                      mín, máx
u8  unk[3]                       el tercero vale 1–5 (sin identificar)
u32 numBones                     máximo real: 115
numBones × { u32 n; u16 hijo[n] }                        jerarquía
numBones × { f32 bbox[6]; u8 a; u8 b; i8 padre; f32 w; u32 u }   35 B por hueso
u32 (= 1)
u32 numPoints                    ≤ 12
numPoints × { f32 pos[3]; u32; u16 hueso; u32 id }       22 B, puntos de anclaje
u32 (= 1)
u32 numSurfaces
numSurfaces × {
    char material[]              terminado en NUL
    u16  idx[]                   lista de triángulos terminada en 0xFFFF
}
u32 numVerts                     (ausente si numSurfaces = 0: 18 archivos; otros 45 tienen 0)
numVerts × { f32 pos[3]; f32 uv[2]; f32 normal[3]; u16 hueso }   34 B por vértice
```

- `padre` < índice del hueso en los 641 archivos (orden topológico) o `-1` en la raíz, y las listas
  de hijos coinciden siempre con los padres.
- **Skinning rígido**: un único hueso por vértice, sin pesos.
- **Los vértices están en el espacio local de su hueso**: `p = G_hueso · v`, donde `G` es la
  transformación global de la pose (no hay matriz inversa de bind). Comprobado transformando las
  mallas con sus `.anim`: con esta fórmula el modelo queda unido; con los vértices en bruto, roto.
- Materiales: mismas rutas que `.staticmesh` (`materials/…/surf_…`), resueltas con `.kmat`.
- Escala: los modelos de arma son enormes (el arco mide ~2 900 u); el juego los escala al dibujarlos.

## `.anim` (versión 2)

```
u32 version (= 2)
u32 numClips
numClips × {
    u32 id                       ANIM_* de scripts/animations.txt (p. ej. 20100 = ANIM_WPN_IDLE)
    u32 numFrames
    u32 numBones, numBones2      siempre iguales
    u32 numTransTracks, numRotTracks
    u32 numEvents
    f32 ?                        2–3 (sin identificar)
    u32 flags                    sin identificar
    u16 ?                        0 en 1 855 de 1 865 clips
    f32 fps                      15 (1 570 clips), 30, 20, 10 o 5
    u16 ?
    numBones × { u16 pistaTraslación; u16 pistaRotación }  0xFFFF = sin pista
    numBones × { f32 t[3]; f32 q[4] }                     pose fija (28 B) para huesos sin pista
    numFrames × u32              casi siempre 0 (sin identificar)
    numTransTracks × numFrames × f32[3]
    numRotTracks   × numFrames × f32[4]                  cuaternión x, y, z, w
    numEvents × 50 B             eventos (disparo, partículas…; sin interpretar)
}
```

Validado en **479/479** archivos y 1 865 clips (sin bytes sobrantes); los índices de pista de cada clip
cubren exactamente `0..numTracks-1`. La transformación local de un hueso es `T(t) · R(q)` y la global
`G = G_padre · local`. La pose fija **cambia por clip**: no es una pose de bind.

## `.kmat`

Texto con bloques `material <nombre> { … }`. La superficie de una malla se resuelve así:

```
materials/harbor/mat_harbor2185/surf_2185_00_00
└─────────── archivo ──────────┘└── material ──┘
→ materials/harbor/mat_harbor2185.kmat, bloque "surf_2185_00_00"
```

| Directiva | Valores observados | Uso en el port |
|---|---|---|
| `sampler "diffuse" 0 "<png>" { filter …; wrap … }` | 12 856 (solo `"diffuse"`) | Textura; `wrap repeat\|mirrored\|clamp`, `filter linear\|linear_anisotropic\|nearest` |
| `cull` | `back` (12 031), `none` (1) | Culling por material |
| `sort` | `default`, `masked`, `translucent`, `water` | Orden de dibujo |
| `blend`, `nodepthmask` | 548, 540 | Pasada translúcida sin escritura de profundidad |
| `alphatest` + `alphafunc gequal` + `alphamask <umbral>` | 480 | `discard` si alfa < umbral (0,5 por defecto) |
| `fullbright` | 876 | Sin iluminación |
| `color_diffuse "r g b a"` | 12 855 | Multiplica la textura |
| `nodraw` | 1 | No se dibuja |
| `param`, `surface_type`, `depthtest`, `color_environment`, `reflectivePower` | — | Se reconocen y se ignoran |

Los 3 926 archivos se parsean sin errores ni directivas desconocidas (12 856 materiales).
Las rutas de textura pasan por `vfs::normalizePath` (se rechazan `..` y rutas absolutas).

## Reproducir la validación

```bash
cmake -S tests/native -B build-host && cmake --build build-host -j
TURK2_KPF="/ruta/absoluta/game.kpf" ./build-host/render3d_tests
```

Lee el `.kpf` en memoria (nada se extrae) y solo imprime recuentos.

## Pendiente de análisis

Los campos marcados "sin identificar" de `.skinnedmesh`, `.anim` (incluidos los eventos) y de la
cabecera de actor, y los flags de sector y de área. Cada parser nuevo sigue el mismo patrón:
- núcleo independiente de GL en su propio módulo;
- límites comprobados en cada offset y tamaño;
- tests host con datos sintéticos en `tests/native/`.
