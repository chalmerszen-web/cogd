"""One TRAIN-only strength study; periodicity is not a keyword decision."""
import ctypes as C
import hashlib
import json
from pathlib import Path
import time
import numpy as np
from data import ROOT,framed_example
from device_domain import pcm


class Features(C.Structure):
    _fields_=[('frequency',C.c_int16),('strength',C.c_int16)]


def read(path):return json.loads(Path(path).read_text(encoding='utf8'))
def sha(path):
    with Path(path).open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()


def study(plan,out):
    began=time.monotonic();out=Path(out);out.mkdir(exist_ok=False)
    for name,expected in plan['hashes'].items():assert sha(ROOT/name)==expected,name
    files=plan['files']
    with np.load(ROOT/files['train'],allow_pickle=False) as dataset:
        positions={(str(c),int(v)):i for i,(c,v) in enumerate(zip(dataset['clip_id'],dataset['variant']))}
    previous=np.load(ROOT/files['old_pitch'],mmap_mode='r')
    weak=np.load(ROOT/files['weak_candidates'],mmap_mode='r')
    assert weak.shape==(44,256,2) and weak.dtype==np.int16
    lib=C.CDLL(str(ROOT/files['periodicity_library']))
    lib.kws_periodicity_size.restype=C.c_size_t
    size=lib.kws_periodicity_size();assert size==770
    lib.kws_periodicity_reset.argtypes=[C.c_void_p];lib.kws_periodicity_reset.restype=None
    lib.kws_periodicity_block.argtypes=[C.c_void_p,C.POINTER(C.c_int16)]
    lib.kws_periodicity_block.restype=Features
    backgrounds=[pcm(ROOT/d['recording'])[-16000:] for d in plan['sources']]
    rng=np.random.default_rng(read(ROOT/files['device_metadata'])['seed'])
    rows=[];positive_speech=[];positive_background=[]
    strength=np.zeros((44,256,2),np.int16)
    for number,descriptor in enumerate(plan['sources']):
        assert time.monotonic()-began<plan['maximum_seconds']
        cap,parent=descriptor['capture'],descriptor['parent']
        assert cap['split']==parent['split']=='train'
        raw=pcm(ROOT/descriptor['recording'])
        assert hashlib.sha256(raw.astype('<i2').tobytes()).hexdigest()==cap['pcm_sha256']
        lo,hi=cap['crop'];low,high=cap['end_interval'];signal=raw[lo:hi]
        row=dict(parent,clip_id=cap['clip_id'],endpoint_schema=1,speech_end_low_sample=low,
            speech_end_high_sample=high,sampling_group='positive' if parent['label'] else 'hard_negative')
        ambient=backgrounds[int(rng.integers(len(backgrounds)))]
        framed,_,_=framed_example(row,signal)
        offset=(max(4096,33792-row['speech_end_low_sample'])+len(framed)-len(signal)-1024)//2
        floor=np.resize(ambient,len(framed))
        framed[:offset]=floor[:offset];framed[offset+len(signal):]=floor[offset+len(signal):]
        for _ in range(8):
            background=backgrounds[int(rng.integers(len(backgrounds)))]
            framed_example(row,signal,rng,background=background)
        i=positions[row['clip_id'],0]
        expected=np.array(previous[i],copy=True)
        expected[:,1]=np.where(expected[:,0]>0,expected[:,1],weak[number,:,1])
        state=C.create_string_buffer(size);lib.kws_periodicity_reset(state)
        for j,frame in enumerate(framed.reshape(-1,256)):
            value=lib.kws_periodicity_block(state,frame.ctypes.data_as(C.POINTER(C.c_int16)))
            strength[number,j]=(value.frequency,value.strength)
        np.testing.assert_array_equal(strength[number],expected)
        assert np.all(strength[number,:,0][previous[i,:,0]==0]==0)
        # Full causal48ms window, source boundaries transferred by original
        # recording alignment. Outside windows include the ORIGINAL TRAIN
        # background floor; neither timing nor masks use classifier scores.
        end=np.arange(1,257)*256;start=end-768;warm=np.arange(256)>=2
        speech_start=offset+cap['lag_samples']-lo+parent['speech_start_sample']
        speech_end=offset+cap['lag_samples']-lo+parent['speech_end_high_sample']
        speech=warm&(start>=speech_start)&(end<=speech_end)
        background=warm&((end<=speech_start-1600)|(start>=speech_end+1600))
        assert not np.any(speech&background)
        assert speech.sum()>=8 and background.sum()>=8
        speech_values=strength[number,speech,1];background_values=strength[number,background,1]
        speech_median=float(np.median(speech_values));background_median=float(np.median(background_values))
        if parent['label']:
            positive_speech.extend(speech_values.tolist());positive_background.extend(background_values.tolist())
        rows.append(dict(clip_id=row['clip_id'],label=parent['label'],language=parent['language'],
            source_group=parent['source_group'],speech_frames=int(speech.sum()),background_frames=int(background.sum()),
            speech_median_q12=speech_median,background_median_q12=background_median,
            difference_q12=speech_median-background_median,
            speech_gt_background=speech_median>background_median,
            reliable_speech_frames=int(np.count_nonzero(strength[number,speech,0])),
            unknown_speech_with_strength=int(np.count_nonzero((strength[number,speech,0]==0)&(speech_values>0)))))
    positives=[row for row in rows if row['label']]
    assert len(rows)==44 and len(positives)==22
    higher=sum(row['speech_gt_background'] for row in positives)
    speech_median=float(np.median(positive_speech));background_median=float(np.median(positive_background))
    gates=dict(exact_C_and_independent_reference=True,original_reliable_frequency_and_strength_preserved=True,
        positive_clips_higher_fraction=higher/len(positives)>=plan['gates']['positive_clips_higher_fraction'],
        pooled_positive_median_difference=speech_median-background_median>=plan['gates']['pooled_positive_minimum_difference_q12'])
    def summarize(group):
        return dict(clips=len(group),higher=sum(r['speech_gt_background'] for r in group),
            median_clip_difference_q12=float(np.median([r['difference_q12'] for r in group])) if group else None)
    result=dict(stage=plan['stage'],complete=True,passed=all(gates.values()),gates=gates,
        rows=44,C_and_reference_values_exact=int(strength.size),original_reliable_frequency_unchanged=True,
        unknown_frequency_always_zero=True,state_bytes=size,
        positives=dict(clips=22,higher=higher,higher_fraction=higher/22,speech_frames=len(positive_speech),
            background_frames=len(positive_background),speech_median_q12=speech_median,
            background_median_q12=background_median,pooled_difference_q12=speech_median-background_median),
        per_language={language:summarize([r for r in positives if r['language']==language]) for language in ('zh','yue')},
        negatives=summarize([r for r in rows if not r['label']]),
        same_original_background_injected=True,no_live_speech_or_false_wake_rate=True,
        no_keyword_classifier_threshold_or_voiced_decision=True,no_DEV_TEST_fit_USB_Flash=True,
        seconds=time.monotonic()-began)
    assert result['seconds']<plan['maximum_seconds']
    for name,expected in plan['hashes'].items():assert sha(ROOT/name)==expected,name
    np.save(out/'features.npy',strength)
    (out/'rows.json').write_text(json.dumps(rows,indent=2)+'\n',encoding='utf8')
    (out/'audit.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
    return result
