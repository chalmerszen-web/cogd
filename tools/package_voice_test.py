"""Package the built and device-verified user test release and its exact sources."""
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import zipfile

from migrate_context import partitions
from kws.flash_guard import validate_application

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build-voice-test'
EVIDENCE = ROOT / 'artifacts/error-limit-20261005'
VERSION = '0.12.0-rc2'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    app = (BUILD / 'esp_hi_agent.bin').read_bytes()
    validate_application(app)
    assert app[48:80].split(b'\0')[0].decode() == VERSION
    assert VERSION.encode() in app
    table = (BUILD / 'partition_table/partition-table.bin').read_bytes()
    layout = partitions(table)
    assert layout['ctx']['size'] == 0x200000 and layout['clip']['size'] == 0x70000
    check = json.loads((EVIDENCE / 'verification.json').read_text('utf8'))
    assert check['ready_for_user_test'] and check['application_sha256'] == digest(app)
    assert check['application_verified'] and check['data_partitions_unchanged']
    release = ROOT / 'firmware/releases' / VERSION
    release.mkdir(parents=True, exist_ok=False)
    options = {}
    for line in (BUILD / 'CMakeCache.txt').read_text('utf8').splitlines():
        if line.startswith('AGENT_') and ':INTERNAL=' not in line:
            name, value = line.split('=', 1)
            options[name.split(':')[0]] = value
    for name in ('AGENT_TLS_COOPERATE', 'AGENT_TLS_ASYM_PERF', 'AGENT_TLS_PUBLIC_PERF',
                 'AGENT_TLS_TX_COMPACT', 'AGENT_TLS_PHASE_TRACE', 'AGENT_VOICE_HEAP_DIAGNOSTICS',
                 'AGENT_ENDPOINT_TRACE', 'AGENT_CAPTURE_PROBE', 'AGENT_KEYWORD_PCM'):
        assert options[name] == 'OFF', name
    assert options['AGENT_USER_TEST'] == 'ON'
    tracked = subprocess.check_output(['git', 'ls-files', '-z', '--cached', '--others',
                                      '--exclude-standard'], cwd=ROOT).decode().split('\0')
    roots = {'core', 'platform', 'plugins', 'boards', 'main', 'components', 'third_party',
             'cmake', 'host_tests', 'tools', 'tests', 'docs'}
    root_files = {'CMakeLists.txt', 'partitions.csv', 'dependencies.lock', '.gitignore',
                  '.gitattributes', 'README.md', 'SPEC.md', 'ACTIONLOG.md',
                  'sdkconfig.defaults', 'sdkconfig.upgrade.defaults', 'sdkconfig.noaudio.defaults',
                  'sdkconfig.voice-test.defaults', '安装最新版.cmd', '打开设备对话.cmd', '开始语音测试.cmd'}
    files = {}
    for name in sorted(set(tracked) - {''}):
        relative = Path(name)
        if name not in root_files and relative.parts[0] not in roots:
            continue
        path = (ROOT / relative).resolve()
        assert path.is_relative_to(ROOT) and path.is_file(), name
        data = path.read_bytes()
        if re.search(rb'\bsk-[A-Za-z0-9_-]{24,}', data):
            raise ValueError('Credential-like literal in source file: ' + name)
        files[name] = data
    assert not any('tls_rx_capacity' in n or 'diagnose_tls_rx_capacity' in n for n in files)
    files['reproduce/sdkconfig'] = (BUILD / 'sdkconfig').read_bytes()
    # The portable build script resolves the model directory within its checkout.
    portable_options = dict(options, AGENT_KWS_VERIFIED_DIR='components/kws_c11/models/voice_test')
    files['reproduce/build-options.json'] = (json.dumps(portable_options, indent=2) + '\n').encode()
    hashes = {name: {'bytes': len(data), 'sha256': digest(data)} for name, data in files.items()}
    with zipfile.ZipFile(release / 'source.zip', 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for name, data in files.items():
            z.writestr(name, data)
        z.writestr('reproduce/source-sha256.json', json.dumps(hashes, indent=2) + '\n')
    with zipfile.ZipFile(release / 'source.zip') as z:
        assert z.testzip() is None
        assert all(digest(z.read(name)) == info['sha256'] for name, info in hashes.items())
    for name, data in (('esp_hi_agent.bin', app), ('partition-table.bin', table)):
        (release / name).write_bytes(data)
    manifest = dict(firmware=VERSION, phase='voice-flow', chip='esp32c3', flash_bytes=4194304,
        app_offset=65536, app_slot_bytes=1572864, accepted=False,
        release_status='user_test_candidate', ready_for_user_test=True,
        files={name: {'bytes': len(data), 'sha256': digest(data)} for name, data in
               [('esp_hi_agent.bin', app), ('partition-table.bin', table)]},
        source='firmware/releases/' + VERSION + '/source.zip',
        source_sha256=digest((release / 'source.zip').read_bytes()), source_files_verified=len(hashes),
        history_budget_bytes=204800, source_options=portable_options,
        build_command='tools/build_voice_test.ps1',
        idf_commit=subprocess.check_output(['git', '-C', '.toolchains/esp-idf', 'rev-parse', 'HEAD'], cwd=ROOT).decode().strip(),
        update='fresh verified backup; application only; data partitions preserved',
        evidence='docs/FIRMWARE_0.12.0_RC2.md',
        limitations=['False wake remains possible; no new wake-model training.',
                     'Original-clip regression fixes error limit, but spoken final-color claims remain incorrect; intermediate reply also leaked tool markup.',
                     'Independent human Mandarin/Cantonese and one-second reply targets remain unaccepted.'])
    (release / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf8')
    shutil.copyfile(ROOT / 'docs/FIRMWARE_0.12.0_RC2.md', release / 'README.md')
    shutil.copyfile(EVIDENCE / 'verification.json', release / 'verification.json')
    latest = ROOT / 'firmware/latest'
    for name in ('esp_hi_agent.bin', 'partition-table.bin', 'manifest.json', 'README.md'):
        shutil.copyfile(release / name, latest / name)
    output = ROOT / 'outputs' / ('esp-hi-' + VERSION + '-test.zip')
    output.parent.mkdir(exist_ok=True)
    with zipfile.ZipFile(output, 'x', zipfile.ZIP_DEFLATED) as z:
        for path in release.iterdir():
            if path.is_file():
                z.write(path, path.name)
    with zipfile.ZipFile(output) as z:
        assert z.testzip() is None
    print(json.dumps(dict(firmware=VERSION, application_bytes=len(app),
        application_sha256=digest(app), source_files=len(hashes), package=str(output)), ensure_ascii=False))


if __name__ == '__main__':
    main()
