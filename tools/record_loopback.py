"""Bounded Windows output capture for acoustic-test source attribution.

Uses the selected output's WASAPI loopback, without microphone monitoring or
changing endpoint settings. Bounded digital silence keeps its output clock
running across test-source handles. SoundCard is confined to a local test venv.
"""
import argparse
import json
from pathlib import Path
import subprocess
import threading
import time
import wave


class LoopbackRecorder:
    def __init__(self, path, seconds=25, device='头戴式耳机 (Misiom-Shooter)', *, keepalive=True, buffer_ms=500):
        self.path, self.seconds, self.device = Path(path), seconds, device
        self.stop_path = self.path.with_suffix('.loopback-stop')
        self.keepalive, self.buffer_ms = keepalive, buffer_ms
        self.process = None

    def __enter__(self):
        runtime = Path(__file__).resolve().parents[1]/'.local/loopback-python/Scripts/python.exe'
        if not runtime.is_file():
            raise RuntimeError('Create the local loopback environment first; see requirements-loopback.txt')
        self.path.parent.mkdir(parents=True, exist_ok=True)
        if any(p.exists() for p in (self.path, self.path.with_suffix('.loopback.json'), self.stop_path)):
            raise FileExistsError('Refusing to overwrite loopback evidence')
        self.log = self.path.with_suffix('.loopback.log').open('wb')
        command=[str(runtime), '-X', 'utf8', __file__, str(self.path), '--seconds', str(self.seconds),
                 '--device', self.device, '--buffer-ms', str(self.buffer_ms), '--stop-file', str(self.stop_path)]
        command.append('--keepalive' if self.keepalive else '--no-keepalive')
        self.process = subprocess.Popen(command,
            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=self.log,
            creationflags=subprocess.CREATE_NO_WINDOW)
        deadline = time.monotonic()+6
        while time.monotonic()<deadline:
            if self.process.poll() is not None:
                self.log.close()
                raise RuntimeError('Loopback initialization failed; inspect its local log')
            if self.path.with_suffix('.loopback.json').exists():
                meta=json.loads(self.path.with_suffix('.loopback.json').read_text(encoding='utf8'))
                if 'started' in meta: return self
            time.sleep(.02)
        self.__exit__(TimeoutError, None, None)
        raise TimeoutError('Loopback initialization timed out')

    def __exit__(self, *error):
        try:
            if self.process and self.process.poll() is None:
                try:
                    self.stop_path.touch(); self.process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    self.process.terminate(); self.process.wait(timeout=5)
                    raise TimeoutError('Loopback did not stop; incomplete evidence retained')
            if self.process.returncode and not error[0]:
                raise RuntimeError('Loopback failed; inspect its local log')
        finally:
            self.log.close()


def capture_packet(recorder):
    """Copy one real WASAPI packet; an empty poll must never invent audio."""
    import numpy as np
    from soundcard import mediafoundation as mf
    if not recorder._capture_available_frames():
        return None
    if list(recorder.channelmap) != [0, 1]:
        raise RuntimeError('Expected the verified stereo loopback format')
    data = mf._ffi.new('BYTE**'); frames = mf._ffi.new('UINT32*')
    flags = mf._ffi.new('DWORD*'); position = mf._ffi.new('UINT64*')
    qpc = mf._ffi.new('UINT64*'); capture = recorder._ppCaptureClient
    mf._com.check_error(capture[0][0].lpVtbl.GetBuffer(
        capture[0], data, frames, flags, position, qpc))
    if not frames[0]:
        return None
    try:
        if flags[0] & 2:  # Native AUDCLNT_BUFFERFLAGS_SILENT, with a real frame count.
            values = np.zeros((frames[0], 2), dtype=np.float32)
        else:
            if data[0] == mf._ffi.NULL:
                raise RuntimeError('Missing loopback packet data')
            values = np.frombuffer(mf._ffi.buffer(data[0], frames[0]*8), '<f4').copy().reshape(-1, 2)
        if not np.isfinite(values).all():
            raise RuntimeError('Non-finite loopback PCM')
        return values, dict(frames=int(frames[0]), flags=int(flags[0]),
                            position=int(position[0]), qpc_100ns=int(qpc[0]))
    finally:
        recorder._capture_release(frames[0])


def packet_findings(blocks):
    """Keep packet errors separate from a host thread's variable read latency."""
    return dict(discontinuities_after_first=sum(bool(b['flags'] & 1) for b in blocks[1:]),
                timestamp_errors=sum(bool(b['flags'] & 4) for b in blocks),
                position_gaps=sum(b['position'] != a['position']+a['frames'] for a, b in zip(blocks, blocks[1:])),
                nonmonotonic_timestamps=sum(b['qpc_100ns'] <= a['qpc_100ns'] for a, b in zip(blocks, blocks[1:])))


def main():
    import hashlib
    import importlib.metadata
    import warnings
    import numpy as np
    import soundcard as sc
    if importlib.metadata.version('SoundCard') != '0.4.6':
        raise RuntimeError('Revalidate the loopback packet adapter for this SoundCard version')
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('path', type=Path)
    p.add_argument('--seconds', type=float, default=25)
    p.add_argument('--device', default='头戴式耳机 (Misiom-Shooter)')
    p.add_argument('--keepalive',action=argparse.BooleanOptionalAction,default=True,
                   help='Keep the selected output clock active with bounded digital silence')
    p.add_argument('--buffer-ms',type=int,choices=(10,40,100,500),default=500)
    p.add_argument('--stop-file',type=Path)
    args = p.parse_args()
    if not 0 < args.seconds <= 90 or args.path.exists() or (args.stop_file and args.stop_file.exists()):
        p.error('Use a new path and a bounded 0..90-second window')
    candidates = [d for d in sc.all_microphones(include_loopback=True) if d.isloopback and d.name == args.device]
    if len(candidates)!=1:
        raise RuntimeError('Expected exactly one matching loopback endpoint')
    device = candidates[0]
    # Keep output advancing until recorder teardown. A shared stop flag could
    # stop its clock while mic.record() is still waiting for the last packet.
    clock_stop = threading.Event()
    meta = dict(device=device.name, device_id=device.id, soundcard=importlib.metadata.version('SoundCard'),
                rate=48000, channels=2, pcm='signed 16-bit, no normalization', frames=0, blocks=[], complete=False,
                buffer_ms=args.buffer_ms,silent_output_keepalive=args.keepalive,
                capture_backend='wasapi-packets-v1', synthesized_frames=0)
    def save():
        temporary = args.path.with_suffix('.loopback.tmp')
        temporary.write_text(json.dumps(meta, ensure_ascii=False, indent=2), encoding='utf8')
        temporary.replace(args.path.with_suffix('.loopback.json'))
    args.path.parent.mkdir(parents=True, exist_ok=True)
    clock=None;clock_error=[];clock_ready=threading.Event()
    def keep_clock():
        try:
            from wave_play import silence
            meta['silent_output']=silence(args.seconds+5,args.device,clock_stop,clock_ready)
        except Exception as error: clock_error.append(str(error))
        finally: clock_ready.set()
    try:
        if args.keepalive:
            clock=threading.Thread(target=keep_clock,daemon=True);clock.start()
            if not clock_ready.wait(5) or clock_error:
                raise RuntimeError('Silent output could not start: '+str(clock_error))
        with warnings.catch_warnings(record=True) as notices, device.recorder(48000, channels=2, blocksize=48*args.buffer_ms) as mic, wave.open(str(args.path),'wb') as wav:
            warnings.simplefilter('always')
            wav.setparams((2,2,48000,0,'NONE','NONE'))
            meta['buffer_frames']=mic.buffersize
            if meta['buffer_frames']<48*args.buffer_ms:raise RuntimeError('Actual loopback buffer is smaller than requested')
            meta['started'] = time.monotonic(); save()
            while time.monotonic()-meta['started'] < args.seconds and not (args.stop_file and args.stop_file.exists()):
                if clock_error or (clock and not clock.is_alive()):
                    raise RuntimeError('Silent output clock ended during capture: '+str(clock_error))
                packet = capture_packet(mic)
                if packet is None:
                    time.sleep(.002)
                    continue
                x, info = packet
                meta['blocks'].append(dict(at=meta['frames'], **info, host_time=time.monotonic(), peak=float(np.max(np.abs(x)))))
                wav.writeframesraw(np.clip(np.rint(x*32768),-32768,32767).astype('<i2').tobytes())
                meta['frames'] += len(x)
            meta['warnings'] = [str(n.message) for n in notices]
            findings = packet_findings(meta['blocks'])
            meta.update(findings)
            meta['warnings'].extend(f'{name}: {count}' for name, count in findings.items() if count)
            if not meta['frames']:
                meta['warnings'].append('No native audio packets received')
            meta['finished'] = time.monotonic()
            meta['stop_reason'] = 'parent_request' if args.stop_file and args.stop_file.exists() else 'duration_bound'
        meta['sha256'] = hashlib.sha256(args.path.read_bytes()).hexdigest()
        meta['complete'] = not meta['warnings']
    finally:
        clock_stop.set()
        if clock:
            clock.join(2)
            if clock.is_alive() or clock_error:
                meta['complete']=False
                meta['silent_output_error']=clock_error or ['Silent output did not stop']
        save()
    if not meta['complete']: raise RuntimeError('Incomplete loopback capture; evidence retained')


if __name__ == '__main__':
    main()
