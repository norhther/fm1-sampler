#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Library instruments from GHOULBOX's CC0 sample sets (Versilian Studios VSCO-2 CE / VCSL, CC0 1.0).
usage: tools/import_ghoulbox_samples.py <ghoulbox-fm1-dungeon-synth>/assets/samples-cc0 [outdir]
Each set: the file nearest C4, resampled so it sounds at C4 (library convention), mono 16 bit, short fade-out."""
import sys, wave
from pathlib import Path
import numpy as np
from scipy.signal import resample_poly
from fractions import Fraction

# name -> (library folder, set, file, semitones from its root up to C4)
MAP = {
    "Grand Piano":    ("Keys",  "PIANO",    "03_GPiano_sus_C4_v2_rr1_Player.wav", 0),
    "Folk Harp":      ("Keys",  "FOLKHARP", "02_EWHarp_Normal_C4_v2_RR1.wav", 0),
    "Renaissance Organ": ("Keys", "RENORGAN", "02_RenOrgan_8foot_Room_C4_rr1.wav", 0),
    "Real Flute":     ("Leads", "FLUTE",    "00_LDFlute_susvib_C4_v1_1.wav", 0),
    "Recorder":       ("Leads", "RECORDER", "01_TenRecorder_Sus_C4_rr1_Main.wav", 0),
    "Tenor Sax":      ("Leads", "SAX",      "02_BrettTenor_NV_Main_A#3_vl2_rr1.wav", 2),   # A#3 -> C4
    "Bowed Psaltery": ("Pads",  "PSALTERY", "01_BowedPsaltery_E4_Main_LongBow_rr2.wav", -4),   # E4 -> C4
}

def read(p):
    w = wave.open(str(p)); n, ch, sw = w.getnframes(), w.getnchannels(), w.getsampwidth()
    raw = w.readframes(n); w.close()
    raw = raw[: len(raw) // (sw * ch) * sw * ch]   # the sets are cut mid-file: whole frames only
    if sw == 3:
        b = np.frombuffer(raw, np.uint8).reshape(-1, 3).astype(np.int32)
        x = (b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)); x = np.where(x >= 1 << 23, x - (1 << 24), x) / float(1 << 23)
    else:
        x = np.frombuffer(raw, np.int16) / 32768.0
    return x.reshape(-1, ch).mean(axis=1), w.getframerate()

def main():
    src = Path(sys.argv[1]); out = Path(sys.argv[2]) if len(sys.argv) > 2 else Path(__file__).resolve().parent.parent / "samples"
    for name, (folder, st, f, up) in MAP.items():
        x, sr = read(src / st / f)
        if up:   # play it faster / slower: resample by the pitch ratio, keep the rate
            fr = Fraction(2 ** (-up / 12.0)).limit_denominator(1000); x = resample_poly(x, fr.numerator, fr.denominator)
        x = x[: int(sr * 1.6)]
        fade = int(sr * 0.15); x[-fade:] *= np.linspace(1, 0, fade)
        x = x / max(1e-9, np.abs(x).max()) * 0.89
        d = out / folder; d.mkdir(parents=True, exist_ok=True)
        w = wave.open(str(d / f"{name}.wav"), "wb"); w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr)
        w.writeframes((x * 32767).astype("<i2").tobytes()); w.close()
        print(folder, name, f"{len(x) / sr:.2f}s")

main()
