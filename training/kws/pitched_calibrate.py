"""Same TRAIN calibration selection, with explicit90-input pitch contract."""
from pitched import checkpoint_pitched, pitched_forward, ENCODING
from contextual_calibrate import _calibrate_contextual
from data import ROOT
import hashlib
import numpy as np
import torch


def calibrate_pitched(checkpoint, features, corpus, maximum=512):
    saved = torch.load(checkpoint, map_location='cpu', weights_only=True)
    inputs = saved['config']['pitch_inputs']
    for descriptor in inputs.values():
        path = ROOT/descriptor['path']
        with path.open('rb') as stream:
            if hashlib.file_digest(stream, 'sha256').hexdigest() != descriptor['sha256']:
                raise ValueError('Changed pitch calibration source')
    from pitched import encode_pitch
    np.testing.assert_array_equal(corpus.pitch, encode_pitch(np.load(ROOT/inputs['rows']['path'], allow_pickle=False)))
    result = _calibrate_contextual(checkpoint, features, corpus, maximum,
                                   checkpoint_pitched, pitched_forward)
    result['pitch_encoding'] = ENCODING
    result['pitch_inputs'] = inputs
    return result
