"""Explicit negative-class coverage; source labels and split identities stay intact."""
import numpy as np

NEAR_WORDS = frozenset(('你好小王', '你好小燕', '你好小杨', '你好小明',
                        '你好乐鑫', '你好小叶', '你好小智'))


def negative_groups(data):
    """Partition all labelled negatives; unlisted text stays in other_hard."""
    label = np.asarray(data['label'])
    text = np.asarray(data['text']).astype(str)
    source = np.asarray(data['sampling_group']).astype(str)
    if label.ndim != 1 or text.shape != label.shape or source.shape != label.shape:
        raise ValueError('Negative metadata shapes differ')
    if not np.all((label == 0) | (label == 1)):
        raise ValueError('Unknown binary label')
    negative = label == 0
    normalized = np.asarray([''.join(c for c in value if c not in ' ,，。!！?？') for value in text])
    fragment = negative & ((source == 'paired_negative') | np.isin(normalized, ('你好', '小言')))
    natural = negative & ~fragment & (source == 'natural_negative')
    near = negative & ~fragment & ~natural & np.isin(normalized, tuple(NEAR_WORDS))
    other = negative & ~fragment & ~natural & ~near
    masks = dict(near_word=near, fragment=fragment, other_hard=other, old_natural=natural)
    assert np.array_equal(sum(mask.astype(np.int8) for mask in masks.values()), negative.astype(np.int8))
    return {name: np.flatnonzero(mask) for name, mask in masks.items()}


def verify():
    rows = dict(label=np.array([0, 0, 0, 0, 1, 0, 0, 0]),
        text=np.array(['你好，小燕', '小言', '', '嗨乐鑫', '你好小叶',
                       'time-cut-suffix', '你好小叶', 'unlisted']),
        sampling_group=np.array(['hard_negative', 'hard_negative', 'natural_negative',
            'hard_negative', 'hard_negative', 'paired_negative', 'natural_negative', 'future_group']))
    expected = dict(near_word=[0], fragment=[1, 5], other_hard=[3, 7], old_natural=[2, 6])
    assert {key: value.tolist() for key, value in negative_groups(rows).items()} == expected
    rng = np.random.default_rng(2026100269)
    for _ in range(32):
        order = rng.permutation(8)
        permuted = {key: value[order] for key, value in rows.items()}
        actual = negative_groups(permuted)
        assert all(set(order[value]) == set(expected[key]) for key, value in actual.items())
    for bad in (dict(rows, label=np.array([0, 2, 0, 0, 1, 0, 0, 0])),
                dict(rows, text=np.array(['wrong length']))):
        try:
            negative_groups(bad)
        except ValueError:
            pass
        else:
            raise AssertionError('Invalid metadata accepted')
    return 'PASS:negative taxonomy,positive exclusion,unknown-text coverage,32 reorderings,invalid metadata'


if __name__ == '__main__':
    print(verify())
