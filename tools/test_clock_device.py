"""Bounded clock checks; never treats SPI status as human visual acceptance."""
import argparse
import hashlib
from datetime import datetime, timezone, timedelta
import json
from pathlib import Path
import shutil
import time
from serial_link import connect

def main():
    p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True)
    p.add_argument('--port',default='COM5');p.add_argument('--reboot',action='store_true')
    p.add_argument('--version',default='0.12.4-clock')
    p.add_argument('--require-held-dc',action='store_true')
    p.add_argument('--camera',type=Path,help='Fresh JPEG updated by an existing bounded camera capture')
    a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
    report={'passed':False,'visual_verified':False,'commands':[],'samples':[]}
    link=connect(a.port);raw=(a.output/'serial.log').open('wb')
    def save():
        (a.output/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    def photo(name):
        if not a.camera:return
        began=time.time();deadline=time.monotonic()+6
        while a.camera.stat().st_mtime<=began:
            if time.monotonic()>=deadline:raise TimeoutError('Camera did not produce a fresh frame')
            time.sleep(.1)
        dest=a.output/(name+'.jpg');shutil.copyfile(a.camera,dest)
        report.setdefault('camera_captures',[]).append({'file':dest.name,
            'sha256':hashlib.sha256(dest.read_bytes()).hexdigest(),
            'captured_at':datetime.now(timezone(timedelta(hours=8))).isoformat()})
        save()
    def query(command,field):
        row={'command':command,'started_at':datetime.now(timezone(timedelta(hours=8))).isoformat()}
        report['commands'].append(row);link.write((command+'\n').encode())
        deadline=time.monotonic()+8
        while time.monotonic()<deadline:
            line=link.readline();raw.write(line);raw.flush()
            try:value=json.loads(line.decode('utf8'))
            except (ValueError,UnicodeDecodeError):continue
            if isinstance(value,dict) and field in value:
                row['response']=value;save();return value
        raise TimeoutError('No matching response to '+command)
    def wait_clock():
        deadline=time.monotonic()+50
        while time.monotonic()<deadline:
            status=query('agent display status','layout')
            assert status['mode']=='clock' and status['error']=='ok',status
            if status['ready'] and status['time_valid'] and status['frames']>0:return status
            time.sleep(1)
        raise TimeoutError('Clock did not initialize and synchronize')
    try:
        before=query('agent status','firmware');report['before']=before
        assert before['version']==a.version,before
        report['context_before']=query('agent context stats','history_budget')
        report['automatic_boot_clock']=wait_clock()
        photo('boot')
        for index in range(6):
            status=query('agent display status','layout')
            assert status['ready'] and status['time_valid'] and status['hour_format']==24
            assert status['layout']=='hhmm_large_ss_bottom_right'
            assert status['width']==160 and status['height']==80 and status['sdk_error']==0
            if a.require_held_dc:
                assert status['transport']=='esp_lcd_spi_held_dc'
                assert status['dc_output_enabled'] and status['dc_level']==1,status
            value=time.strptime(status['time'],'%H:%M:%S')
            actual=value.tm_hour*3600+value.tm_min*60+value.tm_sec
            expected=int(time.time()+480*60)%86400
            delta=min((actual-expected)%86400,(expected-actual)%86400)
            assert delta<=3,(status['time'],expected,delta)
            report['samples'].append({'time':status['time'],'frames':status['frames'],'host_delta_seconds':delta})
            save();photo('sample-'+str(index));time.sleep(1.1)
        assert len({s['time'] for s in report['samples']})==6
        assert report['samples'][-1]['frames']-report['samples'][0]['frames']>=5
        if a.reboot:
            # No display-set command: prove the build activates it by itself.
            state=query('agent status','firmware');assert not state['busy']
            link.write(b'agent reboot\n');deadline=time.monotonic()+5;ack=False
            while time.monotonic()<deadline:
                line=link.readline();raw.write(line)
                if b'@reboot' in line:ack=True;break
            assert ack,'No reboot acknowledgement'
            link.close();time.sleep(3);link=connect(a.port)
            report['after_reboot']=query('agent status','firmware')
            assert report['after_reboot']['version']==a.version
            report['reboot_clock']=wait_clock()
            photo('reboot')
        report['context_after']=query('agent context stats','history_budget')
        report['wake_after']=query('agent wake status','state')
        report['after']=query('agent status','firmware')
        report['passed']=True
    except Exception as e:
        report['error']=repr(e);raise
    finally:
        link.close();raw.close();report['serial_closed']=True;save()
        print(json.dumps({k:v for k,v in report.items() if k not in ('commands','context_before','context_after','wake_after')},ensure_ascii=False,indent=2))

if __name__=='__main__':main()
