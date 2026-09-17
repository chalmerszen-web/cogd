"""Objective raw acoustic/PCM comparisons; these metrics do not establish taste."""
import argparse
import json
from pathlib import Path
import wave
import numpy as np


def read(path):
    with wave.open(str(path)) as f:
        if f.getsampwidth() != 2 or f.getnchannels() != 1:
            raise ValueError("PCM16 mono WAV required")
        return np.frombuffer(f.readframes(f.getnframes()), dtype="<i2").astype(np.float64)/32768, f.getframerate()


def db(value):
    return float(20*np.log10(max(float(value), 1e-12)))


def envelope(samples, rate):
    n = round(rate*.02)
    return np.sqrt(np.mean(samples[:len(samples)//n*n].reshape(-1, n)**2, axis=1))


def compare(path, reference=None):
    data, rate = read(path)
    env = envelope(data, rate)
    report = {"file": str(path), "rate": rate, "seconds": len(data)/rate,
              "peak_dbfs": db(np.max(np.abs(data))), "clipped_samples": int(np.count_nonzero(np.abs(data)>=32767/32768)),
              "dc": float(np.mean(data)), "noise_dbfs": db(np.sqrt(np.mean(data[:rate]**2)))}
    if reference:
        expected, expected_rate = read(reference)
        ref = envelope(expected, expected_rate)
        if len(env)<len(ref):
            raise ValueError("Capture is shorter than reference")
        # Independent DAC and headset clocks drift. Fit a bounded clock ratio to
        # envelopes only; never normalize or resample the stored raw capture.
        best=(-2,None,None,None)
        for scale in (np.arange(.996,1.00401,.00025) if len(ref)>500 else [1.0]):
            trial=np.interp(np.arange(round(len(ref)*scale))/scale,np.arange(len(ref)),ref)
            centered=trial-trial.mean(); ones=np.ones(len(trial))
            variance=np.correlate(env*env,ones,'valid')-np.correlate(env,ones,'valid')**2/len(trial)
            score=np.correlate(env,centered,'valid')/np.sqrt(np.maximum(variance*np.sum(centered**2),1e-25))
            offset=int(np.argmax(score))
            if score[offset]>best[0]:
                best=(float(score[offset]),offset,float(scale),trial)
        correlation,offset,scale,ref=best
        report.update(reference=str(reference), alignment_seconds=offset*.02,
                      envelope_correlation=correlation, reference_seconds=len(expected)/expected_rate,
                      clock_ratio=scale,captured_song_seconds=len(expected)/expected_rate*scale)
        data = data[round(offset*.02*rate):round((offset*.02+len(expected)/expected_rate*scale)*rate)]
        env = envelope(data, rate)
        active = ref[:len(env)] > max(ref.max()*.06, 1e-7)
        if np.any(active):
            report["low_energy_active_windows"] = int(np.count_nonzero(env[active] < max(10**(report["noise_dbfs"]/20)*2, 1e-7)))
    steady = env[env > max(env.max()*.08, 1e-7)]
    report.update(rms_dbfs=db(np.sqrt(np.mean(data*data))),
                  crest_db=db(np.max(np.abs(data)))-db(np.sqrt(np.mean(data*data))),
                  active_p10_dbfs=db(np.percentile(steady,10)), active_p90_dbfs=db(np.percentile(steady,90)))
    report["relative_snr_db"] = report["rms_dbfs"]-report["noise_dbfs"]
    size = 4096
    frames = data[:len(data)//size*size].reshape(-1,size)
    power = np.mean(np.abs(np.fft.rfft(frames*np.hanning(size),axis=1))**2,axis=0)
    frequencies = np.fft.rfftfreq(size,1/rate)
    report["band_power_fraction"] = {f"{a}-{b}":float(np.sum(power[(frequencies>=a)&(frequencies<b)])/max(np.sum(power),1e-30))
        for a,b in [(0,100),(100,300),(300,1000),(1000,3000),(3000,6000),(6000,12000),(12000,22051)]}
    return report


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("wav",type=Path)
    p.add_argument("--reference",type=Path)
    p.add_argument("--output",type=Path)
    args = p.parse_args()
    report = compare(args.wav,args.reference)
    text = json.dumps(report,indent=2,ensure_ascii=False)
    if args.output:
        args.output.write_text(text,encoding="utf-8")
    print(text)


if __name__ == "__main__":
    main()
