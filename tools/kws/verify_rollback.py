"""Verify the restored usable firmware and preserved data after probe testing."""
import json
from pathlib import Path
import time
from serial_test import Link,ROOT

def main():
    out=ROOT/'artifacts/kws-phase1';link=Link('COM5',out/'rollback-state-serial.log')
    try:
        link.command('agent audio volume 80');link.command('agent mic on');link.command('agent wake on')
        deadline=time.monotonic()+20
        while time.monotonic()<deadline:
            wake=link.command('agent wake status');status=link.command('agent status')
            if wake['state']=='listening' and status['wifi'] and status['time_valid']:break
            time.sleep(.25)
        else:raise AssertionError('Restored device did not become ready')
        assert status['version']=='0.6.3-context' and status['key_configured'] and status['nvs']
        assert wake['model']=='wn9s_hilexin' and wake['error']=='ok' and 'keyword' in wake
        context=link.command('agent context stats');audio=link.command('agent audio status')
        before=json.loads((out/'device-probe-final.json').read_text(encoding='utf-8'))
        row=next(r['response'] for r in before if r['command']=='agent context stats')
        previous=json.JSONDecoder().raw_decode(row[row.index('{'):])[0]
        for key in ('events','used','generation','lamport','cursor','acked','last_local','history_budget','partition_bytes','next_record'):
            assert context[key]==previous[key],key
        assert audio['volume']==80 and audio['clip_ready'] and audio['clip_ms']==2000 and audio['mic_enabled']
        report=dict(complete=True,status=status,wake=wake,context=context,audio=audio,
            context_preserved=True,test_clip_preserved=True,serial_closed_on_exit=True)
        (out/'rollback-state.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        print(json.dumps(dict(complete=True,version=status['version'],model=wake['model'],context_events=context['events'],clip_ms=audio['clip_ms']),ensure_ascii=False))
    finally:link.close()

if __name__=='__main__':main()
