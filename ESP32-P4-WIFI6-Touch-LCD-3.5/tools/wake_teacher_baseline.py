"""Compare the unchanged C11 baseline on exactly the teacher development WAVs."""
import argparse,json,sys
from pathlib import Path
from wake_host import Oracle
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('features',type=Path);p.add_argument('library',type=Path);a=p.parse_args()
rows=json.loads((a.features/'results.json').read_text(encoding='utf8'));oracle=Oracle(a.library,740);results=[];groups={}
for r in rows:
 if r['split']=='train':continue
 scores,events=oracle.wav(ROOT/r['path']);accepted=bool(events);g=groups.setdefault(r['wake_group'],{'count':0,'accepted':0})
 g['count']+=1;g['accepted']+=int(accepted);results.append({'clip_id':r['clip_id'],'events':events})
report={'threshold':740,'scope':'same source WAVs, not physical microphone replay','groups':groups,'trials':results}
(a.features/'baseline-report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8');print(json.dumps(groups,indent=2))
