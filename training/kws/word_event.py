"""One bilingual whole-wake event, not a phonetic transcript.

Blank means no wake event at this frame; it does not mean acoustic silence.
An empty negative target therefore permits speech/music with no complete wake.
"""
import numpy as np
import torch
from model import Model,Causal
from ctc_timed import audited_final_windows

TOKENS=('no_wake_event','xiaoyan_wake_event')
CONTRACT='xiaoyan_bilingual_word_event48_stride512_v1'


class WordEventModel(Model):
    def __init__(self):
        super().__init__(48)
        self.layers[-1]=Causal(48,2,1,bias=True)


def event_targets(labels):
    labels=np.asarray(labels)
    if labels.ndim!=1 or labels.dtype!=np.int64 or not np.isin(labels,[0,1]).all():
        raise ValueError('Original binary complete-wake truth required')
    return labels[:,None].copy(),labels.copy()


def event_logits(value):
    """Frame x2 contiguous Q8 score ABI, symmetric round away from zero."""
    value=np.asarray(value)
    if value.ndim!=2 or value.shape[1]!=2 or not np.isfinite(value).all():
        raise ValueError('Expected finite frame x2 whole-event logits')
    scaled=value.astype(np.float64)*256
    rounded=np.sign(scaled)*np.floor(np.abs(scaled)+.5)
    return np.ascontiguousarray(rounded.clip(-32768,32767),dtype=np.int16)


def event_loss(logits,targets,lengths,windows,known_blank_frames=128):
    if (logits.ndim!=3 or not len(logits) or logits.shape[1]!=2 or
            logits.shape[2]%2 or not torch.isfinite(logits).all() or
            bool((logits.abs()>1024).any()) or
            targets.shape!=(len(logits),1) or targets.dtype!=torch.int64 or
            lengths.shape!=(len(logits),) or lengths.dtype!=torch.int64 or
            bool(((lengths<0)|(lengths>1)).any()) or
            bool((targets[:,0]!=lengths).any()) or
            windows.shape!=(len(logits),2) or windows.dtype!=torch.int64 or
            not isinstance(known_blank_frames,int) or known_blank_frames<0 or
            known_blank_frames%2 or known_blank_frames>=logits.shape[-1]):
        raise ValueError('Invalid finite whole-event CTC batch')
    active=lengths==1
    frames=(logits.shape[-1]-known_blank_frames)//2
    if (bool((windows[~active]!=-1).any()) or
            bool((windows[active,0]<0).any()) or
            bool((windows[active,1]<windows[active,0]).any()) or
            bool((windows[active,1]>=frames).any())):
        raise ValueError('Complete-wake event needs its audited source interval')
    prefix_frames=known_blank_frames//2
    stride=logits[:,:,1::2];suffix=stride[:,:,prefix_frames:]
    time=torch.arange(frames)[None]
    outside=(time<windows[:,:1])|(time>windows[:,1:])
    blocked=torch.zeros_like(suffix,dtype=torch.bool)
    blocked[:,1]=active[:,None]&outside
    permitted=suffix.masked_fill(blocked,-4096.)
    logs=permitted.log_softmax(1)
    log_mass=permitted.logsumexp(1)-suffix.logsumexp(1)
    nll=torch.nn.functional.ctc_loss(logs.permute(2,0,1).contiguous(),targets,
        torch.full((len(logits),),frames,dtype=torch.int64),lengths,
        blank=0,reduction='none',zero_infinity=False)
    return (nll-log_mass.sum(1)-
            stride[:,:,:prefix_frames].log_softmax(1)[:,0].sum(1)).mean()


def single_event_probability(scores):
    """Independent normalized probability recurrence, any1..8-frame window."""
    scores=np.asarray(scores,np.float64)
    if scores.ndim!=2 or scores.shape[1]!=2 or not 1<=len(scores)<=8 or not np.isfinite(scores).all():
        raise ValueError('Expected1..8 finite whole-event score pairs')
    p=np.exp(scores-scores.max(1,keepdims=True));p/=p.sum(1,keepdims=True)
    before,word,after=1.,0.,0.
    for blank,wake in p:
        before,word,after=before*blank,(before+word)*wake,(word+after)*blank
    return word+after
