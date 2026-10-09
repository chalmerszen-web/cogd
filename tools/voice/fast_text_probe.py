"""Three authorized host-only TTFT observations; never execute model actions."""
import json
import time
from pathlib import Path
import requests
from qianwen_credentials import qianwen_key

ENDPOINT = 'https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions'
CASES = [('greeting', '你好'), ('arithmetic', '1+2等于几？'), ('complex', '先读取GPIO10，若为低则设为高并确认，再联网查天气，结合温湿度分析是否需要开风扇；说明步骤和需要确认的信息。')]

def main():
    out = Path('artifacts/voice-fast/text-probe-01')
    out.mkdir(parents=True, exist_ok=False)
    report = dict(scope='Host text only; not device speech latency', endpoint=ENDPOINT,
                  requests_version=requests.__version__, retries=0, cases=[])
    def save():
        (out/'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
    save()
    key = qianwen_key()
    with requests.Session() as session:
        session.mount('https://', requests.adapters.HTTPAdapter(max_retries=0))
        for name, prompt in CASES:
            payload = dict(model='qwen-flash', enable_thinking=False, stream=True, max_tokens=128,
                           messages=[dict(role='system', content='你是简洁的设备助手。不声称已执行操作。'), dict(role='user', content=prompt)])
            row = dict(name=name, request=payload, started_unix=time.time(), http_status=None,
                       first_content_seconds=None, first_content=None, result='', done=False, error=None)
            report['cases'].append(row);save();start=time.perf_counter()
            try:
                with session.post(ENDPOINT, json=payload, headers={'Authorization':'Bearer '+key},
                                  stream=True, timeout=(15,45), allow_redirects=False) as response:
                    row['http_status']=response.status_code
                    row['response_headers_seconds']=time.perf_counter()-start
                    if response.status_code!=200:
                        row['error']=response.text[:4096].replace(key,'[REDACTED]')
                    else:
                        for line in response.iter_lines(chunk_size=1):
                            if not line.startswith(b'data:'):continue
                            data=line[5:].strip()
                            if data==b'[DONE]':row['done']=True;break
                            event=json.loads(data)
                            if 'error' in event:row['error']=json.dumps(event['error'],ensure_ascii=False).replace(key,'[REDACTED]')
                            for choice in event.get('choices',[]):
                                content=choice.get('delta',{}).get('content')
                                if content:
                                    if row['first_content_seconds'] is None:
                                        row['first_content_seconds']=time.perf_counter()-start
                                        row['first_content']=content
                                    row['result']+=content
                                if choice.get('finish_reason'):row['finish_reason']=choice['finish_reason']
            except Exception as exc:
                row['error']=(type(exc).__name__+': '+str(exc)).replace(key,'[REDACTED]')
            row['total_seconds']=time.perf_counter()-start;save()
    report['complete']=True;save()
    print(json.dumps([dict(name=r['name'],status=r['http_status'],ttft=r['first_content_seconds'],total=r['total_seconds'],done=r['done'],error=bool(r['error'])) for r in report['cases']]))

if __name__=='__main__':main()
