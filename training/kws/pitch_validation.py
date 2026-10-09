"""Exact fixed DEV PCM reconstruction; no source selection or fit."""
import hashlib
import json
from pathlib import Path
import time
import wave
import numpy as np
from data import ROOT, framed_example, normalize
from pitch_batch import BatchFrontend
from paired_negative import incomplete

def sha(path):
    with Path(path).open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()

def prepare(plan, out):
    start=time.monotonic();out=Path(out);out.mkdir(exist_ok=False)
    for name,digest in plan['hashes'].items():assert sha(ROOT/name)==digest,name
    with np.load(ROOT/plan['files']['features'],allow_pickle=False) as source:
        old={key:source[key] for key in source.files}
    assert old['x'].shape==(730,256,40) and not old['variant'].any()
    frontend=BatchFrontend(ROOT/plan['files']['batch_library'])
    stats=json.loads((ROOT/plan['files']['normalization']).read_text(encoding='utf8'))
    pitch=np.zeros((730,256,2),np.int16);audit=[];cache={};counts={}
    for index,descriptor in enumerate(plan['sources']):
        assert time.monotonic()-start<60
        row=descriptor['row'];path=ROOT/descriptor['path'];kind=descriptor['kind']
        assert row['split']=='validation' and descriptor['index']==index
        assert old['clip_id'][index]==row['clip_id'] and old['source_group'][index]==row['source_group']
        assert old['label'][index]==row['label'] and old['language'][index]==row['language']
        if str(path) not in cache:
            with wave.open(str(path),'rb') as source:
                assert (source.getnchannels(),source.getsampwidth(),source.getframerate())==(1,2,16000)
                raw=source.readframes(source.getnframes())
            assert hashlib.sha256(raw).hexdigest()==descriptor['pcm_sha256']
            cache[str(path)]=np.frombuffer(raw,dtype='<i2').astype(np.int16,copy=True)
        full=cache[str(path)]
        ambient=full[-16000:] if descriptor.get('captured') else None
        signal=full[slice(*descriptor['crop'])] if 'crop' in descriptor else full
        if kind=='ambient':
            signal=np.resize(ambient,65536).astype(np.int16)
            pcm=signal;y=np.zeros(256,np.float32);end=None
        else:
            if 'side' in descriptor:
                signal,_=incomplete(signal,row['speech_start_sample'],row['speech_end_low_sample'],
                                    descriptor['side'],.5,ambient)
            pcm,y,end=framed_example(row,signal)
            if descriptor.get('captured'):
                offset=(max(4096,33792-row['speech_end_low_sample'])+len(pcm)-len(signal)-1024)//2
                floor=np.resize(ambient,len(pcm));pcm[:offset]=floor[:offset];pcm[offset+len(signal):]=floor[offset+len(signal):]
        np.testing.assert_array_equal(y,old['y'][index])
        assert (-1 if end is None else end)==old['event_end'][index]
        logmel,value=frontend(pcm)
        np.testing.assert_array_equal(normalize(logmel,stats),old['x'][index])
        pitch[index]=value;counts[kind]=counts.get(kind,0)+1
        audit.append(dict(index=index,clip_id=row['clip_id'],kind=kind,
                          PCM_sha256=hashlib.sha256(pcm.astype('<i2').tobytes()).hexdigest()))
    assert len(audit)==730
    np.save(out/'pitch.npy',pitch)
    for name,digest in plan['hashes'].items():assert sha(ROOT/name)==digest,name
    result=dict(complete=True,rows=730,old_C_values_exact=730*256*40,pitch_values=pitch.size,
                counts=counts,original_inputs_labels_metadata_preserved=True,
                original_all_DEV_rows_no_selection=True,no_TRAIN_TEST_waveform=True,
                no_model_fit_score_or_threshold=True,pitch_sha256=sha(out/'pitch.npy'),seconds=time.monotonic()-start)
    (out/'audit.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
    (out/'row-provenance.json').write_text(json.dumps(audit,separators=(',',':'))+'\n',encoding='utf8')
    return result
