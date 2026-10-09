"""Fixed TRAIN capture/source comparison; diagnostic F0 reference is not truth."""
import hashlib
import json
from pathlib import Path
import time
import numpy as np
from data import ROOT,framed_example,normalize
from append_captures import capture_path
from device_domain import pcm
from pitch_batch import BatchFrontend


def sha(path):
    with Path(path).open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()


def read(path):return json.loads(Path(path).read_text(encoding='utf8'))


def compare(plan,out):
    began=time.monotonic();out=Path(out);out.mkdir(exist_ok=False)
    for path,expected in plan['hashes'].items():assert sha(ROOT/path)==expected,path
    files=plan['files']
    with np.load(ROOT/files['train'],allow_pickle=False) as f:
        old={key:f[key] for key in ('x','y','label','event_end','clip_id','variant')}
    original_pitch=np.load(ROOT/files['old_pitch'],mmap_mode='r')
    positions={(str(clip),int(variant)):i for i,(clip,variant) in enumerate(zip(old['clip_id'],old['variant']))}
    raw_frontend=BatchFrontend(ROOT/files['raw_library'])
    filtered_frontend=BatchFrontend(ROOT/files['filtered_library'])
    stats=read(ROOT/files['normalization'])
    exact_logmel=exact_original=reference_total=reference_matched=0
    ref_clean_total=ref_clean_matched=0
    rows=[]
    # The frozen device recipe draws TRAIN background even for variant0,
    # then generates variants1..8. Reproduce its RNG advancement exactly.
    backgrounds=[pcm(ROOT/d['recording'])[-16000:] for d in plan['sources']]
    rng=np.random.default_rng(read(ROOT/files['device_metadata'])['seed'])
    for descriptor in plan['sources']:
        assert time.monotonic()-began<45
        cap,parent=descriptor['capture'],descriptor['parent']
        assert cap['split']==parent['split']=='train'
        raw=pcm(ROOT/descriptor['recording'])
        clean=pcm(ROOT/descriptor['source'])
        assert hashlib.sha256(raw.astype('<i2').tobytes()).hexdigest()==cap['pcm_sha256']
        assert hashlib.sha256(clean.astype('<i2').tobytes()).hexdigest()==parent['pcm_sha256']
        lo,hi=cap['crop'];low,high=cap['end_interval']
        signal=raw[lo:hi]
        ambient=backgrounds[int(rng.integers(len(backgrounds)))]
        row=dict(parent,clip_id=cap['clip_id'],endpoint_schema=1,speech_end_low_sample=low,
            speech_end_high_sample=high,sampling_group='positive' if parent['label'] else 'hard_negative')
        framed,y,end=framed_example(row,signal)
        offset=(max(4096,33792-row['speech_end_low_sample'])+len(framed)-len(signal)-1024)//2
        floor=np.resize(ambient,len(framed));framed[:offset]=floor[:offset];framed[offset+len(signal):]=floor[offset+len(signal):]
        for _ in range(8):
            background=backgrounds[int(rng.integers(len(backgrounds)))]
            framed_example(row,signal,rng,background=background)
        index=positions[row['clip_id'],0]
        np.testing.assert_array_equal(y,old['y'][index])
        assert old['label'][index]==row['label'] and old['event_end'][index]==(-1 if end is None else end)
        raw_logmel,raw_pitch=raw_frontend(framed)
        filtered_logmel,filtered_pitch=filtered_frontend(framed)
        np.testing.assert_array_equal(raw_pitch,original_pitch[index])
        np.testing.assert_array_equal(raw_logmel,filtered_logmel)
        np.testing.assert_array_equal(normalize(raw_logmel,stats),old['x'][index])
        exact_logmel+=raw_logmel.size;exact_original+=raw_pitch.size
        # Source is independently rendered at the recorded lag, with no floor.
        # It is a diagnostic matching reference, not a label or pitch annotation.
        clean_aligned=np.zeros(len(raw),np.int16);lag=cap['lag_samples']
        assert 0<=lag and lag+len(clean)<=len(clean_aligned)
        clean_aligned[lag:lag+len(clean)]=clean
        clean_framed,_,_=framed_example(row,clean_aligned[lo:hi])
        _,clean_pitch=raw_frontend(clean_framed)
        _,clean_filtered=filtered_frontend(clean_framed)
        valid_clean=clean_pitch[:,0]>0
        clean_usable=valid_clean & (clean_filtered[:,0]>0)
        clean_agree=clean_usable & (np.abs(clean_filtered[:,0].astype(np.float64)/np.maximum(clean_pitch[:,0],1)-1)<=.20)
        ref_clean_total+=int(valid_clean.sum());ref_clean_matched+=int(clean_agree.sum())
        eligible=valid_clean & (filtered_pitch[:,0]>0)
        agree=eligible & (np.abs(filtered_pitch[:,0].astype(np.float64)/np.maximum(clean_pitch[:,0],1)-1)<=.20)
        reference_total+=int(eligible.sum());reference_matched+=int(agree.sum())
        target_end=int(end) if row['label'] else 65536
        last=min(256,(target_end+255)//256);first=max(0,last-32)
        record=dict(clip_id=row['clip_id'],label=row['label'],language=row['language'],source_group=row['source_group'],
            original_train_index=index,raw_voiced=int(np.count_nonzero(raw_pitch[:,0])),
            filtered_voiced=int(np.count_nonzero(filtered_pitch[:,0])),
            raw_tail_voiced=int(np.count_nonzero(raw_pitch[first:last,0])),
            filtered_tail_voiced=int(np.count_nonzero(filtered_pitch[first:last,0])),
            source_comparable=int(eligible.sum()),source_agree=int(agree.sum()),
            clean_frames=int(valid_clean.sum()),clean_preserved=int(clean_agree.sum()))
        rows.append(record)
    assert len(rows)==44
    positive=[r for r in rows if r['label']]
    negative=[r for r in rows if not r['label']]
    raw_tail=sum(r['raw_tail_voiced'] for r in positive)
    filtered_tail=sum(r['filtered_tail_voiced'] for r in positive)
    gates=dict(improved_positive_tail_voicing=filtered_tail>raw_tail,
        paired_reference_sufficient=reference_total>=64,
        paired_source_F0_agreement=bool(reference_total) and reference_matched/reference_total>=.9,
        source_pitch_preservation=bool(ref_clean_total) and ref_clean_matched/ref_clean_total>=.98)
    result=dict(complete=True,passed=all(gates.values()),gates=gates,rows=44,
        positive_rows=len(positive),negative_rows=len(negative),
        positive_tail=dict(original_voiced_frames=raw_tail,filtered_voiced_frames=filtered_tail),
        source_reference=dict(comparable_frames=reference_total,within20percent=reference_matched,
            agreement=reference_matched/reference_total if reference_total else None),
        clean_reference=dict(frames=ref_clean_total,preserved_within20percent=ref_clean_matched,
            fraction=ref_clean_matched/ref_clean_total if ref_clean_total else None),
        negative_voicing_only=dict(original=sum(r['raw_voiced'] for r in negative),filtered=sum(r['filtered_voiced'] for r in negative),
            caveat='Voicing does not classify wake vs negative; no keyword score or false-wake rate measured.'),
        original_logmel_values_exact=exact_logmel,original_pitch_values_exact=exact_original,
        classifier_scores=0,TRAIN_only=True,not_ground_truth_pitch=True,original_inputs_labels_preserved=True,
        seconds=time.monotonic()-began)
    assert result['seconds']<45
    for path,expected in plan['hashes'].items():assert sha(ROOT/path)==expected,path
    (out/'rows.json').write_text(json.dumps(rows,indent=2)+'\n',encoding='utf8')
    (out/'audit.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
    return result
