#!/usr/bin/env python3
"""Precompress the device website at build time; no target RAM or CPU required.

Generated files are binary gzip streams (no appended NUL). The HTTP server keeps
their original media types and sends Content-Encoding: gzip and their exact
compressed lengths. Source files remain readable and usable by browser fixtures.
"""
from __future__ import annotations

import argparse
import gzip
import io
from pathlib import Path
import tempfile

ASSETS = ("index.html", "app.js", "style.css")


def encode(data: bytes) -> bytes:
    """Exclude timestamp, source filename and host platform from gzip metadata."""
    output = io.BytesIO()
    with gzip.GzipFile(filename="", mode="wb", fileobj=output,
                       compresslevel=9, mtime=0) as stream:
        stream.write(data)
    return output.getvalue()


def pack(source_dir: Path, output_dir: Path) -> None:
    source_dir, output_dir = source_dir.resolve(), output_dir.resolve()
    if source_dir == output_dir:
        raise ValueError("Generated assets must use a separate build directory")
    # Read every required source before replacing any output. Missing input must
    # fail the build, never silently reuse a stale asset from an earlier build.
    packed = {name: encode((source_dir / name).read_bytes()) for name in ASSETS}
    output_dir.mkdir(parents=True, exist_ok=True)
    for name, data in packed.items():
        path = output_dir / (name + ".gz")
        # Atomic replacement avoids a partially written gzip member after an
        # interrupted generation. Refresh mtime even for equal bytes so Ninja
        # observes an up-to-date output after a source/tool-only timestamp change.
        with tempfile.NamedTemporaryFile(dir=output_dir, prefix=".pw-web-", delete=False) as stream:
            temporary = Path(stream.name)
            try:
                stream.write(data)
            except BaseException:
                temporary.unlink(missing_ok=True)
                raise
        try:
            temporary.replace(path)
        finally:
            temporary.unlink(missing_ok=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    pack(args.source_dir, args.output_dir)


if __name__ == "__main__":
    main()
