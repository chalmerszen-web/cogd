"""Host-only CMND inspection of the unchanged C YIN geometry.

Weak candidates are measurements, not voiced decisions or wake events.
"""
from dataclasses import dataclass
import numpy as np


@dataclass
class Curve:
    q15: np.ndarray
    computed: np.ndarray
    eligible: np.ndarray


def cmnd(pcm):
    if not isinstance(pcm,np.ndarray) or pcm.dtype!=np.int16 or pcm.ndim!=1 or not len(pcm) or len(pcm)%256:
        raise ValueError('Expected complete native PCM16 mono16kHz blocks')
    pair=pcm.astype(np.int64).reshape(-1,2).sum(1)
    scaled=np.sign(pair)*(np.abs(pair)//32)
    windows=np.lib.stride_tricks.sliding_window_view(np.r_[np.zeros(256,np.int64),scaled],384)[::128]
    body=windows[:,:256]
    variance=(body*body).sum(1)-(body.sum(1)**2)//256
    eligible=(variance>=256*16)&(np.arange(len(windows))>=2)
    difference=np.empty((len(windows),101),np.uint64)
    for lag in range(1,102):
        delta=body-windows[:,lag:lag+256]
        difference[:,lag-1]=(delta*delta).sum(1)
    assert int(difference.max())<=4292870400
    cumulative=np.cumsum(difference,axis=1,dtype=np.uint64)
    computed=cumulative>0
    lag=np.arange(1,102,dtype=np.uint64)
    q15=(difference*lag*32768+cumulative//2)//np.maximum(cumulative,1)
    q15=np.minimum(q15,131072).astype(np.int32)
    q15[~computed]=32768
    return Curve(q15,computed,eligible)


def _frequency(before,middle,after,lag):
    denominator=2*(before.astype(np.int64)-2*middle+after)
    numerator=(before.astype(np.int64)-after)*256
    offset=np.where(denominator>0,np.sign(numerator)*(np.abs(numerator)//np.maximum(denominator,1)),0)
    period=np.clip(lag*256+np.clip(offset,-128,128),2560,25600).astype(np.uint64)
    return ((32768000+period//2)//period).astype(np.int16)


def legacy(curve):
    n=len(curve.q15)
    output=np.zeros((n,2),np.int16)
    previous=np.full(n,32768,np.int32);before=previous.copy()
    for index in range(101):
        current=curve.q15[:,index];lag=index+1
        ready=curve.computed[:,index]
        selected=curve.eligible & ready & (output[:,0]==0) & (lag>10) & (previous<4915) & (previous<before) & (previous<=current)
        if selected.any():
            output[selected,0]=_frequency(before[selected],previous[selected],current[selected],lag-1)
            output[selected,1]=(4096-(previous[selected]+4)//8).astype(np.int16)
        before=np.where(ready,previous,before)
        previous=np.where(ready,current,previous)
    return output


def weakest_minimum(curve):
    """One fixed minimum at10..100, shortest lag on a tie; no threshold sweep."""
    index=np.argmin(curve.q15[:,9:100],axis=1)+9
    row=np.arange(len(index))
    middle=curve.q15[row,index]
    frequency=_frequency(curve.q15[row,index-1],middle,curve.q15[row,index+1],index+1)
    confidence=np.clip(4096-(middle+4)//8,0,4096).astype(np.int16)
    output=np.stack((frequency,confidence),axis=1).astype(np.int16)
    output[~curve.eligible]=0
    return output
