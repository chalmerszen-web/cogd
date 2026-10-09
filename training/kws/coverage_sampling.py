"""Finite, reproducible shuffled passes with an explicit full-coverage budget."""
import numpy as np


class CoverageSampler:
    def __init__(self, groups, quotas, *, steps, batch, seed, count):
        if set(groups) != set(quotas) or not groups:
            raise ValueError('Every sample group needs an explicit quota')
        if steps < 1 or count < 1 or any(type(q) is not int or q < 1 for q in quotas.values()):
            raise ValueError('Positive steps, count and integer quotas are required')
        if sum(quotas.values()) != batch:
            raise ValueError('Quotas do not add up to the declared batch')
        self.groups = {name: np.asarray(indices, dtype=np.int64) for name, indices in groups.items()}
        for name, indices in self.groups.items():
            if indices.ndim != 1 or not len(indices):
                raise ValueError('Empty or invalid group: ' + name)
            if steps * quotas[name] < len(indices):
                raise ValueError('Budget cannot cover group: ' + name)
        union = np.concatenate(list(self.groups.values()))
        if not np.array_equal(np.sort(union), np.arange(count)):
            raise ValueError('Groups must partition every declared sample exactly once')
        self.quotas, self.steps, self.used = dict(quotas), steps, 0
        self.rng = np.random.default_rng(seed)
        self.pool = {name: np.empty(0, dtype=np.int64) for name in groups}
        self.offset = {name: 0 for name in groups}
        self.counts = np.zeros(count, dtype=np.int32)

    def next(self):
        if self.used == self.steps:
            raise StopIteration('Declared training budget exhausted')
        batch = []
        for name, indices in self.groups.items():
            remaining = self.quotas[name]
            while remaining:
                if self.offset[name] == len(self.pool[name]):
                    self.pool[name] = self.rng.permutation(indices)
                    self.offset[name] = 0
                take = min(remaining, len(self.pool[name]) - self.offset[name])
                begin = self.offset[name]
                batch.append(self.pool[name][begin:begin + take])
                self.offset[name] += take
                remaining -= take
        selected = np.concatenate(batch)
        np.add.at(self.counts, selected, 1)
        self.used += 1
        return selected

    def report(self, *, require_complete=False):
        if require_complete and (self.used != self.steps or not np.all(self.counts > 0)):
            raise ValueError('Training has not completed its full-coverage budget')
        return {name: dict(size=len(indices), quota=self.quotas[name],
            planned_draws=self.steps * self.quotas[name], seen=int((self.counts[indices] > 0).sum()),
            minimum=int(self.counts[indices].min()), maximum=int(self.counts[indices].max()))
            for name, indices in self.groups.items()}
