"""Compare one E/L candidate to frozen E/K using the deployed C integer code."""
import ctypes as C
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import numpy as np

ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'tools/kws'),str(ROOT/'training/kws')]
from parity import Backend, Trace
from evaluate import metrics, threshold_q8


def scores(features,library):
    members=[Backend(library,symbol) for symbol in ('kws_trained_model','kws_secondary_model')]
    result=[]
    for example in features:
        for member in members:member.lib.kws_reset(member.handle)
        parts=[]
        for frame in example:
            logits=[]
            for member in members:
                trace=Trace()
                value=member.lib.kws_step_features(member.handle,frame.ctypes.data_as(C.POINTER(C.c_int8)),C.byref(trace))
                logits.append(value)
            parts.append(int(sum(logits)/2))
        combined=np.asarray(parts,dtype=np.int16)
        original=combined[1::2].astype(float)
        for i in range(len(original)):combined[2*i+1]=int(original[max(0,i-2):i+1].mean())
        result.append(combined)
    return np.stack(result)


def main():
    base=ROOT/'artifacts/kws-bilingual'
    target=base/'evaluation.json'
    if target.exists():raise ValueError('Preserve completed comparison')
    model=base/'adapt-l-int8/model.c'
    secondary=base/'adapt-l-int8/model_secondary.c'
    secondary.write_text(model.read_text().replace('kws_trained_model=', 'kws_secondary_model='))
    build=ROOT/'build-kws-fusion-el-host'
    with (base/'host-build.log').open('x') as log:
        subprocess.run(['cmake','-S',str(ROOT/'tests/kws'),'-B',str(build),'-G','Ninja',
            '-DKWS_MODEL_SOURCE='+str(ROOT/'components/kws_c11/generated/fusion_primary.c'),
            '-DKWS_SECONDARY_SOURCE='+str(secondary)],check=True,stdout=log,stderr=subprocess.STDOUT)
        subprocess.run(['cmake','--build',str(build)],check=True,stdout=log,stderr=subprocess.STDOUT)
        subprocess.run(['ctest','--test-dir',str(build),'--output-on-failure'],check=True,stdout=log,stderr=subprocess.STDOUT)
    data=np.load(base/'features-compact/validation.npz')
    old=np.load(ROOT/'artifacts/kws-phase5/features-weak-logmel/validation.npz')
    assert all(np.array_equal(data[k][:len(old['x'])],old[k]) for k in old.files)
    threshold=threshold_q8(740)
    report={}
    for name,library in [('ek',ROOT/'build-kws-fusion-ek-host/libkws.so'),('el',build/'libkws.so')]:
        values=scores(data['x'],library)
        if name=='ek':
            saved=np.load(ROOT/'artifacts/kws-phase5/fusion-ek-smooth3/validation-scores.npz')
            np.testing.assert_array_equal(values[:len(old['x'])],saved['quantized'])
        subsets={'old':slice(0,len(old['x'])),'compact':slice(len(old['x']),None)}
        report[name]={group:metrics(values[at],{k:data[k][at] for k in data.files},threshold) for group,at in subsets.items()}
        report[name]['library_sha256']=hashlib.sha256(library.read_bytes()).hexdigest()
        np.savez_compressed(base/(name+'-scores.npz'),quantized=values,clip_id=data['clip_id'])
    before,after=report['ek']['old'],report['el']['old']
    gates=dict(old_language_nonregression=all(after['languages'][lang]['hits']>=before['languages'][lang]['hits'] for lang in ('zh','yue')),
        device_language_nonregression=all(after['device']['languages'][lang]['hits']>=before['device']['languages'][lang]['hits'] for lang in ('zh','yue')),
        old_false_nonregression=after['negative_triggered_clips']<=before['negative_triggered_clips'],
        device_false_nonregression=after['device']['negative_triggered_clips']<=before['device']['negative_triggered_clips'],
        partial_nonregression=after['partial_word_triggers']<=before['partial_word_triggers'],
        timing_nonregression=after['premature_events']<=before['premature_events'],
        compact_nonregression=report['el']['compact']['languages']['zh']['hits']>=report['ek']['compact']['languages']['zh']['hits'])
    report.update(complete=True,gates=gates,host_nonregression=all(gates.values()),threshold_per_mille=740,
        threshold_retuned=False,source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        data_sha256=hashlib.sha256((base/'features-compact/validation.npz').read_bytes()).hexdigest(),
        limitation='Development source groups; no new independent human acceptance. SAPI excluded from training and checkpoint selection.')
    target.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(gates=gates,old={name:{'zh':report[name]['old']['languages']['zh']['hits'],
        'yue':report[name]['old']['languages']['yue']['hits'],'negative':report[name]['old']['negative_triggered_clips'],
        'compact':report[name]['compact']['languages']['zh']['hits']} for name in ('ek','el')})))


if __name__=='__main__':main()
