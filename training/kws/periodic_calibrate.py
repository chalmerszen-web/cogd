"""TRAIN-only PTQ with strict continuous-strength provenance and model ABI."""
import hashlib
import numpy as np
import torch
from data import ROOT
from contextual_calibrate import _calibrate_contextual
from periodic import checkpoint_periodic,periodic_forward,encode_pitch,ENCODING


def calibrate_periodic(checkpoint,features,corpus,maximum=512):
    saved=torch.load(checkpoint,map_location='cpu',weights_only=True)
    checkpoint_periodic(saved)
    inputs=saved['config']['pitch_inputs']
    for descriptor in inputs.values():
        with (ROOT/descriptor['path']).open('rb') as stream:
            if hashlib.file_digest(stream,'sha256').hexdigest()!=descriptor['sha256']:
                raise ValueError('Changed periodic-strength calibration source')
    np.testing.assert_array_equal(corpus.pitch,encode_pitch(np.load(ROOT/inputs['rows']['path'],allow_pickle=False)))
    result=_calibrate_contextual(checkpoint,features,corpus,maximum,checkpoint_periodic,periodic_forward)
    result['pitch_encoding']=ENCODING;result['pitch_inputs']=inputs
    return result
