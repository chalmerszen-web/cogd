"""Apply one audited offline-only patch to the pinned CosyVoice source copy."""
import argparse
import difflib
import hashlib
import json
from pathlib import Path

def main():
    ap=argparse.ArgumentParser();ap.add_argument('source',type=Path)
    ap.add_argument('--out',type=Path,default=Path('artifacts/kws-phase2/tts-source-patch'));args=ap.parse_args()
    path=args.source/'cosyvoice/cli/frontend.py';before=path.read_text()
    original='            self.zh_tn_model = ZhNormalizer(remove_erhua=False)\n            self.en_tn_model = EnNormalizer()'
    replacement=('            # Local pilot supplies normalized Chinese text and forbids hidden model downloads.\n'
                 '            offline_raw = os.getenv("COGD_TTS_PRENORMALIZED") == "1"\n'
                 '            self.zh_tn_model = None if offline_raw else ZhNormalizer(remove_erhua=False)\n'
                 '            self.en_tn_model = None if offline_raw else EnNormalizer()')
    if replacement in before:print('Offline patch already applied');return
    if before.count(original)!=1:raise ValueError('Source does not match pinned patch context')
    after=before.replace(original,replacement)
    args.out.mkdir(parents=True,exist_ok=True)
    (args.out/'frontend.patch').write_text(''.join(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='upstream/frontend.py',tofile='local/frontend.py')))
    (args.out/'hashes.json').write_text(json.dumps(dict(upstream_sha256=hashlib.sha256(before.encode()).hexdigest(),
        modified_sha256=hashlib.sha256(after.encode()).hexdigest(),reason='Skip unused text-normalization download when all calls use text_frontend=False'),indent=2))
    path.write_text(after)

if __name__=='__main__':main()
