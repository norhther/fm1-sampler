// SPDX-License-Identifier: GPL-3.0-only
// X0X's TR-909 (9W9 by athousanddetails, after ER-99 by Matthew Cieplak; port by charlesvestal/fm1-x0x, GPL-3.0) as a one-hit synth voice (engine 11).
// 16 pads: BD SD LT MT HT RS CP CH OH CR RD, then a long BD, a high SD, a high HT, CP and CH again for a fuller row of keys.
// The hat, ride and crash are ER-99's recordings (assets/909 in fm1-x0x, GPL-3.0), mu-law in x0x_drum_samples.h.
#pragma once
#include <cmath>
#include <cstring>
#include "drum909.h"

class Tr909Voice {
public:
    void start(int pad, float vel, float sr, float semis = 0.f) {   // semis: the pad's pitch, the hit played faster or slower like a sample
        static const uint8_t voice[16] = {DR_BD, DR_SD, DR_LT, DR_MT, DR_HT, DR_RS, DR_CP, DR_CH, DR_OH, DR_CR, DR_RD, DR_BD, DR_SD, DR_HT, DR_CP, DR_CH};
        drum909_init(&d_);
        const int p = pad & 15, v = voice[p];
        if (p == 11) setNamed(DR_BD, "Decay", 110);    // the long kick
        if (p == 12) setNamed(DR_SD, "Tune", 110);     // a high snare
        if (p == 13) setNamed(DR_HT, "Tune", 120);
        drum909_trigger(&d_, v, 0.35f + 0.65f * vel);
        k_ = sr / 44100.f / std::exp2(semis / 12.f); acc_ = 1.f; pos_ = kBlock;
    }
    float next() {   // the engine runs at 44.1 kHz: linear interpolation to the host rate
        acc_ += 1.f / k_;
        while (acc_ >= 1.f) { acc_ -= 1.f; prev_ = cur_; cur_ = pull(); }
        return prev_ + (cur_ - prev_) * acc_;
    }
    bool alive() const { return drum909_active(&d_) || pos_ < kBlock; }
private:
    static constexpr int kBlock = 64;
    drum909_t d_{}; float dry_[kBlock] = {}, rev_[kBlock] = {}, dly_[kBlock] = {}; int pos_ = kBlock; float k_ = 1.f, acc_ = 1.f, prev_ = 0.f, cur_ = 0.f;
    void setNamed(int voice, const char* name, int value) {
        for (int i = 0; i < drum909_nparams(voice); i++) { const x0x_param_t* q = drum909_param(voice, i); if (q && !strcasecmp(q->name, name)) { drum909_set(&d_, voice, i, value); return; } }
    }
    float pull() {
        if (pos_ >= kBlock) { for (int i = 0; i < kBlock; i++) dry_[i] = rev_[i] = dly_[i] = 0.f; drum909_render(&d_, dry_, rev_, dly_, kBlock); pos_ = 0; }
        return dry_[pos_++];
    }
};
