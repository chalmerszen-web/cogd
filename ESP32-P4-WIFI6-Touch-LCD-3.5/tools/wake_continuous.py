"""P4-only quiet wake playback wrapper for the shared continuous voice test."""
import argparse, hashlib, json, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/voice'))
import continuous_rewake

def main():
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('--wake-gain', type=float, required=True)
    args, rest = parser.parse_known_args()
    if not .03 <= args.wake_gain <= .35:
        raise ValueError('Wake playback gain must be .03 to .35')
    output = Path(rest[rest.index('--out') + 1])
    original_play = continuous_rewake.play
    def play(path, gain):
        actual_gain = args.wake_gain if Path(path).name.startswith('wake-') else gain
        return original_play(path, gain=actual_gain)
    continuous_rewake.play = play
    sys.argv = [str(Path(__file__)), *rest]
    try:
        continuous_rewake.main()
    finally:
        report_path = output / 'report.json'
        if report_path.exists():
            report = json.loads(report_path.read_text(encoding='utf8'))
            report['p4_wake_gain'] = args.wake_gain
            report['p4_wrapper_sha256'] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
            report['acoustic_scope'] = 'Fixed speaker position, synthetic wake audio; no calibrated SPL or distance claim'
            report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')

if __name__ == '__main__':
    main()
