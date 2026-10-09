"""Load the explicitly requested local skill credential without logging it."""
from pathlib import Path
import sys


def qianwen_key():
    skill = Path.home()/'.codex/skills/qianwen-model-suite/scripts'
    sys.path.insert(0, str(skill))
    from qianwen import configuration, load_key
    config = configuration(None)
    if config['origin'] != 'https://dashscope.aliyuncs.com':
        raise ValueError('Firmware endpoint must match the configured official region')
    return load_key(config, None)
