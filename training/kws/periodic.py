"""A separate90-input checkpoint ABI for continuous periodic strength.

Architecture and unit encoding are unchanged. Feature semantics are distinct:
the original trusted frequency stays0 if unknown; strength is retained without
crossing the old voiced threshold. This is never a drop-in for pitched models.
"""
from pitched import PitchedModel,PitchedContext,encode_pitch,pitched_forward

TOPOLOGY='raw40_el48_trustedf0_strength2_causal24_v1'
ENCODING='trustedf0_q4_div128_minCMND_strength_q12_div32_nearest_clip127_v1'


class PeriodicityModel(PitchedModel):
    pass


class PeriodicityContext(PitchedContext):
    pass


def checkpoint_periodic(saved):
    config=saved.get('config',{})
    stem=saved.get('state_dict',{}).get('layers.0.conv.weight')
    if (config.get('topology')!=TOPOLOGY or config.get('pitch_encoding')!=ENCODING or
        config.get('inputs')!=90 or config.get('channels')!=24):
        raise ValueError('Unregistered periodic-strength checkpoint')
    if stem is None or tuple(stem.shape)!=(24,90,3):
        raise ValueError('Periodic-strength stem differs from metadata')
    return PeriodicityModel()


def periodic_forward(network,value,active,*,trace=False):
    return pitched_forward(network,value,active,trace=trace)
