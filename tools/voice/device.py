"""USB voice diagnostics. Secrets are never included in command records."""
import json
from pathlib import Path
import sys
import time

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from serial_link import connect


class Device:
    QUERY_FIELDS={'agent status':'firmware','agent wifi status':'firmware',
                  'agent audio status':'audio_ready','agent mic status':'audio_ready',
                  'agent wake status':'state','agent voice status':'voice_enabled',
                  'agent context stats':'history_budget','agent context summary':'present','agent light get':'r',
                  'agent kws profile':'trained','agent voice noise-clock':'noise_clock_ms'}
    def __init__(self, out, port='COM5'):
        self.out=Path(out); self.out.mkdir(parents=True,exist_ok=True)
        self.link=connect(port);self.buffer=b'';self.rows=[];self.unread=[];self.events=[]
        self.raw=(self.out/'serial.log').open('ab')

    def close(self):
        self.link.close();self.raw.close()

    def lines(self):
        data=self.link.read(4096)
        if data:self.raw.write(data);self.raw.flush();self.buffer+=data
        rows=self.unread;self.unread=[]
        while b'\n' in self.buffer:
            line,self.buffer=self.buffer.split(b'\n',1)
            text=line.decode('utf8','replace').strip();rows.append(text)
            if text.startswith('@voice '):
                try:event=json.loads(text[7:])
                except ValueError:continue
                event['observed']=time.monotonic()
                if not hasattr(self,'events'):self.events=[]
                self.events.append(event)
        return rows

    def send(self, text, secret=False):
        self.link.write((text+'\n').encode())
        self.rows.append(dict(command='[credential provision]' if secret else text, started=time.time()))

    def command(self,text,query=False,timeout=30,secret=False):
        self.send(text,secret); row=self.rows[-1];row['lines']=[]
        field=self.QUERY_FIELDS.get(text)
        if text.startswith('agent audio clip read '):field='offset'
        end=time.monotonic()+timeout
        while time.monotonic()<end:
            lines=self.lines()
            for index,line in enumerate(lines):
                row['lines'].append(line)
                if line.startswith('@error'):
                    if line=='@error cancelled' and text in ('agent cancel','agent voice off'):
                        row['cancelled_job']=True;continue
                    row['error']=line;raise RuntimeError(line)
                try:value=json.loads(line)
                except ValueError:value=None
                matches=query and isinstance(value,dict) and (field is None or field in value)
                if matches or (not query and (line.startswith('@ok') or line.startswith('@done'))):
                    self.unread.extend(lines[index+1:])
                    row['finished']=time.time();return value if query else row
        row['timeout']=True;raise TimeoutError('USB command timed out')

    def save(self,name='commands.json'):
        (self.out/name).write_text(json.dumps(self.rows,ensure_ascii=False,indent=2),encoding='utf8')
