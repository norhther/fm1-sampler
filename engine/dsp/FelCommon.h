// SPDX-License-Identifier: GPL-3.0-only
// Shared pieces of the Felucca-Engines ports (ANALOG, TRIO, LOFI, NOISE: MIT, Leo Kuroshita / Hügelton Instruments).
// The originals are fixed point; these are float with the same ranges: every knob is the 0..127 of the Felucca editor.
#pragma once
#include <cmath>
#include <cstdint>
#include "Common.h"
namespace fel {

inline float pitchOf(float hz) { return 69.f + 12.f * std::log2(std::max(hz, 1.f) / 440.f); }   // MIDI note number, fractional
// Felucca's cutoff scale: 127 steps of 0.86 semitone down from 16 kHz (about 30 Hz at 0)
inline float cutHz(float idx, float sr) { return std::min(16000.f * std::exp2(-(127.f - clampf(idx, 0.f, 127.f)) * 0.86f / 12.f), 0.45f * sr); }
inline float knee(float y, float k) {   // linear up to k, then a soft knee: only the peaks above k saturate
    const float a = std::fabs(y);
    if (a <= k) return y;
    const float r = k + 0.5f * std::tanh((a - k) * 2.f);
    return y < 0.f ? -r : r;
}
inline uint32_t xorshift(uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
inline float unit(uint32_t& s) { return (float)(xorshift(s) >> 8) * (1.f / 16777216.f); }   // 0..1

// Trapezoidal state-variable filter (A. Simper): lp, bp (v1) and the damping k (2 = no resonance, 0.15 = near self-oscillation)
struct Svf {
    float ic1 = 0.f, ic2 = 0.f, a1 = 1.f, a2 = 0.f, a3 = 0.f;
    void set(float hz, float sr, float k) {
        const float g = std::tan(kPi * std::min(hz, 0.45f * sr) / sr);
        a1 = 1.f / (1.f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    void run(float in, float& lp, float& bp) {
        const float v3 = in - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = clampf(2.f * v1 - ic1, -4.5f, 4.5f); ic2 = clampf(2.f * v2 - ic2, -4.5f, 4.5f);
        lp = v2; bp = v1;
    }
};

// band limiting for the steps of saws, pulses, restarts: a step of size S that happened x (0..1) of a sample ago is spread over
// the sample before it and this one (polyBLEP), so the output runs one sample late. cur gets this sample's naive value, then push().
struct Mix {
    float prev = 0.f, cur = 0.f;
    void step(float S, float x) { const float h = 0.5f * S, y = 1.f - x; prev += h * y * y; cur += h * (2.f * x - x * x - 1.f); }
    float push() { const float o = prev; prev = cur; cur = 0.f; return o; }
};
inline float tri(float ph) { return ph < 0.5f ? 4.f * ph - 1.f : 3.f - 4.f * ph; }
// advance a phase by dt; true when it wrapped (x = how far past the wrap, in samples, 0..1)
inline bool adv(float& ph, float dt, float& x) { ph += dt; if (ph >= 1.f) { ph -= 1.f; x = dt > 0.f ? std::min(1.f, ph / dt) : 0.f; return true; } return false; }
inline float sawOsc(float& ph, float dt, float g, Mix& m) {
    float x; if (adv(ph, dt, x)) m.step(-2.f * g, x);
    const float v = g * (2.f * ph - 1.f); m.cur += v; return v;
}
inline float pulseOsc(float& ph, float dt, float pw, float g, Mix& m) {
    const float old = ph; float x;
    if (adv(ph, dt, x)) m.step(2.f * g, x);
    if (old < pw && ph >= pw && ph >= old) m.step(-2.f * g, dt > 0.f ? std::min(1.f, (ph - pw) / dt) : 0.f);
    const float v = g * ((ph < pw ? 1.f : -1.f) - (2.f * pw - 1.f)); m.cur += v; return v;
}
}  // namespace fel
