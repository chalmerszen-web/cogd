"""Read the existing board clip over USB without recording or replacing it."""
import argparse
import hashlib
import json
from pathlib import Path
import time
import wave


def export_clip(device, out, *, create=True, deadline=None):
    """Caller owns a stopped listener; errors never resume or overwrite a clip."""
    out = Path(out)
    if create:
        out.mkdir(parents=True, exist_ok=False)
    path = out / 'device-clip.wav'
    if path.exists():
        raise FileExistsError(path)
    def query(command):
        if deadline is None:
            return device.command(command, query=True)
        remaining = deadline-time.monotonic()
        if remaining <= 0:
            raise TimeoutError('Clip export exceeded its observation budget')
        return device.command(command, query=True, timeout=min(2, remaining))

    state = query('agent audio status')
    if not state['clip_ready'] or state['recording']:
        raise RuntimeError('No completed clip available')
    count = state['clip_ms'] * 16
    if not isinstance(count, int) or not 0 < count <= 160000:
        raise RuntimeError('Invalid clip length')
    pcm = bytearray()
    for offset in range(0, count, 256):
        size = min(256, count - offset)
        block = query(f'agent audio clip read {offset} {size}')
        data = bytes.fromhex(block['pcm'])
        if block['offset'] != offset or len(data) != size * 2:
            raise RuntimeError('Invalid clip response')
        pcm.extend(data)
    with wave.open(str(path), 'wb') as output:
        output.setparams((1, 2, 16000, 0, 'NONE', 'not compressed'))
        output.writeframes(pcm)
    report = dict(complete=True, state=state, samples=count,
                  pcm_sha256=hashlib.sha256(pcm).hexdigest(),
                  wav_sha256=hashlib.sha256(path.read_bytes()).hexdigest())
    (out/'report.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    return report


def retain_turn_clip(device, out, *, expected_wake=None):
    """Diagnostic-only pause after @done; resume only after a verified export.

    Do not toggle voice or reconnect its provider. A failed export leaves wake
    off so the next recording cannot destroy the evidence. The caller must
    abort that diagnostic group; it must never count this gap as rapid re-wake.
    """
    out = Path(out)
    out.mkdir(parents=True, exist_ok=False)
    result = dict(complete=False, diagnostic_only=True, listener_paused=False,
                  listener_restored=False, started_monotonic=time.monotonic())
    timeout = device.link.timeout
    try:
        device.link.timeout = .02
        status = device.command('agent status', query=True, timeout=3)
        before = device.command('agent voice status', query=True, timeout=3)
        wake = device.command('agent wake status', query=True, timeout=3)
        result.update(status_before=status, voice_before=before, wake_before=wake)
        if status['busy'] or not before['voice_enabled'] or not wake['enabled']:
            raise RuntimeError('Clip retention requires an idle active voice listener')
        identity = ('record_at', 'end_at', 'samples', 'completed')
        if expected_wake is not None and any(wake.get(k) != expected_wake.get(k) for k in identity):
            raise RuntimeError('The requested capture was replaced before retention')
        device.command('agent wake off', timeout=3)
        stopped = device.command('agent wake status', query=True, timeout=3)
        result['wake_stopped'] = stopped
        if stopped['enabled']:
            raise RuntimeError('Listener did not stop for clip retention')
        result['listener_paused'] = True
        if any(stopped.get(k) != wake.get(k) for k in identity):
            raise RuntimeError('Capture changed while stopping the listener')
        result['clip'] = export_clip(device, out, create=False, deadline=time.monotonic()+30)
        samples = wake.get('samples')
        if samples is not None and not 0 <= samples-result['clip']['samples'] < 16:
            raise RuntimeError('Exported clip does not match the captured sample count')
        device.command('agent wake on', timeout=3)
        after = device.command('agent wake status', query=True, timeout=3)
        voice = device.command('agent voice status', query=True, timeout=3)
        result.update(wake_after=after, voice_after=voice)
        fields = ('voice_enabled', 'mode', 'asr_model', 'tts_model', 'preconnect')
        if not after['enabled'] or any(before.get(k) != voice.get(k) for k in fields):
            raise RuntimeError('Voice listener configuration changed during retention')
        result['listener_restored'] = result['complete'] = True
        return result
    except Exception as error:
        result['error'] = type(error).__name__ + ': ' + str(error)
        raise
    finally:
        device.link.timeout = timeout
        result['ended_monotonic'] = time.monotonic()
        (out/'retention.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf8')


def main():
    from device import Device
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    device = Device(args.out)
    device.link.timeout = .02
    report = {'complete': False}
    try:
        device.command('agent voice off')
        report.update(export_clip(device, args.out, create=False))
    finally:
        device.save()
        device.close()
        (args.out / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf8')


if __name__ == '__main__':
    main()
