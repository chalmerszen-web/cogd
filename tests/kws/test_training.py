"""Failure-focused contracts for the real training/export path."""
import json
from pathlib import Path
import sys
import subprocess
import ctypes as C
import numpy as np
import pytest
import torch
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'training/kws'))
from data import read_manifest, framed_example, normalize, load_pcm
from calibrate import calibrate, emit_c
from quantize import infer, torch_integer_oracle
from model import Model
from evaluate import events
from quality_gate import check
from data import Backend, Trace, ROOT

def row(index,split='train'):
    return dict(clip_id=str(index),path='unused.wav',label=0,language='zh',source_group='speaker-'+str(index),
                original_recording_id='source-'+str(index),pcm_sha256='hash-'+str(index),license_id='test',split=split)

def test_cross_split_source_and_pcm_rejected(tmp_path):
    for field in ('source_group','original_recording_id','pcm_sha256','reference_speaker_id','noise_parent_id'):
        a=row(1);b=row(2,'test');a[field]=b[field]='same-source'
        manifest=tmp_path/'manifest.jsonl';manifest.write_text(json.dumps(a)+'\n'+json.dumps(b))
        with pytest.raises(ValueError,match='leakage'):read_manifest(manifest)

def test_whole_word_and_tail_labels():
    r=row(0);r.update(label=1,wake_start_sample=1000,wake_end_sample=25000)
    pcm=np.arange(32000,dtype=np.int16)
    framed,labels,end=framed_example(r,pcm)
    np.testing.assert_array_equal(framed[end-24000:end],pcm[1000:25000])
    sample_ends=(np.arange(256)+1)*256
    assert labels[sample_ends<end].sum()==0 and labels.sum()>0
    assert np.all(sample_ends[labels.astype(bool)]>=32768)

def test_derived_source_cannot_cross_split(tmp_path):
    for child_field,parent_field in (('parent_clip_id','clip_id'),('noise_parent_id','clip_id'),
                                     ('source_pcm_sha256','pcm_sha256')):
        a=row(1);b=row(2,'test');b[child_field]=a[parent_field]
        manifest=tmp_path/'manifest.jsonl';manifest.write_text(json.dumps(a)+'\n'+json.dumps(b))
        with pytest.raises(ValueError,match='leakage'):read_manifest(manifest)

def test_augmentation_reproducible_bounded_and_keeps_event():
    r=row(0);r.update(label=1,wake_start_sample=0,wake_end_sample=16000)
    pcm=np.full(16000,30000,dtype=np.int16);noise=np.arange(2000,dtype=np.int16)
    a=framed_example(r,pcm,np.random.default_rng(99),background=noise)
    b=framed_example(r,pcm,np.random.default_rng(99),background=noise)
    np.testing.assert_array_equal(a[0],b[0]);np.testing.assert_array_equal(a[1],b[1])
    assert a[2]==b[2] and a[0].dtype==np.int16 and a[2]>=32768
    assert a[1][(np.arange(256)+1)*256<a[2]].sum()==0

def test_silent_speech_rejected_but_silent_background_is_valid(tmp_path):
    import wave,hashlib
    path=tmp_path/'silence.wav';raw=bytes(16000*2)
    with wave.open(str(path),'wb') as wav:
        wav.setparams((1,2,16000,0,'NONE','not compressed'));wav.writeframes(raw)
    r=row(0);r.update(path=str(path),pcm_sha256=hashlib.sha256(raw).hexdigest())
    assert len(load_pcm(r))==16000
    r['source_dataset']='google/fleurs'
    with pytest.raises(ValueError,match='silent'):load_pcm(r)

def test_fixed_normalization_rounding():
    stats=dict(mean_q8=[3072]*40,inverse_std_q12=[1024]*40)
    x=np.array([[3072+16]*40,[3072-16]*40,[32767]*40],dtype=np.int16)
    np.testing.assert_array_equal(normalize(x,stats)[:,0],[1,-1,127])

def test_real_checkpoint_export_matches_integer_oracle(tmp_path):
    torch.manual_seed(42);torch.set_num_threads(1);net=Model().eval()
    # Exercise folded nonzero BN means/scales rather than identity BN.
    for bn in net.norms.values():
        bn.running_mean.uniform_(-.3,.3);bn.running_var.uniform_(.2,1.4)
    norm=dict(mean_q8=[1000]*40,inverse_std_q12=[1200]*40)
    torch.save(dict(state_dict=net.state_dict(),normalization=norm,config={'seed':42},step=5),tmp_path/'model.pt')
    rng=np.random.default_rng(22);x=rng.integers(-100,100,(2,180,40),dtype=np.int8)
    np.savez(tmp_path/'train.npz',x=x,clip_id=np.array(['a','b']))
    bundle=calibrate(tmp_path/'model.pt',tmp_path,2)
    assert bundle['trained'] and bundle['calibration_split']=='train'
    for features in x:np.testing.assert_array_equal(infer(features,bundle),torch_integer_oracle(features,bundle))
    source=tmp_path/'learned.c';source.write_text(emit_c(bundle))
    component=ROOT/'components/kws_c11'
    sources=[component/(name+'.c') for name in ('kws_quant','kws_detector','kws_nn','kws_frontend','kws_pcen','kws_fft')]
    library=tmp_path/'learned.so'
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-O2','-shared','-fPIC',
        '-I'+str(component/'include'),'-I'+str(ROOT/'third_party/kissfft'),*map(str,sources),
        str(component/'generated/tables.c'),str(source),'-lm','-o',str(library)],check=True)
    backend=Backend(library,'kws_trained_model');trace=Trace()
    for features in x:
        backend.lib.kws_reset(backend.handle);actual=[]
        for frame in features:
            backend.lib.kws_step_features(backend.handle,frame.ctypes.data_as(C.POINTER(C.c_int8)),C.byref(trace))
            actual.append(list(trace.layer))
        np.testing.assert_array_equal(infer(features,bundle),actual)

def test_detector_warmup_confirmation_cooldown():
    assert events(np.ones(256)*100,50)==[32768,56832]
    x=np.zeros(256);x[129]=100
    assert events(x,50)==[]

def test_quality_gate_requires_both_languages_and_bounded_quantization_loss():
    data=dict(label=np.ones(200),language=np.array(['zh']*100+['yue']*100),clip_id=np.arange(200))
    report=dict(split='test',quantized=dict(languages={l:dict(total=100,recall=.8) for l in ('zh','yue')},
        negative_clips=10,negative_triggered_clips=1),recall_drop={'zh':.05,'yue':.04})
    assert not check(report,data)
    report['quantized']['languages']['yue']['recall']=.79
    assert any('yue: recall' in failure for failure in check(report,data))
    report['quantized']['languages']['yue']['recall']=float('nan')
    assert any('yue: recall' in failure for failure in check(report,data))
    report['recall_drop']['zh']=.051
    assert any('zh: quantization' in failure for failure in check(report,data))
    data['clip_id'][:100]=0
    assert any('zh: fewer' in failure for failure in check(report,data))
