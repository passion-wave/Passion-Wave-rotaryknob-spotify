#!/usr/bin/env python3
"""Run production JPEG decoder/lookup against every imported JPEG; no downloads."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--lvgl", required=True, type=Path, help="Existing LVGL 9.2.2 checkout/managed component")
parser.add_argument("--cc", default=os.environ.get("CC", "clang"))
args = parser.parse_args()
lvgl = args.lvgl.resolve()
version = (lvgl / "lv_version.h").read_text()
for key, value in (("MAJOR", 9), ("MINOR", 2), ("PATCH", 2)):
    if not re.search(rf"#define LVGL_VERSION_{key}\s+{value}\b", version):
        raise SystemExit("Tests require the production-pinned LVGL 9.2.2")
root = Path(__file__).resolve().parents[1]
manifest = json.loads((root / "manifest.json").read_text())
images = manifest["images"]
assert [image["index"] for image in images] == list(range(67))
unique = {}
for image in images:
    path = root / image["file"]
    data = path.read_bytes()
    assert hashlib.sha256(data).hexdigest() == image["sha256"], path
    assert len(data) == image["bytes"] and image["width"] == image["height"] == 368
    unique[path] = len(data)
assert len(unique) == manifest["unique_images"] == 55
assert sum(unique.values()) == manifest["embedded_unique_bytes"] == 910580
reference = json.loads((root / "tests/color_reference.json").read_text())
samples = reference["samples"]
assert [root / item["file"] for item in samples] == sorted(unique)
for item in samples:
    assert hashlib.sha256((root / item["file"]).read_bytes()).hexdigest() == item["sha256"]
with tempfile.TemporaryDirectory(prefix="pw-assets-test-") as temporary:
    build = Path(temporary)
    # Only the public ESP error typedef is needed; no RTOS or device behavior is faked.
    (build / "esp_err.h").write_text("#pragma once\ntypedef int esp_err_t;\n")
    regions = reference["regions_xy_side"]
    initializer = lambda data: json.dumps(data).replace("[", "{").replace("]", "}")
    (build / "color_reference.h").write_text(
        f'#define COLOR_IMAGE_COUNT {len(samples)}\n#define COLOR_REGION_COUNT {len(regions)}\n'
        f'#define COLOR_TOLERANCE {reference["tolerance_per_channel"]}\n'
        'static const unsigned color_regions[COLOR_REGION_COUNT][3] = ' + initializer(regions) + ';\n'
        'static const uint8_t color_reference[COLOR_IMAGE_COUNT][COLOR_REGION_COUNT][3] = ' +
        initializer([item["rgb"] for item in samples]) + ';\n')
    common = [args.cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
              "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
              "-DLV_CONF_SKIP", "-DLV_USE_TJPGD=1", "-I", str(root),
              "-I", str(root / "include"), "-I", str(build), "-I", str(lvgl)]
    decoder = build / "decode"
    subprocess.run(common + [str(root / "tests/test_decode.c"), str(root / "pw_asset_decode.c"),
                             str(lvgl / "src/libs/tjpgd/tjpgd.c"), "-o", str(decoder)], check=True)
    subprocess.run([str(decoder)] + [str(path) for path in sorted(unique)], check=True)
    # The actual generated index names, with no embedded pixel data for lookup tests.
    (build / "index.c").write_text('#include "pw_assets_internal.h"\n'
        'const pw_asset_source_t pw_asset_sources[PW_ASSET_COUNT] = {\n' +
        ''.join('    {' + json.dumps(image["name"]) + ', NULL, 0},\n' for image in images) + '};\n')
    lookup = build / "lookup"
    subprocess.run(common + [str(root / "tests/test_lookup.c"), str(root / "pw_assets_lookup.c"),
                             str(build / "index.c"), "-o", str(lookup)], check=True)
    subprocess.run([str(lookup)], check=True)
print("67 asset keys, 55 original JPEG hashes and 910580-byte embedded budget verified")
