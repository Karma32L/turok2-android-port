# miniz (vendored)

- Versión: **3.0.2** (`MZ_VERSION "11.0.2"`), licencia MIT (ver `LICENSE`).
- Origen: release oficial amalgamada
  `https://github.com/richgel999/miniz/releases/download/3.0.2/miniz-3.0.2.zip`
- SHA-256 del zip descargado: `ada38db0b703a56d3dd6d57bf84a9c5d664921d870d8fea4db153979fb5332c5`
- Archivos copiados sin modificar: `miniz.c`, `miniz.h`, `LICENSE`.

| Archivo   | SHA-256 |
|-----------|---------|
| `miniz.c` | `0fcdc9888cb3a29ca8f176bac087e5fe6c7258a6ab06b1c271c1e109a11d3740` |
| `miniz.h` | `295d1a0041aea09609598c0f1f35c1977ca05ad662acbadcfdaac44c140af37b` |
| `LICENSE` | `0115478d567121238cf6cc1c0c361926cf07a49d9e4c9e66da97fac6a01646b3` |

Configuración de compilación (`CMakeLists.txt`): sin API de escritura de ZIP, sin stdio,
sin tiempo y sin nombres compatibles con zlib (evita colisiones con la zlib del sistema).
La API de deflate se mantiene solo porque los tests la usan para generar `.kpf` sintéticos.

Para actualizar: descargar la nueva release en un directorio aislado, verificar, copiar los
tres archivos y actualizar esta tabla.
