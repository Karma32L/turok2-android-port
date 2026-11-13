#!/usr/bin/env python3
"""Empaquetador e inspector de assets de Turok 2 para el port de Android.

Toma los datos de la copia legítima del usuario (instalación de Steam) y los prepara para:
  - builds locales personales: android/app/src/main/assets/game_data/ (ignorado por Git), o
  - el almacenamiento externo de la app en un dispositivo vía adb (recomendado).

Solo usa la librería estándar. Nunca invoca un shell local (subprocess con shell=False).
"""

from __future__ import annotations

import argparse
import fnmatch
import hashlib
import json
import os
import re
import shlex
import shutil
import stat
import subprocess
import sys
import tempfile
import zipfile
import zlib
from dataclasses import dataclass, field
from pathlib import Path, PurePosixPath

APPID = 405830
KPF_NAME = "game.kpf"
EXTRA_DIRS = ("music/n64",)
MANIFEST_NAME = "manifest.json"
PACKAGE = "com.turok2port.app"

REPO_ROOT = Path(__file__).resolve().parent.parent
LOCAL_DEST = REPO_ROOT / "android/app/src/main/assets/game_data"

STEAM_CANDIDATES = (
    ".local/share/Steam",
    ".steam/steam",
    ".steam/root",
    ".var/app/com.valvesoftware.Steam/.local/share/Steam",
    ".var/app/com.valvesoftware.Steam/data/Steam",
)

CHUNK = 1 << 20  # 1 MiB
MAX_MEMBER_SIZE = 512 << 20  # límite por entrada al extraer (anti zip-bomb)

EXIT_OK, EXIT_VERIFY, EXIT_NOT_FOUND = 0, 1, 2

_PACKAGE_RE = re.compile(r"^[A-Za-z][A-Za-z0-9_]*(\.[A-Za-z][A-Za-z0-9_]*)+$")
_SERIAL_RE = re.compile(r"^[A-Za-z0-9._:\-]+$")
_VDF_PATH_RE = re.compile(r'^\s*"path"\s+"((?:[^"\\]|\\.)*)"', re.MULTILINE)
_LOC_RE = re.compile(r"^localization/loc_([a-z]+)\.txt$", re.IGNORECASE)
_DRIVE_RE = re.compile(r"^[A-Za-z]:")


class AssetError(Exception):
    """Error controlado; `code` es el código de salida del proceso."""

    def __init__(self, message: str, code: int = EXIT_VERIFY) -> None:
        super().__init__(message)
        self.code = code


@dataclass(frozen=True)
class GameInstall:
    path: Path
    buildid: str | None


@dataclass(frozen=True)
class AssetFile:
    rel: str  # ruta relativa POSIX dentro de game_data/
    src: Path


@dataclass
class KpfReport:
    path: Path
    size: int
    entries: int
    sha256: str | None
    bad_member: str | None  # primera entrada con CRC/stream corrupto
    unsafe: list[str] = field(default_factory=list)
    languages: list[str] = field(default_factory=list)
    top_level: dict[str, int] = field(default_factory=dict)


# --------------------------------------------------------------------------- utilidades


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        while chunk := fh.read(CHUNK):
            h.update(chunk)
    return h.hexdigest()


def is_safe_member(member: zipfile.ZipInfo | str) -> bool:
    """True si la ruta relativa no puede escapar del directorio destino (zip-slip)."""
    name = member.filename if isinstance(member, zipfile.ZipInfo) else member
    if not name or "\x00" in name or "\\" in name:
        return False
    if name.startswith("/") or _DRIVE_RE.match(name):
        return False
    if any(part == ".." for part in PurePosixPath(name).parts):
        return False
    if isinstance(member, zipfile.ZipInfo) and stat.S_ISLNK(member.external_attr >> 16):
        return False
    return True


def safe_target(base: Path, rel: str) -> Path:
    """Resuelve base/rel y garantiza que el resultado queda estrictamente dentro de base."""
    if not is_safe_member(rel):
        raise AssetError(f"Ruta rechazada (posible path traversal): {rel!r}")
    base_r = base.resolve()
    target = (base_r / rel).resolve()
    if target == base_r or not target.is_relative_to(base_r):
        raise AssetError(f"Ruta fuera del destino rechazada: {rel!r}")
    return target


def copy_atomic(src: Path, dst: Path) -> None:
    """Copia a un temporal en el mismo directorio y renombra: nunca deja archivos a medias."""
    fd, tmp = tempfile.mkstemp(dir=dst.parent, prefix=".tmp-")
    os.close(fd)
    try:
        shutil.copyfile(src, tmp)
        os.replace(tmp, dst)
    except BaseException:
        Path(tmp).unlink(missing_ok=True)
        raise


def _human(size: int) -> str:
    value = float(size)
    for unit in ("B", "KiB", "MiB", "GiB"):
        if value < 1024 or unit == "GiB":
            return f"{value:.1f} {unit}"
        value /= 1024
    return f"{size} B"


# --------------------------------------------------------------------------- detección Steam


def find_steam_roots(home: Path | None = None) -> list[Path]:
    home = home or Path.home()
    roots: list[Path] = []
    for rel in STEAM_CANDIDATES:
        candidate = home / rel
        if candidate.is_dir():
            resolved = candidate.resolve()
            if resolved not in roots:
                roots.append(resolved)
    return roots


def parse_library_paths(vdf_text: str) -> list[Path]:
    """Extrae los "path" de libraryfolders.vdf (no hace falta un parser VDF completo)."""
    return [Path(m.replace("\\\\", "\\")) for m in _VDF_PATH_RE.findall(vdf_text)]


def _acf_value(text: str, key: str) -> str | None:
    m = re.search(rf'^\s*"{re.escape(key)}"\s+"([^"]*)"', text, re.MULTILINE | re.IGNORECASE)
    return m.group(1) if m else None


def library_dirs(roots: list[Path]) -> list[Path]:
    libs: list[Path] = []
    for root in roots:
        candidates = [root]
        for vdf in (root / "steamapps/libraryfolders.vdf", root / "config/libraryfolders.vdf"):
            if vdf.is_file():
                candidates += parse_library_paths(vdf.read_text(encoding="utf-8", errors="replace"))
        for lib in candidates:
            if lib.is_dir() and (resolved := lib.resolve()) not in libs:
                libs.append(resolved)
    return libs


def _is_single_component(name: str) -> bool:
    return name not in ("", ".", "..") and not any(c in name for c in ("/", "\\", "\x00"))


def find_game_dir(roots: list[Path], appid: int = APPID) -> GameInstall:
    for lib in library_dirs(roots):
        acf = lib / "steamapps" / f"appmanifest_{appid}.acf"
        if not acf.is_file():
            continue
        text = acf.read_text(encoding="utf-8", errors="replace")
        installdir = _acf_value(text, "installdir")
        if installdir is None or not _is_single_component(installdir):
            raise AssetError(f"'installdir' inválido en {acf}: {installdir!r}")
        common = (lib / "steamapps/common").resolve()
        game = (common / installdir).resolve()
        if not game.is_relative_to(common):
            raise AssetError(f"La instalación apunta fuera de {common}: {game}")
        if (game / KPF_NAME).is_file():
            return GameInstall(game, _acf_value(text, "buildid"))
    raise AssetError(
        f"No se encontró Turok 2 (appid {appid}) en las bibliotecas de Steam. Usa --steam-dir.",
        EXIT_NOT_FOUND,
    )


def locate_game(steam_dir: Path | None) -> GameInstall:
    if steam_dir is not None:
        steam_dir = steam_dir.expanduser().resolve()
        if (steam_dir / KPF_NAME).is_file():  # apunta directamente a la carpeta del juego
            return GameInstall(steam_dir, None)
        return find_game_dir([steam_dir])
    roots = find_steam_roots()
    if not roots:
        raise AssetError("No se encontró ninguna instalación de Steam. Usa --steam-dir.", EXIT_NOT_FOUND)
    return find_game_dir(roots)


# --------------------------------------------------------------------------- inspección .kpf


def inspect_kpf(path: Path, with_hash: bool = True) -> KpfReport:
    if not zipfile.is_zipfile(path):
        raise AssetError(f"{path} no es un .kpf válido (no es un ZIP)")
    with zipfile.ZipFile(path) as z:
        infos = z.infolist()
        try:
            bad = z.testzip()  # verifica la CRC de cada entrada
        except (zipfile.BadZipFile, zlib.error, EOFError) as exc:
            bad = f"<stream corrupto: {exc}>"
    report = KpfReport(
        path=path,
        size=path.stat().st_size,
        entries=len(infos),
        sha256=sha256_file(path) if with_hash else None,
        bad_member=bad,
    )
    for info in infos:
        if not is_safe_member(info):
            report.unsafe.append(info.filename)
        top = info.filename.split("/", 1)[0]
        report.top_level[top] = report.top_level.get(top, 0) + 1
        if m := _LOC_RE.match(info.filename):
            report.languages.append(m.group(1).lower())
    report.languages.sort()
    return report


PNG_SIG = b"\x89PNG\r\n\x1a\n"
MAGIC_LEN = 16  # bytes de cabecera mostrados por extensión
_PNG_HEAD = 33  # firma (8) + chunk IHDR completo (8 + 13 + 4)


@dataclass
class ExtStats:
    count: int = 0
    size: int = 0
    magic: bytes = b""  # primeros MAGIC_LEN bytes de la primera entrada no vacía
    sample: str = ""


@dataclass
class FormatReport:
    entries: int = 0
    by_top: dict[str, int] = field(default_factory=dict)
    by_ext: dict[str, ExtStats] = field(default_factory=dict)
    # (bit depth, color type, interlace) -> nº de PNG; ("invalid",) para cabeceras rotas
    png: dict[tuple, int] = field(default_factory=dict)


def parse_png_header(head: bytes) -> tuple[int, int, int, int, int] | None:
    """(ancho, alto, bit depth, color type, interlace) o None si la cabecera no es válida."""
    if len(head) < _PNG_HEAD or not head.startswith(PNG_SIG):
        return None
    if head[8:16] != b"\x00\x00\x00\x0dIHDR":  # IHDR debe ser el primer chunk y medir 13
        return None
    w, h = int.from_bytes(head[16:20], "big"), int.from_bytes(head[20:24], "big")
    return w, h, head[24], head[25], head[28]


def survey_formats(path: Path) -> FormatReport:
    """Inventario de solo lectura: lee como mucho _PNG_HEAD bytes por entrada, nada a disco."""
    if not zipfile.is_zipfile(path):
        raise AssetError(f"{path} no es un .kpf válido (no es un ZIP)")
    rep = FormatReport()
    with zipfile.ZipFile(path) as z:
        for info in z.infolist():
            if info.is_dir():
                continue
            rep.entries += 1
            name = info.filename.lower()
            top = name.split("/", 1)[0] if "/" in name else "(raíz)"
            ext = PurePosixPath(name).suffix or "(sin ext)"
            rep.by_top[top] = rep.by_top.get(top, 0) + 1
            st = rep.by_ext.setdefault(ext, ExtStats())
            st.count += 1
            st.size += info.file_size
            need_magic = not st.magic and info.file_size > 0
            if not need_magic and ext != ".png":
                continue
            try:
                with z.open(info) as fh:
                    head = fh.read(_PNG_HEAD)
            except (zipfile.BadZipFile, zlib.error, EOFError, NotImplementedError) as exc:
                raise AssetError(f"No se pudo leer {info.filename!r}: {exc}") from exc
            if need_magic:
                st.magic, st.sample = head[:MAGIC_LEN], info.filename
            if ext == ".png":
                hdr = parse_png_header(head)
                key = hdr[2:] if hdr else ("invalid",)
                rep.png[key] = rep.png.get(key, 0) + 1
    return rep


def _printable(b: bytes) -> str:
    return "".join(chr(c) if 32 <= c < 127 else "." for c in b)


def _print_formats(r: FormatReport) -> int:
    print(f"Archivos: {r.entries} (sin contar entradas de directorio)")
    print("\nPor carpeta principal:")
    for k, v in sorted(r.by_top.items(), key=lambda kv: (-kv[1], kv[0])):
        print(f"  {v:7}  {k}")
    print("\nPor extensión (nº, tamaño, magic de la primera entrada):")
    for k, s in sorted(r.by_ext.items(), key=lambda kv: (-kv[1].count, kv[0])):
        print(f"  {s.count:7}  {k:14} {_human(s.size):>10}  {s.magic.hex(' '):47}  {_printable(s.magic)}")
    if r.png:
        print("\nPNG (bit depth, color type, interlace):")
        for k, v in sorted(r.png.items(), key=lambda kv: -kv[1]):
            print(f"  {v:7}  {k}")
    return EXIT_OK


def ensure_kpf_ok(report: KpfReport) -> None:
    if report.bad_member:
        raise AssetError(f"{report.path.name}: entrada corrupta {report.bad_member!r}")
    if report.unsafe:
        raise AssetError(f"{report.path.name}: {len(report.unsafe)} rutas peligrosas, p.ej. {report.unsafe[0]!r}")


# --------------------------------------------------------------------------- recolección y manifiesto


def collect_assets(game_dir: Path) -> list[AssetFile]:
    files = [AssetFile(KPF_NAME, game_dir / KPF_NAME)]
    for rel_dir in EXTRA_DIRS:
        base = game_dir / rel_dir
        if base.is_symlink():
            raise AssetError(f"Symlink rechazado: {base}")
        if not base.is_dir():
            print(f"[aviso] No existe {base}, se omite.", file=sys.stderr)
            continue
        for p in sorted(base.rglob("*")):
            if p.is_symlink():
                raise AssetError(f"Symlink rechazado: {p}")
            if p.is_file():
                rel = p.relative_to(game_dir).as_posix()
                if not is_safe_member(rel):
                    raise AssetError(f"Nombre de archivo rechazado: {rel!r}")
                files.append(AssetFile(rel, p))
    return files


def build_manifest(files: list[AssetFile], buildid: str | None) -> dict:
    return {
        "appid": APPID,
        "buildid": buildid,
        "files": {f.rel: {"size": f.src.stat().st_size, "sha256": sha256_file(f.src)} for f in files},
    }


def load_manifest(dest: Path) -> dict:
    try:
        data = json.loads((dest / MANIFEST_NAME).read_text(encoding="utf-8"))
        return data if isinstance(data, dict) and isinstance(data.get("files"), dict) else {"files": {}}
    except (OSError, ValueError):
        return {"files": {}}


def write_manifest(path: Path, manifest: dict) -> None:
    fd, tmp = tempfile.mkstemp(dir=path.parent, prefix=".tmp-")
    with os.fdopen(fd, "w", encoding="utf-8") as fh:
        json.dump(manifest, fh, indent=2, sort_keys=True)
    os.replace(tmp, path)


# --------------------------------------------------------------------------- modo local


def install_local(files: list[AssetFile], dest: Path, buildid: str | None, dry_run: bool = False) -> int:
    old = load_manifest(dest)["files"]
    manifest = build_manifest(files, buildid)
    copied = skipped = 0
    for f in files:
        entry = manifest["files"][f.rel]
        target = safe_target(dest, f.rel)
        prev = old.get(f.rel)
        if prev == entry and target.is_file() and target.stat().st_size == entry["size"]:
            skipped += 1
            continue
        print(f"{'[dry-run] ' if dry_run else ''}copiar {f.rel} ({_human(entry['size'])})")
        if dry_run:
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        copy_atomic(f.src, target)
        if sha256_file(target) != entry["sha256"]:
            raise AssetError(f"SHA-256 no coincide tras copiar {f.rel}")
        copied += 1
    if not dry_run:
        dest.mkdir(parents=True, exist_ok=True)
        write_manifest(dest / MANIFEST_NAME, manifest)
    print(f"Local: {copied} copiados, {skipped} sin cambios → {dest}")
    print("AVISO: un APK compilado con estos datos es SOLO para uso personal. No lo distribuyas.")
    return EXIT_OK


# --------------------------------------------------------------------------- modo adb


def remote_base(package: str = PACKAGE) -> str:
    if not _PACKAGE_RE.match(package):
        raise AssetError(f"Nombre de paquete inválido: {package!r}")
    return f"/sdcard/Android/data/{package}/files/game_data"


def adb_prefix(adb: str, serial: str | None) -> list[str]:
    cmd = [adb]
    if serial is not None:
        if not _SERIAL_RE.match(serial):
            raise AssetError(f"Serial de dispositivo inválido: {serial!r}")
        cmd += ["-s", serial]
    return cmd


def adb_plan(prefix: list[str], files: list[AssetFile], remote: str) -> list[list[str]]:
    """Comandos adb a ejecutar. `adb shell` concatena argumentos en un shell REMOTO:
    toda ruta que llega a él va escapada con shlex.quote."""
    dirs = sorted({str(PurePosixPath(remote, f.rel).parent) for f in files})
    plan = [[*prefix, "shell", "mkdir", "-p", *(shlex.quote(d) for d in dirs)]]
    for f in files:
        if not is_safe_member(f.rel):
            raise AssetError(f"Ruta remota rechazada: {f.rel!r}")
        plan.append([*prefix, "push", str(f.src), f"{remote}/{f.rel}"])  # push no pasa por shell
    return plan


def find_adb(explicit: str | None) -> str:
    if explicit:
        p = Path(explicit).expanduser()
        if p.is_file() and os.access(p, os.X_OK):
            return str(p)
        raise AssetError(f"adb no ejecutable: {p}", EXIT_NOT_FOUND)
    candidates = [Path(v) / "platform-tools/adb" for k in ("ANDROID_HOME", "ANDROID_SDK_ROOT") if (v := os.getenv(k))]
    candidates.append(Path.home() / "Android/Sdk/platform-tools/adb")
    if which := shutil.which("adb"):
        candidates.append(Path(which))
    for c in candidates:
        if c.is_file() and os.access(c, os.X_OK):
            return str(c)
    raise AssetError("No se encontró adb. Usa --adb o define ANDROID_HOME.", EXIT_NOT_FOUND)


def _run(cmd: list[str], timeout: float | None = 60) -> str:
    res = subprocess.run(cmd, shell=False, capture_output=True, text=True, timeout=timeout, check=False)
    if res.returncode != 0:
        raise AssetError(f"Falló `{shlex.join(cmd)}`: {res.stderr.strip() or res.stdout.strip()}")
    return res.stdout


def check_single_device(adb: str, serial: str | None) -> None:
    if serial is not None:
        if _run([*adb_prefix(adb, serial), "get-state"]).strip() != "device":
            raise AssetError(f"El dispositivo {serial} no está listo.")
        return
    lines = _run([adb, "devices"]).splitlines()[1:]
    ready = [ln.split()[0] for ln in lines if ln.strip().endswith("\tdevice")]
    if len(ready) != 1:
        raise AssetError(f"Se necesita exactamente 1 dispositivo conectado (hay {len(ready)}). Usa --serial.")


def install_adb(
    files: list[AssetFile], buildid: str | None, adb: str, serial: str | None, dry_run: bool = False
) -> int:
    prefix = adb_prefix(adb, serial)
    remote = remote_base()
    plan = adb_plan(prefix, files, remote)
    if dry_run:
        for cmd in plan:
            print(f"[dry-run] {shlex.join(cmd)}")
        return EXIT_OK
    check_single_device(adb, serial)
    manifest = build_manifest(files, buildid)
    for cmd in plan:
        print(f"→ {shlex.join(cmd[len(prefix):])}")
        _run(cmd, timeout=None)
    for f in files:  # verificación: tamaño remoto == tamaño local
        remote_file = shlex.quote(f"{remote}/{f.rel}")
        size = _run([*prefix, "shell", "stat", "-c", "%s", remote_file]).strip()
        if size != str(manifest["files"][f.rel]["size"]):
            raise AssetError(f"Tamaño remoto incorrecto para {f.rel}: {size}")
    with tempfile.TemporaryDirectory() as tmp:
        local_manifest = Path(tmp) / MANIFEST_NAME
        write_manifest(local_manifest, manifest)
        _run([*prefix, "push", str(local_manifest), f"{remote}/{MANIFEST_NAME}"])
    print(f"adb: {len(files)} archivos verificados en {remote}")
    return EXIT_OK


# --------------------------------------------------------------------------- extracción


def extract_members(kpf: Path, pattern: str, out: Path) -> list[Path]:
    out = out.expanduser().resolve()
    if out.is_relative_to(REPO_ROOT) and not out.is_relative_to(LOCAL_DEST.resolve()):
        raise AssetError(f"Extraer dentro del repo (fuera de game_data/) arriesga versionar datos: {out}")
    written: list[Path] = []
    with zipfile.ZipFile(kpf) as z:
        for info in z.infolist():
            if info.is_dir() or not fnmatch.fnmatchcase(info.filename, pattern):
                continue
            if info.file_size > MAX_MEMBER_SIZE:
                raise AssetError(f"Entrada demasiado grande: {info.filename} ({_human(info.file_size)})")
            target = safe_target(out, info.filename)  # valida también symlinks y '..'
            if not is_safe_member(info):
                raise AssetError(f"Entrada rechazada: {info.filename!r}")
            target.parent.mkdir(parents=True, exist_ok=True)
            with z.open(info) as src, target.open("wb") as dst:
                shutil.copyfileobj(src, dst, CHUNK)  # zipfile valida CRC y tamaño al terminar
            written.append(target)
    return written


# --------------------------------------------------------------------------- CLI


def _print_report(r: KpfReport, lang: str) -> int:
    print(f"Paquete:   {r.path}")
    print(f"Tamaño:    {_human(r.size)}  |  entradas: {r.entries}")
    if r.sha256:
        print(f"SHA-256:   {r.sha256}")
    print(f"CRC:       {'OK' if not r.bad_member else 'CORRUPTO → ' + r.bad_member}")
    print(f"Rutas peligrosas: {len(r.unsafe)}")
    top = ", ".join(f"{k}={v}" for k, v in sorted(r.top_level.items(), key=lambda kv: -kv[1])[:12])
    print(f"Contenido: {top}")
    print(f"Idiomas:   {', '.join(r.languages) or '(ninguno)'}")
    has_lang = lang.lower() in r.languages
    print(f"loc_{lang}.txt: {'presente' if has_lang else 'NO encontrado'}")
    return EXIT_OK if (not r.bad_member and not r.unsafe and has_lang) else EXIT_VERIFY


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description="Prepara los assets de Turok 2 (copia legítima del usuario) para el port.")
    sub = p.add_subparsers(dest="cmd", required=True)

    def with_steam(sp: argparse.ArgumentParser) -> argparse.ArgumentParser:
        sp.add_argument("--steam-dir", type=Path, help="Raíz de Steam/biblioteca o carpeta del juego")
        return sp

    with_steam(sub.add_parser("detect", help="Localiza la instalación"))
    sp = with_steam(sub.add_parser("inspect", help="Verifica integridad e idiomas del .kpf"))
    sp.add_argument("--lang", default="spanish")
    with_steam(sub.add_parser("formats", help="Inventario de formatos del .kpf (solo lectura)"))
    sp = with_steam(sub.add_parser("install", help="Copia los assets al proyecto o al dispositivo"))
    sp.add_argument("--mode", choices=("local", "adb"), required=True)
    sp.add_argument("--adb", help="Ruta a adb")
    sp.add_argument("--serial", help="Serial del dispositivo adb")
    sp.add_argument("--dry-run", action="store_true")
    sp = with_steam(sub.add_parser("extract", help="Extrae entradas del .kpf para inspección"))
    sp.add_argument("--pattern", required=True, help="Patrón glob, p.ej. 'localization/*'")
    sp.add_argument("--out", type=Path, required=True)
    return p


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        game = locate_game(args.steam_dir)
        kpf = game.path / KPF_NAME
        if args.cmd == "detect":
            print(f"Juego:   {game.path}\nBuildID: {game.buildid or '?'}\n{KPF_NAME}: {_human(kpf.stat().st_size)}")
            return EXIT_OK
        if args.cmd == "inspect":
            return _print_report(inspect_kpf(kpf), args.lang)
        if args.cmd == "formats":
            return _print_formats(survey_formats(kpf))
        if args.cmd == "extract":
            written = extract_members(kpf, args.pattern, args.out)
            print(f"Extraídos {len(written)} archivos en {args.out}")
            return EXIT_OK
        # install
        ensure_kpf_ok(inspect_kpf(kpf, with_hash=False))
        files = collect_assets(game.path)
        if args.mode == "local":
            return install_local(files, LOCAL_DEST, game.buildid, args.dry_run)
        try:
            adb = find_adb(args.adb)
        except AssetError:
            if not args.dry_run:
                raise
            adb = "adb"
        return install_adb(files, game.buildid, adb, args.serial, args.dry_run)
    except AssetError as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return exc.code
    except subprocess.TimeoutExpired as exc:
        print(f"Error: tiempo agotado en {shlex.join(map(str, exc.cmd))}", file=sys.stderr)
        return EXIT_VERIFY


if __name__ == "__main__":
    sys.exit(main())
