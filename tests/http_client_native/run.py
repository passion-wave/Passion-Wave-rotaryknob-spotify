#!/usr/bin/env python3
"""Pinned IDF header receiver + real parser; fake transport timeout/fragmentation."""
import os, subprocess, tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[2]
idf=Path(os.environ['IDF_PATH'])
source=(idf/'components/esp_http_client/esp_http_client.c').read_text()
start=source.index('int64_t esp_http_client_fetch_headers(')
end=source.index('\n}',start)+2
with tempfile.TemporaryDirectory(prefix='pw-http-client-') as d:
    work=Path(d); (work/'receiver.inc').write_text(source[start:end])
    binary=work/'test'
    subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(work),'-I'+str(idf/'components/http_parser'),str(Path(__file__).with_name('test_receive.c')),str(idf/'components/http_parser/http_parser.c'),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True,env={**os.environ,'UBSAN_OPTIONS':'halt_on_error=1'})
