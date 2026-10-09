"""Optional raw40/frozen48/pitch2 causal24, strict independent checkpoint ABI."""
import numpy as np
from model import Causal, Model
from contextual import ContextualInput, CONTEXT_FRAMES, _contextual_forward

TOPOLOGY = 'raw40_el48_pitch2_causal24_v1'
ENCODING = 'f0_q4_div128_period_q12_div32_nearest_clip127_v1'


def encode_pitch(raw):
    if not isinstance(raw, np.ndarray) or raw.dtype != np.int16 or raw.ndim < 2 or raw.shape[-1] != 2:
        raise ValueError('Expected PCM-derived INT16 pitch pairs')
    if np.any(raw < 0) or np.any(raw[..., 1] > 4096):
        raise ValueError('Pitch outside the registered frontend range')
    scaled = raw.astype(np.int32)
    scaled[..., 0] = (scaled[..., 0]+64)//128
    scaled[..., 1] = (scaled[..., 1]+16)//32
    return np.minimum(scaled, 127).astype(np.int8)


class PitchedModel(Model):
    def __init__(self):
        super().__init__(24)
        self.layers[0] = Causal(90, 24, 3)


def checkpoint_pitched(saved):
    config = saved.get('config', {})
    stem = saved.get('state_dict', {}).get('layers.0.conv.weight')
    if config.get('topology') != TOPOLOGY or config.get('pitch_encoding') != ENCODING or config.get('inputs') != 90 or config.get('channels') != 24:
        raise ValueError('Unregistered pitched checkpoint')
    if stem is None or tuple(stem.shape) != (24, 90, 3):
        raise ValueError('Pitched checkpoint stem differs from metadata')
    return PitchedModel()


class PitchedContext(ContextualInput):
    def __init__(self, raw, hidden, length, provenance, parents, pitch, parent_pitch):
        super().__init__(raw, hidden, length, provenance, parents)
        if pitch.shape != (*raw.shape[:2], 2):
            raise ValueError('Pitch rows differ from TRAIN data')
        self.pitch = encode_pitch(pitch)
        self.parent_pitch = {}
        for key, (parent_raw, _) in parents.items():
            value = parent_pitch[key]
            if value.shape != (len(parent_raw), 2):
                raise ValueError('Continuous parent pitch shape differs')
            self.parent_pitch[key] = encode_pitch(value)
        for index, row in self.rows.items():
            start, real = row['start_frame'], row['real_frames']
            value = self.parent_pitch[row['data_key'], row['source_index']]
            if not np.array_equal(self.pitch[index, :real], value[start:start+real]):
                raise ValueError('Window pitch differs from continuous history')

    def batch(self, indices):
        base, active = super().batch(indices)
        values = np.zeros((*base.shape[:2], 90), dtype=np.int8)
        values[..., :88] = base
        for output, index in enumerate(indices):
            real, row = int(self.length[index]), self.rows.get(int(index))
            if row is None:
                values[output, CONTEXT_FRAMES:CONTEXT_FRAMES+real, 88:] = self.pitch[index, :real]
            else:
                start = row['start_frame']
                past = min(CONTEXT_FRAMES, start)
                source = self.parent_pitch[row['data_key'], row['source_index']]
                values[output, CONTEXT_FRAMES-past:CONTEXT_FRAMES+real, 88:] = source[start-past:start+real]
        return values, active


def pitched_forward(network, value, active, *, trace=False):
    return _contextual_forward(network, value, active, trace=trace, inputs=90)
