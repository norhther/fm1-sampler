// SPDX-License-Identifier: GPL-3.0-only
// X0X's TB-303 (Open303 by Robin Schmidt, MIT; Devilfish ranges and RAT drive from schwung-303; port by charlesvestal/fm1-x0x, GPL-3.0)
// as a synth engine (engine 9): dsp/bass303.cpp renders blocks, this serves it sample by sample.
// x: the twelve pots 0..127 in bass303.h's order: CUTOFF RESO ENVMOD DECAY ACCENT WAVE(0 saw 1 square) TUNE VOLUME DRIVE DRVTYPE(0 off 1 soft 2 RAT) SLIDE ACCDEC
// A hard hit (velocity > 0.8) is an accented note. The note rings until the track's release, as the 303's own envelope decays.
#pragma once
#include "bass303.h"

class Acid303 {
public:
    void start(const float* x, int note, float vel, float sr) {
        bass303_init(&b_);
        for (int i = 0; i < BASS303_NPARAMS; i++) bass303_set(&b_, i, (int)clampf(x[i], 0.f, 127.f));
        b_.tuning *= BASS303_SR / sr;   // the engine assumes 44.1 kHz: played at the host's 48 kHz it would sound higher
        bass303_note_on(&b_, note, vel > 0.8f ? 1 : 0, 0);
        pos_ = kBlock;
    }
    float next() {
        if (pos_ >= kBlock) { bass303_render(&b_, buf_, kBlock); pos_ = 0; }
        return buf_[pos_++];
    }
private:
    static constexpr int kBlock = 64;
    bass303_t b_{}; float buf_[kBlock] = {}; int pos_ = kBlock;
};
