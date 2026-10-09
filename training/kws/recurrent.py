"""Host-only reset-after GRU32, matching the explicit C11 integer ABI v1.

This is a new representation, not a refit or rename of a rejected DS-TCN.
Inputs are the existing normalized40 features divided by32. It emits two
whole-event logits; vocabulary, time supervision and decision policy stay
separate. No model is trained or qualified by constructing this module.
"""
import numpy as np
import torch
from torch import nn

CONTRACT='xiaoyan_gru32_reset_after_q8_q15_event2_v1'
FIELDS=(('input',(3,32,40),'int8'),('recurrent',(3,32,32),'int8'),
        ('input_bias',(3,32),'int16'),('recurrent_bias',(3,32),'int16'),
        ('output',(2,32),'int8'),('output_bias',(2,),'int16'))


class Recurrent32(nn.Module):
    def __init__(self):
        super().__init__()
        self.recurrent=nn.GRU(40,32,batch_first=True)
        self.output=nn.Linear(32,2)

    def forward(self,values,state=None):
        if (values.ndim!=3 or values.shape[1]!=40 or not values.shape[0] or
                not values.shape[2] or not values.is_floating_point()):
            raise ValueError('Expected nonempty batch x40 x frames normalized values')
        hidden,state=self.recurrent(values.transpose(1,2),state)
        return self.output(hidden).transpose(1,2),state


def quantize(network):
    """Fixed Q8 export; out-of-range weights fail, never silently clamp."""
    if not isinstance(network,Recurrent32) or network.training:
        raise ValueError('Frozen GRU32 required')
    parameters=(network.recurrent.weight_ih_l0,network.recurrent.weight_hh_l0,
                network.recurrent.bias_ih_l0,network.recurrent.bias_hh_l0,
                network.output.weight,network.output.bias)
    result={}
    for (name,shape,dtype),parameter in zip(FIELDS,parameters):
        value=parameter.detach().cpu().numpy().astype(np.float64).reshape(shape)*256
        if not np.isfinite(value).all():
            raise ValueError('Nonfinite parameter')
        integer=np.sign(value)*np.floor(np.abs(value)+.5)
        limits=np.iinfo(dtype)
        if np.any((integer<limits.min)|(integer>limits.max)):
            raise ValueError('Parameter outside fixed quantized range: '+name)
        result[name]=np.ascontiguousarray(integer,dtype=dtype)
    return result


def validate(bundle):
    if not isinstance(bundle,dict) or set(bundle)!={field[0] for field in FIELDS}:
        raise ValueError('Unknown GRU weight fields')
    for name,shape,dtype in FIELDS:
        value=bundle[name]
        if (not isinstance(value,np.ndarray) or value.shape!=shape or value.dtype!=np.dtype(dtype)
                or not value.flags.c_contiguous):
            raise ValueError('Invalid GRU field: '+name)


def emit_c(bundle,symbol='kws_gru32_candidate'):
    """Raw weights only. Firmware activation still requires qualified manifest."""
    validate(bundle)
    if not isinstance(symbol,str) or not symbol.isascii() or not symbol.isidentifier():
        raise ValueError('Invalid C symbol')
    def braces(value):
        if value.ndim==1:
            return '{'+','.join(map(str,value.tolist()))+'}'
        return '{'+','.join(braces(row) for row in value)+'}'
    fields=',\n'.join('    .'+name+'='+braces(bundle[name]) for name,_,_ in FIELDS)
    return '#include "kws_gru32.h"\nconst kws_gru32_model_t '+symbol+'={\n'+fields+'\n};\n'
