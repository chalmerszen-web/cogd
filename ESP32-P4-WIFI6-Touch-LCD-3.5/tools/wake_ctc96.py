"""P4-only phonetic experiment; does not change the C3 CTC token ABI."""
import importlib.util
from pathlib import Path
import numpy as np
import torch
from torch.nn import functional as F

_spec=importlib.util.spec_from_file_location('p4_ctc_backbone',Path(__file__).parent/'kws96_training/model.py')
_module=importlib.util.module_from_spec(_spec);_spec.loader.exec_module(_module)
TOKENS=('blank','ni','hao','xiao','yan2','nei','hou','siu','jin4','other','yan4','hai','jin3','jin1')
PHRASES={(1,2,3,4):'nihao_zh',(11,3,4):'hi_zh',(5,6,7,8):'nihao_yue',(11,7,8):'hi_yue'}
CONTRACT='p4_causal96_ctc14_hai_stride512_v1'

class Model(_module.Model):
    def __init__(self):
        super().__init__(96)
        self.layers[-1]=_module.Causal(96,len(TOKENS),1,bias=True)

def phonemes(values):
    result=[]
    for value in values:
        value=str(value).lower().strip()
        if value in ('yan2','yan4','jin4','jin3','jin1'):token=TOKENS.index(value)
        elif value.rstrip('123456') in ('hi','hai','haai','hoi'):token=11
        elif value.rstrip('123456') in ('ni','hao','xiao','nei','hou','siu'):token=TOKENS.index(value.rstrip('123456'))
        else:token=9
        # Unknown phone counts are not a supervised phonetic annotation.
        if token!=9 or not result or result[-1]!=9:result.append(token)
    return result or [9]

def loss(logits,targets,weights,deadline=None):
    # Keep the first2.048s as causal context. The training examples reserve
    # that interval for warm-up; it is not a runtime delay after rearming.
    probabilities=F.log_softmax(logits[:,:,129::2],1).permute(2,0,1).contiguous().cpu()
    lengths=torch.tensor([len(t) for t in targets],dtype=torch.int64)
    flat=torch.tensor([v for t in targets for v in t],dtype=torch.int64)
    input_lengths=torch.full_like(lengths,len(probabilities)) if deadline is None else ((deadline.cpu()+511)//512-64).clamp(1,len(probabilities))
    costs=F.ctc_loss(probabilities,flat,input_lengths,lengths,
                     blank=0,reduction='none',zero_infinity=False)
    costs=costs/lengths.clamp_min(1)
    objective=(costs*weights.cpu()).mean()
    if deadline is not None:
        # A complete source has ended by its annotated end+200ms. Suppress
        # delayed/repeated phone emissions after that bounded interval.
        mask=torch.arange(len(probabilities))[:,None]>=input_lengths[None,:]
        objective=objective+.1*(-probabilities[:,:,0]*mask).sum()/mask.sum().clamp_min(1)
    return objective

def decode(logits,offset=0,margin=0.):
    """Full ordered phrase, two blank frames, 3s span, accepted-only cooldown."""
    history=[];previous=0;blanks=0;pending=None;cooldown=0;events=[]
    for frame,row in enumerate(logits):
        end=(frame+1)*512+offset
        order=np.argsort(row);token=int(order[-1])
        if row[order[-1]]-row[order[-2]]<=margin:
            history=[];previous=0;blanks=0;pending=None
            continue
        history=[v for v in history if end-v[1]<=48000]
        if token==0:
            blanks+=1
            if pending is not None and blanks>=2:
                if end>=cooldown:events.append((end,pending));cooldown=end+24000
                pending=None;history=[]
        else:
            blanks=0
            if token!=previous:
                pending=None;history.append((token,end));history=history[-4:]
                for phrase,group in PHRASES.items():
                    if tuple(v[0] for v in history[-len(phrase):])==phrase:pending=group
        previous=token
    return events

def collapse(logits):
    result=[];previous=0
    for row in logits:
        token=int(np.argmax(row))
        if token and token!=previous:result.append(token)
        previous=token
    return result
