"""Freeze the tested streaming-voice source and verified app, excluding local evidence/secrets."""
import hashlib
import io
import json
from pathlib import Path
import re
import subprocess
import zipfile
from qianwen_credentials import qianwen_key

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'artifacts/voice-stream/delivery'
BUILD = ROOT / 'build-kws-fusion-ek'
PACKAGE = ROOT / 'firmware/latest'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT)


def main():
    assert not OUT.exists(), 'Keep earlier evidence; do not overwrite a frozen release'
    app = (BUILD / 'esp_hi_agent.bin').read_bytes()
    table = (BUILD / 'partition_table/partition-table.bin').read_bytes()
    backup = Path((ROOT/'artifacts/kws-voice-stream/flash-backup-path.txt').read_text().strip())
    flashed = json.loads((backup/'manifest.json').read_text())
    assert flashed['installed'] and flashed['application_verified'] and flashed['data_partitions_unchanged']
    assert sha(app) == flashed['new_application_sha256'] and len(app) == flashed['new_application_bytes']
    assert sha(table) == '03135fff90b8a4983a4a7a758018626dc296756ac79578e5df9c7b8eb64b3179'
    rollback = ROOT/'firmware/rollback/0.8.1-voice-speed'
    previous = json.loads((rollback/'manifest.json').read_text(encoding='utf8'))
    for name, expected in previous['files'].items():
        data = (rollback/name).read_bytes()
        assert len(data) == expected['bytes'] and sha(data) == expected['sha256'], name
    previous_source = ROOT/previous['source']
    assert sha(previous_source.read_bytes()) == previous['source_sha256']

    # Known credentials are read only in memory, never emitted or archived.
    keys = [qianwen_key().encode()]
    original_keys = Path.home()/'Desktop/key.txt'
    if original_keys.exists():
        keys += [s.encode() for s in re.findall(r'\bsk-[A-Za-z0-9_-]+', original_keys.read_text(encoding='utf-8-sig'))]

    def check(data, name, depth=0):
        if any(key in data for key in keys):
            raise ValueError('Credential material in ' + name)
        if depth < 2 and zipfile.is_zipfile(io.BytesIO(data)):
            with zipfile.ZipFile(io.BytesIO(data)) as archive:
                for member in archive.infolist():
                    if not member.is_dir():
                        check(archive.read(member), name + '/' + member.filename, depth + 1)

    names = sorted(set(git('ls-files', '-z', '--cached', '--others', '--exclude-standard').decode().split('\0')) - {''})
    files = {}
    for name in names:
        path = (ROOT/name).resolve()
        assert path.is_relative_to(ROOT.resolve()) and path.is_file(), name
        data = path.read_bytes(); check(data, name); files[name] = data
    config = (BUILD/'sdkconfig').read_bytes(); check(config, 'sdkconfig')
    files['reproduce/sdkconfig'] = config
    options = {}
    for line in (BUILD/'CMakeCache.txt').read_text().splitlines():
        if line.startswith('AGENT_'):
            key, value = line.split('=', 1); options[key.split(':')[0]] = value
    files['reproduce/build-options.json'] = (json.dumps(options, indent=2)+'\n').encode()
    checksums = {name: {'bytes': len(data), 'sha256': sha(data)} for name, data in files.items()}
    checksum_data = (json.dumps(checksums, ensure_ascii=False, indent=2)+'\n').encode()
    OUT.mkdir()
    (OUT/'source-sha256.json').write_bytes(checksum_data)
    with zipfile.ZipFile(OUT/'source.zip', 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for name, data in files.items(): archive.writestr(name, data)
        archive.writestr('reproduce/source-sha256.json', checksum_data)
    with zipfile.ZipFile(OUT/'source.zip') as archive:
        assert archive.testzip() is None
        for name, expected in checksums.items(): assert sha(archive.read(name)) == expected['sha256'], name
    source_sha = sha((OUT/'source.zip').read_bytes())
    manifest = {
        'firmware': '0.9.0-qwen-stream', 'phase': 'voice-stream', 'chip': 'esp32c3',
        'flash_bytes': 4194304, 'app_offset': 65536, 'app_slot_bytes': 1572864,
        'files': {name: {'bytes': len(data), 'sha256': sha(data)}
                  for name, data in [('esp_hi_agent.bin', app), ('partition-table.bin', table)]},
        'source': 'artifacts/voice-stream/delivery/source.zip', 'source_sha256': source_sha,
        'history_budget_bytes': 204800,
        'update': 'verified full backup; application-only; data partitions preserved',
        'build_command': 'tools/build_kws.ps1 -Mode trained -Fusion -FusionPair ek -WakeThreshold 740',
    }
    (PACKAGE/'esp_hi_agent.bin').write_bytes(app)
    (PACKAGE/'partition-table.bin').write_bytes(table)
    (PACKAGE/'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+'\n', encoding='utf8')
    (PACKAGE/'README.md').write_text('''# ESP-HI 0.9.0-qwen-stream

使用根目录安装最新版.cmd。先完整备份/校验 Flash，仅写应用，保留凭据和历史。
agent voice on 后说“你好，小言”，听到开始提示音再说指令。回答后自动恢复监听。
实时 ASR/TTS 使用本机技能中已配置的千问账户，设备独立联网调用。
使用、实测延迟和边界见 docs/VOICE_CLOUD_USAGE.md、docs/VOICE_STREAM_REPORT.md。
200KiB历史预算与分区不变；0.8.1回滚包位于 firmware/rollback/0.8.1-voice-speed。
0.6.3回滚必须先通过 tools/voice/rollback.py --apply 保存兼容检查点。
''', encoding='utf8')
    release = {'manifest': manifest, 'git_parent': git('rev-parse', 'HEAD').decode().strip(),
               'idf_commit': git('-C', '.toolchains/esp-idf', 'rev-parse', 'HEAD').decode().strip(),
               'source_files': len(checksums), 'known_credential_matches': 0,
               'source_archive_verified': True, 'snapshot_includes_working_tree_changes': True,
               'sdkconfig_sha256': sha(config), 'build_options': options,
               'note': 'Original binary is hash-verified; rebuild path/timestamps may change its hash.'}
    (OUT/'release.json').write_text(json.dumps(release, ensure_ascii=False, indent=2)+'\n', encoding='utf8')
    (OUT/'README.md').write_text('''# 实时语音本机交付

source.zip 包含工作区源码、固定模型/依赖、历史源码及 reproduce 构建配置/逐文件哈希。
原始录音、Flash备份、Key与构建缓存未打包。已扫描本机使用的已知Key，未发现匹配。
release.json 记录来源、构建选项和安装固件哈希，安装包位于 firmware/latest。
解压到独立目录，用 tools/bootstrap.ps1 准备固定IDF；将 reproduce/sdkconfig 复制到
新建 build-kws-fusion-ek/sdkconfig，再执行 release.json 中的 build_command。
原始测试证据与失败记录在上级目录；结论与限制见 docs/VOICE_STREAM_REPORT.md。
0.8.1安装包和原始源码快照独立保留；0.6.3需使用兼容检查点回滚入口。
''', encoding='utf8')
    print(json.dumps({'source_files': len(checksums), 'source_sha256': source_sha,
                      'app_bytes': len(app), 'app_sha256': sha(app), 'known_credential_matches': 0, 'verified': True}))


if __name__ == '__main__':
    main()
