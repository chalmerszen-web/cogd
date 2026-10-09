"""Finite weak-candidate study; exact legacy reconstruction precedes statistics."""
import hashlib
import json
from pathlib import Path
import time
import numpy as np
from data import ROOT,framed_example,normalize
from device_domain import pcm
from pitch_batch import BatchFrontend
from pitch_curve import cmnd,legacy,weakest_minimum


def sha(path):
    with Path(path).open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()
def read(path):return json.loads(Path(path).read_text(encoding='utf8'))


def compare(plan,out):
    start=time.monotonic();out=Path(out);out.mkdir(exist_ok=False)
    for path,expected in plan['hashes'].items():assert sha(ROOT/path)==expected,path
    files=plan['files']
    with np.load(ROOT/files['train'],allow_pickle=False) as f:
        old={k:f[k] for k in ('x','y','label','event_end','clip_id','variant')}
    positions={(str(c),int(v)):i for i,(c,v) in enumerate(zip(old['clip_id'],old['variant']))}
    previous=np.load(ROOT/files['old_pitch'],mmap_mode='r')
    frontend=BatchFrontend(ROOT/files['raw_library'])
    stats=read(ROOT/files['normalization'])
    backgrounds=[pcm(ROOT/d['recording'])[-16000:] for d in plan['sources']]
    rng=np.random.default_rng(read(ROOT/files['device_metadata'])['seed'])
    rows=[];values=clean_values=0
    matched_confidence=[];missed_confidence=[];weak_count=matched_count=0
    legacy_positive_agree=legacy_positive_count=0
    positive_tail_weak=positive_tail_matched=0
    all_weak=np.zeros((44,256,2),np.int16)
    curve_diagnostics=np.zeros((44,256,101),np.int32)
    for number,descriptor in enumerate(plan['sources']):
        assert time.monotonic()-start<plan['maximum_seconds']
        cap,parent=descriptor['capture'],descriptor['parent']
        assert cap['split']==parent['split']=='train'
        raw,clean=(pcm(ROOT/descriptor[k]) for k in ('recording','source'))
        assert hashlib.sha256(raw.astype('<i2').tobytes()).hexdigest()==cap['pcm_sha256']
        assert hashlib.sha256(clean.astype('<i2').tobytes()).hexdigest()==parent['pcm_sha256']
        lo,hi=cap['crop'];low,high=cap['end_interval'];signal=raw[lo:hi]
        row=dict(parent,clip_id=cap['clip_id'],endpoint_schema=1,speech_end_low_sample=low,
            speech_end_high_sample=high,sampling_group='positive' if parent['label'] else 'hard_negative')
        ambient=backgrounds[int(rng.integers(len(backgrounds)))]
        framed,y,end=framed_example(row,signal)
        offset=(max(4096,33792-row['speech_end_low_sample'])+len(framed)-len(signal)-1024)//2
        floor=np.resize(ambient,len(framed));framed[:offset]=floor[:offset];framed[offset+len(signal):]=floor[offset+len(signal):]
        for _ in range(8):
            background=backgrounds[int(rng.integers(len(backgrounds)))]
            framed_example(row,signal,rng,background=background)
        i=positions[row['clip_id'],0]
        np.testing.assert_array_equal(y,old['y'][i]);assert old['event_end'][i]==(-1 if end is None else end)
        logmel,raw_pitch=frontend(framed)
        np.testing.assert_array_equal(normalize(logmel,stats),old['x'][i])
        np.testing.assert_array_equal(raw_pitch,previous[i])
        curve=cmnd(framed)
        np.testing.assert_array_equal(legacy(curve),raw_pitch)
        weak=weakest_minimum(curve)
        all_weak[number]=weak;curve_diagnostics[number]=curve.q15
        values+=raw_pitch.size
        aligned=np.zeros(len(raw),np.int16);lag=cap['lag_samples']
        assert 0<=lag and lag+len(clean)<=len(aligned)
        aligned[lag:lag+len(clean)]=clean
        clean_framed,_,_=framed_example(row,aligned[lo:hi])
        _,clean_pitch=frontend(clean_framed)
        np.testing.assert_array_equal(legacy(cmnd(clean_framed)),clean_pitch)
        clean_values+=clean_pitch.size
        comparable=(raw_pitch[:,0]==0)&(clean_pitch[:,0]>0)&(weak[:,0]>0)
        agree=comparable&(np.abs(weak[:,0].astype(np.float64)/np.maximum(clean_pitch[:,0],1)-1)<=.20)
        both_legacy=(raw_pitch[:,0]>0)&(clean_pitch[:,0]>0)
        original_agree=both_legacy&(np.abs(raw_pitch[:,0].astype(np.float64)/np.maximum(clean_pitch[:,0],1)-1)<=.20)
        target_end=int(end) if end is not None else 65536
        last=min(256,(target_end+255)//256);first=max(0,last-32)
        if row['label']:
            weak_count+=int(comparable.sum());matched_count+=int(agree.sum())
            matched_confidence.extend(weak[agree,1].tolist())
            missed_confidence.extend(weak[comparable&~agree,1].tolist())
            legacy_positive_count+=int(both_legacy.sum());legacy_positive_agree+=int(original_agree.sum())
            positive_tail_weak+=int(comparable[first:last].sum());positive_tail_matched+=int(agree[first:last].sum())
        rows.append(dict(clip_id=row['clip_id'],label=row['label'],language=row['language'],source_group=row['source_group'],
            weak_comparable=int(comparable.sum()),weak_matches=int(agree.sum()),
            legacy_comparable=int(both_legacy.sum()),legacy_matches=int(original_agree.sum()),
            tail_weak_comparable=int(comparable[first:last].sum()),tail_weak_matches=int(agree[first:last].sum()),
            weak_confidence_p50=int(np.median(weak[comparable,1])) if comparable.any() else None))
    assert len(rows)==44
    agreement=matched_count/weak_count if weak_count else None
    gates=dict(original_C_legacy_exact=True,weak_positive_reference_sufficient=weak_count>=64,
        weak_positive_reference_agreement=bool(weak_count) and agreement>=.90)
    def confidence(values):
        return dict(frames=len(values),p50=float(np.median(values)) if values else None,
            quartile_counts=np.histogram(values,bins=[0,1024,2048,3072,4097])[0].tolist())
    result=dict(stage=plan['stage'],complete=True,passed=all(gates.values()),gates=gates,rows=44,
        original_logmel_values_exact=44*256*40,original_pitch_values_exact=values,
        independent_legacy_reconstruction_values_exact=values+clean_values,
        weak_positive=dict(comparable=weak_count,within20percent=matched_count,agreement=agreement,
            tail_comparable=positive_tail_weak,tail_matches=positive_tail_matched),
        legacy_positive=dict(comparable=legacy_positive_count,within20percent=legacy_positive_agree),
        confidence_matches=confidence(matched_confidence),confidence_misses=confidence(missed_confidence),
        per_language={lang:dict(comparable=sum(r['weak_comparable'] for r in rows if r['label'] and r['language']==lang),
            matched=sum(r['weak_matches'] for r in rows if r['label'] and r['language']==lang)) for lang in ('zh','yue')},
        classifier_scores=0,not_ground_truth_F0=True,uncertain_candidates_not_voiced_or_wake_decisions=True,
        original_inputs_labels_preserved=True,no_threshold_candidate_or_subharmonic_search=True,
        no_fit_or_DEV_TEST=True,seconds=time.monotonic()-start)
    assert result['seconds']<plan['maximum_seconds']
    for path,expected in plan['hashes'].items():assert sha(ROOT/path)==expected,path
    np.save(out/'weak-candidates.npy',all_weak);np.save(out/'cmnd.npy',curve_diagnostics)
    (out/'rows.json').write_text(json.dumps(rows,indent=2)+'\n',encoding='utf8')
    (out/'audit.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
    return result
