// Master bus effects: the "FX 2" inserts (chorus, phaser, compressor, stereo width) and the "LIVE FX" performance effects
// (DJ filter, bit/rate crusher, trance gate, beat repeat). All parameters 0..1 (or a small index); everything is bypassed at its rest value.
// Order: chorus, phaser, width, DJ filter, crusher, gate, repeat, compressor. The tempo-synced ones read the beat clock of the session.
#pragma once
#include "Common.h"
#include <vector>

struct FxParams {
    float chorus = 0, phaser = 0, comp = 0, width = 0.5f;   // width: 0 mono, 0.5 unchanged, 1 double
    float filter = 0.5f;                                    // DJ filter: below 0.5 low-pass sweeping down, above 0.5 high-pass sweeping up
    float crush = 0;
    int repeat = 0;   // 0 off, 1..4: 1/4, 1/8, 1/16, 1/32 of a beat... (note values: 1/4 = one beat)
    int gate = 0;     // 0 off, 1..3: 1/8, 1/16, 1/32
};

class MasterFx {
public:
    explicit MasterFx(int sr, float ringSeconds = 2.f) : sr_(sr), rep_((size_t)(sr * ringSeconds) * 2, 0.f) {
        for (auto& b : cho_) b.assign(4096, 0.f);
    }
    void set(const FxParams& p) { p_ = p; }
    const FxParams& params() const { return p_; }
    // false at the rest values: the caller can skip process() and call feedOnly() instead (keeps the repeat / chorus memory warm)
    bool active() const {
        return p_.chorus > 0.004f || p_.phaser > 0.004f || p_.comp >= 0.01f || std::fabs(p_.width - 0.5f) >= 0.01f || std::fabs(p_.filter - 0.5f) >= 0.02f
            || p_.crush >= 0.005f || p_.repeat > 0 || p_.gate > 0;
    }
    void feedOnly(const float* x, int n) {
        repOn_ = false;
        const size_t N = rep_.size() / 2;
        for (int i = 0; i < n; i++) {
            cho_[0][(size_t)choW_] = x[2 * i]; cho_[1][(size_t)choW_] = x[2 * i + 1]; choW_ = (choW_ + 1) & 4095;
            rep_[2 * repW_] = x[2 * i]; rep_[2 * repW_ + 1] = x[2 * i + 1]; if (++repW_ >= N) repW_ = 0;
        }
    }

    // x: interleaved stereo, in place. beat0: the beat position at the first sample, beatsPerSample: the tempo.
    void process(float* x, int n, double beat0, double beatsPerSample) {
        chorus(x, n);
        phaser(x, n);
        width(x, n);
        djFilter(x, n);
        crush(x, n);
        gate(x, n, beat0, beatsPerSample);
        repeat(x, n, beatsPerSample > 0 ? 1.0 / beatsPerSample : sr_ * 0.5);
        comp(x, n);
    }

private:
    // ---- chorus: two modulated delays per side, quarter-cycle apart ----
    void chorus(float* x, int n) {
        const float mix = p_.chorus * 0.7f;
        const bool wet = p_.chorus > 0.004f;
        for (int i = 0; i < n; i++) {
            choPh_ += 0.7f / sr_; if (choPh_ >= 1.f) choPh_ -= 1.f;
            for (int c = 0; c < 2; c++) {
                const float in = x[2 * i + c];
                cho_[c][(size_t)choW_] = in;
                if (!wet) continue;
                const float d = (0.012f + 0.004f * std::sin(2.f * kPi * (choPh_ + 0.5f * c))) * sr_;
                float rp = (float)choW_ - d; if (rp < 0) rp += 4096.f;
                const int i0 = (int)rp & 4095, i1 = (i0 + 1) & 4095; const float fr = rp - std::floor(rp);
                const float y = cho_[c][(size_t)i0] + (cho_[c][(size_t)i1] - cho_[c][(size_t)i0]) * fr;
                x[2 * i + c] = in + y * mix;
            }
            choW_ = (choW_ + 1) & 4095;
        }
    }
    // ---- phaser: four allpass stages swept 300..2400 Hz with some feedback ----
    void phaser(float* x, int n) {
        if (p_.phaser < 0.004f) { for (auto& s : ph_) for (float& z : s) z = 0.f; phFb_[0] = phFb_[1] = 0.f; return; }
        const float m = p_.phaser * 0.5f;
        for (int i = 0; i < n; i++) {
            if ((i & 31) == 0) {
                phPh_ += 32 * 0.25f / sr_; if (phPh_ >= 1.f) phPh_ -= 1.f;
                const float f = 300.f * std::pow(8.f, 0.5f + 0.5f * std::sin(2.f * kPi * phPh_)), t = std::tan(kPi * f / sr_);
                phA_ = (t - 1.f) / (t + 1.f);
            }
            for (int c = 0; c < 2; c++) {
                const float dry = x[2 * i + c];
                float y = dry + phFb_[c] * 0.5f;
                for (int s = 0; s < 4; s++) { const float o = -phA_ * y + ph_[c][s]; ph_[c][s] = y + phA_ * o; y = o; }
                phFb_[c] = y;
                x[2 * i + c] = dry * (1.f - m) + y * m;
            }
        }
    }
    // ---- stereo width (mid/side) ----
    void width(float* x, int n) {
        if (std::fabs(p_.width - 0.5f) < 0.01f) return;
        const float s = p_.width * 2.f;
        for (int i = 0; i < n; i++) {
            const float m = 0.5f * (x[2 * i] + x[2 * i + 1]), d = 0.5f * (x[2 * i] - x[2 * i + 1]) * s;
            x[2 * i] = m + d; x[2 * i + 1] = m - d;
        }
    }
    // ---- DJ filter: one knob, low-pass to the left of centre, high-pass to the right ----
    struct Svf {
        float a1 = 0, a2 = 0, a3 = 0, k = 1.1f, ic1[2] = {0, 0}, ic2[2] = {0, 0};
        void set(float hz, float sr) { const float g = std::tan(kPi * clampf(hz, 20.f, sr * 0.45f) / sr); a1 = 1.f / (1.f + g * (g + k)); a2 = g * a1; a3 = g * a2; }
        void run(int ch, float x, float& lp, float& hp) {
            const float v3 = x - ic2[ch], v1 = a1 * ic1[ch] + a2 * v3, v2 = ic2[ch] + a2 * ic1[ch] + a3 * v3;
            ic1[ch] = 2.f * v1 - ic1[ch]; ic2[ch] = 2.f * v2 - ic2[ch];
            lp = v2; hp = x - k * v1 - v2;
        }
        void reset() { ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0.f; }
    };
    void djFilter(float* x, int n) {
        const float f = p_.filter;
        if (std::fabs(f - 0.5f) < 0.02f) { if (djOn_) { djOn_ = false; dj_.reset(); } return; }
        djOn_ = true;
        const bool low = f < 0.5f;
        const float u = low ? f * 2.f : (f - 0.5f) * 2.f;
        const float hz = low ? 100.f * std::pow(200.f, u) : 20.f * std::pow(300.f, u);   // 100 Hz .. 20 kHz, 20 Hz .. 6 kHz
        djHz_ += (hz - djHz_) * 0.5f;
        if (djHz_ <= 0.f) djHz_ = hz;
        dj_.set(djHz_, (float)sr_);
        for (int i = 0; i < n; i++) for (int c = 0; c < 2; c++) { float lp, hp; dj_.run(c, x[2 * i + c], lp, hp); x[2 * i + c] = low ? lp : hp; }
    }
    // ---- crusher: sample-rate hold and bit depth ----
    void crush(float* x, int n) {
        if (p_.crush < 0.005f) { crCnt_[0] = crCnt_[1] = 0; return; }
        const int hold = 1 + (int)(p_.crush * p_.crush * 28.f);
        const float levels = std::pow(2.f, 15.f - 11.f * p_.crush);
        for (int i = 0; i < n; i++) for (int c = 0; c < 2; c++) {
            if (crCnt_[c] <= 0) { crHeld_[c] = std::round(x[2 * i + c] * levels) / levels; crCnt_[c] = hold; }
            crCnt_[c]--;
            x[2 * i + c] = crHeld_[c];
        }
    }
    // ---- trance gate on the beat grid ----
    void gate(float* x, int n, double beat0, double bps) {
        if (p_.gate <= 0) { gateG_ = 1.f; return; }
        static const double rate[4] = {0, 2, 4, 8};   // gate cycles per beat
        const double r = rate[std::min(3, p_.gate)];
        for (int i = 0; i < n; i++) {
            const double ph = std::fmod((beat0 + i * bps) * r, 1.0);
            gateG_ += ((ph < 0.55 ? 1.f : 0.f) - gateG_) * 0.012f;
            x[2 * i] *= gateG_; x[2 * i + 1] *= gateG_;
        }
    }
    // ---- beat repeat: while engaged the last slice (1/4 .. 1/32 beat... of a beat) loops instead of the input ----
    void repeat(float* x, int n, double samplesPerBeat) {
        const size_t N = rep_.size() / 2;
        if (p_.repeat <= 0) {
            repOn_ = false;
            for (int i = 0; i < n; i++) { rep_[2 * repW_] = x[2 * i]; rep_[2 * repW_ + 1] = x[2 * i + 1]; if (++repW_ >= N) repW_ = 0; }
            return;
        }
        if (!repOn_) {
            static const double div[4] = {1.0, 0.5, 0.25, 0.125};
            repLen_ = (size_t)clampf((float)(samplesPerBeat * div[std::min(3, p_.repeat - 1)]), 128.f, (float)(N - 1));
            repStart_ = (repW_ + N - repLen_) % N; repK_ = 0; repOn_ = true;
        }
        const size_t fade = std::min<size_t>(48, repLen_ / 4);
        for (int i = 0; i < n; i++) {
            const size_t idx = (repStart_ + repK_) % N;
            float g = 1.f;
            if (repK_ < fade) g = (float)repK_ / fade; else if (repK_ + fade > repLen_) g = (float)(repLen_ - repK_) / fade;
            x[2 * i] = rep_[2 * idx] * g; x[2 * i + 1] = rep_[2 * idx + 1] * g;
            if (++repK_ >= repLen_) repK_ = 0;
        }
    }
    // ---- compressor: linked peak detector, 4:1, threshold 0 .. -24 dB, half the lost level made up ----
    void comp(float* x, int n) {
        if (p_.comp < 0.01f) { cmpEnv_ = 0.f; return; }
        const float thr = -24.f * p_.comp, ratio = 4.f, makeup = (1.f - 1.f / ratio) * (-thr) * 0.5f;
        const float att = std::exp(-1.f / (0.005f * sr_)), rel = std::exp(-1.f / (0.12f * sr_));
        for (int i = 0; i < n; i++) {
            const float lv = std::max(std::fabs(x[2 * i]), std::fabs(x[2 * i + 1]));
            cmpEnv_ = lv > cmpEnv_ ? att * (cmpEnv_ - lv) + lv : rel * (cmpEnv_ - lv) + lv;
            const float db = 20.f * std::log10(cmpEnv_ + 1e-9f), over = db - thr;
            const float g = std::pow(10.f, (makeup - (over > 0.f ? over * (1.f - 1.f / ratio) : 0.f)) / 20.f);
            x[2 * i] *= g; x[2 * i + 1] *= g;
        }
    }

    int sr_; FxParams p_;
    std::vector<float> cho_[2]; int choW_ = 0; float choPh_ = 0;
    float ph_[2][4] = {}, phFb_[2] = {0, 0}, phA_ = 0, phPh_ = 0;
    Svf dj_; bool djOn_ = false; float djHz_ = 0;
    float crHeld_[2] = {0, 0}; int crCnt_[2] = {0, 0};
    float gateG_ = 1.f;
    std::vector<float> rep_; size_t repW_ = 0, repStart_ = 0, repLen_ = 0, repK_ = 0; bool repOn_ = false;
    float cmpEnv_ = 0;
};
