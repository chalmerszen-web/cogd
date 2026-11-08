#!/usr/bin/env python3
"""Reproduce the checked-in Q15 table; no scipy, network or firmware writes.

24k -> 16k rational resampling is interpolate-by-2, low-pass at 48k, decimate-by-3.
Use a 97-tap symmetric windowed-sinc prototype, fc=7000 Hz, Kaiser beta=8.6.
The two 49-tap phases are centered on the output sample time (zero delay), and
each phase is normalized to an exact Q15 DC sum of 32768. The unused outer tap
of the half-sample phase is zero. Endpoint extension is done by the C++ caller.
"""
import math

def i0(x):
    term=total=1.0
    for k in range(1,100):
        term *= (x*x/4)/(k*k); total += term
        if term < total*1e-17: break
    return total

def rounded(x):
    return math.floor(x+0.5) if x>=0 else -math.floor(-x+0.5)

def coefficients():
    rows=[]; beta=8.6; radius=48; cut=7000/48000; denominator=i0(beta)
    for phase in range(2):
        raw=[]
        for k in range(-24,25):
            d=2*k-phase
            if abs(d)>radius: raw.append(0.0);continue
            sinc=1.0 if not d else math.sin(2*math.pi*cut*d)/(2*math.pi*cut*d)
            raw.append(4*cut*sinc*i0(beta*math.sqrt(max(0.0,1-(d/radius)**2)))/denominator)
        total=sum(raw);row=[rounded(x/total*32768) for x in raw]
        correction=32768-sum(row)
        if phase==0:row[24]+=correction
        else:
            assert correction%2==0
            row[24]+=correction//2;row[25]+=correction//2
        assert sum(row)==32768 and all(-32768<=x<=32767 for x in row)
        rows.append(row)
    return rows

if __name__=='__main__':
    for phase,row in enumerate(coefficients()):
        print('    { // phase %d, offsets -24..24, sum 32768'%phase)
        for i in range(0,len(row),7):print('        '+', '.join(map(str,row[i:i+7]))+(',' if i+7<len(row) else ''))
        print('    },')
