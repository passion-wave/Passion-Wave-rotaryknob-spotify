#!/usr/bin/env python3
"""Reject S3 builds that place the 64 KiB LVGL pool back in internal RAM.

Run with ESP-IDF's Python environment (pyelftools is an IDF dependency).
This checks the linked binary; runtime headroom still needs device measurement.
"""
import argparse
from pathlib import Path

from elftools.elf.elffile import ELFFile


def check(path: Path) -> None:
    with path.open("rb") as stream:
        elf = ELFFile(stream)
        symbols = elf.get_section_by_name(".symtab")
        if symbols is None:
            raise ValueError("missing ELF symbols")
        pools = [s for s in symbols.iter_symbols() if s.name.startswith("work_mem_int")]
        if len(pools) != 1 or pools[0]["st_size"] != 65536:
            raise ValueError("missing or changed 64 KiB LVGL pool; review memory budget")
        pool = pools[0]
        section = elf.get_section(pool["st_shndx"])
        if section.name != ".ext_ram.bss" or not 0x3C000000 <= pool["st_value"] < 0x3E000000:
            raise ValueError("LVGL pool is not in ESP32-S3 external RAM")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_dirs", nargs="+", type=Path)
    args = parser.parse_args()
    failed = False
    for directory in args.build_dirs:
        try:
            check(directory / "passionwave_spotify.elf")
            print(f"OK {directory}: 65536-byte LVGL pool in PSRAM")
        except (OSError, ValueError, KeyError, TypeError) as error:
            print(f"ERROR {directory}: {error}")
            failed = True
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
