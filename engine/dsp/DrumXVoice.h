// SPDX-License-Identifier: GPL-3.0-only
// DRUM-X of JIANT (FELUCCA TONIC, GPL-3.0; Leo Kuroshita and Juan Jiant), after the idea of Sonic Charge's Microtonic: a tuned oscillator with a
// pitch envelope and FM, filtered noise, their envelopes, two patches A and B and a morph between them. A float port of drumx_voice.c's dx_run
// (without the kit-wide FM ENV / LAYER / HANDS / TONE extras: the factory kits run with them off).
#pragma once
#include "FelCommon.h"
#include "DrumXKits.h"
class DrumXVoice {
public:
    // lane 0..7 of kit (1-based), patch morph 0..1 (0 A, 1 B), tune in semitones
    void start(int kit, int lane, float morph, float tune, float sr, uint32_t seed) {
        const DrumXLane& L = kDrumXKits[((kit - 1) % kDrumXKitCount + kDrumXKitCount) % kDrumXKitCount].lane[lane & 7];
        sr_ = sr; mode_ = L.mode; live_ = true; t_ = 0.f; rng_ = seed * 2654435761u + 0x2545F491u; ph_ = ph2_ = ph3_ = 0.f; flt_ = fel::Svf(); lp_ = 0.f; blk_ = 0;
        float q[5]; for (int i = 0; i < 5; i++) q[i] = L.a[i] + (L.b[i] - L.a[i]) * morph;
        const float d = q[2];
        ta_ = 0.003f * std::exp2(9.f * d / 127.f);
        tn_ = (mode_ >> 4 & 1) ? ta_ * 0.25f : ta_;
        tp_ = ((mode_ >> 5 & 3) == 1) ? clampf(ta_ * 0.5f, 0.004f, 0.25f) : clampf(ta_ / 6.f, 0.001f, 0.04f);
        midi_ = 24.f + 0.75f * q[0] + tune; depth_ = q[1] * 0.375f; ng_ = q[3] / 127.f; color_ = q[4];
        og_ = (1.f - ng_) * 11.f / 16.f; ng_ *= 11.f / 16.f;
        const bool bp = (mode_ >> 2 & 3) == 1;
        flt_.set(fel::cutHz(color_, sr_), sr_, bp ? 0.586f : 1.46f);
    }
    bool alive() const { return live_; }
    float next() {
        if (!live_) return 0.f;
        const float dt = 1.f / sr_; t_ += dt;
        const float ea = std::exp(-t_ / ta_), en = std::exp(-t_ / tn_), ep = std::exp(-t_ / tp_);
        const int wave = mode_ & 3, pm = mode_ >> 5 & 3;
        float off = depth_ * ep;
        if (pm == 2) { if (blk_-- <= 0) { blk_ = 31; held_ = (fel::unit(rng_) * 2.f - 1.f); } off *= held_; }
        const float f0 = 440.f * std::exp2((midi_ + off - 69.f) / 12.f);
        if (pm == 3) { ph3_ += f0 * 0.125f * dt; ph3_ -= std::floor(ph3_); off *= std::sin(2.f * kPi * ph3_); }
        const float f = pm == 3 ? 440.f * std::exp2((midi_ + off - 69.f) / 12.f) : f0;
        float y = 0.f;
        if (og_ > 0.f) {
            ph_ += std::min(f, 0.45f * sr_) * dt; ph_ -= std::floor(ph_);
            if (wave == 0) { const float g = 1.f + color_ * 48.f / 4096.f; y = color_ > 0.f ? std::tanh(std::sin(2.f * kPi * ph_) * g) : std::sin(2.f * kPi * ph_); }
            else {
                static const float R[4] = {1.f, 1.f, 6021.f / 4096.f, 11305.f / 4096.f};
                ph2_ += f * R[wave] * dt; ph2_ -= std::floor(ph2_);
                y = std::sin(2.f * kPi * (ph_ + color_ * 0.0117f * std::sin(2.f * kPi * ph2_)));
            }
            y *= og_ * ea;
            if (mode_ & 0x80) y = std::tanh(y * 3.f) * 22000.f / 32768.f;
        }
        if (ng_ > 0.f) {
            const float r = (fel::unit(rng_) * 2.f - 1.f) * 0.7071f;
            float lp, bp; flt_.run(r, lp, bp);
            const int flt = mode_ >> 2 & 3;
            const float o = flt == 0 ? lp : flt == 1 ? bp : r - 1.46f * bp - lp;
            y += clampf(o * 2.f, -1.f, 1.f) * ng_ * en;
        }
        lp_ += (y - lp_) * (1.f - std::exp(-2.f * kPi * 8000.f / sr_));   // the edge taken off, ~8 kHz
        if (ea < 0.0005f && en < 0.0005f) live_ = false;
        if (t_ >= 2.5f) { lp_ *= clampf((2.5f - t_) / 0.4f + 1.f, 0.f, 1.f); if (t_ >= 2.9f) live_ = false; }   // ponytail: no hit rings past 2.9 s (the longest kick decays would)
        return lp_;
    }
private:
    float sr_ = 44100.f, t_ = 0.f, ta_ = 0.1f, tn_ = 0.1f, tp_ = 0.01f, midi_ = 60.f, depth_ = 0.f, ng_ = 0.f, og_ = 0.f, color_ = 0.f;
    float ph_ = 0.f, ph2_ = 0.f, ph3_ = 0.f, lp_ = 0.f, held_ = 0.f; int blk_ = 0; uint32_t rng_ = 1; uint8_t mode_ = 0; bool live_ = false; fel::Svf flt_;
};
