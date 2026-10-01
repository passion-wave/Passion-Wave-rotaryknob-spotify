#!/usr/bin/env python3
"""Read-only, resumable esptool backup. Keep destination OUTSIDE repositories.

Read/verify leave the chip in its loader instead of booting the original app
between blocks. Resumed blocks are size-checked; only a fresh whole-image device
verification qualifies the assembled backup, including all resumed bytes.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', required=True)
    parser.add_argument('--chip', choices=['esp32', 'esp32s3'], required=True)
    parser.add_argument('--size', type=lambda x: int(x, 0), required=True)
    parser.add_argument('--destination', type=Path, required=True)
    parser.add_argument('--baud', type=int, choices=[115200, 230400, 460800], default=115200)
    args = parser.parse_args(argv)
    if args.size not in (4 * 1024 * 1024, 16 * 1024 * 1024):
        parser.error('Use the measured flash size (4 MiB or 16 MiB).')
    dest = args.destination.resolve()
    if any((p / '.git').exists() for p in [dest, *dest.parents]):
        parser.error('Backups can contain credentials; destination must be outside git.')
    os.umask(0o077)
    dest.mkdir(parents=True, exist_ok=True, mode=0o700)
    identity = {'chip': args.chip, 'size': args.size, 'port': args.port, 'chunk_size': 131072}
    identity_path = dest / 'identity.json'
    if identity_path.exists() and json.loads(identity_path.read_text()) != identity:
        parser.error('Existing backup identity does not match this request.')
    # An old success marker must never survive a new read/verify failure or
    # interrupted assembly. Identity rejection above does not start a new run.
    verified = dest / 'verified.json'
    verified_temporary = dest / 'verified.incomplete'
    verified.unlink(missing_ok=True)
    verified_temporary.unlink(missing_ok=True)
    identity_path.write_text(json.dumps(identity, indent=2) + '\n')
    command = [sys.executable, '-m', 'esptool', '--chip', args.chip,
               '--port', args.port, '--baud', str(args.baud),
               '--before', 'default_reset', '--after', 'no_reset']
    chunks = []
    for offset in range(0, args.size, identity['chunk_size']):
        size = min(identity['chunk_size'], args.size - offset)
        chunk = dest / f'chunk-{offset:08x}.bin'
        if not (chunk.exists() and chunk.stat().st_size == size):
            temporary = dest / f'chunk-{offset:08x}.incomplete'
            for attempt in range(1, 4):
                # A previous attempt can leave a complete-length but rejected
                # file. A later zero exit status must not accept that old file.
                temporary.unlink(missing_ok=True)
                with (dest / f'read-{offset:08x}-{attempt}.log').open('wb') as log:
                    try:
                        result = subprocess.run(command + ['read_flash', hex(offset), hex(size), str(temporary)],
                                                stdout=log, stderr=subprocess.STDOUT, timeout=90)
                    except (subprocess.TimeoutExpired, OSError):
                        result = None
                if result is not None and result.returncode == 0 and temporary.exists() and temporary.stat().st_size == size:
                    temporary.replace(chunk)
                    break
                print(f'Block {offset:#x}: retry {attempt}/3', flush=True)
            else:
                print(f'Backup incomplete at {offset:#x}; no firmware written.', file=sys.stderr)
                return 1
        chunks.append(chunk)
        print(f'Read {offset + size}/{args.size} bytes', flush=True)
    image = dest / f'{args.chip}-original.bin'
    image_temporary = dest / f'{image.name}.incomplete'
    with image_temporary.open('wb') as output:
        for chunk in chunks:
            output.write(chunk.read_bytes())
        output.flush()
        os.fsync(output.fileno())
    image_temporary.replace(image)
    with (dest / 'verify.log').open('wb') as log:
        try:
            result = subprocess.run(command + ['verify_flash', '0x0', str(image)],
                                    stdout=log, stderr=subprocess.STDOUT, timeout=300)
        except (subprocess.TimeoutExpired, OSError):
            result = None
    if result is None or result.returncode:
        print('Complete bytes captured but device verification failed; backup NOT qualified.', file=sys.stderr)
        return 1
    digest = hashlib.sha256(image.read_bytes()).hexdigest()
    with verified_temporary.open('w') as output:
        output.write(json.dumps({**identity, 'image': image.name, 'sha256': digest,
                                 'device_verify': 'passed'}, indent=2) + '\n')
        output.flush()
        os.fsync(output.fileno())
    verified_temporary.replace(verified)
    print(f'Verified original flash: {image.name}, SHA256 {digest}', flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
