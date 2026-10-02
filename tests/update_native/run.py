#!/usr/bin/env python3
"""Adversarial native verifier tests. All signing keys exist only in a temp dir."""
from __future__ import annotations
import copy
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec

ROOT = Path(__file__).resolve().parents[2]
IDF = Path(os.environ.get("IDF_PATH", Path.home() / "esp/esp-idf-v5.4.3"))
MBED = IDF / "components/mbedtls/mbedtls"
# Reusable host-only library cache; no signing material is stored here.
LIBS = Path(os.environ.get("PW_UPDATE_MBEDTLS_BUILD", str(Path(tempfile.gettempdir()) / "pw-update-mbedtls")))

RESULTS = dict(zip("ok invalid_argument memory signature untrusted_key format noncanonical example product role release channel hardware size protocol config_schema security_version bootloader downgrade image_hash image_header image_version state".split(), range(23)))

def image(role: str, version: str = "0.2.0", security: int = 1) -> bytes:
    # Synthetic app DESCRIPTOR fixture, never bootable or offered to a device.
    data = bytearray(1024)
    data[0] = 0xE9; data[1] = 1
    struct.pack_into("<H", data, 12, 9 if role == "controller_s3" else 0)
    struct.pack_into("<I", data, 28, len(data)-32)
    struct.pack_into("<II", data, 32, 0xABCD5432, security)
    data[48:48+len(version)] = version.encode()
    project = b"passionwave_spotify" if role == "controller_s3" else b"passionwave_companion"
    data[80:80+len(project)] = project
    return bytes(data)

def canonical(value: dict) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True, allow_nan=False).encode()

def main() -> None:
    if not (LIBS / "library/libmbedcrypto.a").exists():
        subprocess.run(["cmake", "-S", str(MBED), "-B", str(LIBS), "-DENABLE_PROGRAMS=OFF", "-DENABLE_TESTING=OFF", "-DCMAKE_BUILD_TYPE=Release", "-DMBEDTLS_FATAL_WARNINGS=OFF"], check=True, stdout=subprocess.DEVNULL)
        subprocess.run(["cmake", "--build", str(LIBS), "--parallel", "8"], check=True, stdout=subprocess.DEVNULL)
    with tempfile.TemporaryDirectory(prefix="pw-update-native-") as directory:
        temp = Path(directory)
        binary = temp / "verifier"
        subprocess.run(["cc", "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined", "-g",
            "-I"+str(ROOT / "firmware/components/pw_update_verify/include"), "-I"+str(MBED / "include"), "-I"+str(IDF / "components/json/cJSON"),
            str(ROOT / "tests/update_native/test_driver.c"), str(ROOT / "firmware/components/pw_update_verify/pw_update_verify.c"),
            str(IDF / "components/json/cJSON/cJSON.c"), str(LIBS / "library/libmbedcrypto.a"), "-lm", "-o", str(binary)], check=True)
        key = ec.generate_private_key(ec.SECP256R1())
        other = ec.generate_private_key(ec.SECP256R1())
        def pub(k) -> bytes:
            return k.public_key().public_bytes(serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo)
        public = temp / "public.pem"; public.write_bytes(pub(key))
        role_names = ["companion_esp32", "controller_s3"]
        blobs = [image(role) for role in role_names]
        base = json.loads((ROOT / "examples/update-manifest.example.json").read_text())
        base.update(example_only=False, version="0.2.0", release_id="native-test", channel="stable")
        base["signing"].update(key_id="test-p256")
        for i, role in enumerate(role_names):
            base["images"][i].update(role=role, version="0.2.0", release_id="native-test", hardware_id="jc3636k518c-"+("s3" if i else "esp32"), bytes=len(blobs[i]), sha256=hashlib.sha256(blobs[i]).hexdigest(), security_version=1, minimum_bootloader="1.0.0", accepted_peer_protocol_max=2, readable_config_schema_max=2)
        count = 0
        def run(name, expected="ok", mutate=None, raw=None, signature=None, public_bytes=None, policy="normal", image_blobs=None, image_result="ok"):
            nonlocal count
            value = copy.deepcopy(base)
            if mutate:
                mutate(value)
            encoded = raw if raw is not None else canonical(value)
            signed = signature if signature is not None else key.sign(encoded, ec.ECDSA(hashes.SHA256()))
            (temp / "manifest.json").write_bytes(encoded)
            (temp / "manifest.sig").write_bytes(signed)
            public.write_bytes(public_bytes or pub(key))
            args = [str(binary), str(temp / "manifest.json"), str(temp / "manifest.sig"), str(public), str(RESULTS[expected]), name, policy]
            if image_blobs is not None:
                for i, blob in enumerate(image_blobs):
                    (temp / f"image{i}.bin").write_bytes(blob)
                args += [str(temp / "image0.bin"), str(temp / "image1.bin"), str(RESULTS[image_result])]
            subprocess.run(args, check=True)
            count += 1
        run("valid pair and both actual image streams", image_blobs=blobs)
        original = canonical(base)
        run("unsigned alteration", "signature", raw=original.replace(b'"0.2.0"', b'"9.2.0"'), signature=key.sign(original, ec.ECDSA(hashes.SHA256())))
        run("different signing key", "signature", signature=other.sign(original, ec.ECDSA(hashes.SHA256())))
        run("wrong trusted key", "signature", public_bytes=pub(other))
        run("wrong EC group", "untrusted_key", public_bytes=pub(ec.generate_private_key(ec.SECP384R1())))
        run("truncated signature", "invalid_argument", signature=b"\x30\x00")
        run("raw signature is not DER", "signature", signature=b"\x01"*64)
        run("unknown key identifier", "untrusted_key", mutate=lambda x: x["signing"].update(key_id="attacker-key"))
        run("example never installable", "example", mutate=lambda x: x.update(example_only=True))
        run("other product", "product", mutate=lambda x: x.update(product_id="passion-wave-ha"))
        run("pretty encoding rejected", "noncanonical", raw=json.dumps(base, sort_keys=True, indent=None).encode())
        run("unsorted encoding rejected", "noncanonical", raw=json.dumps(base, separators=(",", ":")).encode())
        run("trailing bytes rejected", "format", raw=original+b" ")
        run("embedded NUL rejected", "format", raw=original+b"\0")
        run("unknown property rejected", "format", mutate=lambda x: x.update(trusted=True))
        run("duplicate key rejected", "noncanonical", raw=original.replace(b'"channel":"stable"', b'"channel":"stable","channel":"stable"'))
        run("float integer rejected", "noncanonical", raw=original.replace(b'"config_schema":1', b'"config_schema":1.0'))
        run("excess nesting rejected", "format", raw=b'['*9+b'0'+b']'*9)
        run("oversize rejected before parser", "invalid_argument", raw=b' '*16385)
        run("wrong role rejected", "role", mutate=lambda x: x["images"][1].update(role="companion_esp32"))
        run("one image rejected", "role", mutate=lambda x: x["images"].pop())
        run("release mismatch", "release", mutate=lambda x: x["images"][0].update(release_id="different"))
        run("version mismatch", "release", mutate=lambda x: x["images"][0].update(version="0.2.1"))
        run("board mismatch", "hardware", mutate=lambda x: x["images"][0].update(hardware_id="different-board"))
        run("HTTP downgrade rejected", "format", mutate=lambda x: x["images"][0].update(url="http://example.com/fw.bin"))
        run("credentials in URL rejected", "format", mutate=lambda x: x["images"][0].update(url="https://user:secret@example.com/fw.bin"))
        run("empty host", "format", mutate=lambda x: x["images"][0].update(url="https://?x"))
        run("invalid port", "format", mutate=lambda x: x["images"][0].update(url="https://example.com:99999999/fw.bin"))
        run("invalid host label", "format", mutate=lambda x: x["images"][0].update(url="https://-example.com/fw.bin"))
        run("slot overflow", "size", policy="small_slot")
        run("security rollback", "security_version", policy="security2")
        run("old bootloader", "bootloader", policy="old_bootloader")
        run("version downgrade", "downgrade", policy="new_running")
        run("missing hardware evidence", "invalid_argument", policy="missing_policy")
        run("beta opt-in", "channel", mutate=lambda x: x.update(channel="beta"), policy="stable_only")
        run("config too new", "config_schema", mutate=lambda x: x.update(config_schema=3))
        run("config range unbounded", "config_schema", mutate=lambda x: x["images"][0].update(readable_config_schema_max=9))
        run("protocol range unbounded", "protocol", mutate=lambda x: x["images"][0].update(accepted_peer_protocol_max=9))
        run("final protocol mismatch", "protocol", mutate=lambda x: x["images"][0].update(emitted_protocol=3))
        run("intermediate protocol mismatch", "protocol", mutate=lambda x: x["images"][0].update(emitted_protocol=2), policy="narrow_old_s3")
        run("automatic companion fallback compatibility", "protocol", mutate=lambda x: x["images"][1].update(emitted_protocol=2), policy="narrow_old_companion")
        def beta_versions(x):
            x["version"] = "0.2.0-beta.2"
            for im in x["images"]: im["version"] = x["version"]
        run("numeric prerelease precedence", "downgrade", mutate=beta_versions, policy="pre_running")
        run("modified image", image_blobs=[b[:-1]+b'X' for b in blobs], image_result="image_hash")
        run("truncated image", image_blobs=[b[:-1] for b in blobs], image_result="size")
        run("oversized image", image_blobs=[b+b'X' for b in blobs], image_result="size")
        run("swapped role images", image_blobs=list(reversed(blobs)), image_result="image_hash")
        for name, offset, value, expected in [("wrong authenticated chip", 12, 1, "image_header"), ("wrong authenticated security", 36, 2, "security_version"), ("wrong authenticated app version", 48, ord('9'), "image_version"), ("wrong authenticated project", 80, ord('X'), "image_version")]:
            changed=[]
            for b in blobs:
                c=bytearray(b); c[offset]=value; changed.append(bytes(c))
            def rehash(x):
                for i,b in enumerate(changed): x["images"][i]["sha256"]=hashlib.sha256(b).hexdigest()
            run(name, mutate=rehash, image_blobs=changed, image_result=expected)
        # Producer interoperability: real cryptography output -> exact C/mbedTLS verifier.
        spec=importlib.util.spec_from_file_location("pw_bundle", ROOT/"tools/build_update_bundle.py")
        module=importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
        private=temp/"private.pem"
        private.write_bytes(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
        os.chmod(private, 0o600)
        paths={}
        for i,role in enumerate(role_names):
            paths[role]=temp/f"producer-{role}.bin"; paths[role].write_bytes(blobs[i])
        encoded, signature, payloads=module.build(copy.deepcopy(base), paths, private)
        run("producer signature interoperability", raw=encoded, signature=signature, image_blobs=payloads)
        bundle=temp/"test.pwota"; module.write_bundle(bundle,encoded,signature,payloads)
        package=bundle.read_bytes()
        assert package[:8]==b"PWOTA1\r\n"
        nm,ns=struct.unpack_from("<IH",package,8)
        assert package[14:14+nm]==encoded and package[14+nm:14+nm+ns]==signature
        assert package[14+nm+ns:]==b''.join(blobs)
        try: module.write_bundle(bundle,encoded,signature,payloads)
        except FileExistsError: pass
        else: raise AssertionError("producer overwrote immutable output")
        print(f"update native: {count} signed adversarial/interoperability cases passed; bundle framing/immutability passed")

if __name__ == "__main__":
    main()
