"""Check integer parity and report the feature manifest's evaluation scope.

After development data is retired into TRAIN this is a training monitor,
not an independent recall estimate.
"""
import argparse,ctypes as C,hashlib,json,sys,time
from pathlib import Path
import numpy as np
from wake_host import ROOT,Oracle,build,Head96
sys.path[:0]=[str(ROOT/'training/kws'),str(ROOT/'tools/kws')]
from quantize import infer,torch_integer_oracle
from parity import Backend

def main():
    p=argparse.ArgumentParser();p.add_argument('--models',type=Path,required=True);p.add_argument('--features',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--thresholds',type=int,nargs='+',default=[740,780,820]);p.add_argument('--parity-only',action='store_true');a=p.parse_args()
    assert all(500<=v<=950 for v in a.thresholds)
    a.models=a.models.resolve();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=False)
    library=build(a.models,a.out/'host');rng=np.random.default_rng(2026110102);parity=[]
    random=rng.integers(-128,128,(300,40),dtype=np.int8);random[:4]=np.array([0,127,-128,1],dtype=np.int8)[:,None]
    for name,symbol in [('first','kws_verified_primary'),('second','kws_verified_secondary'),('verifier','kws_trained_model')]:
        model=json.loads((a.models/(name+'.json')).read_text());expected=infer(random,model)
        np.testing.assert_array_equal(expected,torch_integer_oracle(random,model))
        backend=Head96(library,symbol) if json.loads((a.models/'runtime.json').read_text()).get('private_kernel_dir') else Backend(library,symbol,48);actual=[]
        for frame in random:
            trace=backend.trace_type();backend.lib.kws_step_features(backend.handle,frame.ctypes.data_as(C.POINTER(C.c_int8)),C.byref(trace))
            actual.append(list(trace.layer)[:expected.shape[1]])
        np.testing.assert_array_equal(expected,np.asarray(actual))
        parity.append({'head':name,'frames':len(random),'integer_values':int(expected.size),'exact':True})
    if a.parity_only:
        report={'complete':True,'scope':'integer parity only; no acoustic acceptance or model selection','parity':parity}
        (a.out/'integer-parity.json').write_text(json.dumps(report,indent=2),encoding='utf8');print(json.dumps(report));return
    data=dict(np.load(a.features/'validation.npz',allow_pickle=False));report={'complete':False,'parity':parity,'thresholds':{}}
    metadata=json.loads((a.features/'features.json').read_text(encoding='utf8'))
    report['evaluation_scope']='training_monitor' if metadata.get('development_reused_for_training') else 'source_disjoint_development'
    report['round1_test_reused_in_training_monitor']=bool(metadata.get('round1_test_reused_for_training'))
    began=time.monotonic()
    for setting in a.thresholds:
        q=int(np.floor(np.log(setting/(1000-setting))*256+.5));oracle=Oracle(library,q);groups={};rows=[]
        for i,x in enumerate(data['x']):
            scores,events=oracle.features(x);end=int(data['event_end'][i]);low=int(max(data['event_start_accept'][i],end-1280));high=end+6400
            hit=bool(data['label'][i]) and any(low<=t<=high for t in events)
            false=any(not data['label'][i] or not low<=t<=high for t in events)
            row={'clip_id':str(data['clip_id'][i]),'group':str(data['wake_group'][i]),'p4':bool(data['p4_domain'][i]),'label':int(data['label'][i]),'hit':hit,'false_event':false,'events':events}
            rows.append(row)
            for group in [row['group']]+(['p4_'+row['group']] if row['p4'] else []):
                g=groups.setdefault(group,{'clips':0,'hits':0,'false_events':0});g['clips']+=1;g['hits']+=int(hit);g['false_events']+=int(false)
        report['thresholds'][str(setting)]={'q8':q,'groups':groups,'rows':rows}
        (a.out/'validation.json').write_text(json.dumps(report,indent=2),encoding='utf8')
        print(json.dumps({'threshold':setting,'groups':groups}),flush=True)
    report.update(complete=True,seconds=time.monotonic()-began,workspace_host_bytes=oracle.lib.workspace(),validation_sha256=hashlib.sha256((a.features/'validation.npz').read_bytes()).hexdigest(),no_test_audio_read=True)
    (a.out/'validation.json').write_text(json.dumps(report,indent=2),encoding='utf8')

if __name__=='__main__':main()
