#!/usr/bin/env python3
"""Check CMake metadata and actual app descriptors via esptool, without USB access."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import subprocess
import sys


VERSION_FILE = Path(__file__).resolve().parents[1] / 'VERSION'
PROJECTS = {'esp32s3': 'passionwave_spotify', 'esp32': 'passionwave_companion'}


class VersionError(Exception):
    pass


def image_field(output: str, name: str) -> str:
    prefix = name + ': '
    values = [line[len(prefix):] for line in output.splitlines() if line.startswith(prefix)]
    if len(values) != 1 or not values[0]:
        raise VersionError(f'esptool returned missing or ambiguous {name}')
    # esptool 4.10.0 prints the 32-byte app descriptor strings with raw trailing
    # NUL padding; 4.12.0 sanitizes it. Reproduced with both official versions on
    # our actual app images. Remove only that padding, not spaces/embedded NULs
    # or Python bytes-literal syntax; the caller still compares exact values.
    value = values[0].rstrip('\x00')
    if not value:
        raise VersionError(f'esptool returned empty {name}')
    return value


def check_build(build_dir: Path, expected: str) -> tuple[str, str]:
    try:
        metadata = json.loads((build_dir / 'project_description.json').read_text())
    except (OSError, UnicodeError, ValueError) as error:
        raise VersionError('project_description.json missing or invalid') from error
    if not isinstance(metadata, dict):
        raise VersionError('project_description.json must be an object')
    # The top-level "version" is IDF's JSON format version, not the app version.
    if metadata.get('project_version') != expected:
        raise VersionError(f'CMake project_version differs from VERSION ({expected})')
    project = metadata.get('project_name')
    target = metadata.get('target')
    if not isinstance(target, str) or target not in PROJECTS or PROJECTS[target] != project:
        raise VersionError('CMake project_name/target does not match either firmware project')
    image_name = f'{project}.bin'
    if metadata.get('app_bin') != image_name:
        raise VersionError('CMake app_bin does not match project_name')
    image = build_dir / image_name
    if not image.is_file():
        raise VersionError(f'app image missing: {image_name}')
    try:
        result = subprocess.run(
            [sys.executable, '-m', 'esptool', 'image_info', '--version', '2', str(image)],
            capture_output=True, text=True, timeout=30, check=False)
    except (OSError, subprocess.TimeoutExpired, UnicodeError) as error:
        raise VersionError('esptool image_info could not finish') from error
    if result.returncode:
        raise VersionError('esptool image_info failed; use the ESP-IDF Python environment')
    if image_field(result.stdout, 'Project name') != project:
        raise VersionError('binary Project name differs from CMake metadata')
    if image_field(result.stdout, 'App version') != expected:
        raise VersionError(f'binary App version differs from VERSION ({expected})')
    return project, image_name


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build_dirs', type=Path, nargs='+', help='ESP-IDF build directory')
    args = parser.parse_args(argv)
    try:
        expected = VERSION_FILE.read_text().strip()
        if not expected or len(expected) > 31 or any(c.isspace() for c in expected):
            raise ValueError('invalid app version')
    except (OSError, UnicodeError, ValueError):
        print('ERROR: repository VERSION missing or invalid.', file=sys.stderr)
        return 1
    failed = False
    for directory in args.build_dirs:
        try:
            project, image_name = check_build(directory, expected)
            print(f'OK {directory}: {project} {expected} ({image_name})')
        except VersionError as error:
            print(f'ERROR {directory}: {error}', file=sys.stderr)
            failed = True
    return int(failed)


if __name__ == '__main__':
    raise SystemExit(main())
