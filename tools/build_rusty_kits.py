#!/usr/bin/env python3
"""Rusty Kit and Boom Bap Kit: two 16 pad drum kits cut from Karoryfer Big Rusty Drums (CC0 1.0).
Source: https://github.com/sfzinstruments/karoryfer.big-rusty-drums (recordings, Karoryfer Samples, public domain dedication).
usage: build_rusty_kits.py [out_dir (default ../samples)] [cache_dir (default /tmp/big_rusty)]
Needs ffmpeg, numpy, scipy. The first run downloads about 3 MB of FLAC hits into the cache.
Rusty Kit = the drums as recorded. Boom Bap Kit = the same hits pushed through an SP-1200 style chain
(saturation, 26 kHz zero-order hold, 12 bit, low pass): the dusty sampled-break sound."""
import json, os, subprocess, sys, urllib.request
import numpy as np
from scipy import signal
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_instruments as G

REPO = "sfzinstruments/karoryfer.big-rusty-drums"
RAW = f"https://raw.githubusercontent.com/{REPO}/HEAD/"
SR = G.SR


def tree():
    with urllib.request.urlopen(f"https://api.github.com/repos/{REPO}/git/trees/HEAD?recursive=1") as r:
        return [t["path"] for t in json.load(r)["tree"] if t["path"].startswith("Samples/") and t["path"].endswith(".flac")]


def fetch(path, cache):
    dst = os.path.join(cache, path.replace("/", "_"))
    if not os.path.exists(dst):
        with urllib.request.urlopen(RAW + urllib.request.quote(path)) as r, open(dst, "wb") as f:
            f.write(r.read())
    return dst


def load(path, cache):
    """FLAC -> mono float at SR (ffmpeg downmixes and resamples)."""
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", fetch(path, cache), "-ac", "1", "-ar", str(SR), "-f", "f32le", "-"], check=True, capture_output=True).stdout
    return np.frombuffer(raw, dtype="<f4").astype(float)


def mix(parts, cache, files):
    """parts: [(folder, name pattern, gain)]; the mic layers of one hit, summed."""
    y = np.zeros(1)
    for folder, pat, g in parts:
        hit = [p for p in files if p.startswith(f"Samples/{folder}/") and p.endswith(pat)]
        if not hit:
            sys.exit(f"missing {folder} {pat}")
        x = load(hit[0], cache) * g
        if len(x) > len(y):
            y = np.pad(y, (0, len(x) - len(y)))
        y[:len(x)] += x
    return y


def trim_front(y):
    """Start at the attack (the library hits have a little air before it)."""
    i = int(np.argmax(np.abs(y) > 0.04 * np.max(np.abs(y))))
    return y[max(0, i - 8):]


def hits(cache, files):
    L = lambda folder, name, g=1.0: (folder, name, g)
    layers = [
        [L("kick_24/kick/kick", "k_vl8_rr1.flac"), L("kick_24/kick/oh", "k_vl8_rr1.flac", 0.5)],                      # 0 kick
        [L("kick_24/kick/kick", "k_vl4_rr1.flac"), L("kick_24/kick/oh", "k_vl4_rr1.flac", 0.5)],                      # 1 soft kick
        [L("snare_14/center/top", "sn_center_vl8_rr1.flac"), L("snare_14/center/btm", "sn_center_vl8_rr1.flac", 0.5), L("snare_14/center/oh", "sn_center_vl8_rr1.flac", 0.4)],   # 2 snare
        [L("snare_14/center/top", "sn_center_vl3_rr1.flac"), L("snare_14/center/btm", "sn_center_vl3_rr1.flac", 0.5)],  # 3 ghost snare
        [L("snare_14/rimshot/top", "sn_rims_vl4_rr1.flac"), L("snare_14/rimshot/btm", "sn_rims_vl4_rr1.flac", 0.5)],   # 4 rimshot
        [L("hihat_14/tc/cl", "ht_tc_vl5_rr1.flac")],                                                                   # 5 closed hat
        [L("hihat_14/open/cl", "ht_open_vl4_rr1.flac")],                                                               # 6 open hat
        [L("tom_18/center/cl", "t18_vl7_rr1.flac")],                                                            # 7 low tom
        [L("tom_15/center/cl", "t15_vl7_rr1.flac")],                                                            # 8 mid tom
        [L("tom_14/center/cl", "t14_vl6_rr1.flac")],                                                            # 9 high tom
        [L("ride_22/rd/cl", "rd_vl6_rr1.flac")],                                                                       # 10 ride
        [L("crash_17/cr/cl", "cr_vl4_rr1.flac")],                                                                      # 11 crash
        [L("snare_14/rimshot/top", "sn_rims_vl6_rr1.flac"), L("snare_14/rimshot/btm", "sn_rims_vl6_rr1.flac", 0.5)],   # 12 hard rimshot
        [L("hihat_14/footspl/cl", "ht_footspl_vl4_rr1.flac")],                                                         # 13 foot splash
        [L("china_18/cn/cl", "cn_vl3_rr1.flac")],                                                                      # 14 china
        [L("kick_24_nodamp/kick/kick", "k_nodamp_vl5_rr1.flac"), L("kick_24_nodamp/kick/oh", "k_nodamp_vl5_rr1.flac", 0.5)],        # 15 open kick
    ]
    return [trim_front(mix(p, cache, files)) for p in layers]


def boom_bap(x):
    """SP-1200 style: drive, 26 kHz sample and hold, 12 bit, then a low pass."""
    y = np.tanh(1.8 * x / (np.max(np.abs(x)) + 1e-9)) / np.tanh(1.8)
    hold = int(round(SR / 26040))
    y = np.repeat(y[::hold], hold)[:len(y)]
    y = np.round(y * 2047) / 2047
    sos = signal.butter(4, 10000, "low", fs=SR, output="sos")
    return signal.sosfilt(sos, y)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "samples")
    cache = sys.argv[2] if len(sys.argv) > 2 else "/tmp/big_rusty"
    os.makedirs(cache, exist_ok=True); os.makedirs(os.path.join(out, "Drums"), exist_ok=True)
    h = hits(cache, tree())
    for name, fx in (("Rusty Kit", lambda x: x), ("Boom Bap Kit", boom_bap)):
        path = os.path.join(out, "Drums", name + ".wav")
        G.write_wav(path, G.place([fx(x) for x in h]))
        print(path, os.path.getsize(path) // 1024, "KB")


if __name__ == "__main__":
    main()
