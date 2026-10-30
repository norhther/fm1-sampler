#!/usr/bin/env python3
"""Generates the synthesized instruments of the sample library (original sounds, nothing is sampled from a recording).

usage: tools/gen_instruments.py [outdir]      default: <repo>/samples

Conventions of the library: a melodic sample plays at its own pitch for MIDI note 60 (middle C), so keys, pads and leads are C4 and
basses are C2. Drum kits hold up to 16 hits, each one a clear onset after silence (the app slices them on transients, pad = note - 60).
44.1 kHz, 16 bit, mono unless the instrument needs a stereo image. Deterministic: the noise of every sound is seeded from its name.
"""
import os
import sys
import wave
import zlib

import numpy as np
from scipy import signal

SR = 44100
C4, C2 = 261.6255653, 65.4063913


def seeded(name):
    return np.random.default_rng(zlib.crc32(name.encode()))


# ---------- building blocks ----------
def tt(sec):
    return np.arange(int(sec * SR)) / SR


def expenv(t, tau):
    return np.exp(-t / tau)


def attack(t, sec):
    return np.minimum(1.0, t / max(sec, 1e-4))


def blep(ph, dt):
    out = np.zeros_like(ph)
    m = ph < dt
    x = ph[m] / dt[m]
    out[m] = x + x - x * x - 1.0
    m = ph > 1.0 - dt
    x = (ph[m] - 1.0) / dt[m]
    out[m] = x * x + x + x + 1.0
    return out


def saw(f, n, ph0=0.0):
    """Band limited sawtooth (PolyBLEP); f: Hz, scalar or one value per sample."""
    dt = np.broadcast_to(np.asarray(f, dtype=float) / SR, (n,)).copy()
    ph = (ph0 + np.cumsum(dt)) % 1.0
    return 2.0 * ph - 1.0 - blep(ph, dt)


def pulse(f, n, width=0.5, ph0=0.0):
    dt = np.broadcast_to(np.asarray(f, dtype=float) / SR, (n,)).copy()
    ph = (ph0 + np.cumsum(dt)) % 1.0
    a = 2.0 * ph - 1.0 - blep(ph, dt)
    ph2 = (ph + width) % 1.0
    b = 2.0 * ph2 - 1.0 - blep(ph2, dt)
    return 0.5 * (a - b)


def sine(f, n, ph0=0.0):
    dt = np.broadcast_to(np.asarray(f, dtype=float) / SR, (n,))
    return np.sin(2 * np.pi * (ph0 + np.cumsum(dt)))


def cents(c):
    return 2.0 ** (c / 1200.0)


def lp(x, fc, q=0.707, order=1):
    """Fixed low pass (RBJ biquad)."""
    w = 2 * np.pi * min(fc, SR * 0.45) / SR
    al = np.sin(w) / (2 * q)
    b = np.array([(1 - np.cos(w)) / 2, 1 - np.cos(w), (1 - np.cos(w)) / 2])
    a = np.array([1 + al, -2 * np.cos(w), 1 - al])
    y = x
    for _ in range(order):
        y = signal.lfilter(b / a[0], a / a[0], y, axis=0)
    return y


def hp(x, fc, q=0.707):
    w = 2 * np.pi * min(fc, SR * 0.45) / SR
    al = np.sin(w) / (2 * q)
    b = np.array([(1 + np.cos(w)) / 2, -(1 + np.cos(w)), (1 + np.cos(w)) / 2])
    a = np.array([1 + al, -2 * np.cos(w), 1 - al])
    return signal.lfilter(b / a[0], a / a[0], x, axis=0)


def bp(x, fc, q=2.0):
    w = 2 * np.pi * min(fc, SR * 0.45) / SR
    al = np.sin(w) / (2 * q)
    b = np.array([al, 0, -al])
    a = np.array([1 + al, -2 * np.cos(w), 1 - al])
    return signal.lfilter(b / a[0], a / a[0], x, axis=0)


def sweep_lp(x, fc_env, q=1.0, k=18):
    """Low pass whose cutoff follows fc_env (Hz per sample): k fixed filters, blended in log frequency."""
    lo, hi = max(40.0, float(np.min(fc_env))), min(SR * 0.45, float(np.max(fc_env)))
    if hi <= lo * 1.02:
        return lp(x, lo, q)
    fcs = np.geomspace(lo, hi, k)
    bank = np.stack([lp(x, f, q) for f in fcs])
    pos = np.clip((np.log(np.clip(fc_env, lo, hi)) - np.log(lo)) / (np.log(hi) - np.log(lo)) * (k - 1), 0, k - 1 - 1e-9)
    i = pos.astype(int)
    fr = pos - i
    idx = np.arange(len(x))
    return bank[i, idx] * (1 - fr) + bank[i + 1, idx] * fr


def reverb(x, rt60=1.2, mix=0.2, seed="rev", predelay=0.012, damp=6000.0):
    """Noise-tail reverb; mono in -> stereo out, stereo in -> stereo out."""
    rng = seeded(seed)
    n = int(rt60 * SR * 1.1)
    t = np.arange(n) / SR
    out = []
    chans = [x, x] if x.ndim == 1 else [x[:, 0], x[:, 1]]
    for c in range(2):
        ir = rng.standard_normal(n) * np.exp(-6.9 * t / rt60)
        ir = lp(ir, damp, order=2)
        ir[: int(predelay * SR)] = 0
        ir /= np.sqrt(np.sum(ir ** 2)) + 1e-9
        out.append(signal.fftconvolve(chans[c], ir)[: len(x)])
    wet = np.stack(out, axis=1)
    dry = np.stack(chans, axis=1)
    return dry * (1 - mix) + wet * mix * 2.0


def chorus(x, depth_ms=6.0, rate=0.35, mix=0.5, base_ms=12.0):
    """Mono -> stereo ensemble chorus (two modulated delays)."""
    n = len(x)
    t = np.arange(n) / SR
    out = []
    for ph in (0.0, 0.5):
        d = (base_ms + depth_ms * np.sin(2 * np.pi * (rate * t + ph))) * SR / 1000.0
        out.append(np.interp(np.arange(n) - d, np.arange(n), x))
    wet = np.stack(out, axis=1)
    dry = np.stack([x, x], axis=1)
    return dry * (1 - mix * 0.5) + wet * mix


def karplus(f, n, decay=0.996, bright=0.5, rng=None, pos=0.5):
    """Plucked string (Karplus-Strong with a one-pole damping in the loop)."""
    rng = rng or np.random.default_rng(1)
    period = SR / f
    L = int(period)
    frac = period - L
    buf = rng.uniform(-1, 1, L + 2)
    buf = lp(buf, 1500 + 9000 * bright)           # the excitation: how hard the string is plucked
    k = int(pos * L)
    buf = buf - np.roll(buf, k) * 0.5             # pluck position comb
    out = np.zeros(n)
    prev = 0.0
    a = (1 - frac) / (1 + frac)                    # fractional delay (allpass), keeps the pitch exact
    ap_x = 0.0
    ap_y = 0.0
    ring = list(buf[:L])
    idx = 0
    for i in range(n):
        s = ring[idx]
        damped = decay * (0.5 * s + 0.5 * prev)
        prev = s
        y = a * (damped - ap_y) + ap_x
        ap_x = damped
        ap_y = y
        ring[idx] = y
        idx += 1
        if idx == L:
            idx = 0
        out[i] = s
    return out


def fm(f, t, ratio, index_env, amp_env=None):
    """Two operator FM; index_env: modulation index per sample."""
    n = len(t)
    mod = np.sin(2 * np.pi * f * ratio * t)
    y = np.sin(2 * np.pi * f * t + index_env * mod)
    return y if amp_env is None else y * amp_env


def modal(f, t, ratios, amps, taus):
    y = np.zeros(len(t))
    for r, a, tau in zip(ratios, amps, taus):
        if f * r < SR * 0.45:
            y += a * np.sin(2 * np.pi * f * r * t) * np.exp(-t / tau)
    return y


def fade(x, tail=0.2, head=0.002):
    n = len(x)
    g = np.ones(n)
    h = int(head * SR)
    tl = int(tail * SR)
    if h:
        g[:h] = np.linspace(0, 1, h)
    if tl:
        g[-tl:] = np.linspace(1, 0, tl) ** 1.5
    return x * (g if x.ndim == 1 else g[:, None])


def noise(n, rng):
    return rng.standard_normal(n)


def vib(t, rate, depth, delay=0.0, ramp=0.4):
    """Vibrato factor for a frequency: 1 +- depth, fading in after `delay`."""
    g = np.clip((t - delay) / ramp, 0, 1)
    return 1.0 + depth * g * np.sin(2 * np.pi * rate * t)


# ---------- KEYS (C4) ----------
def e_piano():
    t = tt(5.0)
    rng = seeded("E-Piano")
    f = C4
    idx = 2.0 * expenv(t, 0.7) + 0.35
    body = fm(f, t, 1.0, idx) + 0.5 * fm(f * cents(2.5), t, 1.0, idx)
    tine = 0.18 * np.sin(2 * np.pi * f * 14 * t) * expenv(t, 0.06)          # the bell-like tine at the attack
    amp = attack(t, 0.003) * (0.7 * expenv(t, 2.4) + 0.3 * expenv(t, 0.45))
    y = (body + tine) * amp + 0.05 * hp(noise(len(t), rng), 3000) * expenv(t, 0.012)
    y = lp(y, 5200, 0.6)
    st = reverb(y, 0.9, 0.12, "E-Piano")
    return fade(st, 0.5)


def wurly():
    t = tt(4.0)
    f = C4
    idx = 3.2 * expenv(t, 0.22) + 0.7
    y = fm(f, t, 1.0, idx) + 0.35 * fm(2 * f, t, 1.0, 0.8 * idx)
    y *= attack(t, 0.002) * expenv(t, 1.25) * (1 - 0.14 * (0.5 + 0.5 * np.sin(2 * np.pi * 5.4 * t)))   # tremolo
    y = np.tanh(1.6 * y)
    y = lp(y, 4200)
    return fade(chorus(y, 3, 0.4, 0.25), 0.4)


def organ():
    t = tt(6.0)
    rng = seeded("Organ")
    f = C4
    bars = [(0.5, 0.55), (1.5, 0.45), (1.0, 1.0), (2.0, 0.8), (3.0, 0.5), (4.0, 0.55), (5.0, 0.28), (8.0, 0.32)]   # 16', 5 1/3', 8', 4', 2 2/3', 2', 1 3/5', 1'
    outs = []
    for lfo_ph in (0.0, 0.25):                                          # a rotating speaker: the two sides are out of phase
        vf = 1 + 0.0016 * np.sin(2 * np.pi * (6.6 * t + lfo_ph))
        y = np.zeros(len(t))
        for r, a in bars:
            y += a * sine(f * r * vf, len(t))
        y *= 1 + 0.09 * np.sin(2 * np.pi * (6.6 * t + lfo_ph + 0.2))
        outs.append(y)
    st = np.stack(outs, axis=1) * attack(t, 0.008)[:, None]
    click = 0.25 * hp(noise(len(t), rng), 2000) * expenv(t, 0.004)
    st += click[:, None]
    st = np.tanh(0.9 * st / 3.0) * 1.2
    return fade(reverb(st, 0.7, 0.08, "Organ"), 0.5)


def marimba():
    t = tt(3.5)
    rng = seeded("Marimba")
    y = modal(C4, t, [1, 3.93, 9.25, 16.1], [1.0, 0.42, 0.12, 0.04], [1.3, 0.38, 0.12, 0.05])
    y += 0.5 * lp(noise(len(t), rng), 900, 0.8) * expenv(t, 0.012)          # the mallet
    y *= attack(t, 0.001)
    return fade(reverb(y, 0.8, 0.1, "Marimba"), 0.4)


def music_box():
    t = tt(4.0)
    rng = seeded("Music Box")
    y = modal(C4, t, [1, 2.76, 5.4, 8.93], [1.0, 0.55, 0.28, 0.14], [2.6, 1.3, 0.5, 0.22])
    y += 0.12 * hp(noise(len(t), rng), 5000) * expenv(t, 0.006)
    y *= attack(t, 0.001)
    return fade(reverb(y, 1.1, 0.16, "Music Box"), 0.5)


def bell():
    t = tt(7.0)
    f = C4
    idx = 4.5 * expenv(t, 1.6) + 0.4
    y = fm(f, t, 3.5, idx, expenv(t, 3.2)) + 0.5 * fm(f * 2.0, t, 2.01, 0.6 * idx, expenv(t, 2.2))
    y += 0.3 * np.sin(2 * np.pi * f * 5.43 * t) * expenv(t, 0.9)
    y *= attack(t, 0.001)
    return fade(reverb(y, 2.2, 0.22, "Bell"), 0.8)


def harpsichord():
    n = int(3.0 * SR)
    rng = seeded("Harpsichord")
    a = karplus(C4, n, 0.9972, 0.9, rng, 0.18)
    b = karplus(C4 * cents(3), n, 0.9965, 0.9, rng, 0.22)
    y = 0.6 * a + 0.4 * b
    y += 0.2 * hp(rng.standard_normal(n), 4000) * expenv(np.arange(n) / SR, 0.004)    # the plectrum
    return fade(reverb(y, 0.6, 0.1, "Harpsichord"), 0.4)


def clav():
    t = tt(2.5)
    n = len(t)
    f = C4
    src = pulse(f, n, 0.18) + 0.4 * saw(f * 2, n)
    y = sweep_lp(src, 7000 * expenv(t, 0.07) + 900, 2.0)
    y *= attack(t, 0.001) * expenv(t, 0.55)
    y = y - np.roll(y, int(SR / f * 0.31)) * 0.5                           # the pick-up position comb
    return fade(chorus(y, 1.5, 0.3, 0.12), 0.3)


def vibraphone():
    t = tt(6.0)
    rng = seeded("Vibraphone")
    outs = []
    for ph in (0.0, 0.2):
        y = modal(C4, t, [1, 4.0, 10.1], [1.0, 0.42, 0.1], [4.2, 2.0, 0.8])
        y *= 1 - 0.35 * (0.5 + 0.5 * np.sin(2 * np.pi * (4.6 * t + ph)))
        outs.append(y)
    st = np.stack(outs, axis=1)
    st += (0.3 * lp(noise(len(t), rng), 1200) * expenv(t, 0.01))[:, None]
    st *= attack(t, 0.001)[:, None]
    return fade(reverb(st, 1.3, 0.14, "Vibraphone"), 0.6)


# ---------- BASS (C2) ----------
def sub808():
    t = tt(4.0)
    f = C2
    fe = f * (1 + 1.2 * expenv(t, 0.035))
    y = sine(fe, len(t))
    y = np.tanh(1.8 * y) * 0.9
    y *= attack(t, 0.002) * (expenv(t, 1.4))
    y += 0.2 * hp(noise(len(t), seeded("808 Sub")), 1500) * expenv(t, 0.004)
    return fade(y, 0.4)


def saw_bass():
    t = tt(3.0)
    n = len(t)
    f = C2
    src = saw(f, n) + saw(f * cents(7), n, 0.3) * 0.8
    y = sweep_lp(src, 3200 * expenv(t, 0.2) + 220, 2.8)
    y += 0.55 * sine(f, n)
    y *= attack(t, 0.002) * expenv(t, 1.5)
    return fade(np.tanh(1.2 * y), 0.3)


def square_bass():
    t = tt(3.0)
    n = len(t)
    f = C2
    src = pulse(f, n, 0.5) + 0.5 * pulse(f * cents(5), n, 0.35)
    y = sweep_lp(src, 1900 * expenv(t, 0.12) + 260, 1.6)
    y += 0.5 * sine(f, n)
    y *= attack(t, 0.002) * expenv(t, 1.1)
    return fade(y, 0.3)


def pluck_bass():
    n = int(4.0 * SR)
    rng = seeded("Pluck Bass")
    y = karplus(C2, n, 0.9985, 0.25, rng, 0.3)
    t = np.arange(n) / SR
    y += 0.35 * sine(C2, n) * expenv(t, 0.9)
    return fade(lp(y, 2400), 0.4)


def reese():
    t = tt(6.0)
    n = len(t)
    f = C2
    cut = 520 + 380 * np.sin(2 * np.pi * 0.22 * t - 1.0)
    outs = []
    for d in (-14, -5, 5, 14):
        outs.append(sweep_lp(saw(f * cents(d), n, 0.2 * (d % 7)), cut, 1.5))
    left = outs[0] + 0.6 * outs[1] + 0.3 * outs[2]
    right = outs[3] + 0.6 * outs[2] + 0.3 * outs[1]
    st = np.stack([left, right], axis=1) * 0.5
    st += 0.35 * sine(f, n)[:, None]
    st *= attack(t, 0.01)[:, None]
    return fade(np.tanh(1.1 * st), 0.5)


def funk_bass():
    t = tt(2.5)
    n = len(t)
    f = C2
    src = saw(f, n) * 0.7 + pulse(f, n, 0.3) * 0.7
    y = sweep_lp(src, 2600 * expenv(t, 0.07) + 300, 4.0)
    y += 0.4 * sine(f, n)
    y *= attack(t, 0.0015) * expenv(t, 0.75)
    y += 0.15 * hp(noise(n, seeded("Funk Bass")), 2500) * expenv(t, 0.01)     # the slap
    return fade(np.tanh(1.3 * y), 0.3)


# ---------- PADS (C4) ----------
def strings():
    t = tt(7.0)
    n = len(t)
    f = C4
    L = np.zeros(n)
    R = np.zeros(n)
    for i, d in enumerate([-17, -11, -5, 5, 11, 17]):
        v = saw(f * cents(d) * vib(t, 5.1 + 0.2 * i, 0.0014, 1.0), n, 0.13 * i)
        (L if i % 2 == 0 else R)[:] += v
        (R if i % 2 == 0 else L)[:] += 0.35 * v
    st = np.stack([L, R], axis=1) / 3.0
    st = lp(st, 2900, 0.7, order=2)
    st *= (attack(t, 0.9) ** 1.5)[:, None]
    return fade(reverb(st, 1.8, 0.22, "Strings"), 0.8)


def choir():
    t = tt(7.0)
    n = len(t)
    f = C4
    rng = seeded("Choir")
    src = np.zeros(n)
    for i, d in enumerate([-9, -3, 3, 9]):
        src += saw(f * cents(d) * vib(t, 5.0 + 0.3 * i, 0.003, 0.6), n, 0.2 * i)
    src += 0.05 * noise(n, rng)
    ah = 1.0 * bp(src, 700, 6) + 0.55 * bp(src, 1220, 8) + 0.3 * bp(src, 2600, 10) + 0.12 * bp(src, 3400, 10)
    ah *= attack(t, 0.6) ** 1.4
    st = chorus(ah, 5, 0.3, 0.6)
    return fade(reverb(st, 2.2, 0.3, "Choir"), 0.9)


def warm_pad():
    t = tt(7.0)
    n = len(t)
    f = C4
    src = pulse(f * cents(-8), n, 0.3) + pulse(f * cents(8), n, 0.45) + 0.4 * saw(f, n)
    y = sweep_lp(src, 1400 + 450 * np.sin(2 * np.pi * 0.15 * t), 1.1)
    y *= attack(t, 1.0) ** 1.4
    st = chorus(y, 7, 0.22, 0.65, 14)
    return fade(reverb(st, 2.0, 0.28, "Warm Pad"), 0.9)


def glass_pad():
    t = tt(7.0)
    n = len(t)
    f = C4
    rng = seeded("Glass Pad")
    outs = []
    for side in (-1, 1):
        y = np.zeros(n)
        for k, r in enumerate([1, 2, 3, 4, 5, 6, 8, 10]):
            lfo = 0.6 + 0.4 * np.sin(2 * np.pi * (0.07 + 0.045 * k) * t + rng.uniform(0, 6.28))
            y += (1.0 / r ** 1.15) * lfo * sine(f * r * cents(0.35 * side * k), n, rng.uniform(0, 1))
        outs.append(y)
    st = np.stack(outs, axis=1)
    st *= (attack(t, 1.0) ** 1.3)[:, None]
    return fade(reverb(st, 3.0, 0.4, "Glass Pad"), 1.0)


def brass_pad():
    t = tt(6.0)
    n = len(t)
    f = C4
    src = saw(f * cents(-6), n) + saw(f * cents(6), n, 0.3) + saw(f, n, 0.6)
    cut = 500 + 2100 * (1 - np.exp(-t / 0.45)) + 250 * np.sin(2 * np.pi * 4.8 * t) * np.clip(t - 0.8, 0, 1)
    y = sweep_lp(src, cut, 1.3) * (attack(t, 0.22) ** 1.2)
    y *= vib(t, 5.2, 0.0, 0)
    st = chorus(y, 4, 0.28, 0.45)
    return fade(reverb(st, 1.2, 0.14, "Brass Pad"), 0.7)


def dark_pad():
    t = tt(7.0)
    n = len(t)
    rng = seeded("Dark Pad")
    src = saw(C4 / 2, n) * 0.9 + saw(C4 * cents(5), n, 0.4) * 0.6 + 0.05 * noise(n, rng)
    y = sweep_lp(src, 650 + 260 * np.sin(2 * np.pi * 0.1 * t), 1.4)
    y *= attack(t, 1.6) ** 1.5
    st = chorus(y, 9, 0.12, 0.7, 16)
    return fade(reverb(st, 3.2, 0.4, "Dark Pad"), 1.0)


# ---------- LEADS (C4) ----------
def square_lead():
    t = tt(4.0)
    n = len(t)
    f = C4 * vib(t, 5.6, 0.004, 0.35)
    y = pulse(f, n, 0.5) + 0.7 * pulse(f * cents(4), n, 0.5, 0.3)
    y = lp(y, 5200) * attack(t, 0.008)
    return fade(chorus(y, 2, 0.5, 0.25), 0.4)


def saw_lead():
    t = tt(4.0)
    n = len(t)
    f = C4 * vib(t, 5.4, 0.003, 0.4)
    L = np.zeros(n)
    R = np.zeros(n)
    for i, d in enumerate([-22, -14, -7, 0, 7, 14, 22]):
        v = saw(f * cents(d), n, 0.137 * i)
        pan = (i - 3) / 3.0
        L += v * (1 - pan) * 0.5 + v * 0.25
        R += v * (1 + pan) * 0.5 + v * 0.25
    st = np.stack([L, R], axis=1) / 3.2
    st = lp(st, 6500, 0.8) * attack(t, 0.01)[:, None]
    return fade(reverb(st, 0.9, 0.12, "Saw Lead"), 0.5)


def chip():
    t = tt(3.5)
    n = len(t)
    f = C4 * vib(t, 6.0, 0.0035, 0.18)
    y = pulse(f, n, 0.25) * attack(t, 0.003)
    y = np.round(y * 7.5) / 7.5                      # a 4 bit sound chip
    return fade(y * 0.8, 0.3)


def flute():
    t = tt(4.0)
    n = len(t)
    rng = seeded("Flute")
    f = C4 * vib(t, 5.0, 0.005, 0.25)
    y = sine(f, n) + 0.22 * sine(f * 2, n) + 0.07 * sine(f * 3, n)
    breath = bp(noise(n, rng), 3200, 1.2) * (0.02 + 0.09 * expenv(t, 0.08))
    y = (y + breath * 1.6) * (attack(t, 0.07) ** 1.2)
    return fade(reverb(y, 1.0, 0.15, "Flute"), 0.5)


def pluck_lead():
    n = int(2.5 * SR)
    t = np.arange(n) / SR
    rng = seeded("Pluck Lead")
    y = karplus(C4, n, 0.9984, 0.95, rng, 0.12)
    y += 0.5 * karplus(C4 * 2, n, 0.995, 0.95, rng, 0.2)
    y += 0.12 * saw(C4, n) * expenv(t, 0.08)
    return fade(chorus(y, 2, 0.4, 0.2), 0.3)


def sync_lead():
    dur = 3.5
    os_ = 4                                           # generated at 4x the rate and decimated: hard sync aliases badly otherwise
    n = int(dur * SR * os_)
    t = np.arange(n) / (SR * os_)
    master = C4 * vib(t, 5.5, 0.002, 0.5)
    slave = master * (1 + 4.0 * np.exp(-t / 0.55))
    mph = np.cumsum(master / (SR * os_))
    reset = np.floor(mph)
    first = np.concatenate([[True], np.diff(reset) > 0])
    sph = np.zeros(n)
    acc = 0.0
    inc = slave / (SR * os_)
    for i in range(n):
        if first[i]:
            acc = 0.0
        acc = (acc + inc[i]) % 1.0
        sph[i] = acc
    y = 2 * sph - 1
    y = signal.decimate(y, os_, ftype="fir", zero_phase=True)
    t1 = tt(dur)[: len(y)]
    y = lp(y, 7500) * attack(t1, 0.006)
    return fade(chorus(y, 2, 0.35, 0.2), 0.4)


# ---------- DRUMS ----------
def d_sine_drop(f0, f1, tau_f, tau_a, dur, click=0.3, drive=1.0, rng=None):
    t = tt(dur)
    f = f1 + (f0 - f1) * np.exp(-t / tau_f)
    y = np.tanh(drive * sine(f, len(t))) * np.exp(-t / tau_a)
    if click and rng is not None:
        y += click * hp(rng.standard_normal(len(t)), 1800) * np.exp(-t / 0.003)
    return y * attack(t, 0.0008)


def d_noise(dur, tau, rng, lo=None, hi=None, bpf=None, q=1.0):
    t = tt(dur)
    y = rng.standard_normal(len(t))
    if lo:
        y = hp(y, lo)
    if hi:
        y = lp(y, hi)
    if bpf:
        y = bp(y, bpf, q) * 2
    return y * np.exp(-t / tau) * attack(t, 0.0005)


def d_clap(rng, tail=0.16, gap=0.011, n=4):
    t = tt(0.35)
    y = np.zeros(len(t))
    for i in range(n):
        s = int(i * gap * SR)
        burst = bp(rng.standard_normal(len(t)), 1500, 1.5) * 2 * np.exp(-np.maximum(t - i * gap, 0) / 0.006) * (t >= i * gap)
        y += burst * (0.7 if i < n - 1 else 1.0)
    y += bp(rng.standard_normal(len(t)), 1300, 1.2) * 2 * np.exp(-np.maximum(t - n * gap, 0) / tail) * (t >= n * gap) * 0.8
    return y


def d_metal(freqs, dur, tau, lo, rng):
    t = tt(dur)
    y = np.zeros(len(t))
    for f in freqs:
        y += pulse(f, len(t), 0.5, rng.uniform(0, 1))
    return hp(y, lo) * np.exp(-t / tau) * attack(t, 0.0005) / len(freqs) * 2


def kit808(name="808 Kit"):
    rng = seeded(name)
    hits = [
        d_sine_drop(170, 46, 0.03, 0.34, 0.8, 0.35, 1.8, rng),                            # kick
        d_sine_drop(140, 38, 0.06, 0.9, 1.5, 0.15, 2.4, rng),                             # long kick
        d_sine_drop(260, 175, 0.02, 0.07, 0.4, 0.0, 1.0) * 0.8 + d_noise(0.4, 0.07, rng, 1200, 9000) * 0.9,   # snare
        d_clap(rng),
        d_sine_drop(1800, 1750, 0.01, 0.012, 0.1, 0.0) + 0.5 * d_sine_drop(480, 470, 0.01, 0.02, 0.1) + d_noise(0.1, 0.01, rng, 2500) * 0.5,  # rim
        d_metal(205.3 * np.array([2, 3, 4.16, 5.43, 6.79, 8.21]), 0.2, 0.025, 7000, rng) * 0.9,   # closed hat
        d_metal(205.3 * np.array([2, 3, 4.16, 5.43, 6.79, 8.21]), 0.7, 0.2, 6500, rng) * 0.8,    # open hat
        d_sine_drop(150, 95, 0.05, 0.28, 0.6, 0.1, 1.2, rng),                              # toms
        d_sine_drop(200, 130, 0.05, 0.26, 0.6, 0.1, 1.2, rng),
        d_sine_drop(280, 190, 0.05, 0.22, 0.5, 0.1, 1.2, rng),
        bp(d_metal([540, 800], 0.4, 0.14, 300, rng), 700, 1.4) * 1.8,                      # cowbell
        d_sine_drop(2500, 2400, 0.01, 0.018, 0.12, 0.0) * 0.9,                             # clave
        d_sine_drop(400, 290, 0.04, 0.16, 0.4, 0.15, 1.1, rng) * 0.9,                      # conga
        d_noise(0.15, 0.04, rng, bpf=5200, q=1.2) * (0.3 + np.minimum(tt(0.15) / 0.03, 1)) * 0.7,   # maraca
        d_metal(205.3 * np.array([2, 3, 4.16, 5.43, 6.79, 8.21, 9.5]), 1.6, 0.55, 3500, rng) * 0.7 + d_noise(1.6, 0.5, rng, 4000) * 0.4,   # crash
        d_sine_drop(1800, 300, 0.05, 0.14, 0.4, 0.0, 1.5) * 0.7,                           # zap
    ]
    return place(hits)


def electro_kit():
    rng = seeded("Electro Kit")
    hh = 205.3 * np.array([2, 3, 4.16, 5.43, 6.79, 8.21])
    hits = [
        d_sine_drop(130, 52, 0.02, 0.22, 0.5, 0.5, 2.6, rng),
        d_sine_drop(100, 44, 0.04, 0.55, 1.0, 0.25, 2.2, rng),
        d_sine_drop(240, 190, 0.015, 0.05, 0.3, 0.0, 1.2) * 0.7 + d_noise(0.3, 0.055, rng, 1500, 11000) * np.minimum(1, (0.14 - tt(0.3)) * 60 + 1) * 1.0,
        d_clap(rng, 0.1, 0.008, 3),
        d_noise(0.08, 0.012, rng, bpf=1900, q=2) * 2.2,
        d_metal(hh * 1.1, 0.12, 0.02, 8000, rng),
        d_metal(hh * 1.1, 0.45, 0.12, 7500, rng) * 0.9,
        d_sine_drop(200, 110, 0.03, 0.18, 0.4, 0.2, 2.0, rng),
        d_sine_drop(260, 150, 0.03, 0.16, 0.4, 0.2, 2.0, rng),
        d_sine_drop(340, 210, 0.03, 0.14, 0.4, 0.2, 2.0, rng),
        bp(d_metal([560, 845], 0.3, 0.1, 400, rng), 720, 1.6) * 1.8,
        np.tanh(2 * d_sine_drop(900, 200, 0.03, 0.09, 0.3, 0.0)) * 0.7,
        d_noise(0.2, 0.03, rng, bpf=3000, q=0.9) * 1.6,
        d_sine_drop(3000, 400, 0.02, 0.07, 0.2, 0.0) * 0.8,
        d_metal(hh * 0.9, 1.4, 0.5, 4500, rng) * 0.7 + d_noise(1.4, 0.45, rng, 5000) * 0.35,
        np.tanh(3 * d_sine_drop(70, 60, 0.2, 0.7, 1.0, 0.0)) * 0.8,
    ]
    return place(hits)


def lofi_kit():
    rng = seeded("Lo-Fi Kit")
    hits = [
        lp(d_sine_drop(120, 52, 0.04, 0.2, 0.5, 0.2, 1.4, rng), 700),
        lp(d_sine_drop(95, 46, 0.05, 0.3, 0.7, 0.1, 1.3, rng), 600),
        d_sine_drop(200, 170, 0.02, 0.07, 0.3, 0.0) * 0.5 + d_noise(0.3, 0.09, rng, 900, 5500) * 1.2,
        d_clap(rng, 0.14, 0.013, 3) * 0.8,
        d_sine_drop(1500, 1450, 0.01, 0.015, 0.1, 0.0) * 0.8 + d_noise(0.1, 0.01, rng, 2000, 6000) * 0.6,
        d_noise(0.1, 0.018, rng, 5500, 9500) * 0.8,
        d_noise(0.45, 0.14, rng, 5000, 9500) * 0.7,
        lp(d_sine_drop(130, 85, 0.05, 0.2, 0.5, 0.1, 1.1, rng), 900),
        lp(d_sine_drop(170, 110, 0.05, 0.18, 0.5, 0.1, 1.1, rng), 1000),
        lp(d_sine_drop(230, 150, 0.05, 0.16, 0.4, 0.1, 1.1, rng), 1200),
        d_noise(0.3, 0.07, rng, bpf=2500, q=3) * 1.2,
        d_sine_drop(1900, 1800, 0.01, 0.02, 0.1, 0.0) * 0.8,
        lp(d_sine_drop(310, 240, 0.04, 0.14, 0.3, 0.1, 1.1, rng), 1800),
        d_noise(0.12, 0.03, rng, bpf=4200, q=1.3) * (0.4 + np.minimum(tt(0.12) / 0.025, 1)),
        d_noise(1.0, 0.35, rng, 3500, 9000) * 0.6,
        lp(d_sine_drop(60, 55, 0.1, 0.5, 0.8, 0.0, 1.5), 400),
    ]
    kit = place(hits)
    kit = lp(kit, 7500, 0.7)
    q = np.round(kit * 255.0) / 255.0                   # 9 bit
    q = np.repeat(q[::2], 2)[: len(kit)]                  # half the sample rate
    return q


# relative level of the 16 hits of a kit (every hit is first brought to full scale): the slicer ignores onsets under 5 % of the loudest one
HIT_LEVEL = [1.0, 1.0, 0.85, 0.8, 0.7, 0.55, 0.55, 0.9, 0.9, 0.9, 0.6, 0.7, 0.8, 0.55, 0.6, 0.6]
# time slot of every hit in seconds: the long kick, the open hat and the crash get more room; every hit is silent for the last 80 ms of its slot
SLOT = [0.5, 1.0, 0.5, 0.5, 0.4, 0.4, 0.7, 0.5, 0.5, 0.5, 0.5, 0.4, 0.5, 0.4, 1.4, 0.9]
FIRST_HIT = 0.01


def hit_starts():
    return [FIRST_HIT + sum(SLOT[:i]) for i in range(len(SLOT))]


def place(hits):
    starts = hit_starts()
    out = np.zeros(int((starts[-1] + SLOT[-1] + 0.2) * SR))
    for i, h in enumerate(hits):
        s = int(starts[i] * SR)
        h = np.array(h[: int((SLOT[i] - 0.08) * SR)])
        h = h / (np.max(np.abs(h)) + 1e-9) * HIT_LEVEL[i]
        k = min(len(h), int(0.1 * SR))
        h[len(h) - k:] *= np.linspace(1, 0, k) ** 2          # the tail is faded out so that the next hit starts from silence
        out[s:s + len(h)] += h
    return out


INSTRUMENTS = {
    "Keys": [("E-Piano", e_piano), ("Wurly", wurly), ("Organ", organ), ("Marimba", marimba), ("Music Box", music_box), ("Bell", bell),
             ("Harpsichord", harpsichord), ("Clav", clav), ("Vibraphone", vibraphone)],
    "Bass": [("808 Sub", sub808), ("Saw Bass", saw_bass), ("Square Bass", square_bass), ("Pluck Bass", pluck_bass), ("Reese", reese), ("Funk Bass", funk_bass)],
    "Pads": [("Strings", strings), ("Choir", choir), ("Warm Pad", warm_pad), ("Glass Pad", glass_pad), ("Brass Pad", brass_pad), ("Dark Pad", dark_pad)],
    "Leads": [("Square Lead", square_lead), ("Saw Lead", saw_lead), ("Chip", chip), ("Flute", flute), ("Pluck Lead", pluck_lead), ("Sync Lead", sync_lead)],
    "Drums": [("808 Kit", kit808), ("Electro Kit", electro_kit), ("Lo-Fi Kit", lofi_kit)],
}


# sounds without a stereo image worth the size: written as mono (the reverb of a mono sound is mono anyway)
MONO = {"E-Piano", "Wurly", "Marimba", "Music Box", "Harpsichord", "Clav", "Flute", "Pluck Lead", "Sync Lead", "Square Lead"}


def write_wav(path, x, peak=0.89):
    x = np.nan_to_num(np.asarray(x, dtype=float))
    x = signal.lfilter([1.0, -1.0], [1.0, -0.9985], x, axis=0)   # DC blocker (about 10 Hz): plucks and hard sync leave an offset
    m = np.max(np.abs(x))
    if m > 0:
        x = x / m * peak
    pcm = np.clip(np.round(x * 32767), -32768, 32767).astype("<i2")
    ch = 1 if pcm.ndim == 1 else pcm.shape[1]
    with wave.open(path, "wb") as w:
        w.setnchannels(ch)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(pcm.tobytes())


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "samples")
    total = 0
    for group, items in INSTRUMENTS.items():
        os.makedirs(os.path.join(out, group), exist_ok=True)
        for name, fn in items:
            x = fn()
            if name in MONO and x.ndim == 2:
                x = x.mean(axis=1)
            p = os.path.join(out, group, name + ".wav")
            write_wav(p, x)
            sz = os.path.getsize(p)
            total += sz
            print(f"{group:6s} {name:14s} {len(x) / SR:5.2f} s {'stereo' if x.ndim == 2 else 'mono  '} {sz / 1024:7.0f} KB")
    print(f"total {total / 1048576:.1f} MB")


if __name__ == "__main__":
    main()
