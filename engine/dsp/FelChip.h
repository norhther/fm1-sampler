// SPDX-License-Identifier: GPL-3.0-only
// LOFI and NOISE of Felucca-Engines (MIT), as floats. x[0..7] are the engine's eight knobs, 0..127 (enum knobs are the index).
#pragma once
#include "FelCommon.h"
namespace fel {

// LOFI: chip voice. pulse / triangle / saw / noise / 32-sample 4-bit wave RAM, quantised, held (downsampled), swept, arpeggiated.
// CHIP = STEP (4) replaces the ADSR with a 4-bit stepped volume envelope (CRSH picks it). x: CHIP WAVE DUTY CRSH SWP VIB ARP TONE
class Lofi {
public:
    void start(const float* x, float sr, uint32_t) {
        for (int i = 0; i < 8; i++) x_[i] = x[i];
        sr_ = sr; held_ = 0.f; cnt_ = 0; lfsr_ = 0x7FFF; sweep_ = 0.f; lp_ = 0.f; arp_ = 0; ph_ = 0.f; vph_ = 0.f; frame_ = 0.f;
        vol_ = 15; div_ = stepPeriod((int)x_[3] >> 2);
        const int c = (int)x_[0], w = (int)x_[2] >> 3 & 15;
        wdc_ = 0.f;
        if ((int)x_[1] == 4) {   // the wave table's mean: no DC from lopsided shapes
            float sum = 0.f;
            for (int i = 0; i < 32; i++) sum += c == 3 ? (nib(w, i) >= 8 ? 1.f : -1.f) : (nib(w, i) * 2 - 15) / 15.f;
            wdc_ = sum / 32.f;
        }
    }
    float next(float hz, float tone, float) {
        const int chip = (int)x_[0], wave = (int)x_[1], step = chip == 4;
        const int bits = chip == 2 ? 8 : chip == 3 ? 1 : 4;
        const int crush = step ? 0 : (int)x_[3];
        const int hold = std::max(1, (int)std::lround((1 + crush / 8 + (chip == 1 ? 1 : 0)) * sr_ / 44100.f));
        if (blk_-- <= 0) {   // every 32 samples (a 44.1 kHz block): sweep, arpeggio, vibrato
            blk_ = 31;
            sweep_ += x_[4] / 8.f / 16.f * 44100.f / sr_; arp_++;
            static const int8_t ARPS[4][3] = {{0, 0, 0}, {0, 12, 0}, {0, 4, 7}, {0, 3, 7}};
            const float semi = -std::min(sweep_, 300.f) + ARPS[(int)x_[6] & 3][(int)((float)arp_ / (28.f * sr_ / 44100.f)) % 3];
            inc_ = hz * std::exp2(semi / 12.f) / sr_;
            lpk_ = fpole(clampf((3000.f + x_[7] * 256.f + std::max(0.f, tone) * 3000.f) / 32768.f, 0.f, 1.f));
        }
        float inc = inc_;
        if (x_[5] > 0.f) inc *= 1.f + std::sin(2.f * kPi * vph_) * (x_[5] / 127.f * 0.0154f);
        vph_ += 5.4f * 32.f / sr_ / 32.f; vph_ -= std::floor(vph_);
        if (--cnt_ <= 0) {
            cnt_ = hold; float s = 0.f; bool q = true;
            switch (wave) {
                case 1: if (step) { const int k = (int)(ph_ * 32.f) & 31; s = ((k < 16 ? 15 - k : k - 16) - 7.5f) / 7.5f; q = false; } else s = tri(ph_); break;
                case 2: s = 2.f * ph_ - 1.f; break;
                case 3: { if (ph_ + inc * hold >= 1.f || chip == 3) lfsr_ = (lfsr_ >> 1) | (((lfsr_ ^ (lfsr_ >> 1)) & 1u) << 14); s = (lfsr_ & 1u) ? 1.f : -1.f; break; }
                case 4: { const int k = (int)(ph_ * 32.f) & 31, b = nib((int)x_[2] >> 3 & 15, k); s = (bits == 1 ? (b >= 8 ? 1.f : -1.f) : (b * 2 - 15) / 15.f) - wdc_; q = false; break; }
                default: { static const float SD[4] = {1, 2, 4, 6}; const float duty = step ? SD[((int)x_[2] / 32) & 3] / 8.f : (1.f + (float)((int)x_[2] / 32)) / 8.f; s = (ph_ < duty ? 1.f : -1.f) - (2.f * duty - 1.f); break; }
            }
            if (q) { const float lv = (float)(1 << (bits - 1)) - 0.5f; s = std::floor(s * lv + 0.5f) / lv; }
            if (crush > 64) s = std::floor(s * 8.f + 0.5f) / 8.f;
            held_ = s;
        }
        ph_ += inc; ph_ -= std::floor(ph_);
        lp_ += (held_ - lp_) * lpk_;
        return lp_ * 0.5f * (step ? stepEnv() : 1.f);
    }
    // the stepped envelope's level, 0..1, advanced every sample at the 240 Hz frame clock
    bool stepDone() const { const int b = (int)x_[3] >> 2; return (int)x_[0] == 4 && b >= 1 && b <= 16 && vol_ == 0; }
private:
    float x_[8], sr_ = 44100.f, held_ = 0.f, sweep_ = 0.f, lp_ = 0.f, ph_ = 0.f, vph_ = 0.f, frame_ = 0.f, inc_ = 0.f, lpk_ = 1.f, wdc_ = 0.f;
    int cnt_ = 0, arp_ = 0, blk_ = 0, vol_ = 15, div_ = 0; uint32_t lfsr_ = 0x7FFF;
    static int stepPeriod(int b) { return b <= 16 ? b - 1 : b - 16; }
    float fpole(float a) const { return 1.f - std::pow(1.f - std::min(a, 0.9999f), 44100.f / sr_); }   // a one-pole corner set at 44.1 kHz, at sr
    float stepEnv() {
        const int b = (int)x_[3] >> 2;
        if (!b) return 1.f;
        frame_ += 240.f / sr_;
        while (frame_ >= 1.f) { frame_ -= 1.f; if (div_) div_--; else { div_ = stepPeriod(b); if (vol_) vol_--; else if (b > 16) vol_ = 15; } }
        return vol_ / 15.f;
    }
    static int nib(int table, int k) {   // sample k (0..31) of wave RAM table: 4 bits, two a byte, high nibble first
        static const uint8_t W[16][16] = {
            {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, {0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
            {0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, {0x89,0xAC,0xDE,0xEF,0xFF,0xEE,0xDC,0xA9,0x86,0x53,0x21,0x10,0x00,0x11,0x23,0x56},
            {0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF,0xFE,0xDC,0xBA,0x98,0x76,0x54,0x32,0x10}, {0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xAA,0xBB,0xCC,0xDD,0xEE,0xFF},
            {0x01,0x12,0x33,0x45,0x56,0x77,0x89,0x9A,0x56,0x67,0x88,0x9A,0xAB,0xCC,0xDE,0xEF}, {0xEE,0xEF,0xFF,0xFF,0xFF,0xEE,0xED,0xDC,0x11,0x10,0x00,0x00,0x00,0x11,0x12,0x23},
            {0xCE,0xFF,0xED,0xBA,0x9A,0xBD,0xEF,0xFE,0xC9,0x64,0x21,0x00,0x00,0x01,0x24,0x69}, {0x8B,0xEF,0xED,0xBA,0x9A,0xAA,0x98,0x77,0x88,0x87,0x65,0x55,0x65,0x42,0x10,0x14},
            {0x8D,0xFE,0xDD,0xDD,0xCD,0xDD,0xDE,0xFD,0x82,0x01,0x22,0x22,0x32,0x22,0x21,0x02}, {0x8F,0xFC,0xA7,0x67,0x89,0x88,0x88,0x87,0x88,0x77,0x77,0x76,0x78,0x98,0x53,0x00},
            {0x8F,0xEE,0xFD,0xDB,0x98,0x66,0x66,0x77,0x88,0x89,0x99,0x97,0x64,0x22,0x01,0x10}, {0x7F,0x96,0xCF,0xBB,0xCC,0xBB,0xAA,0x98,0x77,0x65,0x54,0x43,0x34,0x40,0x39,0x60},
            {0xF3,0x00,0x12,0x22,0x11,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22}, {0xFF,0xFF,0xFF,0xFF,0xAA,0xAA,0xAA,0xAA,0x55,0x55,0x55,0x55,0x00,0x00,0x00,0x00}};
        const int b = W[table & 15][k >> 1 & 15];
        return (k & 1) ? b & 15 : b >> 4;
    }
};

// NOISE: analog-style noise (ANLG, DUST impulses) or shift-register noise (LFSR, META tone), tilted white -> pink -> brown (COLR),
// through a resonant low-pass .. band-pass. x: MODE COLR FREQ RES TRK DENS/LEN DRFT CRSH/CLK
class Noise {
public:
    void start(const float* x, float sr, uint32_t seed) {
        for (int i = 0; i < 8; i++) x_[i] = x[i];
        sr_ = sr; const uint32_t sd = (seed + 1u) * 0x9E3779B9u; rng_ = sd ? sd : 1u; flt_ = Svf(); b0_ = b1_ = b2_ = br_ = 0.f;
        ph_ = 0.f; reg_ = 0u; dc_ = 0.f; held_ = 0.f; hcnt_ = 0; drift_ = 0.f; tg_ = 0.f; cnt_ = 0; blk_ = 0;
        const float r = 44100.f / sr_;   // the Kellet poles and the brown pole, designed at 44.1 kHz, at sr
        pole(P0_, G0_, 32690.f / 32768.f, 3245.f / 32768.f, r); pole(P1_, G1_, 31555.f / 32768.f, 9716.f / 32768.f, r);
        pole(P2_, G2_, 18678.f / 32768.f, 17247.f / 16384.f, r); pole(PB_, GB_, 32580.f / 32768.f, 3480.f / 32768.f, r);
    }
    float next(float hz, float tone, float motion) {
        const int mode = (int)x_[0] & 3, sel = ((int)x_[5] >> 4) & 7, crsh = mode < 2 ? (int)x_[7] : 0;
        if (blk_-- <= 0) block(hz, tone, motion);
        float w = 0.f;
        if (mode <= 1) {
            const uint32_t r = xorshift(rng_);
            if (mode == 0) w = ((float)(r >> 17) - 16384.f) / 32768.f;
            if (thr_ > 0.f && (float)(r & 0xFFFFu) < thr_ * 65536.f) { const float a = 0.25f + (float)((r >> 18) & 0x7FFFu) / 32768.f; w += (r & 0x20000u) ? a : -a; }
        } else if (mode == 2) {
            ph_ += inc_; if (ph_ >= 1.f) { ph_ -= std::floor(ph_); reg_ = (reg_ >> 1) ^ (-(reg_ & 1u) & kTaps[sel]); }
            w = (reg_ & 1u) ? 12000.f / 32768.f : -12000.f / 32768.f;
        } else {
            const SeqT& q = kSeq[sel]; const int j = std::min((int)q.len - 1, (int)(ph_ * q.len));
            ph_ += inc_; ph_ -= std::floor(ph_);
            w = ((q.w[j >> 5] >> (j & 31)) & 1u) ? q.hi / 32768.f : q.lo / 32768.f;
        }
        float xs = w;
        if (colr_ > 0.f) {   // pink: three leaky poles (Kellet), brown: a leaky integrator
            b0_ = b0_ * P0_ + w * G0_; b1_ = b1_ * P1_ + w * G1_; b2_ = b2_ * P2_ + w * G2_;
            const float pk = (b0_ + b1_ + b2_ + w * 6055.f / 32768.f) * 0.25f;
            if (colr_ <= 64.f) xs = w + (pk - w) * cf_;
            else { br_ = br_ * PB_ + w * GB_; xs = pk + (br_ - pk) * cf_; }
        }
        float lp, bp; flt_.run(xs, lp, bp);
        bp *= bpg_;
        float y = lp + (bp - lp) * mix_;
        y = knee(y * gq_, 24000.f / 32768.f);
        if (mode == 3) { dc_ += (y - dc_) / 512.f; y -= dc_; }
        if (crsh) {
            if (--hcnt_ <= 0) { hcnt_ = std::max(1, (int)std::lround((1 + crsh / 8) * sr_ / 44100.f)); const float st = std::exp2((float)(crsh / 11)) / 32768.f; held_ = std::floor(y / st + 0.5f) * st; }
            y = held_;
        }
        return y * 0.9f;
    }
private:
    struct SeqT { uint32_t w[4]; uint8_t len; int16_t hi, lo; };
    static constexpr uint32_t kTaps[8] = {0x420000u, 0x90000u, 1u << 16 | 1u << 13, 0x6000u, 0x100Du, 0x500u, 0x110u, 0x60u};
    static constexpr SeqT kSeq[8] = {
        {{0xD13C50C1u, 0x91C2F95Cu, 0xA58DED6Cu, 0x01FD533Bu}, 127, 16128, -16384}, {{0xDBD165F1u, 0x9E46A1FDu, 0x84C15692u, 0x019B1CEBu}, 127, 16128, -16384},
        {{0x41008001u, 0x00492402u, 0x00824824u, 0x00000000u}, 93, 16384, -3404}, {{0x27179461u, 0x03F566EDu, 0x00000000u, 0x00000000u}, 63, 15872, -16384},
        {{0x75A6C487u, 0x02FCA8CFu, 0x00000000u, 0x00000000u}, 63, 15872, -16384}, {{0x05763E69u, 0, 0, 0}, 31, 15360, -16384},
        {{0x00000F59u, 0, 0, 0}, 15, 14336, -16384}, {{0x0000001Du, 0, 0, 0}, 7, 12288, -16384}};
    float x_[8], sr_ = 44100.f, ph_ = 0.f, inc_ = 0.f, dc_ = 0.f, held_ = 0.f, drift_ = 0.f, tg_ = 0.f, thr_ = 0.f;
    float colr_ = 0.f, cf_ = 0.f, bpg_ = 1.f, mix_ = 0.f, gq_ = 1.f, b0_ = 0, b1_ = 0, b2_ = 0, br_ = 0, P0_, P1_, P2_, PB_, G0_, G1_, G2_, GB_;
    int cnt_ = 0, hcnt_ = 0, blk_ = 0; uint32_t rng_ = 1, reg_ = 0; Svf flt_;
    static void pole(float& P, float& G, float p, float g, float r) { P = std::pow(p, r); G = g * (1.f - P) / (1.f - p); }
    void block(float hz, float tone, float motion) {   // every 32 samples
        blk_ = 31;
        const int mode = (int)x_[0] & 3, sel = ((int)x_[5] >> 4) & 7; const float drft = x_[6];
        float d16 = 0.f, gain = 1.f;
        if (drft > 0.f) {   // a new random target every 0.7 .. 3 s, followed with a 0.7 s lag
            if (--cnt_ <= 0) { tg_ = unit(rng_) * 2.f - 1.f; cnt_ = 1024 + (int)(unit(rng_) * 3071.f); cnt_ = (int)(cnt_ * sr_ / 44100.f); }
            drift_ += (tg_ - drift_) / 1024.f * (44100.f / sr_);
            d16 = drift_ * 256.f * (drft / 127.f) * (drft / 127.f); gain = 1.f - 0.5f * (1.f - drift_) * drft / 128.f;
        }
        colr_ = clampf(x_[1] + tone * 20.f, 0.f, 127.f);
        cf_ = colr_ <= 64.f ? colr_ / 64.f : (colr_ - 64.f) / 63.f;
        const float pit = pitchOf(hz);
        const float idx = clampf(x_[2] + tone * 25.f + (pit - 60.f) * x_[4] * 0.00923f + d16 * 0.0729f, 0.f, 127.f);
        const float k = 2.f - x_[3] * 7800.f / 127.f / 4096.f;
        flt_.set(cutHz(idx, sr_), sr_, k);
        bpg_ = std::sqrt(k) * 1.5f; mix_ = x_[3] * 258.f / 32768.f;
        const float wf = 1.f - colr_ * 230.f / 32767.f;
        float oct = ((127.f - idx) * 7.f * wf + 214.f + (mode == 1 ? 512.f : 0.f)) / 256.f;
        gq_ = std::exp2(std::min(oct, 5.f)) * gain;
        thr_ = 0.f;
        if (mode <= 1 && x_[5] > 0.f) thr_ = std::exp2(x_[5] * 11.f / 128.f) / sr_;
        if (mode == 2) {
            inc_ = std::min(1.f, hz * std::exp2((x_[7] + d16 / 16.f) / 12.f) / sr_);
            uint32_t mask = kTaps[sel]; mask |= mask >> 1; mask |= mask >> 2; mask |= mask >> 4; mask |= mask >> 8; mask |= mask >> 16;
            if (!(reg_ & mask)) reg_ = rng_ | 1u;
            reg_ &= mask;
        } else if (mode == 3) {
            inc_ = (x_[7] > 0.f || d16 != 0.f) ? hz * std::exp2((x_[7] + d16 / 16.f) / 12.f) / sr_ : hz / sr_;
            inc_ = std::min(inc_, 1.f);
        }
        (void)motion;
    }
};
}  // namespace fel
