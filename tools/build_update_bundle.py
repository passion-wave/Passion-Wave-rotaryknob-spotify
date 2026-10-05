#!/usr/bin/env python3
"""Build a signed paired PWOTA1 bundle; no upload, device access or trust changes.

Requires cryptography. Private keys are explicit local PEM paths and never copied
into outputs. This tool rejects example manifests and wrong ESP role/version.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))
from tools.check import validate_manifest

MAGIC = b"PWOTA1\r\n"
ROLES = ("companion_esp32", "controller_s3")

def canonical_manifest(value: dict) -> bytes:
    data = json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True, allow_nan=False).encode("ascii")
    if len(data) > 16384:
        raise ValueError("Manifest exceeds 16 KiB")
    return data

def check_descriptor(data: bytes, role: str, version: str, security: int) -> None:
    if len(data) < 288 or len(data) > 16777216 or data[0] != 0xE9 or not 1 <= data[1] <= 16:
        raise ValueError(f"{role}: not a supported ESP application image")
    chip = struct.unpack_from("<H", data, 12)[0]
    if chip != (9 if role == "controller_s3" else 0):
        raise ValueError(f"{role}: wrong ESP chip")
    if struct.unpack_from("<I", data, 32)[0] != 0xABCD5432:
        raise ValueError(f"{role}: missing IDF application descriptor")
    def text(offset: int) -> str:
        raw = data[offset:offset + 32]
        if b"\0" not in raw:
            raise ValueError(f"{role}: unterminated application descriptor")
        return raw.split(b"\0", 1)[0].decode("ascii")
    expected_project = "passionwave_spotify" if role == "controller_s3" else "passionwave_companion"
    if text(48) != version or text(80) != expected_project:
        raise ValueError(f"{role}: wrong application version or project")
    if struct.unpack_from("<I", data, 36)[0] != security:
        raise ValueError(f"{role}: security_version differs from actual image")

def build(manifest: dict, image_paths: dict[str, Path], private_key: Path) -> tuple[bytes, bytes, list[bytes]]:
    if manifest.get("product_id") != "passion-wave-rotaryknob-spotify" or manifest.get("manifest_version") != 1:
        raise ValueError("Wrong product or manifest version")
    if manifest.get("example_only") is not False:
        raise ValueError("Example manifests cannot produce installable bundles")
    images = manifest.get("images", [])
    if len(images) != 2 or sorted(item.get("role", "") for item in images) != sorted(ROLES):
        raise ValueError("Exactly one image per role is required")
    blobs: dict[str, bytes] = {}
    for item in images:
        role = item["role"]
        if item.get("version") != manifest.get("version") or item.get("release_id") != manifest.get("release_id"):
            raise ValueError("Manifest and both images must share version/release_id")
        if not str(item.get("url", "")).startswith("https://"):
            raise ValueError("Image URLs must use HTTPS")
        data = image_paths[role].read_bytes()
        check_descriptor(data, role, item["version"], item["security_version"])
        item["bytes"] = len(data)
        item["sha256"] = hashlib.sha256(data).hexdigest()
        blobs[role] = data
    if manifest.get("signing", {}).get("encoding") != "detached-over-canonical-manifest-v1":
        raise ValueError("Unsupported signature framing")
    validate_manifest(manifest)  # Structure only; device policy is checked on each MCU.
    key = serialization.load_pem_private_key(private_key.read_bytes(), password=None)
    if not isinstance(key, ec.EllipticCurvePrivateKey) or not isinstance(key.curve, ec.SECP256R1):
        raise ValueError("An ECDSA P-256 private signing key is required")
    encoded = canonical_manifest(manifest)
    signature = key.sign(encoded, ec.ECDSA(hashes.SHA256()))
    return encoded, signature, [blobs[role] for role in ROLES]

def write_bundle(destination: Path, encoded: bytes, signature: bytes, blobs: list[bytes]) -> None:
    if destination.exists():
        raise FileExistsError("Output already exists; versioned bundles are immutable")
    temporary: str | None = None
    try:
        with tempfile.NamedTemporaryFile(dir=destination.parent, prefix=".pwota-", delete=False) as file:
            temporary = file.name
            file.write(MAGIC)
            file.write(struct.pack("<IH", len(encoded), len(signature)))
            file.write(encoded)
            file.write(signature)
            for blob in blobs:
                file.write(blob)
            file.flush()
            os.fsync(file.fileno())
        os.link(temporary, destination)  # Atomic and refuses overwriting an existing output.
    finally:
        if temporary:
            os.unlink(temporary)

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--companion", type=Path, required=True)
    parser.add_argument("--s3", type=Path, required=True)
    parser.add_argument("--private-key", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        manifest = json.loads(args.manifest.read_text(encoding="utf-8"), object_pairs_hook=reject_duplicates)
        encoded, signature, blobs = build(manifest, {ROLES[0]: args.companion, ROLES[1]: args.s3}, args.private_key)
        write_bundle(args.output, encoded, signature, blobs)
    except (ValueError, KeyError, OSError, TypeError) as error:
        parser.exit(2, f"Bundle rejected: {error}\n")
    print(f"Signed paired bundle: {args.output.name}; manifest SHA256 {hashlib.sha256(encoded).hexdigest()}")

def reject_duplicates(pairs: list[tuple[str, object]]) -> dict:
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("Duplicate JSON key")
        result[key] = value
    return result

if __name__ == "__main__":
    main()
