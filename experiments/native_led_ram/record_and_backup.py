"""Record the existing device and verify a full, read-only Flash backup."""
from datetime import datetime, timedelta, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'artifacts/native-led-20261009'
sys.path.insert(0, str(ROOT / 'tools'))
from migrate_context import partitions, application_size


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    backup = OUT / 'flash-before.bin'
    if backup.exists():
        raise SystemExit('Existing backup retained; refusing to overwrite it.')
    state = json.loads((OUT / 'baseline/state.json').read_text(encoding='utf-8'))
    release = ROOT / 'firmware/releases' / state['agent status']['version']
    manifest = json.loads((release / 'manifest.json').read_text(encoding='utf-8'))
    command = [sys.executable, '-m', 'esptool', '--chip', 'esp32c3',
               '--port', 'COM5', '--baud', '460800']
    with (OUT / 'backup.log').open('w', encoding='utf-8') as log:
        for after, operation in [
            ('no-reset', ['read-flash', '0', '0x400000', str(backup)]),
            ('hard-reset', ['verify-flash', '0', str(backup)]),
        ]:
            result = subprocess.run(command + ['--after', after] + operation,
                                    stdout=log, stderr=subprocess.STDOUT)
            log.flush()
            if result.returncode:
                raise SystemExit('Backup verification failed; see backup.log')
    flash = backup.read_bytes()
    assert len(flash) == 4 * 1024 * 1024
    table = partitions(flash[0x8000:0x9000])
    app_offset = table['factory']['offset']
    image = flash[app_offset:app_offset + table['factory']['size']]
    image = image[:application_size(image)]
    assert sha(image) == manifest['files']['esp_hi_agent.bin']['sha256']
    (OUT / 'application-before.bin').write_bytes(image)
    report = {
        'recorded_at': datetime.now(timezone(timedelta(hours=8))).isoformat(),
        'port': 'COM5', 'version': state['agent status']['version'],
        'full_flash_bytes': len(flash), 'full_flash_sha256': sha(flash),
        'application_bytes': len(image), 'application_sha256': sha(image),
        'matches_published_release': True, 'backup_device_verified': True,
        'partitions': table, 'original_light': state['agent light get'],
        'original_voice_enabled': state['agent voice status']['voice_enabled'],
        'release_manifest': str(release / 'manifest.json'),
        'source_archive_sha256': manifest['source_sha256'],
        'experiment_method': 'RAM-only native RV32IMC image; no Flash write planned',
    }
    (OUT / 'firmware-before.json').write_text(
        json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps(report, ensure_ascii=False, indent=2), flush=True)


if __name__ == '__main__':
    main()
