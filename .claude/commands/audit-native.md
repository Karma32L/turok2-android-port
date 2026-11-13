Actúa como auditor senior de seguridad de código nativo. Audita `src/`, `tools/` y `android/` (o `$ARGUMENTS` si se indica una ruta) sin modificar archivos.

Revisa:

1. **Seguridad de memoria en C/C++**
   - Desbordamientos de búfer: `memcpy`/`strcpy`/`sprintf`/`strcat`/`gets`, índices y offsets no validados.
   - Parseo de formatos binarios: tamaños/offsets leídos del fichero sin comprobar contra el tamaño real; desbordamiento de enteros en `count * size`; conversiones signed/unsigned.
   - Use-after-free, double-free, fugas, punteros colgantes, lectura de memoria no inicializada.
   - Uso de `new`/`malloc` crudo donde corresponde RAII (`std::unique_ptr`, `std::vector`).
   - Endianness y alineación en lecturas de structs empaquetados.
2. **Path traversal en extractores (`tools/` y cualquier código que escriba ficheros)**
   - Nombres de entrada de archivos empaquetados con `..`, rutas absolutas, `\`, bytes nulos o symlinks (zip-slip).
   - Verifica que la ruta final canonicalizada (`std::filesystem::weakly_canonical`) queda dentro del directorio destino.
   - En Python: nada de `shell=True`, `extractall` sin filtro (`filter="data"`), ni `os.path.join` con entrada no validada.
3. **AndroidManifest.xml**
   - Permisos solicitados: cada uno justificado; marca cualquier permiso peligroso o innecesario (almacenamiento amplio, red, etc.).
   - `android:exported` explícito; `allowBackup`, `debuggable`, `usesCleartextTraffic`, `requestLegacyExternalStorage`.
4. **Regla de assets**: confirma que no hay `.kpf`, `.exe`, `.dll`, `.z64` ni datos del juego versionados (`git ls-files`) ni empaquetados en el APK.
5. **Flags de compilación**: `-Wall -Wextra`, `-fstack-protector-strong`, `-D_FORTIFY_SOURCE=2` en release, sin símbolos de depuración en release.

Entrega un reporte breve agrupado por severidad (Crítica / Alta / Media / Baja) con `archivo:línea`, causa raíz y mitigación propuesta.
