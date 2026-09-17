"""Bounded baseline firmware USB checks. Never provisions, resets, flashes or actively captures. Wake listening may naturally trigger; no acoustic quality verdict."""
import argparse
import json
from pathlib import Path
import time
from serial_link import connect

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--port',default='COM5')
    parser.add_argument('--cloud',action='store_true')
    parser.add_argument('--cloud-only',action='store_true')
    args=parser.parse_args()
    args.cloud=args.cloud or args.cloud_only
    if args.output.exists():parser.error('Output must be new')
    args.output.mkdir(parents=True)
    report=dict(complete=False,started=time.time(),commands=[],cloud=args.cloud)
    def save():
        (args.output/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    save()
    with connect(args.port) as link:
        def command(text,query=False,expected=None,chat=False,required_tools=()):
            row=dict(command=text,started=time.time(),lines=[],objects=[],status_objects=[],tools=[])
            report['commands'].append(row);save()
            raw=args.output/('%03d.raw'%len(report['commands']))
            buffer=b'';deadline=time.monotonic()+(180 if chat else 20)
            next_status=time.monotonic()+2
            with raw.open('xb') as log:
                try:
                    link.write((text+'\n').encode())
                    while time.monotonic()<deadline:
                        data=link.read(4096)
                        if data:log.write(data);log.flush();buffer+=data
                        while b'\n' in buffer:
                            line,buffer=buffer.split(b'\n',1)
                            line=line.decode('utf8','replace').strip()
                            row['lines'].append(line)
                            if chat:
                                at=line.find('{"firmware"')
                                if at>=0:
                                    try:
                                        status,end=json.JSONDecoder().raw_decode(line[at:])
                                        row['status_objects'].append(status)
                                        line=line[:at]+line[at+end:]
                                    except ValueError:
                                        pass
                                if line.startswith('@tool '):
                                    name,body=line[6:].split(' ',1)
                                    try:payload=json.loads(body)
                                    except ValueError:payload=None
                                    row['tools'].append(dict(name=name,payload=payload,raw=line))

                            try:obj=json.loads(line)
                            except (ValueError,TypeError):obj=None
                            if isinstance(obj,dict):
                                field='status_objects' if chat and 'firmware' in obj else 'objects'
                                row[field].append(obj)
                            save()
                            terminal=line.startswith('@error') or line.startswith('@done') or (not chat and line.startswith('@ok')) or (query and isinstance(obj,dict))
                            if terminal:
                                row['terminal']=line;row['ended']=time.time();save()
                                if expected:assert expected in line,(text,line)
                                else:assert not line.startswith('@error'),(text,line)
                                if chat:
                                    for tool in row['tools']:
                                        assert isinstance(tool['payload'],dict) and tool['payload'].get('error') in (None,'','ok'),tool
                                    for name in required_tools:
                                        assert any(t['name']==name and isinstance(t['payload'],dict) and t['payload'].get('error') in (None,'','ok') for t in row['tools']),name
                                return row['objects'][-1] if row['objects'] else row
                        if chat and time.monotonic()>=next_status:
                            link.write(b'agent status\n');next_status=time.monotonic()+5
                    raise TimeoutError('Command deadline exceeded')
                except BaseException as error:
                    row['error']=repr(error);row['ended']=time.time();save()
                    if chat:link.write(b'agent cancel\n')
                    raise
        def poll(text,predicate,seconds=15):
            end=time.monotonic()+seconds
            while time.monotonic()<end:
                value=command(text,query=True)
                if predicate(value):return value
                time.sleep(.15)
            raise TimeoutError('State did not settle: '+text)
        def gpio(pin,**state):
            return command('agent gpio '+('set ' if state else 'get ')+json.dumps(dict(pin=pin,**state)))
        initial_audio=initial_light=None
        try:
            report['before']=command('agent status',query=True)
            report['context_before']=command('agent context stats',query=True)
            initial_audio=command('agent audio status',query=True)
            initial_light=command('agent light get',query=True)
            assert report['before']['version']=='0.6.0-upgrade' and not report['before']['busy']
            command('agent wake off');command('agent mic off')
            if not args.cloud_only:
                command('agent control capabilities',query=True)
                gpio(10,mode='input')
                for value in (1,0):
                    gpio(10,mode='output',value=value)
                    assert gpio(10)['value']==value
                assert gpio(10,mode='pwm',hz=1000,duty=500)['mode']=='pwm'
                pwm=gpio(10);assert pwm['hz']==1000 and pwm['duty']==500
                gpio(10,mode='input');gpio(0)
                for pin in (9,18):
                    before=gpio(10)
                    command('agent gpio set '+json.dumps(dict(pin=pin,mode='output',value=1)),expected='forbidden')
                    assert gpio(10)==before
                plan=dict(steps=[dict(op='gpio_write',pin=10,value=1),dict(op='light',r=3,g=2,b=1),dict(op='wait',ms=5000)])
                command('agent control run '+json.dumps(plan))
                poll('agent control status',lambda x:x.get('active',False))
                command('agent gpio get {"pin":10}',expected='busy')
                command('agent cancel')
                poll('agent control status',lambda x:not x.get('active',False))
                assert gpio(10)['mode']=='input'
                assert command('agent light get',query=True)==initial_light
                command('agent mic on')
                poll('agent audio status',lambda x:x.get('mic_enabled',False))
                gpio(10,mode='output',value=1);assert gpio(10)['value']==1
                gpio(10,mode='input');command('agent mic off')
                poll('agent audio status',lambda x:not x.get('mic_enabled',False))
                command('agent audio volume 40')
                before_play=command('agent audio status',query=True)
                command('agent audio play '+json.dumps(dict(bpm=120,wave='sine',notes=[[60,1]])))
                played=poll('agent audio status',lambda x:not x.get('playing',False))
                assert played['play_error']=='ok' and played['job']>before_play['job']
                command('agent audio volume '+str(initial_audio['volume']))
                command('agent wake on')
                wake=poll('agent wake status',lambda x:x.get('enabled',False) and x.get('state')=='listening' and x.get('error')=='ok')
                assert wake['model']=='wn9s_hilexin' and 'keyword' in wake and 'verify' in wake
                time.sleep(.3);command('agent wake off')
                poll('agent wake status',lambda x:not x.get('enabled',False))
            if args.cloud:
                command('agent chat 请先查询设备能力，只操作GPIO10：设高、读取确认，再恢复input。不要操作其他引脚或音频。',chat=True,required_tools=('device_gpio_set','device_gpio_get'))
                assert gpio(10)['mode']=='input'
                command('agent chat --no-stream 请查询能力，提交仅GPIO10输出高和灯光3,2,1、等待100毫秒的短定时计划，然后查询计划状态。不要操作其他引脚或音频。',chat=True,required_tools=('device_control_run',))
                poll('agent control status',lambda x:not x.get('active',False))
                assert gpio(10)['mode']=='input'
            report['complete']=True
        except BaseException as error:
            report['error']=repr(error);save();raise
        finally:
            report['cleanup_errors']=[]
            for text in ['agent cancel','agent wake off','agent mic off','agent audio stop','agent gpio set {"pin":10,"mode":"input"}']:
                try:command(text)
                except BaseException as error:report['cleanup_errors'].append(dict(command=text,error=repr(error)))
            if initial_audio is not None:
                try:command('agent audio volume '+str(initial_audio['volume']))
                except BaseException as error:report['cleanup_errors'].append(dict(error=repr(error)))
            if initial_light is not None:
                try:command('agent light set {r} {g} {b}'.format(**initial_light))
                except BaseException as error:report['cleanup_errors'].append(dict(error=repr(error)))
            try:
                report['context_after']=command('agent context stats',query=True)
                if not args.cloud:
                    for key in ('mode','events','pending','used','bank_size','generation','lamport','cursor','acked','last_local','history_budget','partition_bytes'):
                        assert report['context_after'][key]==report['context_before'][key],key
            except BaseException as error:report['cleanup_errors'].append(dict(error=repr(error)))
            try:report['after']=command('agent status',query=True)
            except BaseException as error:report['cleanup_errors'].append(dict(error=repr(error)))
            report['ended']=time.time()
            if report['cleanup_errors']:report['complete']=False
            save()
if __name__=='__main__':main()
