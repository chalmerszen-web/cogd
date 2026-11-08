"""Windows: compile this one translation unit with the existing P4 compile DB.
No CMake reconfigure, link, network, credentials, serial, device or flash access.
"""
import ctypes
import hashlib
import json
import subprocess
import sys
from pathlib import Path

root=Path(__file__).resolve().parents[2]
base=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else root/'.local/aoq-native-20261107/pcm-fixture'
base.mkdir(parents=True,exist_ok=True)
component=root/'ESP32-P4-WIFI6-Touch-LCD-3.5/components/p4_aoq'
source=component/'aoq_pcm_fixture.cpp'
database=root/'ESP32-P4-WIFI6-Touch-LCD-3.5/build-aoq-native/compile_commands.json'
commands=json.loads(database.read_text(encoding='utf-8'))
entry=next(v for v in commands if v['file'].replace('\\','/').endswith('/p4_aoq/aoq_probe.cpp'))
shell=ctypes.WinDLL('shell32',use_last_error=True)
shell.CommandLineToArgvW.argtypes=[ctypes.c_wchar_p,ctypes.POINTER(ctypes.c_int)]
shell.CommandLineToArgvW.restype=ctypes.POINTER(ctypes.c_wchar_p)
count=ctypes.c_int()
parsed=shell.CommandLineToArgvW(entry['command'],ctypes.byref(count))
if not parsed: raise ctypes.WinError(ctypes.get_last_error())
try:args=[parsed[i] for i in range(count.value)]
finally:ctypes.windll.kernel32.LocalFree(parsed)
obj=base/'pcm-fixture-p4.o'
for flag,value in [('-c',source),('-o',obj),('-MF',base/'pcm-fixture-p4.d'),('-MT','pcm-fixture-p4.o')]:
    if flag in args:args[args.index(flag)+1]=str(value)
assert '-Werror' in args and '-c' in args
with (base/'p4-compile.log').open('w',encoding='utf-8') as log:
    result=subprocess.run(args,cwd=entry['directory'],stdout=log,stderr=subprocess.STDOUT,timeout=120)
report=dict(exit_code=result.returncode,source=str(source),source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
            header_sha256=hashlib.sha256((component/'aoq_pcm_fixture.h').read_bytes()).hexdigest(),
            flags_from=entry['file'],compile_database=str(database),
            command_sha256=hashlib.sha256(entry['command'].encode()).hexdigest(),
            scope='P4_object_compile_only_no_link_network_device')
if result.returncode==0:report['object_sha256']=hashlib.sha256(obj.read_bytes()).hexdigest()
(base/'p4-compile.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('P4 PCM fixture object compile exit='+str(result.returncode))
raise SystemExit(result.returncode)
