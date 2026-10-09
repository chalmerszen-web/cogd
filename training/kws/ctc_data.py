"""Full-source word targets for the experimental CTC token ABI.

Written source labels are not human phonetic annotations. In particular, an
unverified non-target ending uses OTHER, never an invented tone annotation.
"""
WORDS = ('你好小言', '你好乐鑫', '你好小叶', '你好小明',
         '你好小智', '你好小杨', '你好小燕', '你好小王')
TOKENS = ('blank', 'NI', 'HAO', 'XIAO', 'YAN2', 'NEI', 'HOU', 'SIU',
          'JIN4', 'other', 'YAN4', 'YAN1', 'JIN3', 'JIN1')


def token_target(text, language, *, full_phrase=False):
    if full_phrase is not True or not isinstance(text, str):
        raise ValueError('Complete-source evidence is required')
    text = ''.join(c for c in text if c not in ' ,，。!！?？')
    if text not in WORDS or language not in ('zh', 'yue'):
        raise ValueError('Unknown word or language; do not guess token targets')
    prefix = (1, 2, 3) if language == 'zh' else (5, 6, 7)
    if text == WORDS[0]:
        return prefix + (4 if language == 'zh' else 8,)
    if text == WORDS[1]:
        return prefix[:2] + (9, 9)
    return prefix + (9,)
