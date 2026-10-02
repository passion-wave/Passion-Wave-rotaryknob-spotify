"""Build-time gzip regressions; these do not claim ESP transport performance."""
import gzip
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zlib

from tools import pw_web_pack

ROOT = Path(__file__).resolve().parents[2]
WEB = ROOT / "firmware/web"


class WebPackTests(unittest.TestCase):
    def test_real_assets_round_trip_and_budget(self):
        total = 0
        for name in pw_web_pack.ASSETS:
            with self.subTest(asset=name):
                source = (WEB / name).read_bytes()
                data = pw_web_pack.encode(source)
                # Both gzip and independent zlib gzip framing must consume one
                # complete stream with no extra NUL or trailing garbage.
                self.assertEqual(gzip.decompress(data), source)
                decoder = zlib.decompressobj(wbits=31)
                self.assertEqual(decoder.decompress(data) + decoder.flush(), source)
                self.assertTrue(decoder.eof)
                self.assertEqual(decoder.unused_data, b"")
                self.assertLess(len(data), len(source))
                total += len(data)
        self.assertLess(total, 32 * 1024, "Initial website transfer budget exceeded")

    def test_reproducible_headers_and_arbitrary_bytes(self):
        for data in (b"", b"\0" * 4096, bytes(range(256)), "Gerätewebsite öäü".encode()):
            with self.subTest(length=len(data)):
                first, second = pw_web_pack.encode(data), pw_web_pack.encode(data)
                self.assertEqual(first, second)
                self.assertEqual(first[:4], b"\x1f\x8b\x08\x00")
                self.assertEqual(first[4:8], b"\0" * 4)
                self.assertEqual(first[9], 255)  # OS independent, no Python 3.11/12 zlib OS byte.
                self.assertEqual(gzip.decompress(first), data)

    def test_cli_rebuild_reflects_source_and_ignores_source_metadata(self):
        with tempfile.TemporaryDirectory() as directory:
            source, output = Path(directory) / "source", Path(directory) / "build"
            source.mkdir()
            for name in pw_web_pack.ASSETS:
                (source / name).write_bytes((WEB / name).read_bytes())
            command = [sys.executable, str(ROOT / "tools/pw_web_pack.py"),
                       "--source-dir", str(source), "--output-dir", str(output)]
            subprocess.run(command, check=True)
            before = {name: (output / (name + ".gz")).read_bytes() for name in pw_web_pack.ASSETS}
            for name in pw_web_pack.ASSETS:
                os.utime(source / name, (1, 1))
            subprocess.run(command, check=True)
            for name in pw_web_pack.ASSETS:
                self.assertEqual((output / (name + ".gz")).read_bytes(), before[name])
            new_css = (source / "style.css").read_bytes() + b"\n/* build test */\n"
            (source / "style.css").write_bytes(new_css)
            subprocess.run(command, check=True)
            self.assertEqual(gzip.decompress((output / "style.css.gz").read_bytes()), new_css)
            self.assertEqual(sorted(p.name for p in output.iterdir()),
                             sorted(name + ".gz" for name in pw_web_pack.ASSETS))

    def test_missing_source_fails_without_replacing_existing_artifact(self):
        with tempfile.TemporaryDirectory() as directory:
            source, output = Path(directory) / "source", Path(directory) / "build"
            source.mkdir()
            output.mkdir()
            (source / "index.html").write_bytes(b"changed")
            (output / "index.html.gz").write_bytes(b"old artifact")
            with self.assertRaises(FileNotFoundError):
                pw_web_pack.pack(source, output)
            self.assertEqual((output / "index.html.gz").read_bytes(), b"old artifact")

    def test_generated_files_cannot_replace_source_files(self):
        with self.assertRaises(ValueError):
            pw_web_pack.pack(WEB, WEB)


if __name__ == "__main__":
    unittest.main()
