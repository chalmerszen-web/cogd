"""Explicit host optimizer experiment; legacy recipes retain constant rates."""
import math

CONTRACT = 'xiaoyan_gru64_q6_cosine6000_v1'


def cosine_rate(start, finish, steps, step):
    if (isinstance(start, bool) or isinstance(finish, bool) or
            not isinstance(start, (int, float)) or not isinstance(finish, (int, float)) or
            not math.isfinite(start) or not math.isfinite(finish) or
            not 0 < finish <= start or type(steps) is not int or steps < 2 or
            type(step) is not int or not 1 <= step <= steps):
        raise ValueError('Invalid finite optimizer schedule')
    if step == 1:
        return float(start)
    if step == steps:
        return float(finish)
    return finish + (start - finish) * (1 + math.cos(math.pi * (step - 1) / (steps - 1))) / 2


def declared_schedule(cfg, plan):
    name = cfg.get('learning_rate_schedule')
    if name is None:
        return None
    if (name != 'cosine_v1' or plan.get('optimization_contract') != CONTRACT or
            cfg.get('hidden') != 64 or cfg.get('matrix_q') != 6 or
            not cfg.get('named_word_regularizer') or cfg.get('phonetic_regularizer') or
            cfg.get('steps') != 6000 or cfg.get('seed') != 2026100479 or
            cfg.get('learning_rate') != .003 or cfg.get('learning_rate_final') != .0003):
        raise ValueError('Explicit fixed GRU64/Q6 cosine experiment required')
    values = cfg['learning_rate'], cfg['learning_rate_final'], cfg['steps']
    cosine_rate(*values, 1)
    return values
