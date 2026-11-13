# Sistema de archivos virtual (VFS)

Código: `src/vfs/` (núcleo sin dependencias de plataforma + `vfs_sdl.cpp` para Android),
`src/loc/` (parser de localización). Tests: `tests/native/`.

## Capas y prioridad

`<ext>` = `SDL_AndroidGetExternalStoragePath()` = `/sdcard/Android/data/com.turok2port.app/files`.

| Prioridad | Capa | Origen |
|---|---|---|
| 1 (base) | `game.kpf` | `game_data/game.kpf` vía `SDL_RWFromFile` (almacenamiento interno o assets del APK). Si no existe: `<ext>/game_data/game.kpf` (lo que instala `prepare_assets.py install --mode adb`). |
| 2 (mods) | `*.kpf`, `*.zip` | `<ext>/mods/`, montados en orden alfabético de bytes: `b.zip` sobrescribe a `a.kpf`. |
| 3 (overrides) | archivos sueltos | `<ext>/overrides/`, con las mismas rutas que dentro del `.kpf`. |

Las rutas son insensibles a mayúsculas (el juego original es de Windows) y aceptan `/` o `\`.

Ejemplo: para sustituir la traducción sin tocar `game.kpf`:

```bash
adb push loc_spanish.txt /sdcard/Android/data/com.turok2port.app/files/overrides/localization/loc_spanish.txt
```

## Modelo de amenazas y defensas

Todo lo que viene de `.kpf`/`.zip` y de `<ext>` (modificable por el usuario vía MTP/adb) es
**no confiable**.

| Riesgo | Defensa |
|---|---|
| Zip-slip / path traversal | `normalizePath` rechaza `..`, rutas absolutas, `:` (unidades, ADS), NUL y caracteres de control. Las entradas rechazadas no son alcanzables. El VFS nunca escribe a disco. |
| Nombres truncados o con NUL incrustado | Se lee el nombre con su longitud real (`mz_zip_reader_get_filename`), no `m_filename`/`strlen`. |
| Symlinks en el ZIP | Rechazados por `external_attr`. |
| Symlinks en `<ext>` | Toda apertura usa `openat` + `O_NOFOLLOW` componente a componente desde el fd de la raíz (sin TOCTOU). FIFOs/dispositivos rechazados (`O_NONBLOCK` + `fstat`). |
| Bomba ZIP | Antes de reservar: tamaño declarado ≤ `maxFileSize` (256 MiB) y ratio ≤ 200:1 para archivos > 1 MiB. miniz descomprime en un buffer del tamaño declarado y falla si el contenido real difiere. |
| Cabeceras incoherentes | Se rechaza `comp_size == 0` con tamaño > 0 (miniz devolvería éxito sin escribir), stored con tamaños distintos, cifrado y métodos distintos de stored/deflate. |
| Corrupción | CRC-32 verificada en cada lectura; EOCD/directorio central inválido → la capa no se monta. |
| Agotamiento | Límites de entradas por capa, profundidad de directorios, número de mods y longitud de ruta (`vfs::Limits`). |

El `.kpf` nunca se carga entero en memoria: miniz lee el directorio central y cada entrada
bajo demanda a través de `ByteSource::readAt`.

## Localización (`loc_<idioma>.txt`)

Gramática observada en el archivo del juego (UTF-8, CRLF):

```
"texto"                 → cadena indexada (índice = orden de aparición)
clave = "texto"         → cadena con clave
// comentario           → ignorado
```

Escapes: `\n`, `\t`, `\"`, `\\`. Las líneas mal formadas o con UTF-8 inválido se cuentan y se
omiten. Claves duplicadas: gana la primera.

## Diagnóstico al arrancar

`main.cpp` monta el VFS, carga `localization/loc_spanish.txt` y escribe en logcat las capas,
el número de archivos y las primeras cadenas. El fondo es verde si todo fue bien y rojo si no.

```bash
adb logcat -s turok2:*
```

## Tests en el host

```bash
cmake -S tests/native -B build-host
cmake --build build-host -j
ctest --test-dir build-host --output-on-failure
```

Compilan con ASan + UBSan (`-DTURK2_SANITIZE=OFF` para desactivarlo) y usan solo `.kpf`
sintéticos generados en memoria por `tests/native/zip_builder.hpp`.
