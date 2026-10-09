"""Optional raw40 + frozen E/L48, causal24 model. Not a registered default."""
from model import Causal, Model


class ConditionedModel(Model):
    def __init__(self):
        super().__init__(24)
        self.layers[0] = Causal(88, 24, 3)


def checkpoint_conditioned(saved):
    config = saved.get('config', {})
    if config.get('topology') != 'raw40_el48_causal24_v1' or config.get('inputs') != 88 or config.get('channels') != 24:
        raise ValueError('Unregistered conditioned checkpoint')
    stem = saved.get('state_dict', {}).get('layers.0.conv.weight')
    if stem is None or tuple(stem.shape) != (24, 88, 3):
        raise ValueError('Conditioned stem tensor and metadata differ')
    return ConditionedModel()
