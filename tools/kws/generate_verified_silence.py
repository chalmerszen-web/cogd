"""Generate a hash-bound, typed initial state for the opt-in24/24/48 runtime.

Runs host C11 inference only; does not train or access audio/USB/network devices.
The three input models must be explicitly supplied. Output never changes the
default E/L seed or IDF selection. Compile the emitted hash check into consumers.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
KWS = ROOT / 'components/kws_c11'
SOURCES = [
    'components/kws_c11/include/kws.h', 'components/kws_c11/kws_internal.h',
    'components/kws_c11/kws_buffers.h',
    'components/kws_c11/include/kws_verified.h', 'components/kws_c11/kws_verified_internal.h',
    'components/kws_c11/kws_verified.c', 'components/kws_c11/include/kws_temporal_owner.h',
    'components/kws_c11/kws_temporal_owner.c', 'components/kws_c11/kws_nn.c',
    'components/kws_c11/kws_frontend.c', 'components/kws_c11/kws_fft.c',
    'components/kws_c11/kws_pcen.c', 'components/kws_c11/include/kws_pcen.h',
    'components/kws_c11/kws_quant.c', 'components/kws_c11/kws_detector.c',
    'components/kws_c11/generated/tables.c', 'components/kws_c11/generated/fft_config.inc',
    'third_party/kissfft/kiss_fft.h', 'third_party/kissfft/kiss_fft.c',
    'third_party/kissfft/_kiss_fft_guts.h', 'tools/kws/export_verified_silence.c',
    'tools/kws/generate_verified_silence.py',
]

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('first', 'second', 'verifier', 'out', 'build'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    args.build.mkdir(parents=True, exist_ok=True)
    # Alias only the exported descriptors; local weight/bias symbols remain static.
    models = []
    for name, original, symbol in [('first', 'kws_trained_model', 'kws_verified_primary'),
                                   ('second', 'kws_secondary_model', 'kws_verified_secondary'),
                                   ('verifier', 'kws_trained_model', 'kws_trained_model')]:
        source = getattr(args, name).resolve()
        value = source.read_text(encoding='utf8')
        if value.count(original) != 1:
            raise ValueError(f'Expected one exported descriptor in {source}')
        target = args.out / (name + '.c')
        target.write_text(value.replace(original, symbol), encoding='utf8', newline='\n')
        models.append(target)
    code = ['kws_quant.c', 'kws_detector.c', 'kws_nn.c', 'kws_frontend.c',
            'kws_fft.c', 'kws_pcen.c', 'kws_temporal_owner.c', 'kws_verified.c', 'generated/tables.c']
    command = ['gcc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
        '-DKWS_BUILD_CHANNELS=48', '-DKWS_ALLOW_MIXED24=1',
        '-I' + str(KWS / 'include'), '-I' + str(KWS), '-I' + str(ROOT / 'third_party/kissfft'),
        *[str(KWS / name) for name in code], *map(str, models),
        str(ROOT / 'tools/kws/export_verified_silence.c'), '-lm',
        '-o', str(args.build / 'export_verified_silence')]
    subprocess.run(command, check=True)
    run = subprocess.run([str(args.build / 'export_verified_silence')],
                         check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    seed = args.out / 'kws_verified_seed.inc'
    seed.write_bytes(run.stdout)
    paths = [ROOT / name for name in SOURCES] + [args.first.resolve(), args.second.resolve(),
        args.verifier.resolve(), *models, seed]
    hashes = {path.resolve().relative_to(ROOT).as_posix(): sha(path) for path in paths}
    measured = json.loads(run.stderr)
    manifest = dict(format='kws.verified.silence/1', topology=[24, 24, 48],
        capacity=48, allow_registered24=True, frontend='fixed_logmel_no_pcen',
        exported_state='typed causal fields only; compact24 history; no pointers/FFT scratch',
        **measured, hashes=hashes, command=command)
    (args.out / 'kws_verified_seed.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf8')
    script = [
        '# Generated; include this before enabling AGENT_KWS_VERIFIED_PRIME.',
        'get_filename_component(KWS_VERIFIED_SEED_ROOT "${CMAKE_CURRENT_LIST_DIR}/' +
            Path(os.path.relpath(ROOT, args.out.resolve())).as_posix() + '" ABSOLUTE)',
        'function(kws_verified_seed_check relative expected)',
        '  set(path "${KWS_VERIFIED_SEED_ROOT}/${relative}")',
        '  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")',
        '  file(SHA256 "${path}" actual)',
        '  if(NOT actual STREQUAL expected)',
        '    message(FATAL_ERROR "Verified silence seed is stale: ${path}")',
        '  endif()', 'endfunction()']
    for path, value in hashes.items():
        script.append(f'kws_verified_seed_check("{Path(path).as_posix()}" "{value}")')
    (args.out / 'kws_verified_seed.cmake').write_text('\n'.join(script) + '\n', encoding='utf8')
    print(json.dumps(dict(typed_seed_generated=True, **measured)), flush=True)

if __name__ == '__main__':
    main()
