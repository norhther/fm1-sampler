// SPDX-License-Identifier: GPL-3.0-only
// X0X's TR-808 (8W8's circuit models by athousanddetails; port by charlesvestal/fm1-x0x, GPL-3.0) as a one-hit synth voice (engine 10).
// The 16 pads are 8W8's lanes: BD SD LT MT HT LC MC HC RS CL MA CP CB CH OH CY (conga = the tom track's switch, clave the rim's, maracas the clap's).
#pragma once
#include "drum808.h"

class Drum808Voice {
public:
    void start(int pad, float vel, float sr) {
        static const uint8_t track[16] = {D8_BD, D8_SD, D8_LT, D8_MT, D8_HT, D8_LT, D8_MT, D8_HT, D8_RS, D8_RS, D8_CP, D8_CP, D8_CB, D8_CH, D8_OH, D8_CY};
        static const uint8_t sw[16] = {0, 0, 0, 0, 0, 1, 1, 1, 0, 1, 1, 0, 0, 0, 0, 0};
        drum808_init(&d_);
        const int t = track[pad & 15]; d_.sw[t] = sw[pad & 15];
        drum808_trigger(&d_, t, 0.35f + 0.65f * vel);
        k_ = sr / 44100.f; pos_ = kBlock;
    }
    // the engine is fixed at 44.1 kHz: on another host rate the voice is resampled by linear interpolation
    float next() {
        acc_ += 1.f / k_;
        while (acc_ >= 1.f) { acc_ -= 1.f; prev_ = cur_; cur_ = pull(); }
        return prev_ + (cur_ - prev_) * acc_;
    }
    bool alive() const { return drum808_active(&d_) || pos_ < kBlock; }
private:
    static constexpr int kBlock = 64;
    drum808_t d_{}; float dry_[kBlock] = {}, rev_[kBlock] = {}, dly_[kBlock] = {}; int pos_ = kBlock; float k_ = 1.f, acc_ = 1.f, prev_ = 0.f, cur_ = 0.f;
    float pull() {
        if (pos_ >= kBlock) { for (int i = 0; i < kBlock; i++) dry_[i] = rev_[i] = dly_[i] = 0.f; drum808_render(&d_, dry_, rev_, dly_, kBlock); pos_ = 0; }
        return dry_[pos_++];
    }
};
