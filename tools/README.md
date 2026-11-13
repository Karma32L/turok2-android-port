# tools/

Utilidades que se ejecutan en el host (PC), por ejemplo extractores o convertidores de los datos del juego.

Reglas:
- Los datos de entrada los aporta el usuario desde su copia legítima. **Nunca** se versionan ni sus salidas (`.kpf`, `.exe`, `.dll`, `.z64`, ...).
- Todo extractor debe rechazar rutas con `..`, absolutas o symlinks, y comprobar que la ruta final queda dentro del directorio destino (zip-slip).
- Valida tamaños y offsets antes de leer.
