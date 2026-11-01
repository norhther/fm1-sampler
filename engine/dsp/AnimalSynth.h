// SPDX-License-Identifier: GPL-3.0-only
// A float port of JIANT FM's ANIMAL engine (https://github.com/juanjiant-bit/JIANT firmware/src/eng_animal.c, GPL-3.0-only, by Juan Ignacio Antonucci
// on Felucca by Leo Kuroshita): animal calls from four cheap physical primitives, a source, one or two resonators, and a slow pattern layer.
// Engine 12. x = the eight knobs, as the firmware's EDIT page (0..127 unless noted); MODE picks how the rest read:
//   x0 MODE  0 CMOS (the 40106 / 4093 "bird machine": Schmitt RC oscillators)  1 PULSE (impulses into a resonator: cricket, cicada, frog)
//            2 WHSTL (a syrinx as a Hopf oscillator: songbirds, dove, owl)  3 WING (fly-bys: bee, mosquito)  4 GLOTT (a mammal's larynx and two formants)
//   x1 TOUCH / CLICK / PRESS / BUZZ / MOUTH     x2 COUPLE / Q / CHAOS / DRIFT / ROUGH     x3 CAP / SIZE (64 = as played, a semitone a step, lower = higher)
//   x4 HYST (-64..63) / DETUNE / SWEEP / DOPPLR / SWEEP     x5 RATE (syllables a second, 0.05 .. 40 Hz)     x6 GROUP 0..15 (HOLD TRILL 1+1 .. 32+32)
//   x7 FM (0..40) / TEMP (degrees C, Dolbear's law) / AIR (GLOTT)
// Not bit-exact with the firmware (it is fixed point): CMOS has no BLEP on its edges, the levels are set by ear against the firmware's.
#pragma once
#include "Common.h"
#include "DungeonSynth.h"

namespace animal {

inline float lfoHz(float v) { return 0.05f * std::pow(800.f, v / 127.f); }

class Voice {
public:
    void start(const float* x, float vel, float sr, uint32_t seed) {
        *this = Voice();
        for (int i = 0; i < 8; i++) x_[i] = x[i];
        mode_ = (int)clampf(x[0], 0.f, 4.f); vel_ = vel; sr_ = sr; rng_ = seed * 2654435761u + 7u;
        for (int i = 0; i < 4; i++) ph_[i] = 0.f;
        vs_ = 0.5f - 1.f / 16.f; ss_ = true; sg_ = true; vg_ = 1.f / 3.f; vf_ = kVtl; vf2_ = kVtl; sf_ = false;
        lx_ = 0.56f; r_ = 0.f; spd_ = 8;
        f1_.set(300.f, 0.186f, sr); f2_.set(900.f, 0.127f, sr);
    }
    float next(float hz, float tone) {
        if ((tick_++ & 31) == 0) control(hz);
        switch (mode_) {
            case 0: return cmos(hz);
            case 1: return pulse(hz);
            case 2: return whstl(hz);
            case 3: return wing(hz);
            default: return glott(hz);
        }
    }
private:
    static constexpr float kVth = 0.58f, kVtl = 0.38f, kSq = 0.34f;
    float x_[8] = {}; int mode_ = 0; float vel_ = 1.f, sr_ = 48000.f; uint32_t rng_ = 1; int tick_ = 0;
    float ph_[4] = {}, env_ = 1.f, sph_ = 0.f; int cnt_ = 0, spd_ = 8;
    // CMOS
    float vs_ = 0.f, vg_ = 0.f, vf_ = 0.f, vf2_ = 0.f, gate_ = 1.f, sweep_ = 0.f, yd_ = 0.f, lp_ = 0.f, dc_ = 0.f; bool ss_ = true, sg_ = true, sf_ = false, sf2_ = false;
    // PULSE / GLOTT / WHSTL / WING
    dungeon::Bp f1_, f2_; float s1_ = 0.f, s2_ = 0.f, r_ = 0.f, jw_ = 0.f, dr_ = 0.f, drT_ = 0.f, lx_ = 0.f, tl_ = 0.f, icMul_ = 1.f; int par_ = 0, drC_ = 0; float lpw_ = 0.f;

    float rnd() { rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5; return (rng_ >> 8) / 16777216.f; }
    float tempF() const { return std::max(26.f / 256.f, (clampf(x_[7], 0.f, 40.f) - 3.f) / 19.f); }
    float reg() const { return 64.f - x_[3]; }   // semitones
    static float hook(float u, float depth) { return -depth * std::exp(-4.f * u); }

    // the pattern of every mode but CMOS: syllables at RATE, grouped, each with its own speed (+-45 %) and shape
    void pattern() {
        const int g = (int)clampf(x_[6], 0.f, 15.f);
        static const uint8_t G[16][2] = {{1, 0}, {1, 0}, {1, 1}, {1, 3}, {2, 2}, {2, 6}, {3, 3}, {3, 9}, {4, 4}, {4, 12}, {6, 6}, {8, 8}, {8, 24}, {12, 12}, {16, 16}, {32, 32}};
        float rate = lfoHz(x_[5]) * (mode_ == 4 ? 1.f : tempF()) * 32.f / sr_;   // per control tick
        const float irr = mode_ == 1 ? std::fabs(x_[4]) * 2.f : x_[2];
        rate *= 1.f + (float)(spd_ - 8) * irr / 2048.f;
        if (g == 0) { sph_ = std::min(1.f, sph_ + rate); env_ = 1.f; return; }
        sph_ += rate;
        if (sph_ >= 1.f) { sph_ -= 1.f; cnt_ = cnt_ + 1 >= G[g][0] + G[g][1] ? 0 : cnt_ + 1; spd_ = (int)(rnd() * 16.f); }
        const float d = mode_ == 1 ? 0.5f : mode_ == 4 ? 0.6f : mode_ == 2 ? 0.7f : 1.f, u = sph_;
        if (cnt_ >= G[g][0] || u >= d) { env_ = 0.f; return; }
        const float x = u / d;
        switch (mode_) {
            case 1: env_ = x < 0.2f ? x * 5.f : (1.f - x) * 1.25f; break;
            case 4: env_ = x < 0.1f ? x * 10.f : (1.f - x) * (1.f - x) * 1.235f; break;
            case 2: env_ = 4.f * x * (1.f - x); break;
            default: { const float z = (x - 0.5f) * 5.f; env_ = 1.f / (1.f + z * z); }
        }
        env_ = std::min(1.f, env_);
    }

    void control(float hz) {
        if (mode_ != 0) { pattern(); controlOther(); return; }
        // CMOS: the third oscillator (phrases) and the slow one (chirps), once per 32 samples
        const float tickS = 32.f / sr_, touch = clampf(x_[1] / 127.f + (vel_ * 127.f - 100.f) * 160.f / 32768.f, 0.f, 1.f), h = x_[4], ah = std::fabs(h);
        float rf = 1.f;
        if (x_[6] > 0.f) {
            const float ig = lfoHz(20.f + std::min(x_[6], 15.f) * 4.f) * tickS, k = sg_ ? ig * 1.156f : ig * 1.734f;
            vg_ += ((sg_ ? 1.f : 0.f) - vg_) * std::min(1.f, k * 6.28f);
            if (sg_ && vg_ >= 2.f / 3.f) sg_ = false; else if (!sg_ && vg_ <= 1.f / 3.f) sg_ = true;
            rf = 0.65f + (vg_ - 1.f / 3.f) * 2.85f;   // the chirps speed up as it charges
        } else sg_ = true;
        const float w = 0.12f + ah * 0.0106f, vth = 0.5f + w * 0.5f, vtl = 0.5f - w * 0.5f, lw = std::log((1.f + w) / (1.f - w));
        const float d = (38.f + x_[3] * 179.f / 127.f) / 256.f, cyc = lfoHz(x_[5]) * rf * (1.f + touch * 0.6f) * tickS;
        const float la = cyc * lw / d, lb = cyc * lw / (1.f - d);
        if (sg_) {
            vs_ += ((ss_ ? 1.f : 0.f) - vs_) * (1.f - std::exp(-(ss_ ? la : lb)));
            if (ss_ && vs_ >= vth) { ss_ = false; vs_ -= (rnd() - 0.5f) * touch * 0.12f; }
            else if (!ss_ && vs_ <= vtl) { ss_ = true; vs_ += (rnd() - 0.5f) * touch * 0.12f; }
        } else { vs_ = vtl; ss_ = true; }
        vs_ = clampf(vs_, 0.f, 1.f);
        const float x15 = clampf((vs_ - vtl) / (vth - vtl), 0.f, 1.f), fm = x_[7];
        const float gamt = clampf((40.f - fm) / 20.f, 0.f, 1.f), famt = clampf(fm / 20.f, 0.f, 1.f);
        sweep_ = (ah * famt * 6.f / 16.f) * (1.f - x15) * (h < 0.f ? 1.f : -1.f);   // semitones: up from below to the key, or down from above
        gate_ = !sg_ ? 0.f : ss_ ? 1.f : 1.f - gamt;
        (void)hz;
    }
    void controlOther() {
        if (mode_ == 3) {   // WING: a drift target every 40 .. 160 ms
            if (--drC_ <= 0) { drT_ = (rnd() - 0.5f) * 2.f * x_[2] / 127.f * 2.f; drC_ = (int)((0.04f + rnd() * 0.12f) * sr_ / 32.f); }
            dr_ += (drT_ - dr_) / 32.f;
        }
        if (mode_ == 2) jw_ += (rnd() - 0.5f) * x_[2] * 0.004f - jw_ / 32.f;
    }

    float cmos(float hz) {
        const float f = hz * 2.f * std::pow(2.f, sweep_ / 12.f), l = 0.719f * f / sr_, k = 1.f - std::exp(-std::min(l, 0.5f));   // an octave up: C5 chirps at ~1.1 kHz
        const float cpl = x_[2] / 127.f; float kk = sf_ ? k : k * 1.28f;
        if (cpl > 0.f) kk += (sf2_ ? 1.f : -1.f) * k * cpl * 0.5f;
        vf_ += ((sf_ ? 1.f : 0.f) - vf_) * std::max(0.f, kk);
        if (sf_ ? vf_ >= kVth : vf_ <= kVtl) sf_ = !sf_;
        if (cpl > 0.f) {
            const float k2 = k * (0.5f + cpl * 0.37f), kb = k2 + (sf_ ? 1.f : -1.f) * k2 * cpl * 0.5f;
            vf2_ += ((sf2_ ? 1.f : 0.f) - vf2_) * std::max(0.f, kb);
            if (sf2_ ? vf2_ >= kVth : vf2_ <= kVtl) sf2_ = !sf2_;
        }
        const float y = sf_ ? kSq : -kSq;
        lp_ += (y - lp_) * 0.37f; dc_ += (lp_ - dc_) / 512.f;   // the breadboard and its speaker, and the coupling capacitor
        return (lp_ - dc_) * 2.f * gate_;
    }

    float pulse(float hz) {
        const float fr = hz * std::pow(2.f, reg() / 12.f), d = (127.f - x_[1]) * 2322.f / 256.f / 16.f, det = x_[4] * 0.5f / 16.f;
        const float fp = fr * std::pow(2.f, (det - d) / 12.f), k = std::max(0.0244f, 0.667f * std::pow(2.f, -x_[2] * 145.f / 4096.f));
        f1_.set(std::min(fr, sr_ * 0.45f), 1.f / std::max(k, 0.02f), sr_);
        ph_[0] += fp / sr_; float imp = 0.f;
        if (ph_[0] >= 1.f) { ph_[0] -= 1.f; imp = 3.f * std::min(1.f, k * 12.f); if (x_[4] != 0.f) ph_[0] += (rnd() - 0.5f) * std::fabs(x_[4]) * 0.02f; }   // a pulse, its size for a ring of about the same level at any Q; DETUNE also jitters them
        const float v = f1_.run(imp), og = 1.f + k * 10.f;   // a broadband ring is short: louder
        return dungeon::softKnee(v * og * 0.8f) * env_;
    }

    float whstl(float hz) {
        const float pr = env_ * (x_[1] / 127.f) * (0.4f + vel_ * 0.6f), mu = pr - 0.2f + (rnd() - 0.5f) * x_[2] / 512.f;
        const float syl = lfoHz(x_[5]) * tempF(), hs = std::min(0.49f, (5000.f + syl * 3116.f * 0.0001f * 1.f) / 32768.f / 8.f * 8.f);
        r_ += hs / 8.f * (mu * r_ - r_ * r_ * r_);
        if (pr > 0.2f && r_ < 0.0025f) r_ = 0.0025f;
        r_ = clampf(r_, 0.f, 1.46f);
        float dep = x_[4] * 0.375f; if (syl > 16.f && dep != 0.f) dep *= 16.f / syl;
        const float semis = reg() + hook(sph_, dep) + jw_ + r_ * r_ * 0.19f, f = hz * std::pow(2.f, semis / 12.f);
        ph_[0] += f / sr_; ph_[0] -= std::floor(ph_[0]);
        float y = std::sin(2.f * kPi * ph_[0]) * r_;
        if (x_[2] > 0.f) y += std::sin(4.f * kPi * ph_[0]) * r_ * x_[2] / 256.f * 0.5f;
        return y * 1.2f;
    }

    float wing(float hz) {
        const float f = hz * std::pow(2.f, (reg() + dopp() + dr_) / 12.f), dt = f / sr_;
        ph_[0] += dt; ph_[0] -= std::floor(ph_[0]);
        const float pw = 0.5f - x_[1] * 0.0033f;
        float p2 = ph_[0] + pw; p2 -= std::floor(p2);
        const float x = (dungeon::saw(ph_[0], dt) - dungeon::saw(p2, dt)) * 0.5f;
        const float lk = clampf((3000.f + x_[1] * 110.f + env_ * (x_[1] + 40.f) * 32.f + 0.f) / 32768.f, 0.05f, 0.9f);
        lpw_ += (x - lpw_) * lk;
        return lpw_ * 2.2f * env_;
    }
    float dopp() const {
        if ((int)clampf(x_[6], 0.f, 15.f) == 0) return 0.f;
        const float u = sph_, s = 3.f * u * u - 2.f * u * u * u;
        return x_[4] * (1.f - 2.f * s) / 8.f;
    }

    float glott(float hz) {
        const float mo = x_[1], ro = x_[2], alt = std::min(ro, 64.f) * 30.f / 32768.f, cha = ro > 64.f ? (ro - 64.f) * 67.f / 4096.f : 0.f, sh = std::min(ro, 64.f) * 160.f / 32768.f;
        const float sz = (64.f - x_[3]) * 112.f, air = (x_[7] * x_[7] * 5.f / 16.f) / 256.f, tk = (1500.f + mo * 190.f) / 32768.f;
        const float inc0 = hz * std::pow(2.f, hook(sph_, x_[4] * 0.375f) / 12.f) / sr_;
        auto cut = [](float u) { return 30.f * std::pow(533.33f, u / 127.f); };
        if ((tick_ & 31) == 1) { f1_.set(cut((11878.f + mo * 37.f + sz) / 256.f), 1.f / 0.186f, sr_); f2_.set(cut((16333.f + mo * 25.f + sz) / 256.f), 1.f / 0.127f, sr_); }
        float ic = inc0 * icMul_;
        ph_[0] += ic;
        if (ph_[0] >= 1.f) {   // a new glottal cycle: its length and strength
            ph_[0] -= 1.f; par_ ^= 1;
            icMul_ = 1.f;
            if (alt > 0.f) icMul_ += (par_ ? alt : -alt) * 4.f;   // period doubling: long, short, long
            if (cha > 0.f) { lx_ = 3.9f * lx_ * (1.f - lx_); if (lx_ < 0.001f) lx_ = 0.56f; icMul_ += (lx_ - 0.5f) * cha * 0.25f; }
            ic = inc0 * icMul_;
        }
        const float amp = (par_ && alt > 0.f) ? 1.f - sh : 1.f;
        float x = (2.f * ph_[0] - 1.f - 0.f) * amp;
        tl_ += (x - tl_) * std::min(1.f, tk); x = tl_;
        if (air > 0.f) x += (rnd() - 0.5f) * air;
        x *= 0.25f;
        const float y = f1_.run(x) + 0.75f * f2_.run(x);
        return dungeon::softKnee(y * 1.5f * 6.f) * env_;
    }
};

}  // namespace animal
