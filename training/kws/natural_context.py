"""Retain source coordinates for the existing natural-negative TRAIN windows."""
import numpy as np


def natural_provenance(windows, first):
    if type(first) is not int or first < 0:
        raise ValueError('Expected nonnegative first TRAIN index')
    raw = windows['x']
    if raw.dtype != np.int8 or raw.ndim != 3 or raw.shape[1:] != (256, 40):
        raise ValueError('Expected original256x40 natural windows')
    count = len(raw)
    for key in ('source', 'start_frame', 'length'):
        if windows[key].shape != (count,) or not np.issubdtype(windows[key].dtype, np.integer):
            raise ValueError('Invalid source-coordinate array')
    if windows['language'].shape != (count,):
        raise ValueError('Invalid language metadata')
    rows = []
    for index in range(count):
        source, start, real = (int(windows[key][index]) for key in ('source', 'start_frame', 'length'))
        if source < 0 or start < 0 or start % 2 or not 1 <= real <= 256:
            raise ValueError('Invalid source/phase/real-frame coordinate')
        rows.append(dict(index=first+index, data_key='natural', source_index=source,
            start_frame=start, real_frames=real, label=0, language=str(windows['language'][index])))
    return rows
