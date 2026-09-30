#!/usr/bin/env python3
"""Decode original JPEGs with macOS ImageIO via sips, independent of TJpgDec.

Writes only numeric reference samples. Never modifies the imported JPEGs.
Normal host tests consume the checked-in JSON and do not require macOS.
"""
from pathlib import Path
import hashlib
import json
import struct
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
manifest = json.loads((root / "manifest.json").read_text())
regions = [[68, 68, 16], [176, 76, 16], [176, 168, 16], [176, 240, 16]]
version = subprocess.check_output(["/usr/bin/sips", "--version"], text=True).strip()
samples = []
with tempfile.TemporaryDirectory(prefix="pw-original-jpeg-colors-") as temporary:
    for name in sorted({image["file"] for image in manifest["images"]}):
        path = root / name
        bitmap = Path(temporary) / "reference.bmp"
        subprocess.run(["/usr/bin/sips", "-s", "format", "bmp", str(path), "--out", str(bitmap)],
                       check=True, stdout=subprocess.DEVNULL)
        data = bitmap.read_bytes()
        signature, _, _, _, offset = struct.unpack_from("<2sIHHI", data)
        size, width, height, planes, bits, compression = struct.unpack_from("<IiiHHI", data, 14)
        assert signature == b"BM" and size >= 40 and width == 368 and abs(height) == 368
        assert planes == 1 and bits == 24 and compression == 0
        stride = (width * 3 + 3) & ~3
        colors = []
        for x, y, side in regions:
            sums = [0, 0, 0]
            for dy in range(side):
                row = y + dy if height < 0 else 367 - y - dy
                for dx in range(side):
                    pixel = offset + row * stride + (x + dx) * 3
                    for channel in range(3):
                        sums[channel] += data[pixel + 2 - channel]
            colors.append([round(value / (side * side)) for value in sums])
        samples.append({"file": name, "sha256": hashlib.sha256(path.read_bytes()).hexdigest(), "rgb": colors})
reference = {"decoder": f"macOS ImageIO via {version}, original JPEG -> uncompressed 24-bit BMP",
             "purpose": "Independent RGB region means catch JPEG decoder channel swaps.",
             "regions_xy_side": regions, "tolerance_per_channel": 12, "samples": samples}
(root / "tests/color_reference.json").write_text(json.dumps(reference, indent=2) + "\n")
print(f"Independent original-JPEG color references: {len(samples)} images, {len(regions)} regions each")
