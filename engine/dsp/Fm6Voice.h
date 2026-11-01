// SPDX-License-Identifier: GPL-3.0-only
// FM6 (engine 13): a six-operator FM voice, msfa (the core of Dexed, Apache-2.0, Google / Pascal Gauthier) as ported to C for Felucca by
// Leo Kuroshita (dsp/Fm6Core.inc, used as is), driven here one 32-sample block at a time. A patch is the 155-byte DX7 single-voice layout.
#pragma once
#include <cstring>
#include "Common.h"
#include "Fm6Tables.h"
#include "Fm6Patches.h"
namespace fm6 {
#include "Fm6Core.inc"

// 128-byte packed record (the 32-voice bank's) -> 155-byte voice
inline void unpack(const uint8_t* b, uint8_t* v) {
    std::memset(v, 0, FP_SIZE);
    for (int k = 0; k < 6; k++) {
        const uint8_t* o = b + k * 17; uint8_t* d = v + k * FP_OP;
        for (int i = 0; i < 11; i++) d[i] = o[i] & 0x7F;
        d[FP_LC] = o[11] & 3; d[FP_RC] = (o[11] >> 2) & 3; d[FP_RS] = o[12] & 7; d[FP_DET] = (o[12] >> 3) & 15;
        d[FP_AMS] = o[13] & 3; d[FP_KVS] = (o[13] >> 2) & 7; d[FP_OL] = o[14] & 0x7F; d[FP_MODE] = o[15] & 1; d[FP_FC] = (o[15] >> 1) & 31; d[FP_FF] = o[16] & 0x7F;
    }
    for (int i = 0; i < 9; i++) v[FP_PR1 + i] = b[102 + i] & 0x7F;
    v[FP_ALG] &= 31; v[FP_FB] = b[111] & 7; v[FP_OKS] = (b[111] >> 3) & 1;
    for (int i = 0; i < 4; i++) v[FP_LFS + i] = b[112 + i] & 0x7F;
    v[FP_LKS] = b[116] & 1; v[FP_LFW] = (b[116] >> 1) & 7; v[FP_LPMS] = (b[116] >> 4) & 7; v[FP_TRNSP] = b[117] & 0x7F;
    for (int i = 0; i < 10; i++) v[FP_NAME + i] = b[118 + i] & 0x7F;
}

class Voice {
public:
    void start(int patch, int note, float vel, float sr) {
        std::memset(this, 0, sizeof *this);
        unpack(kFm6PatchBytes[((patch % kFm6PatchCount) + kFm6PatchCount) % kFm6PatchCount], p_);
        sr_ = sr; note_ = note;
        fm6_note_init(&n_, p_, note + p_[FP_TRNSP] - 24, (int32_t)clampf(vel, 0.f, 1.f) * 0 + (int32_t)(clampf(vel, 0.f, 1.f) * 127.f), 1);
        fm6_lfo_reset(&lfo_, p_); fm6_lfo_key(&lfo_);
        n_.live = 1; pos_ = 32;
    }
    void release() { if (n_.down) fm6_note_key(&n_, p_, 0); }
    // bend in semitones; the 44.1 kHz tables are retuned to the host rate
    float next(float bend) {
        if (pos_ >= 32) block(bend);
        return buf_[pos_++];
    }
    bool done() { return !n_.live || (fm6_note_done(&n_, p_, p_[FP_ALG]) && (n_.live = 0, true)); }
private:
    uint8_t p_[FP_SIZE]; fm6_note_t n_; fm6_lfo_t lfo_; float buf_[32]; int pos_ = 32, note_ = 60; float sr_ = 44100.f;
    void block(float bend) {
        int32_t bus[32], dt[6] = {0, 0, 0, 0, 0, 0};
        const int32_t lf = fm6_note_logfreq((int32_t)std::lround((note_ + bend + p_[FP_TRNSP] - 24) * 16.f), 0) + (int32_t)std::lround(std::log2(44100.f / sr_) * 16777216.f);
        const int32_t lv = fm6_lfo_sample(&lfo_), ld = fm6_lfo_delay(&lfo_);
        pos_ = 0;
        if (!n_.live || !fm6_note_compute(&n_, p_, bus, lv, ld, lf, p_[FP_ALG], p_[FP_FB], dt, 0)) { for (int i = 0; i < 32; i++) buf_[i] = 0.f; return; }
        for (int i = 0; i < 32; i++) { const int32_t x = bus[i] - n_.dc; n_.dc += x >> 10; buf_[i] = (float)x / 16777216.f * 0.1f; }   // a one-pole high-pass: FM puts sidebands at 0 Hz
    }
};
}  // namespace fm6
