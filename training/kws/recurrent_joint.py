"""Host-training-only source-supported token supervision for the GRU32 cell.

Fourteen token outputs are auxiliary. Firmware receives the unchanged GRU32
and exactly the last two event output rows. No failed checkpoint is loaded.
Unknown negative tokens retain the existing OTHER catch-all, not a transcript.
"""
import torch
from torch import nn
from recurrent import Recurrent32
from ctc_data import TOKENS

CONTRACT='xiaoyan_gru32_ctc14_event2_host_projection_v1'
PHONE_CLASSES=len(TOKENS)


class RecurrentJoint32(Recurrent32):
    def __init__(self):
        super().__init__()
        self.output=nn.Linear(32,PHONE_CLASSES+2)


def event_projection(network):
    """Copy all recurrent tensors and the exact two deployable event rows.

    Floating GEMM shapes may round differently. The tensor and quantized
    coefficient identity is exact; actual integer acoustic parity is later.
    """
    if not isinstance(network,RecurrentJoint32) or network.training:
        raise ValueError('Frozen joint GRU32 required')
    parameters={name:value.detach().clone() for name,value in network.state_dict().items()}
    parameters['output.weight']=parameters['output.weight'][PHONE_CLASSES:].clone()
    parameters['output.bias']=parameters['output.bias'][PHONE_CLASSES:].clone()
    source=network.output.weight
    result=Recurrent32().to(device=source.device,dtype=source.dtype).eval()
    result.load_state_dict(parameters,strict=True)
    return result
