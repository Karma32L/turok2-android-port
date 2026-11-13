# tools/

Utilidades que se ejecutan en el host (PC).

## `prepare_assets.py`: empaquetador e inspector de assets

Prepara los datos de **tu copia legítima** de *Turok 2: Seeds of Evil* (Steam, appid 405830) para el port.
Solo usa la librería estándar de Python (3.11+) y nunca invoca un shell local.

**Qué copia:**
- `game.kpf`: ZIP con todo el juego (texturas, modelos, sonidos, niveles, `.cfg` y localización, incluido `localization/loc_spanish.txt`). Se copia íntegro, sin extraerlo.
- `music/n64/*.ogg`: la banda sonora N64.
- `manifest.json`: tamaño y SHA-256 de cada archivo, más el buildid. Sirve para verificar la copia y para hacer copias incrementales.

### Uso
```bash
# 1. Localizar la instalación (rutas de Steam nativo/Flatpak + libraryfolders.vdf)
python3 tools/prepare_assets.py detect
python3 tools/prepare_assets.py detect --steam-dir "/ruta/a/Turok 2 - Seeds of Evil"

# 2. Verificar integridad (CRC de cada entrada, SHA-256, rutas peligrosas) e idioma
python3 tools/prepare_assets.py inspect --lang spanish

# 3a. RECOMENDADO: enviar al dispositivo vía adb (el APK queda limpio)
#     Instala antes el APK para que exista el directorio de la app.
python3 tools/prepare_assets.py install --mode adb --dry-run   # muestra los comandos
python3 tools/prepare_assets.py install --mode adb [--serial SERIAL] [--adb /ruta/adb]
#     Destino: /sdcard/Android/data/com.turok2port.app/files/game_data/

# 3b. Build local personal de un clic: copia a android/app/src/main/assets/game_data/
python3 tools/prepare_assets.py install --mode local [--dry-run]

# Extra: inventario de formatos (carpetas, extensiones, magic, cabeceras PNG).
#        Solo lectura: lee como mucho 33 bytes por entrada y no escribe nada a disco.
python3 tools/prepare_assets.py formats

# Extra: extraer partes del .kpf para inspeccionarlas (fuera del repo)
python3 tools/prepare_assets.py extract --pattern 'localization/*' --out ~/turok2-inspect
```

**Códigos de salida:** `0` OK · `1` error de verificación · `2` juego o adb no encontrado.

### Seguridad
- **Zip-slip / path traversal:** rechaza entradas con `..`, rutas absolutas, `\`, letras de unidad, bytes nulos y symlinks. Además, cada destino se resuelve y se comprueba que queda dentro del directorio objetivo.
- **Symlinks en el origen:** dentro de `music/n64` se rechazan.
- **`installdir` del `.acf`:** debe ser un único componente de ruta, contenido en `steamapps/common`.
- **adb:**
  - se ejecuta con `shell=False`;
  - el paquete y el serial se validan con regex;
  - las rutas que llegan a `adb shell` (shell remoto) van escapadas con `shlex.quote`;
  - si no indicas `--serial`, exige exactamente un dispositivo conectado.
- **`extract`:**
  - limita el tamaño por entrada (anti zip-bomb);
  - se niega a escribir dentro del repo, salvo en `game_data/`.

### Reglas legales
- Los datos y todo lo que se genere a partir de ellos (`.kpf`, `.ogg`, extracciones, APKs con datos) **nunca** se suben a Git, Releases ni a ningún otro canal.
- `assets/game_data/` está en `.gitignore` y solo sirve para builds personales.

### Pruebas
```bash
python3 -m unittest discover -s tests -v   # datos 100% sintéticos
```
