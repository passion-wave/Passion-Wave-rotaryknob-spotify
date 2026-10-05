#!/usr/bin/env python3
"""Exercise the real streaming/staging C core with temporary P-256 signatures."""
from pathlib import Path
import hashlib
import importlib.util
import json
import os
import struct
import subprocess
import tempfile
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("verifier_tests", ROOT / "tests/update_native/run.py")
helpers = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helpers)
if not (helpers.LIBS / "library/libmbedcrypto.a").exists():
    subprocess.run(["cmake", "-S", str(helpers.MBED), "-B", str(helpers.LIBS), "-DENABLE_PROGRAMS=OFF", "-DENABLE_TESTING=OFF", "-DMBEDTLS_FATAL_WARNINGS=OFF"], check=True, stdout=subprocess.DEVNULL)
    subprocess.run(["cmake", "--build", str(helpers.LIBS), "--parallel", "8"], check=True, stdout=subprocess.DEVNULL)
with tempfile.TemporaryDirectory(prefix="pw-stage-native-") as directory:
    temp = Path(directory)
    binary = temp / "stage-test"
    includes = [ROOT / "firmware/components/pw_update_service/include", ROOT / "firmware/components/pw_update_verify/include", ROOT / "firmware/components/pw_protocol/include", helpers.MBED / "include", helpers.IDF / "components/json/cJSON"]
    sources = [ROOT / "tests/update_service_native/test_stage.c", ROOT / "firmware/components/pw_update_service/pw_update_stage.c", ROOT / "firmware/components/pw_update_verify/pw_update_verify.c", ROOT / "firmware/components/pw_protocol/pw_protocol.c", helpers.IDF / "components/json/cJSON/cJSON.c"]
    subprocess.run(["cc", "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra", "-Werror", "-Wno-deprecated-declarations", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g", *["-I" + str(p) for p in includes], *map(str, sources), str(helpers.LIBS / "library/libmbedcrypto.a"), "-lm", "-o", str(binary)], check=True)
    key = ec.generate_private_key(ec.SECP256R1())
    public = temp / "public.pem"
    public.write_bytes(key.public_key().public_bytes(serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo))
    manifest = json.loads((ROOT / "examples/update-manifest.example.json").read_text())
    manifest.update(example_only=False, version="0.2.0", release_id="native-stage", channel="stable")
    manifest["signing"].update(key_id="test-p256")
    blobs = []
    for index, role in enumerate(["companion_esp32", "controller_s3"]):
        blob = helpers.image(role)
        blobs.append(blob)
        manifest["images"][index].update(role=role, version="0.2.0", release_id="native-stage", hardware_id="jc3636k518c-" + ("s3" if index else "esp32"), bytes=len(blob), sha256=hashlib.sha256(blob).hexdigest(), security_version=1, minimum_bootloader="1.0.0", accepted_peer_protocol_max=2, readable_config_schema_max=2)
    encoded = helpers.canonical(manifest)
    signature = key.sign(encoded, ec.ECDSA(hashes.SHA256()))
    bundle = temp / "sample.pwota"
    bundle.write_bytes(b"PWOTA1\r\n" + struct.pack("<IH", len(encoded), len(signature)) + encoded + signature + b"".join(blobs))
    subprocess.run([str(binary), str(bundle), str(public)], check=True, env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})
