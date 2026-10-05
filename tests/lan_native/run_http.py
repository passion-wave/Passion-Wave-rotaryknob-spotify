import os, subprocess, tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[2]
s=(root/'firmware/components/pw_app/pw_app.c').read_text()
with tempfile.TemporaryDirectory(prefix='pw-lan-http-') as directory:
 p=Path(directory); fragments=[]
 for name in ('lan_peer','same_origin','lan_authorized','authorized'):
  start=s.index('static bool '+name+'(');end=s.index('\n}',start)+2;fragments.append(s[start:end])
 (p/'auth.inc').write_text('\n'.join(fragments))
 subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror','-pedantic','-fsanitize=address,undefined','-I'+str(p),'-I'+str(root/'firmware/components/pw_app/include'),str(root/'tests/lan_native/test_http.c'),str(root/'firmware/components/pw_app/pw_lan_gate.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True,env={**os.environ,'UBSAN_OPTIONS':'halt_on_error=1'})
