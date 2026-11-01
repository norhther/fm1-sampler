// SPDX-License-Identifier: GPL-3.0-only
// A float port of SLOOP's synthesised drum voice (isod89/sloop-fm1 firmware/src/drum_synth.c, GPL-3.0-only, on Felucca by
// Leo Kuroshita / Hugelton Instruments): up to four layers into one filter. tone (sine / tri / square / fm / bell, with a pitch
// drop and a second partial), click (the beater), noise (white / six metal squares / metal + white / chip LFSR / clap bursts),
// a resonant SVF on the noise or on everything, a high-pass on the noise, drive and the kit's crush. Decays are times to -60 dB.
#pragma once
#include "Common.h"

struct SloopSound {
    int wave, src;                       // 0 none 1 sine 2 tri 3 square 4 fm 5 bell; 0 none 1 white 2 metal 3 cym 4 chip 0x11 clap
    float hz, bend, bendMs, holdMs, decayMs, tlev, t2, t2lev, click, nlev, nholdMs, ndecMs;
    int fmode; float fcut, res; int fall; float fenv, hpf, chip, drive, levelDb;
};
struct SloopKit { const char* name; const char* style; int crush; SloopSound s[16]; };

class SloopDrumVoice {
public:
    // lane 0..15, vel 0..1, semis: the pad's pitch offset (tune)
    void start(const SloopSound& d, int crush, float vel, float sr, float semis, uint32_t seed) {
        *this = SloopDrumVoice(); d_ = d; sr_ = sr; crush_ = crush; gain_ = vel; rng_ = seed * 2654435761u + 99u;
        lg_ = std::pow(10.f, d.levelDb / 20.f);
        hz_ = d.hz * std::pow(2.f, semis / 12.f);
        amp_ = d.wave ? d.tlev / 127.f : 0.f; ka_ = std::exp(-6.9f / (std::max(5.f, d.decayMs) * 0.001f * sr));
        hold_ = d.holdMs * 0.001f * sr; nhold_ = (d.src & 0x10) ? 0.f : d.nholdMs * 0.001f * sr;
        nz_ = (d.src & 15) ? d.nlev / 127.f : 0.f; kn_ = (d.src & 0x10) ? std::exp(-6.9f / (0.012f * sr)) : std::exp(-6.9f / (std::max(5.f, d.ndecMs) * 0.001f * sr));
        ck_ = d.click / 127.f; kpe_ = std::exp(-6.9f / (std::max(5.f, d.bendMs) * 0.001f * sr)); pe_ = (d.bend || d.fenv != 0.f) ? 1.f : 0.f;
        cutHz_ = d.fcut * std::pow(2.f, (vel * 127.f - 110.f) / 127.f * 0.55f * 0.f + (vel - 0.866f) * 0.6f);
        if (!d.wave && semis != 0.f) cutHz_ *= std::pow(2.f, semis / 12.f);   // a tone-less sound: TUNE shifts its brightness too
        const float hp = d.hpf * (!d.wave ? std::pow(2.f, semis / 12.f) : 1.f);
        ahp_ = d.hpf > 0.f ? onePole(hp) : 0.f;
        for (int i = 0; i < 6; i++) { mph_[i] = rnd(); }
        chipInc_ = d.chip > 0.f ? std::min(0.5f, 8.f * d.chip / sr) : 0.f; lfsr_ = 0x4001u; rnd_ = rng_;
        setFilter();
    }
    bool alive() const { return (d_.src & 0x10 && bursts_ < 4) || hold_ > 0.f || amp_ > 0.001f || nz_ > 0.001f || ck_ > 0.001f; }
    float next() {
        static const float M[6] = {205.3f, 304.4f, 369.6f, 522.7f, 540.f, 800.f};
        const float dt1 = 1.f / sr_;
        // control: envelopes
        if (hold_ > 0.f) hold_ -= 1.f; else amp_ *= ka_;
        if ((d_.src & 0x10) && bursts_ < 4 && t_ >= bursts_ * 0.0095f) {   // clap: bursts at 0, 9.5, 19 ms; the 4th is the tail
            nz_ = d_.nlev / 127.f;
            if (++bursts_ == 4) { kn_ = std::exp(-6.9f / (std::max(5.f, d_.ndecMs) * 0.001f * sr_)); nhold_ = d_.nholdMs * 0.001f * sr_; }
        }
        if (nhold_ > 0.f) nhold_ -= 1.f; else nz_ *= kn_;
        ck_ *= 0.9f; pe_ *= kpe_;
        if (fmode_ && d_.fenv != 0.f && (++fc_ & 15) == 0) setFilter();
        t_ += dt1;
        const float f = hz_ * std::pow(2.f, pe_ * d_.bend / 12.f);
        float tone = 0.f, nz = 0.f; const int src = d_.src & 15;
        if (d_.wave) {
            const float inc = f * dt1;
            switch (d_.wave) {
                case 1: tone = std::sin(2.f * kPi * ph_); break;
                case 2: tone = 4.f * std::fabs(ph_ - 0.5f) - 1.f; break;
                case 3: tone = ph_ < 0.5f ? 0.73f : -0.73f; break;
                case 4: tone = std::sin(2.f * kPi * (ph_ + amp_ * 0.5f * std::sin(2.f * kPi * ph2_))); ph2_ += inc * 1.41f; ph2_ -= std::floor(ph2_); break;
                default: tone = (ph_ < 0.5f ? 0.37f : -0.37f) + (ph2_ < 0.5f ? 0.37f : -0.37f); ph2_ += inc * 1.484f; ph2_ -= std::floor(ph2_); break;
            }
            ph_ += inc; ph_ -= std::floor(ph_);
            tone *= amp_;
            if (d_.t2 > 0.f) { ph3_ += inc * d_.t2; ph3_ -= std::floor(ph3_); tone += std::sin(2.f * kPi * ph3_) * amp_ * amp_ * d_.t2lev / 127.f; }
        }
        if (src) {
            if (src == 1) nz = white();
            else if (src == 4) { cacc_ += chipInc_; if (cacc_ >= 1.f) { cacc_ -= 1.f; const uint32_t b = (lfsr_ ^ (lfsr_ >> 1)) & 1u; lfsr_ = (lfsr_ >> 1) | (b << 14); } nz = (lfsr_ & 1u) ? 0.6f : -0.6f; }
            else {   // metal / cym: six squares at the 808 cymbal ratios (+ white)
                const float base = hz_ / 205.3f; int sum = 0;
                for (int i = 0; i < 6; i++) { mph_[i] += M[i] * base * dt1; mph_[i] -= std::floor(mph_[i]); sum += mph_[i] < 0.5f ? 1 : 0; }
                nz = (float)(sum * 2 - 6) * 0.3f; if (src == 3) nz += white() * 0.5f;
            }
            if (d_.wave == 5) { nz += tone; tone = 0.f; }
            if (ahp_ > 0.f) { hp1_ += (nz - hp1_) * ahp_; nz -= hp1_; hp2_ += (nz - hp2_) * ahp_; nz -= hp2_; }
            nz *= nz_; if (d_.src & 0x10) nz *= 2.f;
        }
        if (ck_ > 0.001f) { const float w = white(); nz += (w - cprev_) * ck_; cprev_ = w; }
        float x = d_.fall ? tone + nz : nz;
        if (fmode_) {
            const float v3 = x - f2_, v1 = a1_ * f1_ + a2_ * v3, v2 = f2_ + a2_ * f1_ + a3_ * v3;
            f1_ = clampf(2.f * v1 - f1_, -4.f, 4.f); f2_ = clampf(2.f * v2 - f2_, -4.f, 4.f);
            x = fmode_ == 1 ? v2 : fmode_ == 2 ? v1 : x - fk_ * v1 - v2;
        }
        if (!d_.fall) x += tone;
        if (d_.drive > 0.f) { const float dr = 1.f + d_.drive / 16.f; x = std::tanh(x * dr) / std::tanh(dr * 0.8f) * 0.8f; }
        if (crush_) { const int bits = crush_ & 15, hold = (crush_ >> 4) + 1; if (--holdn_ <= 0) { holdn_ = hold; const float q = (float)(1 << std::max(1, 15 - bits)); held_ = std::round(x * q) / q; } x = held_; }
        return x * gain_ * lg_ * 0.5f;
    }
private:
    SloopSound d_{}; float sr_ = 48000.f, gain_ = 1.f, lg_ = 1.f, hz_ = 100.f, amp_ = 0.f, ka_ = 0.f, hold_ = 0.f, nhold_ = 0.f, nz_ = 0.f, kn_ = 0.f, ck_ = 0.f, kpe_ = 0.f, pe_ = 0.f, cutHz_ = 1000.f, ahp_ = 0.f;
    float ph_ = 0.f, ph2_ = 0.f, ph3_ = 0.f, mph_[6] = {}, t_ = 0.f, hp1_ = 0.f, hp2_ = 0.f, cprev_ = 0.f, held_ = 0.f, chipInc_ = 0.f, cacc_ = 0.f;
    float f1_ = 0.f, f2_ = 0.f, a1_ = 0.f, a2_ = 0.f, a3_ = 0.f, fk_ = 1.f;
    uint32_t rng_ = 1, rnd_ = 1, lfsr_ = 0x4001u; int crush_ = 0, bursts_ = 1, fmode_ = 0, fc_ = 0, holdn_ = 0;
    float rnd() { rng_ = rng_ * 1664525u + 1013904223u; return (rng_ >> 8) / 16777216.f; }
    float white() { rnd_ = rnd_ * 1664525u + 1013904223u; return ((rnd_ >> 8) / 16777216.f - 0.5f) * 1.f; }
    float onePole(float hz) const { const float g = std::tan(kPi * std::min(hz, sr_ * 0.45f) / sr_); return g / (1.f + g); }
    void setFilter() {
        fmode_ = d_.fmode; if (!fmode_) return;
        const float hz = clampf(cutHz_ * std::pow(2.f, pe_ * d_.fenv), 30.f, std::min(16000.f, sr_ * 0.45f));
        fk_ = 2.f - d_.res * 1.85f;
        const float g = std::tan(kPi * hz / sr_);
        a1_ = 1.f / (1.f + g * (g + fk_)); a2_ = a1_ * g; a3_ = a2_ * g;
    }
};
