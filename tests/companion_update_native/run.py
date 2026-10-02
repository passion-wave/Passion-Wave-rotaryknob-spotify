#!/usr/bin/env python3
"""Run actual receiver + real signature verifier against bounded NVS/flash model."""
from pathlib import Path
import hashlib
import importlib.util
import json
import os
import subprocess
import tempfile
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
ROOT=Path(__file__).resolve().parents[2]
IDF=Path(os.environ.get("IDF_PATH",Path.home()/"esp/esp-idf-v5.4.3"))
MBED=IDF/"components/mbedtls/mbedtls"
LIBS=Path(os.environ.get("PW_UPDATE_MBEDTLS_BUILD",str(Path(tempfile.gettempdir()) / "pw-update-mbedtls")))
spec=importlib.util.spec_from_file_location("update_test_helpers",ROOT/"tests/update_native/run.py")
helpers=importlib.util.module_from_spec(spec);spec.loader.exec_module(helpers)

def main():
    if not (LIBS/"library/libmbedcrypto.a").exists():
        subprocess.run(["cmake","-S",str(MBED),"-B",str(LIBS),"-DENABLE_PROGRAMS=OFF","-DENABLE_TESTING=OFF","-DCMAKE_BUILD_TYPE=Release","-DMBEDTLS_FATAL_WARNINGS=OFF"],check=True,stdout=subprocess.DEVNULL)
        subprocess.run(["cmake","--build",str(LIBS),"--parallel","8"],check=True,stdout=subprocess.DEVNULL)
    with tempfile.TemporaryDirectory(prefix="pw-companion-tests-") as directory:
        tmp=Path(directory);binary=tmp/"receiver"
        args=["cc","-std=c11","-D_POSIX_C_SOURCE=200809L","-Wall","-Wextra","-Werror","-fsanitize=address,undefined","-g"]
        for path in [ROOT/"firmware/components/pw_companion_update/include",ROOT/"firmware/components/pw_update_verify/include",ROOT/"firmware/components/pw_protocol/include",MBED/"include",IDF/"components/json/cJSON"]:
            args.append("-I"+str(path))
        args += [str(ROOT/"tests/companion_update_native/test_receiver.c"),str(ROOT/"firmware/components/pw_companion_update/pw_companion_update_core.c"),str(ROOT/"firmware/components/pw_update_verify/pw_update_verify.c"),str(ROOT/"firmware/components/pw_protocol/pw_protocol.c"),str(IDF/"components/json/cJSON/cJSON.c"),str(LIBS/"library/libmbedcrypto.a"),"-lm","-o",str(binary)]
        subprocess.run(args,check=True)
        key=ec.generate_private_key(ec.SECP256R1())
        old=helpers.image("companion_esp32","0.1.0")
        new=helpers.image("companion_esp32")
        s3=helpers.image("controller_s3")
        m=json.loads((ROOT/"examples/update-manifest.example.json").read_text())
        m.update(example_only=False,version="0.2.0",release_id="receiver-test",channel="stable")
        m["signing"].update(key_id="test-p256")
        for i,blob in enumerate([new,s3]):
            m["images"][i].update(version="0.2.0",release_id="receiver-test",hardware_id="jc3636k518c-"+("s3" if i else "esp32"),bytes=len(blob),sha256=hashlib.sha256(blob).hexdigest(),security_version=1,minimum_bootloader="1.0.0",accepted_peer_protocol_max=2,readable_config_schema_max=2)
        encoded=helpers.canonical(m)
        files=[encoded,key.sign(encoded,ec.ECDSA(hashes.SHA256())),key.public_key().public_bytes(serialization.Encoding.PEM,serialization.PublicFormat.SubjectPublicKeyInfo),old,new]
        paths=[]
        for i,data in enumerate(files):
            path=tmp/f"input{i}";path.write_bytes(data);paths.append(str(path))
        subprocess.run([str(binary)]+paths,check=True)
if __name__=="__main__":main()
