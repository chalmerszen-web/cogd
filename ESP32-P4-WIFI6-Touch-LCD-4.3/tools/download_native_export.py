"""Retrieve the native export in bounded chunks through the existing CLI session."""
from pathlib import Path
import subprocess,json,base64,sys,zipfile
ROOT=Path(__file__).resolve().parents[1]
cli=Path.home()/'AppData/Local/Programs/lceda-pro/lceda-pro.exe'
session=sys.argv[1]
meta=json.loads((ROOT/'evidence/native-export-metadata.json').read_text(encoding='utf8'))['value']
target=ROOT/'hardware'/Path(meta['name']).name
partial=target.with_suffix('.zip.part')
with partial.open('wb') as out:
    for start in range(0,meta['size'],1000000):
        code='return await new Promise((resolve,reject)=>{const r=new FileReader();r.onload=()=>resolve(r.result);r.onerror=reject;r.readAsDataURL(globalThis.p4NativeExport.slice(%d,%d));});'%(start,start+1000000)
        result=subprocess.run([str(cli),'invoke','--session',session,'--ext-uuid','eda','--code',code,'--timeout','60000'],capture_output=True,timeout=90,check=True)
        reply=json.loads(result.stdout.decode('utf8'));assert reply['ok'],reply
        out.write(base64.b64decode(reply['value'].split(',',1)[1]))
assert partial.stat().st_size==meta['size']
with zipfile.ZipFile(partial) as archive:assert archive.testzip() is None
partial.replace(target)
print(json.dumps({'file':str(target),'bytes':target.stat().st_size}))
