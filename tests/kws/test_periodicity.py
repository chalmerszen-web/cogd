"""Independent NumPy CMND vs C, trusted-sine and fixed-noise strength checks."""
import argparse
import ctypes as C
import json
from pathlib import Path
import sys
import time
import numpy as np

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'training/kws'))
from pitch_curve import cmnd,legacy,weakest_minimum


class Features(C.Structure):
    _fields_=[('frequency',C.c_int16),('strength',C.c_int16)]


def check(path):
    began=time.monotonic()
    lib=C.CDLL(str(path))
    lib.kws_periodicity_size.restype=C.c_size_t
    size=lib.kws_periodicity_size()
    assert size==770
    for prefix in ('kws_pitch','kws_periodicity'):
        reset=getattr(lib,prefix+'_reset');reset.argtypes=[C.c_void_p];reset.restype=None
        block=getattr(lib,prefix+'_block');block.argtypes=[C.c_void_p,C.POINTER(C.c_int16)];block.restype=Features
    def feed(pcm,prefix='kws_periodicity'):
        pcm=np.ascontiguousarray(pcm,dtype=np.int16)
        assert len(pcm)%256==0
        state=C.create_string_buffer(size);getattr(lib,prefix+'_reset')(state)
        result=np.empty((len(pcm)//256,2),np.int16)
        for i,frame in enumerate(pcm.reshape(-1,256)):
            value=getattr(lib,prefix+'_block')(state,frame.ctypes.data_as(C.POINTER(C.c_int16)))
            result[i]=(value.frequency,value.strength)
        return result
    rng=np.random.default_rng(20261003410)
    pcm=rng.integers(-32768,32768,(4096,256),dtype=np.int16)
    n=np.arange(256)
    for at in range(0,4096,512):
        pcm[at:at+32]=0
        pcm[at+32:at+64]=-32768
        pcm[at+64:at+96]=32767
        pcm[at+96:at+128]=np.where(n%2,32767,-32768).astype(np.int16)
        pcm[at+128:at+160]=np.where(n//2%2,32767,-32768).astype(np.int16)
    signal=pcm.reshape(-1)
    curve=cmnd(signal)
    expected=legacy(curve)
    original=feed(signal,'kws_pitch')
    np.testing.assert_array_equal(original,expected)
    weak=weakest_minimum(curve)
    expected_strength=np.where(expected[:,0]>0,expected[:,1],weak[:,1])
    output=feed(signal)
    np.testing.assert_array_equal(output[:,0],expected[:,0])
    np.testing.assert_array_equal(output[:,1],expected_strength)
    clean_frames=0
    for frequency in (90,120,160,200,320,480,720):
        for amplitude in (300,3000,30000):
            t=np.arange(42*256)
            signal=np.rint(amplitude*np.sin(t*2*np.pi*frequency/16000)).astype(np.int16)
            actual=feed(signal)
            target=actual[12:,0].astype(float)/16
            assert np.all(np.abs(target-frequency)<=max(2.,frequency*.02))
            np.testing.assert_array_equal(actual,feed(signal,'kws_pitch'))
            clean_frames+=len(target)
    assert clean_frames==630
    snr=[]
    t=np.arange(96*256)
    tone=1000*np.sin(t*2*np.pi*200/16000)
    # One shared fixed bounded noise realization, changing only nominal SNR.
    noise=rng.uniform(-np.sqrt(3),np.sqrt(3),len(t))
    for db in (20,0,-10):
        mixed=np.rint(tone+noise*(1000/np.sqrt(2))*10**(-db/20))
        assert np.max(np.abs(mixed))<32768
        actual=feed(mixed.astype(np.int16))[12:]
        original=feed(mixed.astype(np.int16),'kws_pitch')[12:]
        np.testing.assert_array_equal(actual[:,0],original[:,0])
        assert np.all(actual[(original[:,0]>0),1]==original[(original[:,0]>0),1])
        snr.append(dict(nominal_SNR_db=db,median_strength_Q12=float(np.median(actual[:,1])),
            trusted_frames=int(np.count_nonzero(actual[:,0])),
            unknown_with_strength=int(np.count_nonzero((actual[:,0]==0)&(actual[:,1]>0)))))
    assert snr[0]['median_strength_Q12']>snr[1]['median_strength_Q12']>snr[2]['median_strength_Q12']
    white=rng.integers(-32768,32768,256*256,dtype=np.int16)
    actual=feed(white)
    assert not actual[:,0].any() and np.all((actual[:,1]>=0)&(actual[:,1]<=4096))
    result=dict(complete=True,independent_NumPy_values=4096*4,C_and_NumPy_exact=True,
        clean_frames=630,state_bytes=size,fixed_SNR=snr,
        white_noise_frames=256,white_unknown_with_strength=int(np.count_nonzero(actual[:,1])),
        no_weak_frequency=True,no_voiced_or_classifier_decisions=True,seconds=time.monotonic()-began)
    print(json.dumps(result),flush=True)
    return result


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('library',type=Path)
    check(parser.parse_args().library)
