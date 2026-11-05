"""Exact C decisions on recorded DEVELOPMENT PCM; never a physical acceptance."""
import argparse,ctypes as C,hashlib,json,wave
from pathlib import Path
import numpy as np
from scipy.signal import butter,sosfilt,correlate,correlation_lags
from wake_host import ROOT,Oracle,build
from wake_prepare import pcm,bounds

def main():
    p=argparse.ArgumentParser();p.add_argument('--models',type=Path,required=True)
    p.add_argument('--capture',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--require-device-parity',action='store_true');p.add_argument('--input-gain',type=int,choices=(1,2,3,4),default=1)
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False);assert not a.require_device_parity or a.input_gain==1
    report=json.loads(a.capture.read_text(encoding='utf8'));assert report['complete'] and 'DEVELOPMENT' in report['scope']
    assert all(t['source']['split']=='validation' for t in report['trials'])
    lib=build(a.models,a.out/'host');oracle=Oracle(lib,268);dll=oracle.lib
    dll.decision_reset.argtypes=[C.c_int];dll.decision_step.argtypes=[C.c_int,C.c_int,C.c_uint64];dll.decision_step.restype=C.c_int
    sos=butter(3,[200,6000],btype='bandpass',fs=16000,output='sos');cached=[]
    def decide(scores,setting):
        q=int(np.floor(np.log(setting/(1000-setting))*256+.5));dll.decision_reset(q)
        return [i for i,row in enumerate(scores) if dll.decision_step(int(row[3]),int(row[4]),32768+(i+1)*512)]
    for t in report['trials']:
        row=t['source'];raw=pcm(t['path']);source=pcm(row['path']);assert hashlib.sha256((ROOT/row['path']).read_bytes()).hexdigest()==row['wav_sha256']
        assert dll.reset(268)==0;score=np.zeros(5,np.int16);zero=np.zeros(512,np.int16)
        for _ in range(64):dll.feed(zero.ctypes.data_as(C.POINTER(C.c_int16)),score.ctypes.data_as(C.POINTER(C.c_int16)))
        scores=[];events=[];scaled=raw.astype(np.int32)*a.input_gain
        clipped=int(np.sum((scaled>32767)|(scaled<-32768)));inference=np.clip(scaled,-32768,32767).astype(np.int16)
        for i,block in enumerate(inference.reshape(-1,512)):
            detected=dll.feed(block.ctypes.data_as(C.POINTER(C.c_int16)),score.ctypes.data_as(C.POINTER(C.c_int16)));assert detected>=0
            scores.append(score.copy())
            if detected:events.append(i)
        scores=np.asarray(scores);assert decide(scores,740)==events,'Decision-only replay differs from complete C inference'
        frames=list(map(json.loads,(ROOT/t['path']).with_name('frames.jsonl').read_text().splitlines()))
        device=np.array([f['heads_q8']+f['scores_q8'] for f in frames]);exact=np.array_equal(scores,device)
        if a.require_device_parity:
            assert exact,(row['clip_id'],np.abs(scores.astype(int)-device).max())
            assert events==[f['sequence'] for f in frames if f['flags']&1]
        r=sosfilt(sos,raw.astype(float));s=sosfilt(sos,source.astype(float));cross=correlate(r,s,mode='full',method='fft');lags=correlation_lags(len(r),len(s))
        valid=(lags>=50000)&(lags<=64000)&(lags+len(s)<=len(r));indices=np.flatnonzero(valid);lag=int(lags[indices[np.argmax(abs(cross[valid]))]])
        quality=float(abs(np.dot(r[lag:lag+len(s)],s))/np.sqrt(np.dot(r[lag:lag+len(s)],r[lag:lag+len(s)])*np.dot(s,s)))
        lo,hi=bounds(source,maximum_samples=64000)
        entry={'id':row['clip_id'],'label':row['label'],'group':row['wake_group'],'condition':row['condition'],'lag':lag,'correlation':quality,
               'accept_bounds':[lag+lo-1024,lag+hi+6400],'device_scores_exact':bool(exact),'input_clipped_samples':clipped,'capture_sha256':hashlib.sha256((ROOT/t['path']).read_bytes()).hexdigest()}
        np.savez_compressed(a.out/(row['clip_id']+'.npz'),scores=scores,device=device)
        cached.append((entry,scores));print(json.dumps({'clip':entry['id'],'exact':bool(exact),'correlation':quality}),flush=True)
    result={'scope':'DEVELOPMENT replay of physical microphone captures, not ordinary firmware acceptance','complete':False,'trials':[r for r,s in cached],
            'capture_report_sha256':hashlib.sha256(a.capture.read_bytes()).hexdigest(),'thresholds':{},'device_parity_required':a.require_device_parity,'input_gain':a.input_gain}
    for setting in range(500,951,10):
        groups={};out=[]
        for row,scores in cached:
            event=[(i+1)*512 for i in decide(scores,setting)];lo,hi=row['accept_bounds']
            hit=bool(row['label']) and any(lo<=t<=hi for t in event);false=any(not row['label'] or not lo<=t<=hi for t in event)
            g=groups.setdefault(row['group'],{'count':0,'hits':0,'false_events':0});g['count']+=1;g['hits']+=int(hit);g['false_events']+=int(false)
            out.append({'id':row['id'],'hit':hit,'false':false,'events':event})
        passed=all((g['hits']/g['count']>=.95 if key!='negative' else True) and g['false_events']==0 for key,g in groups.items())
        result['thresholds'][str(setting)]={'groups':groups,'passed':passed,'rows':out}
    result['complete']=True;result['passing_settings']=[int(k) for k,v in result['thresholds'].items() if v['passed']]
    (a.out/'report.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8');print(json.dumps({'passing_settings':result['passing_settings']}))

if __name__=='__main__':main()
