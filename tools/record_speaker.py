"""Bounded local capture: legacy DirectShow or explicit per-stream WASAPI RAW.

RAW is opt-in for the verified M4 tests, with native-format/packet metadata.
Neither backend enables monitoring or changes persistent endpoint settings.
"""
import shutil
import json
import subprocess
import time
from pathlib import Path


class Recorder:
    def __init__(self, path, seconds=150, device="麦克风 (Misiom-Shooter)", raw=False, buffer_ms=500):
        self.path = Path(path)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.seconds = seconds
        self.device = device
        self.process = None
        self.raw = raw
        self.buffer_ms = buffer_ms

    def __enter__(self):
        if self.raw:
            if not 0<self.seconds<=60:raise ValueError('RAW test capture must be bounded to 60 seconds')
            if self.buffer_ms not in (100,500):raise ValueError('Use100 or500ms RAW capture buffering')
            self.stop_path=self.path.with_suffix('.capture-stop')
            self.metadata_path=self.path.with_suffix('.wasapi.json')
            if any(p.exists() for p in (self.path,self.stop_path,self.metadata_path)):raise ValueError('Use fresh RAW capture paths')
            self.log=self.path.with_suffix('.capture.log').open('wb')
            try:
                self.process=subprocess.Popen([str(Path('.local/loopback-python/Scripts/python.exe')),'-X','utf8',
                    'tools/record_wasapi.py','--mode','raw','--mono','--device',self.device,'--buffer-ms',str(self.buffer_ms),
                    '--seconds',str(self.seconds),'--output',str(self.path),'--stop-file',str(self.stop_path)],
                    stdout=self.log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
                deadline=time.monotonic()+5
                while self.process.poll() is None and time.monotonic()<deadline:
                    try:m=json.loads(self.metadata_path.read_text(encoding='utf8'))
                    except (FileNotFoundError,json.JSONDecodeError):m={}
                    if m.get('started'):self.started=time.monotonic();return self
                    time.sleep(.03)
                raise RuntimeError('RAW capture did not become ready; evidence retained')
            except BaseException:
                # A context manager whose entry fails will not receive __exit__.
                try:
                    if self.process and self.process.poll() is None:
                        self.process.terminate();self.process.wait(timeout=3)
                finally:self.log.close()
                raise
        executable = shutil.which("ffmpeg")
        if not executable:
            raise RuntimeError("FFmpeg with DirectShow is required")
        self.log = self.path.with_suffix(".capture.log").open("wb")
        command = [executable, "-hide_banner", "-nostats", "-y", "-f", "dshow",
                   "-sample_rate", "44100", "-sample_size", "16", "-channels", "1",
                   "-audio_buffer_size", "50", "-i", "audio=" + self.device,
                   "-t", str(self.seconds), "-c:a", "pcm_s16le", str(self.path)]
        try:
            self.process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
                                            stderr=self.log, creationflags=subprocess.CREATE_NO_WINDOW)
        except BaseException:
            self.log.close()
            raise
        time.sleep(1)
        if self.process.poll() is not None:
            self.log.close()
            raise RuntimeError("Capture failed; see " + str(self.path.with_suffix(".capture.log")))
        self.started = time.monotonic()
        return self

    def __exit__(self, *_):
        try:
            if self.process and self.process.poll() is None:
                try:
                    if self.raw:self.stop_path.touch();self.process.wait(timeout=5)
                    else:self.process.communicate(b"q\n", timeout=5)
                except subprocess.TimeoutExpired:
                    self.process.terminate()
                    self.process.wait(timeout=5)
        finally:
            self.log.close()
        if self.process.returncode and not _[0]:
            raise RuntimeError("Capture ended with code " + str(self.process.returncode))
        if self.raw and not _[0]:
            m=json.loads(self.metadata_path.read_text(encoding='utf8'))
            if not m.get('complete') or m.get('stop_reason')!='parent_request':raise RuntimeError('RAW capture incomplete or reached its duration bound')
