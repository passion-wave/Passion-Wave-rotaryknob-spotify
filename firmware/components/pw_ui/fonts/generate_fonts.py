#!/usr/bin/env python3
"""Regenerate the small German glyph fallback from pinned LVGL 9.2.2 inputs.

No downloader, no npm install and no device access. Install lv_font_conv 1.5.3
separately, then pass its CLI and the managed LVGL root explicitly.
"""
from pathlib import Path
import argparse
import hashlib
import re
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--lvgl", type=Path, required=True)
parser.add_argument("--converter", type=Path, required=True)
args = parser.parse_args()
font = args.lvgl / "scripts/built_in_font/Montserrat-Medium.ttf"
expected = "421f26b23e2be6b98373d32acd3cb2897b154d4bf0a77d26534ce476e4cbed53"
if hashlib.sha256(font.read_bytes()).hexdigest() != expected:
    raise SystemExit("Font input changed; review source/license and metrics before regeneration")
version = subprocess.check_output(["node", str(args.converter), "--version"], text=True).strip()
if version != "1.5.3":
    raise SystemExit(f"Expected lv_font_conv 1.5.3, got {version!r}")
output = Path(__file__).resolve().parent
for size in (14, 18, 24, 32):
    path = output / f"pw_font_de_{size}.c"
    subprocess.run([
        "node", str(args.converter), "--size", str(size), "--bpp", "4",
        "--format", "lvgl", "--font", str(font), "--symbols", "ÄÖÜäöüß·–—…",
        "--lv-font-name", f"pw_font_de_{size}", "--lv-fallback", f"lv_font_montserrat_{size}",
        "--no-compress", "--output", str(path),
    ], check=True)
    text = path.read_text()
    text = text.replace(str(font), "lvgl@9.2.2/scripts/built_in_font/Montserrat-Medium.ttf")
    text = text.replace(str(path), path.name).replace('#include "lvgl/lvgl.h"', '#include "lvgl.h"')
    base = (args.lvgl / f"src/font/lv_font_montserrat_{size}.c").read_text()
    # A subset has no descender; retain the exact parent line box/baseline so
    # ASCII fallback and German characters align within one line.
    for field in ("line_height", "base_line"):
        value = re.search(r"\." + field + r" = (\d+)", base)[1]
        text = re.sub(r"\." + field + r" = \d+", f".{field} = {value}", text)
    path.write_text("// Derived Montserrat glyphs. SPDX-License-Identifier: OFL-1.1\n"
                    "// Original font copyright and license: ../FONT-OFL.txt.\n" + text.rstrip() + "\n")
