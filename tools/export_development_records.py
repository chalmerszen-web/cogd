"""Export public copies of development records; keep original evidence unchanged."""
import argparse
import collections
import hashlib
import json
import os
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
TEXT = {'.json', '.jsonl', '.log', '.txt', '.md', '.csv', '.tsv', '.ndjson',
        '.py', '.c', '.h', '.inc', '.cmake', '.ps1', '.sh', '.yaml', '.yml',
        '.toml', '.patch', '.diff', '.stdout', '.stderr', '.err', '.s', '.ld',
        '.defaults', '.config', '.source', '.sha256'}
SKIP_DIRS = {'node_modules', 'site-packages', '__pycache__', '.git', '.local',
             '.cache', 'CMakeFiles', 'managed_components'}
MODEL_CACHES = ('artifacts/kws-phase2/tts/mandarin/',
                'artifacts/kws-phase2/tts/cantonese/',
                'artifacts/kws-phase2/tts/references/', 'artifacts/kws-phase2/asr/')
SENSITIVE_KEY = re.compile(
    r'^(?:password|passwd|passphrase|api[_-]?key|access[_-]?token|refresh[_-]?token|'
    r'authorization|client[_-]?secret|secret[_-]?key|wifi[_-]?(?:password|ssid)|ssid)$', re.I)
TOKENS = re.compile(
    r'\b(?:sk-[A-Za-z0-9_-]{20,}|gh[pousr]_[A-Za-z0-9]{30,}|'
    r'github_pat_[A-Za-z0-9_]{30,}|AKIA[0-9A-Z]{16}|'
    r'xox[baprs]-[A-Za-z0-9-]{20,})\b')
PRIVATE_KEY = re.compile(
    r'-----BEGIN (?:[A-Z ]*PRIVATE KEY)-----.*?-----END (?:[A-Z ]*PRIVATE KEY)-----', re.S)
AUTH = re.compile(r'(?i)(authorization\s*[:=]\s*["\s]*(?:bearer|basic)\s+)[A-Za-z0-9_./+=-]{12,}')
QUOTED_SECRET = re.compile(
    r'''(?ix)(["']?(?:password|passwd|passphrase|api[_-]?key|access[_-]?token|refresh[_-]?token|client[_-]?secret|wifi[_-]?(?:password|ssid)|ssid)["']?\s*[:=]\s*)(["'])([^\r\n"']+)(\2)''')
PRIVATE_CONTEXT = re.compile(r'^(?:context(?:[-_]export|[-_]dump)?|history|events)\.(?:jsonl|ndjson)$', re.I)


def scrub_text(value, counts):
    for label, pattern, replacement in (
        ('token', TOKENS, '[REDACTED]'),
        ('private_key', PRIVATE_KEY, '[PRIVATE KEY REMOVED]'),
        ('authorization', AUTH, r'\1[REDACTED]'),
        ('credential_field', QUOTED_SECRET, r'\1\2[REDACTED]\4'),
    ):
        value, changed = pattern.subn(replacement, value)
        counts[label] += changed
    return value


def scrub_json(value, counts):
    if isinstance(value, dict):
        result = {}
        for key, item in value.items():
            if SENSITIVE_KEY.fullmatch(key) and isinstance(item, str) and item:
                result[key] = '[REDACTED]'
                counts['credential_field'] += 1
            else:
                result[key] = scrub_json(item, counts)
        return result
    if isinstance(value, list):
        return [scrub_json(item, counts) for item in value]
    return scrub_text(value, counts) if isinstance(value, str) else value


def export(source, destination):
    raw = source.read_bytes()
    counts = collections.Counter()
    # Some PowerShell redirections produced UTF-16 logs; public copies use UTF-8.
    if raw.startswith((b'\xff\xfe', b'\xfe\xff')):
        text = raw.decode('utf-16', errors='replace')
    elif raw[:200].count(b'\0') > 20:
        text = raw.decode('utf-16-le', errors='replace')
    else:
        text = raw.decode('utf-8-sig', errors='replace')
    if source.suffix.lower() == '.json':
        try:
            text = json.dumps(scrub_json(json.loads(text), counts), ensure_ascii=False, indent=2) + '\n'
        except (ValueError, RecursionError):
            text = scrub_text(text, counts)
    else:
        text = scrub_text(text, counts)
    data = text.encode('utf8')
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(data)
    return {'source_bytes': len(raw), 'bytes': len(data),
            'source_sha256': hashlib.sha256(raw).hexdigest(),
            'sha256': hashlib.sha256(data).hexdigest(),
            'redactions': {key: n for key, n in counts.items() if n}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.relative_to(ROOT)
    output.mkdir(parents=True, exist_ok=True)
    entries = {}
    excluded = []
    for base, dirs, files in os.walk(ROOT / 'artifacts'):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS and not d.startswith(('build', 'publication-'))]
        for name in files:
            source = Path(base) / name
            relative = source.relative_to(ROOT).as_posix()
            if source.suffix.lower() not in TEXT or relative.startswith(MODEL_CACHES):
                continue
            if source.is_symlink() or PRIVATE_CONTEXT.fullmatch(name):
                excluded.append({'path': relative, 'reason': 'private context or symlink'})
                continue
            entries[relative] = export(source, output / relative)
    manifest = {'files': entries, 'excluded_records': excluded,
                'policy': 'Public UTF-8 copies with credential fields/tokens removed. '
                          'Original files are unchanged. Raw Flash, private context exports, '
                          'audio, binary caches, downloaded models and build directories are excluded.'}
    (output / 'record-manifest.json').write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf8')
    print(json.dumps({'files': len(entries), 'bytes': sum(x['bytes'] for x in entries.values()),
                      'redacted_files': sum(bool(x['redactions']) for x in entries.values()),
                      'excluded_private_records': len(excluded)}, ensure_ascii=False), flush=True)


if __name__ == '__main__':
    main()
