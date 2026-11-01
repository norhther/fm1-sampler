// SPDX-License-Identifier: GPL-3.0-only
// ANALOG and TRIO of Felucca-Engines (MIT), as floats. x[0..7] are the engine's eight knobs, 0..127 (WAVE etc. are the enum index).
#pragma once
#include "FelCommon.h"
namespace fel {

// ANALOG: two band-limited oscillators (saw / square / tri / sine / PWM / hard-synced saw / saw + sub square), noise, drive, low-pass.
// x: WAVE DTN MIX NOIS CUT RES DRV KTR
class Analog {
public:
    void start(const float* x, float sr, uint32_t seed) {
        for (int i = 0; i < 8; i++) x_[i] = x[i];
        sr_ = sr; rng_ = 0x1234567u + seed * 2654435761u; ph0_ = 0.f; ph1_ = 0.25f; t_ = 0.f; blk_ = 0; flt_ = Svf(); mix_ = Mix();
    }
    float next(float hz, float tone, float motion) {
        const int wave = std::min(6, (int)x_[0]);
        const float dt1 = hz / sr_; t_ += 1.f / sr_;
        if (blk_-- <= 0) block(hz, tone, motion);
        const float m2 = x_[2] / 127.f, m1 = 1.f - m2, det = x_[1];
        if (wave == 5) {   // SYNC: osc 2 a saw restarted by osc 1; its step at the restart is band-limited
            const float dt2 = hz * std::exp2((det * 3.f / 16.f + motion * 6.f) / 12.f) / sr_;
            sawOsc(ph0_, dt1, m1, mix_);
            const float old = ph1_; float x = 0.f;
            if (ph0_ < dt1) {   // osc 1 just wrapped: osc 2 restarts x of a sample ago
                x = std::min(1.f, ph0_ / dt1);
                const float at = old + dt2 * (1.f - x); const float S = (-1.f) - (2.f * (at - std::floor(at)) - 1.f);
                ph1_ = x * dt2; mix_.step(S * m2, x); mix_.cur += m2 * (2.f * ph1_ - 1.f);
            } else sawOsc(ph1_, dt2, m2, mix_);
        } else if (wave == 6) {   // SUB: saw + a square an octave below
            const float dt2 = 0.5f * dt1 * std::exp2(det / 1200.f);
            sawOsc(ph0_, dt1, m1, mix_); pulseOsc(ph1_, dt2, 0.5f, m2, mix_);
        } else {
            const float dt2 = dt1 * std::exp2(det / 1200.f);
            osc(wave, ph0_, dt1, m1); osc(wave, ph1_, dt2, m2);
        }
        float s = mix_.push();
        if (x_[3] > 0.f) s += (unit(rng_) - 0.5f) * (x_[3] * 200.f / 32768.f);
        if (x_[6] > 0.f) s = std::tanh(s * (1.f + x_[6] / 64.f));
        float lp, bp; flt_.run(0.5f * s, lp, bp);
        return knee(lp, 0.49f) * 2.f * 0.7f;
    }
private:
    float x_[8], sr_ = 44100.f, ph0_ = 0.f, ph1_ = 0.f, t_ = 0.f, pw_ = 0.5f;
    int blk_ = 0; uint32_t rng_ = 1; Svf flt_; Mix mix_;
    float osc(int wave, float& ph, float dt, float g) {
        switch (wave) {
            case 1: return pulseOsc(ph, dt, 0.5f, g, mix_);
            case 2: { ph += dt; ph -= std::floor(ph); const float v = g * tri(ph); mix_.cur += v; return v; }
            case 3: { ph += dt; ph -= std::floor(ph); const float v = g * std::sin(2.f * kPi * ph); mix_.cur += v; return v; }
            case 4: return pulseOsc(ph, dt, pw_, g, mix_);
            default: return sawOsc(ph, dt, g, mix_);
        }
    }
    void block(float hz, float tone, float motion) {   // every 32 samples
        blk_ = 31;
        const float idx = x_[4] + tone * 25.f + x_[7] * (pitchOf(hz) - 60.f) / 256.f;
        flt_.set(cutHz(idx, sr_), sr_, 2.f - x_[5] * 7600.f / 127.f / 4096.f);
        pw_ = clampf(0.5f + 0.3f * std::sin(2.f * kPi * 0.6f * t_) + 0.3f * motion, 0.06f, 0.94f);   // PWM: the LFO Felucca has on SHP
    }
};

// TRIO: three oscillators with ring modulation and hard sync into a 12 dB/oct multimode filter (the sound chips of early home computers).
// x: WAVE INT2 INT3 DTN MODE CUT RES PW
class Trio {
public:
    void start(const float* x, float sr, uint32_t seed) {
        for (int i = 0; i < 8; i++) x_[i] = x[i];
        sr_ = sr; rng_ = 0x2468ACEu + seed * 2654435761u; ph_[0] = 0.f; ph_[1] = 0.11f; ph_[2] = 0.22f; nz_ = 0.f; drift_ = 0.f; blk_ = 0;
        flt_ = Svf(); mix_ = Mix();
    }
    float next(float hz, float tone, float motion) {
        const Set& ws = kSets[(int)x_[0] & 15];
        const float dt1 = hz / sr_, dt2 = hz * std::exp2((x_[1] + x_[3] / 100.f) / 12.f) / sr_, dt3 = hz * std::exp2((x_[2] - x_[3] / 100.f) / 12.f) / sr_;
        if (blk_-- <= 0) block(tone, motion);
        const float p3 = ph_[2];
        wave(ws.w[2], ph_[2], dt3, ws.g[2], 0);
        float x1 = -1.f;
        if (ws.flags & 1) ring(dt1, ws.g[0], p3, dt3, ws.flags & 2 ? &x1 : nullptr);
        else wave(ws.w[0], ph_[0], dt1, ws.g[0], ws.flags & 2 ? &x1 : nullptr);
        if (ws.flags & 2) sync(ws.w[1], dt2, ws.g[1], x1);
        else wave(ws.w[1], ph_[1], dt2, ws.g[1], 0);
        const float s = knee(mix_.push(), 0.49f) * 0.5f;
        float lp, bp; flt_.run(s, lp, bp);
        const int mode = (int)x_[4] & 3;
        const float y = mode == 0 ? lp : mode == 1 ? bp * 1.367f : mode == 2 ? s - k_ * bp - lp : s - k_ * bp;
        return knee(y, 0.49f) * 2.2f;
    }
private:
    enum { OFF, TRI, SAW, PLS, NOI, ST, PS };
    struct Set { uint8_t w[3]; uint8_t flags; float g[3]; };   // flags: 1 ring, 2 sync
    static constexpr float G3 = 11000.f / 32768.f, G2 = 15000.f / 32768.f, G1 = 24000.f / 32768.f, K = 1.f / 32768.f;
    static constexpr Set kSets[16] = {
        {{SAW, SAW, SAW}, 0, {G3, G3, G3}}, {{PLS, PLS, PLS}, 0, {G3, G3, G3}},
        {{PLS, PLS, TRI}, 0, {G3, G3, G3 + 3000 * K}}, {{SAW, SAW, TRI}, 0, {G3, G3, G3 + 3000 * K}},
        {{TRI, TRI, TRI}, 0, {G3 + 2000 * K, G3 + 2000 * K, G3 + 2000 * K}},
        {{ST, ST, TRI}, 0, {G3 + 3000 * K, G3 + 3000 * K, G3}}, {{PS, PS, TRI}, 0, {G3 + 3000 * K, G3 + 3000 * K, G3}},
        {{PLS, OFF, NOI}, 0, {G2, 0.f, G2 - 4000 * K}}, {{NOI, OFF, OFF}, 0, {G1, 0.f, 0.f}},
        {{SAW, SAW, OFF}, 2, {G2 - 5000 * K, G2 + 5000 * K, 0.f}}, {{PLS, PLS, OFF}, 2, {G2 - 5000 * K, G2 + 5000 * K, 0.f}},
        {{SAW, SAW, TRI}, 2, {G3 - 3000 * K, G3 + 4000 * K, G3}},
        {{TRI, OFF, OFF}, 1, {G1, 0.f, 0.f}}, {{TRI, OFF, TRI}, 1, {G2 + 2000 * K, 0.f, G2 - 3000 * K}},
        {{TRI, SAW, OFF}, 3, {G2 + 2000 * K, G2, 0.f}}, {{TRI, PLS, OFF}, 3, {G2 + 2000 * K, G2, 0.f}}};
    float x_[8], sr_ = 44100.f, ph_[3], nz_ = 0.f, drift_ = 0.f, pw_ = 0.5f, pdc_ = 0.f, psm_ = 0.f, k_ = 1.f;
    int blk_ = 0; uint32_t rng_ = 1; Svf flt_; Mix mix_;
    static float st(float ph) { const float t = ph < 0.5f ? ph * 2.f : 2.f - ph * 2.f; return (float)(((int)(ph * 65536.f) & (int)(t * 65535.f)) - 16384) / 32768.f; }   // saw AND triangle, 16 bit
    float ps(float ph) const { return (ph >= pw_ ? ph : 0.f) - psm_; }
    float naive(int w, float ph) const {
        switch (w) { case TRI: return tri(ph); case SAW: return 2.f * ph - 1.f; case PLS: return (ph < pw_ ? 1.f : -1.f) - pdc_; case ST: return st(ph); case PS: return ps(ph); default: return 0.f; }
    }
    void wave(int w, float& ph, float dt, float g, float* wrapX) {   // one oscillator into mix_; wrapX: where it starts a cycle
        const float old = ph; float x;
        const bool wrapped = adv(ph, dt, x);
        if (wrapped && wrapX) *wrapX = x;
        switch (w) {
            case SAW: if (wrapped) mix_.step(-2.f * g, x); break;
            case PLS:
                if (wrapped) mix_.step(2.f * g, x);
                if (!wrapped && old < pw_ && ph >= pw_) mix_.step(-2.f * g, std::min(1.f, (ph - pw_) / dt));
                break;
            case NOI: if (std::floor(old * 16.f) != std::floor(ph * 16.f) || wrapped) nz_ = unit(rng_) * 2.f - 1.f; break;
            default: break;
        }
        mix_.cur += g * (w == NOI ? nz_ : naive(w, ph));
    }
    void ring(float dt, float g, float p3, float dt3, float* wrapX) {   // osc 1 a triangle whose sign osc 3's top phase bit flips
        const float old = ph_[0]; float x;
        if (adv(ph_[0], dt, x) && wrapX) *wrapX = x;
        (void)old;
        const float s = tri(ph_[0]), n3 = p3 + dt3, sg = (n3 - std::floor(n3)) >= 0.5f ? -g : g;
        if ((p3 >= 0.5f) != ((n3 - std::floor(n3)) >= 0.5f)) mix_.step(2.f * s * sg, std::min(1.f, std::fmod(n3, 0.5f) / std::max(dt3, 1e-9f)));
        mix_.cur += s * sg;
    }
    void sync(int w, float dt, float g, float wx) {   // osc 2 (saw / pulse) restarted where osc 1 starts a cycle (wx)
        const float old = ph_[1]; float x;
        if (wx >= 0.f) {
            const float at = old + dt * (1.f - wx);
            mix_.step(g * (naive(w, 0.f) - naive(w, at - std::floor(at))), wx);
            ph_[1] = wx * dt;
        } else {
            const bool wrapped = adv(ph_[1], dt, x);
            if (w == PLS) { if (wrapped) mix_.step(2.f * g, x); else if (old < pw_ && ph_[1] >= pw_) mix_.step(-2.f * g, std::min(1.f, (ph_[1] - pw_) / dt)); }
            else if (wrapped) mix_.step(-2.f * g, x);
        }
        mix_.cur += g * naive(w, ph_[1]);
    }
    void block(float tone, float motion) {
        blk_ = 31;
        const float duty = clampf((x_[7] - 64.f) / 128.f + 0.5f + 0.25f * motion, 0.024f, 0.976f);
        pw_ = duty; pdc_ = 2.f * duty - 1.f; psm_ = 1.f - duty * duty;
        drift_ += ((unit(rng_) - 0.5f) * 4.f - drift_) * 0.125f;   // the slightly unstable filter of the chips
        k_ = 2.f - x_[6] * 1.75f / 127.f;
        flt_.set(cutHz(x_[5] + tone * 25.f + drift_, sr_), sr_, k_);
    }
};
}  // namespace fel
