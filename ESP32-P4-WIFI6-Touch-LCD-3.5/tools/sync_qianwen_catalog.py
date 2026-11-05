"""Compile installed skill metadata, never credentials or model weights, for P4."""
import argparse,json
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--skill',type=Path,default=Path.home()/'.codex/skills/qianwen-model-suite');p.add_argument('--models',type=Path);a=p.parse_args()
source=json.loads((a.skill/'references/capabilities.json').read_text(encoding='utf8'))
profiles={}
for name,item in source['capabilities'].items():
    profile={k:item[k] for k in ('name','purpose','transport','models','path','method','async','protocol','sources') if k in item}
    template=a.skill/item.get('template',f'templates/{name}.json')
    if template.exists():profile['example']=json.loads(template.read_text(encoding='utf8')).get('request',{})
    profile['verification']='not_live_tested'
    profiles[name]=profile
# Current official protocol endpoints, using the same account origin.
profiles['realtime-omni'].update(transport='websocket',path='/api-ws/v1/realtime',example={
    'model':'qwen3.5-omni-flash-realtime','events':[
        {'wait':'session.created'},
        {'send':{'type':'session.update','session':{'modalities':['text'],'instructions':'用一句话回答。','turn_detection':None}}},
        {'wait':'session.updated'},
        {'send':{'type':'conversation.item.create','item':{'type':'message','role':'user','content':[{'type':'input_text','text':'你好'}]}}},
        {'send':{'type':'response.create'}},{'wait':'response.done'}]})
profiles['realtime-asr'].update(transport='websocket',path='/api-ws/v1/inference',
    protocol='Ordered events: send run-task JSON, wait task-started, binary_url authorized mono WAV/PCM, send finish-task, wait task-finished. For qwen3-asr realtime override path=/api-ws/v1/realtime and use its session/input_audio_buffer events.')
profiles['world'].update(transport='http',method='POST',
    path='/api/v2/apps/happyoyster-1.0-adventure/openapi/v1/worlds',
    protocol='World creation/status HTTP API. The browser/WebRTC world player is not a P4 renderer.',
    sources=['https://platform.qianwenai.com/docs/api-reference/world-model/adventure/create-world'])
out=Path(__file__).resolve().parents[1]/'components/p4_board/qianwen_catalog.json'
catalog={'source_version':source['version'],'source_verified_on':source['verified_on'],'protocol_reviewed_on':'2026-11-02','capabilities':profiles}
if a.models:
    live=json.loads(a.models.read_text(encoding='utf8'))
    assert live['http_status']==200
    catalog['models']={'retrieved_utc':live['tested_utc'],'ids':[m['id'] for m in live['response']['data']]}
out.write_text(json.dumps(catalog,ensure_ascii=False,separators=(',',':')),encoding='utf8')
print(json.dumps({'profiles':len(profiles),'bytes':out.stat().st_size}))
