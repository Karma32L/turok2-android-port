"""Pruebas de tools/prepare_assets.py con datos 100% sintéticos (ningún asset real)."""

from __future__ import annotations

import stat
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import prepare_assets as pa  # noqa: E402


def make_kpf(path: Path, entries: dict[str, bytes], compression: int = zipfile.ZIP_DEFLATED) -> Path:
    with zipfile.ZipFile(path, "w", compression) as z:
        for name, data in entries.items():
            z.writestr(name, data)
    return path


def make_steam_tree(root: Path, installdir: str = "Turok 2 - Seeds of Evil") -> Path:
    steamapps = root / "steamapps"
    (steamapps / "common").mkdir(parents=True)
    (steamapps / "libraryfolders.vdf").write_text(f'"libraryfolders"\n{{\n\t"0"\n\t{{\n\t\t"path"\t\t"{root}"\n\t}}\n}}\n')
    (steamapps / f"appmanifest_{pa.APPID}.acf").write_text(
        f'"AppState"\n{{\n\t"appid"\t\t"{pa.APPID}"\n\t"installdir"\t\t"{installdir}"\n\t"buildid"\t\t"123"\n}}\n'
    )
    game = steamapps / "common" / installdir
    return game


class TmpCase(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = Path(self._tmp.name)

    def tearDown(self) -> None:
        self._tmp.cleanup()


class TestSafety(TmpCase):
    def test_rejects_traversal_names(self) -> None:
        for bad in ("../evil", "a/../../b", "/etc/passwd", "a\\..\\b", "C:/win", "x\x00y", ""):
            with self.subTest(bad=bad):
                self.assertFalse(pa.is_safe_member(bad))

    def test_accepts_normal_names(self) -> None:
        for ok in ("game.kpf", "music/n64/blind boss.ogg", "localization/loc_spanish.txt"):
            self.assertTrue(pa.is_safe_member(ok))

    def test_rejects_symlink_entry(self) -> None:
        info = zipfile.ZipInfo("link")
        info.external_attr = (stat.S_IFLNK | 0o777) << 16
        self.assertFalse(pa.is_safe_member(info))

    def test_safe_target_blocks_escape(self) -> None:
        with self.assertRaises(pa.AssetError):
            pa.safe_target(self.tmp, "../fuera")
        self.assertTrue(pa.safe_target(self.tmp, "a/b.txt").is_relative_to(self.tmp.resolve()))

    def test_safe_target_blocks_symlinked_dir(self) -> None:
        outside = self.tmp / "outside"
        outside.mkdir()
        base = self.tmp / "base"
        base.mkdir()
        (base / "link").symlink_to(outside)
        with self.assertRaises(pa.AssetError):
            pa.safe_target(base, "link/x.txt")


class TestSteamDetection(TmpCase):
    def test_parse_library_paths(self) -> None:
        vdf = '"libraryfolders"\n{\n\t"0"\n\t{\n\t\t"path"\t\t"/data/Steam"\n\t}\n\t"1"\n\t{\n\t\t"path"\t\t"/mnt/juegos"\n\t}\n}'
        self.assertEqual(pa.parse_library_paths(vdf), [Path("/data/Steam"), Path("/mnt/juegos")])

    def test_find_game_dir(self) -> None:
        game = make_steam_tree(self.tmp)
        game.mkdir()
        make_kpf(game / pa.KPF_NAME, {"a.txt": b"x"})
        found = pa.find_game_dir([self.tmp])
        self.assertEqual(found.path, game.resolve())
        self.assertEqual(found.buildid, "123")

    def test_malicious_installdir_rejected(self) -> None:
        make_steam_tree(self.tmp, installdir="../../etc")
        with self.assertRaises(pa.AssetError):
            pa.find_game_dir([self.tmp])

    def test_not_found_exit_code(self) -> None:
        (self.tmp / "steamapps").mkdir()
        with self.assertRaises(pa.AssetError) as ctx:
            pa.find_game_dir([self.tmp])
        self.assertEqual(ctx.exception.code, pa.EXIT_NOT_FOUND)


class TestInspect(TmpCase):
    def test_detects_spanish_and_ok_crc(self) -> None:
        kpf = make_kpf(
            self.tmp / "g.kpf",
            {"localization/loc_spanish.txt": "misión".encode(), "localization/loc_english.txt": b"mission"},
        )
        r = pa.inspect_kpf(kpf)
        self.assertIsNone(r.bad_member)
        self.assertEqual(r.languages, ["english", "spanish"])
        self.assertEqual(r.sha256, pa.sha256_file(kpf))

    def test_detects_corrupted_crc(self) -> None:
        marker = b"DATOS-SINTETICOS-" * 8
        kpf = make_kpf(self.tmp / "g.kpf", {"a.bin": marker}, zipfile.ZIP_STORED)
        raw = kpf.read_bytes()
        kpf.write_bytes(raw.replace(marker, marker[:-1] + b"!", 1))
        r = pa.inspect_kpf(kpf, with_hash=False)
        self.assertEqual(r.bad_member, "a.bin")
        with self.assertRaises(pa.AssetError):
            pa.ensure_kpf_ok(r)

    def test_flags_unsafe_entries(self) -> None:
        kpf = make_kpf(self.tmp / "g.kpf", {"../evil.txt": b"x", "ok.txt": b"y"})
        self.assertEqual(pa.inspect_kpf(kpf, with_hash=False).unsafe, ["../evil.txt"])


def synthetic_png_head(w: int, h: int, bd: int = 8, ct: int = 6, il: int = 0) -> bytes:
    ihdr = w.to_bytes(4, "big") + h.to_bytes(4, "big") + bytes([bd, ct, 0, 0, il])
    return pa.PNG_SIG + b"\x00\x00\x00\x0dIHDR" + ihdr + b"\x00\x00\x00\x00"


class TestFormats(TmpCase):
    def test_groups_by_folder_and_extension(self) -> None:
        kpf = make_kpf(
            self.tmp / "g.kpf",
            {
                "textures/a.png": synthetic_png_head(4, 2),
                "textures/b.PNG": synthetic_png_head(8, 8, ct=2),
                "fonts/f.kfont": b"texture \"fonts/f.png\"\n",
                "default.cfg": b"bind x y\n",
                "models/m.staticmesh": b"",
            },
        )
        r = pa.survey_formats(kpf)
        self.assertEqual(r.entries, 5)
        self.assertEqual(r.by_top, {"textures": 2, "fonts": 1, "(raíz)": 1, "models": 1})
        self.assertEqual(r.by_ext[".png"].count, 2)
        self.assertEqual(r.by_ext[".kfont"].magic, b'texture "fonts/f')
        self.assertEqual(r.by_ext[".staticmesh"].magic, b"")  # vacío: sin magic
        self.assertEqual(r.png, {(8, 6, 0): 1, (8, 2, 0): 1})

    def test_invalid_png_header_counted(self) -> None:
        kpf = make_kpf(self.tmp / "g.kpf", {"a.png": b"no soy un png", "b.png": pa.PNG_SIG})
        self.assertEqual(pa.survey_formats(kpf).png, {("invalid",): 2})

    def test_parse_png_header(self) -> None:
        self.assertEqual(pa.parse_png_header(synthetic_png_head(3, 5, il=1)), (3, 5, 8, 6, 1))
        bad_ihdr = bytearray(synthetic_png_head(3, 5))
        bad_ihdr[11] = 14  # longitud de IHDR distinta de 13
        self.assertIsNone(pa.parse_png_header(bytes(bad_ihdr)))

    def test_not_a_zip(self) -> None:
        p = self.tmp / "x.kpf"
        p.write_bytes(b"nada")
        with self.assertRaises(pa.AssetError):
            pa.survey_formats(p)


class TestExtract(TmpCase):
    def test_zip_slip_blocked(self) -> None:
        kpf = make_kpf(self.tmp / "g.kpf", {"../evil.txt": b"x"})
        out = self.tmp / "out"
        with self.assertRaises(pa.AssetError):
            pa.extract_members(kpf, "*", out)
        self.assertFalse((self.tmp / "evil.txt").exists())

    def test_extract_pattern(self) -> None:
        kpf = make_kpf(self.tmp / "g.kpf", {"localization/loc_spanish.txt": b"hola", "models/a.bin": b"x"})
        written = pa.extract_members(kpf, "localization/*", self.tmp / "out")
        self.assertEqual([p.name for p in written], ["loc_spanish.txt"])
        self.assertEqual(written[0].read_bytes(), b"hola")

    def test_refuses_output_inside_repo(self) -> None:
        kpf = make_kpf(self.tmp / "g.kpf", {"a.txt": b"x"})
        with self.assertRaises(pa.AssetError):
            pa.extract_members(kpf, "*", pa.REPO_ROOT / "src/datos")


class TestInstallLocal(TmpCase):
    def _game(self) -> Path:
        game = self.tmp / "game"
        (game / "music/n64").mkdir(parents=True)
        make_kpf(game / pa.KPF_NAME, {"a.txt": b"x"})
        (game / "music/n64/blind boss.ogg").write_bytes(b"OggS-sintetico")
        return game

    def test_copy_verify_and_incremental(self) -> None:
        files = pa.collect_assets(self._game())
        self.assertEqual([f.rel for f in files], ["game.kpf", "music/n64/blind boss.ogg"])
        dest = self.tmp / "dest"
        pa.install_local(files, dest, "123")
        self.assertTrue((dest / "music/n64/blind boss.ogg").is_file())
        self.assertIn("game.kpf", pa.load_manifest(dest)["files"])
        with mock.patch.object(pa, "copy_atomic") as copy:
            pa.install_local(files, dest, "123")  # segunda vez: nada que copiar
            copy.assert_not_called()

    def test_dry_run_writes_nothing(self) -> None:
        dest = self.tmp / "dest"
        pa.install_local(pa.collect_assets(self._game()), dest, None, dry_run=True)
        self.assertFalse(dest.exists())

    def test_symlink_in_source_rejected(self) -> None:
        game = self._game()
        (game / "music/n64/link.ogg").symlink_to("/etc/passwd")
        with self.assertRaises(pa.AssetError):
            pa.collect_assets(game)


class TestAdb(TmpCase):
    def test_plan_quotes_remote_shell_paths(self) -> None:
        files = [pa.AssetFile("music/n64 x/blind boss.ogg", self.tmp / "a.ogg")]
        plan = pa.adb_plan(["adb"], files, pa.remote_base())
        mkdir = plan[0]
        self.assertEqual(mkdir[:4], ["adb", "shell", "mkdir", "-p"])
        self.assertTrue(mkdir[4].startswith("'") and mkdir[4].endswith("n64 x'"))
        self.assertEqual(plan[1][-1], f"{pa.remote_base()}/music/n64 x/blind boss.ogg")

    def test_invalid_package_and_serial(self) -> None:
        with self.assertRaises(pa.AssetError):
            pa.remote_base("com.x; rm -rf /")
        with self.assertRaises(pa.AssetError):
            pa.adb_prefix("adb", "abc;reboot")

    def test_subprocess_never_uses_shell(self) -> None:
        src = self.tmp / "game.kpf"
        src.write_bytes(b"12345")
        files = [pa.AssetFile("game.kpf", src)]
        calls: list[tuple[list[str], dict]] = []

        def fake_run(cmd, **kw):
            calls.append((cmd, kw))
            out = "List of devices attached\nSERIAL1\tdevice\n" if cmd[-1] == "devices" else "5\n"
            return mock.Mock(returncode=0, stdout=out, stderr="")

        with mock.patch.object(pa.subprocess, "run", side_effect=fake_run):
            self.assertEqual(pa.install_adb(files, "1", "adb", None), pa.EXIT_OK)
        self.assertTrue(calls)
        self.assertTrue(all(kw.get("shell") is False for _, kw in calls))

    def test_requires_single_device(self) -> None:
        out = "List of devices attached\nA\tdevice\nB\tdevice\n"
        with mock.patch.object(pa.subprocess, "run", return_value=mock.Mock(returncode=0, stdout=out, stderr="")):
            with self.assertRaises(pa.AssetError):
                pa.check_single_device("adb", None)


if __name__ == "__main__":
    unittest.main()
