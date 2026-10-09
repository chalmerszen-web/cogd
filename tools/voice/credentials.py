"""Read only the explicitly selected local credential; never print its value."""
from pathlib import Path
import re


def platform_key(path):
    text=Path(path).read_text(encoding='utf-8-sig').strip()
    if re.fullmatch(r'sk-[A-Za-z0-9_-]+',text):return text
    keys=[m.group(0) for line in text.splitlines() if 'vocalign' in line.lower()
          for m in [re.search(r'\bsk-[A-Za-z0-9_-]+',line)] if m]
    if len(keys)!=1:raise ValueError('Expected one Vocalign-labeled API key')
    return keys[0]
