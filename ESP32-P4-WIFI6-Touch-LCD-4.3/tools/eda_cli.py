import subprocess,sys,json
from pathlib import Path
exe=Path.home()/'AppData/Local/Programs/lceda-pro/lceda-pro.exe'
r=subprocess.run([str(exe),*sys.argv[1:]],capture_output=True,timeout=180)
print(r.stdout.decode('utf8','replace').strip())
if r.stderr:print(r.stderr.decode('utf8','replace'),file=sys.stderr)
sys.exit(r.returncode)
