// Effect chain used on the master bus and, one instance each, on every track.
// INSERT / TONE / MOD pages: chorus, flanger, phaser, compressor, drive, 3-band EQ, tremolo, auto-wah, ring mod, stereo width.
// LIVE / LIVE 2 pages (for turning while playing): DJ filter, crusher, trance gate, beat repeat, reverse, freeze, tape stop.
// All parameters are 0..1 (or a small index) and every effect is bypassed at its rest value.
// Order: drive, EQ, chorus, flanger, phaser, tremolo, wah, ring mod, width, DJ filter, crusher, gate, repeat / reverse / freeze, tape stop, compressor.
// The tempo-synced ones (tremolo, gate, repeat, reverse) read the beat clock of the session.
#pragma once
#include <array>
#include <cmath>
#include "Common.h"
#include <vector>

struct FxParams {
    float chorus = 0, flanger = 0, phaser = 0, comp = 0, width = 0.5f;   // width: 0 mono, 0.5 unchanged, 1 double
    float drive = 0, eqLow = 0.5f, eqMid = 0.5f, eqHigh = 0.5f;          // EQ: 0.5 flat, +-12 dB
    float tremolo = 0, wah = 0, ringmod = 0;
    float filter = 0.5f;                                                // DJ filter: below 0.5 low-pass sweeping down, above 0.5 high-pass sweeping up
    float crush = 0, tapeStop = 0, tsDown = 1.8f, tsUp = 1.8f;   // tape stop: depth, seconds to run down, seconds to spin up
    float spring = 0, springDamp = 0.5f, phone = 0, punch = 0;   // OP-1 style: spring reverb (mix, damping), phone (telephone band + grit), punch (transients + saturation)
    float fold = 0, autopan = 0, vinyl = 0, vowel = 0;   // FX 4: wave folder, auto pan (one turn every two beats), vinyl (crackle, hiss, dull top), vowel (a formant filter that walks A E I O U)
    float comb = 0, combTune = 0.5f, octave = 0.5f, fshift = 0.5f;   // FX 5: comb filter (amount, the note it rings at), octaver (centre off, left an octave down, right an octave up), frequency shifter (centre off)
    int repeat = 0;    // 0 off, 1..4: 1/4, 1/8, 1/16, 1/32 of a beat... (note values: 1/4 = one beat)
    int reverse = 0;   // the same slices, played backwards
    int gate = 0;      // 0 off, 1..3: 1/8, 1/16, 1/32
    int freeze = 0;    // 1: the last 150 ms are held as a pad
};

class MasterFx {
public:
    explicit MasterFx(int sr, float ringSeconds = 2.f) : sr_(sr), rep_((size_t)(sr * ringSeconds) * 2, 0.f), ts_((size_t)(sr * ringSeconds) * 2, 0.f) {
        for (auto& b : cho_) b.assign(4096, 0.f);
        for (auto& b : fla_) b.assign(2048, 0.f);
    }
    void set(const FxParams& p) {
        const bool eqChanged = p.eqLow != p_.eqLow || p.eqMid != p_.eqMid || p.eqHigh != p_.eqHigh;
        p_ = p;
        if (eqChanged) setupEq();
    }
    const FxParams& params() const { return p_; }
    // false at the rest values: the caller can skip process() and call feedOnly() instead (keeps the repeat / chorus memory warm)
    bool active() const {
        return p_.chorus > 0.004f || p_.flanger > 0.004f || p_.phaser > 0.004f || p_.comp >= 0.01f || std::fabs(p_.width - 0.5f) >= 0.01f
            || p_.drive > 0.004f || eqOn() || p_.tremolo > 0.004f || p_.wah > 0.004f || p_.ringmod > 0.004f
            || std::fabs(p_.filter - 0.5f) >= 0.02f || p_.crush >= 0.005f || p_.tapeStop > 0.02f || tsOn_ || p_.spring > 0.004f || p_.phone > 0.004f || p_.punch > 0.004f
            || p_.fold > 0.004f || p_.autopan > 0.004f || p_.vinyl > 0.004f || p_.vowel > 0.004f
            || p_.comb > 0.004f || std::fabs(p_.octave - 0.5f) >= 0.02f || std::fabs(p_.fshift - 0.5f) >= 0.02f
            || p_.repeat > 0 || p_.reverse > 0 || p_.gate > 0 || p_.freeze > 0;
    }
    void feedOnly(const float* x, int n) {
        repOn_ = false; tsOn_ = false; cbOn_ = ocOn_ = fsOn_ = false;   // the comb, octaver and shifter start clean when they come back
        const size_t N = rep_.size() / 2;
        for (int i = 0; i < n; i++) {
            cho_[0][(size_t)choW_] = x[2 * i]; cho_[1][(size_t)choW_] = x[2 * i + 1]; choW_ = (choW_ + 1) & 4095;
            fla_[0][(size_t)flaW_] = x[2 * i]; fla_[1][(size_t)flaW_] = x[2 * i + 1]; flaW_ = (flaW_ + 1) & 2047;
            rep_[2 * repW_] = x[2 * i]; rep_[2 * repW_ + 1] = x[2 * i + 1]; if (++repW_ >= N) repW_ = 0;
            ts_[2 * tsW_] = x[2 * i]; ts_[2 * tsW_ + 1] = x[2 * i + 1]; if (++tsW_ >= N) tsW_ = 0;
        }
    }

    // x: interleaved stereo, in place. beat0: the beat position at the first sample, beatsPerSample: the tempo.
    void process(float* x, int n, double beat0, double beatsPerSample) {
        drive(x, n);
        fold(x, n);
        punch(x, n);
        eq(x, n);
        chorus(x, n);
        flanger(x, n);
        phaser(x, n);
        tremolo(x, n, beat0, beatsPerSample);
        wah(x, n);
        ringmod(x, n);
        width(x, n);
        djFilter(x, n);
        crush(x, n);
        phone(x, n);
        vowel(x, n);
        fshift(x, n);
        octaver(x, n);
        comb(x, n);
        vinyl(x, n);
        autopan(x, n, beat0, beatsPerSample);
        gate(x, n, beat0, beatsPerSample);
        slicer(x, n, beatsPerSample > 0 ? 1.0 / beatsPerSample : sr_ * 0.5);
        spring(x, n);
        tapeStop(x, n);
        comp(x, n);
    }

private:
    // ---- drive: soft clipping, the level is brought back so it does not just get louder ----
    void drive(float* x, int n) {
        if (p_.drive < 0.004f) return;
        const float g = 1.f + p_.drive * 14.f, makeup = 1.f / (1.f + (g - 1.f) * 0.22f), mix = std::min(1.f, p_.drive * 3.f);
        for (int i = 0; i < n * 2; i++) { const float d = x[i], w = std::tanh(d * g) * makeup * 1.6f; x[i] = d + (w - d) * mix; }
    }
    // ---- fold: a wave folder: the peaks that pass the ceiling turn back instead of clipping, so the sound gets new, bright overtones ----
    void fold(float* x, int n) {
        if (p_.fold < 0.004f) return;
        const float g = 1.f + p_.fold * 6.f, mix = std::min(1.f, p_.fold * 4.f);
        for (int i = 0; i < n * 2; i++) { const float d = x[i], w = std::sin(d * g * 1.2f) * 0.8f; x[i] = d + (w - d) * mix; }
    }
    // ---- vowel: three formant band-passes that walk A, E, I, O, U as the knob goes up (the first bit of the knob is only the mix) ----
    void vowel(float* x, int n) {
        if (p_.vowel < 0.004f) { if (vwOn_) { vwOn_ = false; for (auto& f : vw_) f.reset(); } return; }
        static const float F[5][3] = {{800, 1150, 2900}, {400, 1600, 2700}, {350, 1700, 2700}, {450, 800, 2830}, {325, 700, 2530}};
        vwOn_ = true;
        const float pos = clampf(p_.vowel, 0.f, 1.f) * 3.999f, fr = pos - std::floor(pos); const int a = (int)pos;
        for (int b = 0; b < 3; b++) { vw_[b].k = 0.22f; vw_[b].set(F[a][b] * std::pow(F[a + 1][b] / F[a][b], fr), (float)sr_); }
        const float mix = std::min(1.f, p_.vowel * 5.f), wg[3] = {1.f, 0.7f, 0.4f};
        for (int i = 0; i < n; i++) for (int c = 0; c < 2; c++) {
            const float d = x[2 * i + c]; float w = 0.f;
            for (int b = 0; b < 3; b++) { float lp, hp; vw_[b].run(c, d, lp, hp); w += (d - hp - lp) * wg[b]; }   // d - hp - lp: the band-pass, unity at its centre
            x[2 * i + c] = d + (w * 1.1f - d) * mix;
        }
    }
    // ---- comb: a short feedback delay tuned to a note: the sound rings at that pitch (metal, flute, a plucked string); the loop is damped, the highs die first ----
    void comb(float* x, int n) {
        if (p_.comb < 0.004f) { cbOn_ = false; return; }
        if (!cbOn_) { cbOn_ = true; for (auto& b : cb_) b.fill(0.f); cbW_ = 0; cbZ_[0] = cbZ_[1] = 0.f; }
        const float amt = clampf(p_.comb, 0.f, 1.f), hz = 60.f * std::pow(2.f, clampf(p_.combTune, 0.f, 1.f) * 5.f), D = clampf((float)sr_ / hz - 0.82f, 3.f, (float)kCombLen - 2.f);   // 0.82: the delay of the damping one-pole is part of the loop
        const float g = 0.4f + 0.55f * amt, mix = std::min(1.f, amt * 3.f), norm = std::sqrt(1.f - g * g) * 1.1f;   // sqrt(1-g^2): the average level stays where it was
        for (int i = 0; i < n; i++) {
            float rp = (float)cbW_ - D; if (rp < 0.f) rp += (float)kCombLen;
            const int i0 = (int)rp % kCombLen, i1 = (i0 + 1) % kCombLen; const float fr = rp - std::floor(rp);
            for (int c = 0; c < 2; c++) {
                const float d = cb_[c][(size_t)i0] * (1.f - fr) + cb_[c][(size_t)i1] * fr;
                cbZ_[c] += 0.55f * (d - cbZ_[c]);
                const float in = x[2 * i + c]; float y = in + g * cbZ_[c];
                if (!std::isfinite(y)) { y = 0.f; cbZ_[c] = 0.f; }   // a bad sample must not live in the loop for ever
                cb_[c][(size_t)cbW_] = y;
                x[2 * i + c] = in + (std::tanh(y * norm) - in) * mix;
            }
            cbW_ = (cbW_ + 1) % kCombLen;
        }
    }
    // ---- octaver: a pitch shifter (two taps of a delay line that slide against the write head and cross-fade): left of the centre the sound is mixed with its octave below, right of it with its octave above ----
    void octaver(float* x, int n) {
        const float u = p_.octave - 0.5f;
        if (std::fabs(u) < 0.02f) { ocOn_ = false; return; }
        if (!ocOn_) { ocOn_ = true; for (auto& b : oc_) b.fill(0.f); ocW_ = 0; ocPh_ = 0.f; }
        const float W = 0.045f * (float)sr_, ratio = u < 0.f ? 0.5f : 2.f, step = (1.f - ratio) / W, m = std::min(1.f, std::fabs(u) * 3.f);
        for (int i = 0; i < n; i++) {
            float wet[2] = {0.f, 0.f};
            for (int tap = 0; tap < 2; tap++) {
                float ph = ocPh_ + 0.5f * (float)tap; ph -= std::floor(ph);
                float rp = (float)ocW_ - ph * W; while (rp < 0.f) rp += (float)kOctLen;
                const int i0 = (int)rp % kOctLen, i1 = (i0 + 1) % kOctLen; const float fr = rp - std::floor(rp), w = std::sin(kPi * ph) * std::sin(kPi * ph);   // the two windows add up to 1
                for (int c = 0; c < 2; c++) wet[c] += (oc_[c][(size_t)i0] * (1.f - fr) + oc_[c][(size_t)i1] * fr) * w;
            }
            for (int c = 0; c < 2; c++) { oc_[c][(size_t)ocW_] = x[2 * i + c]; x[2 * i + c] = x[2 * i + c] * (1.f - 0.5f * m) + wet[c] * 0.9f * m; }
            ocW_ = (ocW_ + 1) % kOctLen; ocPh_ += step; ocPh_ -= std::floor(ocPh_);
        }
    }
    // ---- frequency shifter: every partial moves by the same number of Hz (not the same ratio), so a note turns into a bell, then into noise. A Hilbert pair of all-pass chains makes the 90 degree twin of the sound ----
    void fshift(float* x, int n) {
        const float u = (p_.fshift - 0.5f) * 2.f;
        if (std::fabs(u) < 0.04f) { fsOn_ = false; return; }
        if (!fsOn_) { fsOn_ = true; for (auto& c : ap_) for (auto& a : c) a = Ap2(); fsD_[0] = fsD_[1] = 0.f; fsRamp_ = 0; }
        static const float cI[4] = {0.6923878f, 0.9360654323f, 0.9882295227f, 0.9987488453f}, cQ[4] = {0.4021921162f, 0.8561710882f, 0.9722909546f, 0.9952884791f};
        const float hz = std::copysign(u * u * 800.f, u), dth = 2.f * kPi * hz / (float)sr_, mix0 = std::min(1.f, std::fabs(u) * 4.f), rampN = 0.03f * (float)sr_;
        for (int i = 0; i < n; i++) {
            const float mix = mix0 * std::min(1.f, (float)(fsRamp_++) / rampN);   // the all-pass chains need a few ms to settle: fade the effect in
            fsPh_ += dth; fsPh_ -= 2.f * kPi * std::floor(fsPh_ / (2.f * kPi));
            const float co = std::cos(fsPh_), si = std::sin(fsPh_);
            for (int c = 0; c < 2; c++) {
                const float v = x[2 * i + c]; float a = v, b = v;
                for (int k = 0; k < 4; k++) { a = ap_[c][k].run(a, cI[k] * cI[k]); b = ap_[c][4 + k].run(b, cQ[k] * cQ[k]); }
                const float aD = fsD_[c]; fsD_[c] = a;   // the first path is one sample late: that makes the pair 90 degrees apart over the whole range
                const float w = aD * co + b * si;
                x[2 * i + c] = v + (w - v) * mix;
            }
        }
    }
    // ---- vinyl: the top is dulled, a little hiss, and pops and crackle at random (more of them as the knob goes up) ----
    void vinyl(float* x, int n) {
        if (p_.vinyl < 0.004f) return;
        const float a = 1.f - std::exp(-2.f * kPi * (14000.f - 9000.f * p_.vinyl) / sr_), dens = (3.f + 90.f * p_.vinyl) / sr_, hiss = 0.0025f * p_.vinyl;
        for (int i = 0; i < n; i++) {
            vyRng_ = vyRng_ * 1664525u + 1013904223u; const float r = (vyRng_ >> 8) / 16777216.f;
            if (r < dens) { vyPop_ = ((vyRng_ & 1) ? 1.f : -1.f) * (0.05f + 0.25f * ((vyRng_ >> 4) & 255) / 255.f) * (0.5f + p_.vinyl); }
            vyPop_ *= 0.55f;   // a pop dies in a few samples
            const float nz = ((vyRng_ >> 12) / 524288.f - 1.f) * hiss;
            for (int c = 0; c < 2; c++) { vyLp_[c] += a * (x[2 * i + c] - vyLp_[c]); x[2 * i + c] = vyLp_[c] + vyPop_ + nz; }
        }
    }
    // ---- auto pan: the sound swings left and right, one turn every two beats ----
    void autopan(float* x, int n, double beat0, double bps) {
        if (p_.autopan < 0.004f) return;
        for (int i = 0; i < n; i++) {
            const float pan = 0.5f + 0.5f * p_.autopan * std::sin(2.f * kPi * (float)std::fmod((beat0 + i * bps) * 0.5, 1.0));
            x[2 * i] *= std::cos(pan * kPi * 0.5f) * 1.4142f; x[2 * i + 1] *= std::sin(pan * kPi * 0.5f) * 1.4142f;
        }
    }
    // ---- punch: the attack of every hit is pushed up (fast level against slow level), then a little saturation glues it ----
    void punch(float* x, int n) {
        if (p_.punch < 0.004f) return;
        const float fa = std::exp(-1.f / (0.001f * sr_)), sa = std::exp(-1.f / (0.04f * sr_)), amt = p_.punch * 3.f, sat = 1.f + p_.punch * 2.5f, mk = 1.f / std::tanh(sat);
        for (int i = 0; i < n; i++) {
            const float lv = std::max(std::fabs(x[2 * i]), std::fabs(x[2 * i + 1]));
            puF_ = lv > puF_ ? lv : fa * puF_ + (1.f - fa) * lv; puS_ = sa * puS_ + (1.f - sa) * lv;
            const float boost = 1.f + amt * clampf((puF_ - puS_) / (puS_ + 0.02f), 0.f, 2.f) * 0.5f;
            for (int c = 0; c < 2; c++) { const float v = x[2 * i + c] * boost; x[2 * i + c] = std::tanh(v * sat) * mk * 0.9f + v * 0.1f; }
        }
    }
    // ---- phone: 400 Hz .. 3.4 kHz band, a few bits, a little hiss and clipping ----
    void phone(float* x, int n) {
        if (p_.phone < 0.004f) return;
        const float a = 1.f - std::exp(-2.f * kPi * 400.f / sr_), b = 1.f - std::exp(-2.f * kPi * (4500.f - 1200.f * p_.phone) / sr_), mix = std::min(1.f, p_.phone * 2.f);
        const int hold = 1 + (int)(p_.phone * 3.f); const float q = 1.f / (float)(1 << (int)(10 - p_.phone * 4.f));
        for (int i = 0; i < n; i++) {
            for (int c = 0; c < 2; c++) {
                const float d = x[2 * i + c];
                phHp_[c] += a * (d - phHp_[c]); float v = d - phHp_[c]; phHp2_[c] += a * (v - phHp2_[c]); v -= phHp2_[c];   // high-pass, two poles
                phLp1_[c] += b * (v - phLp1_[c]); phLp2_[c] += b * (phLp1_[c] - phLp2_[c]); v = phLp2_[c] * 1.4f;   // low-pass, two poles
                if (c == 0 && phCnt_ % hold == 0) phHold_[0] = std::round(v / q) * q; if (c == 1 && phCnt_ % hold == 0) phHold_[1] = std::round(v / q) * q;
                phRng_ = phRng_ * 1664525u + 1013904223u; const float hiss = ((phRng_ >> 9) / 4194304.f - 1.f) * 0.004f * p_.phone;
                const float w = std::tanh((phHold_[c] + hiss) * (1.f + p_.phone));
                x[2 * i + c] = d + (w - d) * mix;
            }
            phCnt_++;
        }
    }
    // ---- spring reverb: a chain of allpass filters smears the attack into the "boing", fed back through a short delay and a damping filter ----
    void spring(float* x, int n) {
        if (p_.spring < 0.004f) { if (spOn_) { spOn_ = false; std::fill(spD_.begin(), spD_.end(), 0.f); for (float& q : spZ_) q = 0.f; } return; }
        spOn_ = true;
        const float fb = 0.62f, damp = 0.25f + 0.7f * (1.f - p_.springDamp), mix = p_.spring * 0.8f, ap = 0.62f;
        for (int i = 0; i < n; i++) {
            const float in = 0.5f * (x[2 * i] + x[2 * i + 1]);
            const size_t rp = (spW_ + kSpD - 1300) % kSpD;
            float v = in + spD_[rp] * fb;
            for (int k = 0; k < kSpAp; k++) { const float y = -ap * v + spZ_[(size_t)k]; spZ_[(size_t)k] = v + ap * y; v = y; }   // dispersion: the highs arrive before the lows
            spLp_ += damp * (v - spLp_);
            spD_[spW_] = spLp_; spW_ = (spW_ + 1) % kSpD;
            x[2 * i] += spLp_ * 1.8f * mix; x[2 * i + 1] += spLp_ * 1.8f * mix;
        }
    }
    // ---- three band EQ: low shelf 120 Hz, peak 1 kHz, high shelf 6 kHz ----
    struct Biquad {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1[2] = {0, 0}, x2[2] = {0, 0}, y1[2] = {0, 0}, y2[2] = {0, 0};
        void set(float nb0, float nb1, float nb2, float na0, float na1, float na2) { b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0; a1 = na1 / na0; a2 = na2 / na0; }
        float run(int c, float x) { const float y = b0 * x + b1 * x1[c] + b2 * x2[c] - a1 * y1[c] - a2 * y2[c]; x2[c] = x1[c]; x1[c] = x; y2[c] = y1[c]; y1[c] = y; return y; }
    };
    bool eqOn() const { return std::fabs(p_.eqLow - 0.5f) >= 0.02f || std::fabs(p_.eqMid - 0.5f) >= 0.02f || std::fabs(p_.eqHigh - 0.5f) >= 0.02f; }
    void setupEq() {
        auto shelf = [&](Biquad& b, float hz, float db, bool high) {
            const float A = std::pow(10.f, db / 40.f), w = 2.f * kPi * hz / sr_, c = std::cos(w), alpha = std::sin(w) * 0.5f * 1.41421356f, s = 2.f * std::sqrt(A) * alpha;
            if (!high) b.set(A * ((A + 1) - (A - 1) * c + s), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - s), (A + 1) + (A - 1) * c + s, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - s);
            else b.set(A * ((A + 1) + (A - 1) * c + s), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - s), (A + 1) - (A - 1) * c + s, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - s);
        };
        shelf(eqL_, 120.f, (p_.eqLow - 0.5f) * 24.f, false);
        shelf(eqH_, 6000.f, (p_.eqHigh - 0.5f) * 24.f, true);
        const float A = std::pow(10.f, (p_.eqMid - 0.5f) * 24.f / 40.f), w = 2.f * kPi * 1000.f / sr_, c = std::cos(w), alpha = std::sin(w) / (2.f * 0.8f);
        eqM_.set(1 + alpha * A, -2 * c, 1 - alpha * A, 1 + alpha / A, -2 * c, 1 - alpha / A);
    }
    void eq(float* x, int n) {
        if (!eqOn()) return;
        for (int i = 0; i < n; i++) for (int c = 0; c < 2; c++) x[2 * i + c] = eqH_.run(c, eqM_.run(c, eqL_.run(c, x[2 * i + c])));
    }
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
    // ---- flanger: a very short delay swept slowly, with feedback ----
    void flanger(float* x, int n) {
        const bool wet = p_.flanger > 0.004f;
        const float mix = p_.flanger * 0.8f;
        for (int i = 0; i < n; i++) {
            flaPh_ += 0.22f / sr_; if (flaPh_ >= 1.f) flaPh_ -= 1.f;
            for (int c = 0; c < 2; c++) {
                const float in = x[2 * i + c];
                if (!wet) { fla_[c][(size_t)flaW_] = in; continue; }
                const float d = (0.0006f + 0.0034f * (0.5f + 0.5f * std::sin(2.f * kPi * (flaPh_ + 0.25f * c)))) * sr_;
                float rp = (float)flaW_ - d; if (rp < 0) rp += 2048.f;
                const int i0 = (int)rp & 2047, i1 = (i0 + 1) & 2047; const float fr = rp - std::floor(rp);
                const float y = fla_[c][(size_t)i0] + (fla_[c][(size_t)i1] - fla_[c][(size_t)i0]) * fr;
                fla_[c][(size_t)flaW_] = in + y * 0.55f;
                x[2 * i + c] = in + y * mix;
            }
            flaW_ = (flaW_ + 1) & 2047;
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
    // ---- tremolo: the level follows a sine in eighth notes ----
    void tremolo(float* x, int n, double beat0, double bps) {
        if (p_.tremolo < 0.004f) return;
        for (int i = 0; i < n; i++) {
            const float g = 1.f - p_.tremolo * 0.5f * (1.f + std::sin(2.f * kPi * (float)std::fmod((beat0 + i * bps) * 2.0, 1.0)));
            x[2 * i] *= g; x[2 * i + 1] *= g;
        }
    }
    // ---- auto-wah: a band-pass whose centre follows the level of the input ----
    void wah(float* x, int n) {
        if (p_.wah < 0.004f) { wahEnv_ = 0.f; wahLow_[0] = wahLow_[1] = wahBand_[0] = wahBand_[1] = 0.f; return; }
        const float m = p_.wah, att = std::exp(-1.f / (0.004f * sr_)), rel = std::exp(-1.f / (0.09f * sr_)), k = 0.32f;
        for (int i = 0; i < n; i++) {
            const float lv = std::max(std::fabs(x[2 * i]), std::fabs(x[2 * i + 1]));
            wahEnv_ = lv > wahEnv_ ? att * (wahEnv_ - lv) + lv : rel * (wahEnv_ - lv) + lv;
            const float fc = 350.f * std::pow(8.f, std::min(1.f, wahEnv_ * 3.f)), g = std::tan(kPi * std::min(fc, sr_ * 0.4f) / sr_);
            const float a1 = 1.f / (1.f + g * (g + k)), a2 = g * a1, a3 = g * a2;
            for (int c = 0; c < 2; c++) {   // state variable filter (same as the DJ one), the band-pass output
                const float in = x[2 * i + c], v3 = in - wahLow_[c], v1 = a1 * wahBand_[c] + a2 * v3, v2 = wahLow_[c] + a2 * wahBand_[c] + a3 * v3;
                wahBand_[c] = 2.f * v1 - wahBand_[c]; wahLow_[c] = 2.f * v2 - wahLow_[c];
                x[2 * i + c] = in * (1.f - 0.6f * m) + v1 * k * 2.4f * m;
            }
        }
    }
    // ---- ring modulator: the carrier runs from 20 Hz to 3 kHz ----
    void ringmod(float* x, int n) {
        if (p_.ringmod < 0.004f) return;
        const float f = 20.f * std::pow(150.f, p_.ringmod), mix = std::min(1.f, p_.ringmod * 6.f), inc = 2.f * kPi * f / sr_;
        for (int i = 0; i < n; i++) {
            const float s = std::sin(rmPh_); rmPh_ += inc; if (rmPh_ > 2.f * kPi) rmPh_ -= 2.f * kPi;
            for (int c = 0; c < 2; c++) x[2 * i + c] += (x[2 * i + c] * s - x[2 * i + c]) * mix;
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
    // ---- beat repeat / reverse / freeze: while engaged the last slice loops (forwards, backwards, or as an overlapped pad) instead of the input ----
    void slicer(float* x, int n, double samplesPerBeat) {
        const size_t N = rep_.size() / 2;
        const int mode = p_.freeze > 0 ? 3 : p_.reverse > 0 ? 2 : p_.repeat > 0 ? 1 : 0;
        if (mode == 0) {
            repOn_ = false;
            for (int i = 0; i < n; i++) { rep_[2 * repW_] = x[2 * i]; rep_[2 * repW_ + 1] = x[2 * i + 1]; if (++repW_ >= N) repW_ = 0; }
            return;
        }
        if (!repOn_ || mode != repMode_) {
            static const double div[4] = {1.0, 0.5, 0.25, 0.125};
            const int step = std::min(3, std::max(0, (mode == 2 ? p_.reverse : p_.repeat) - 1));
            repLen_ = (size_t)clampf(mode == 3 ? (float)(0.15 * sr_) : (float)(samplesPerBeat * div[step]), 128.f, (float)(N - 1));
            repStart_ = (repW_ + N - repLen_) % N; repK_ = 0; repOn_ = true; repMode_ = mode;
        }
        const size_t fade = std::min<size_t>(48, repLen_ / 4);
        for (int i = 0; i < n; i++) {
            float l, r;
            if (mode == 3) {   // two read heads half a slice apart, Hann windowed: they always add up to one
                const size_t k2 = (repK_ + repLen_ / 2) % repLen_;
                const float w1 = 0.5f - 0.5f * std::cos(2.f * kPi * (float)repK_ / repLen_), w2 = 0.5f - 0.5f * std::cos(2.f * kPi * (float)k2 / repLen_);
                const size_t i1 = (repStart_ + repK_) % N, i2 = (repStart_ + k2) % N;
                l = rep_[2 * i1] * w1 + rep_[2 * i2] * w2; r = rep_[2 * i1 + 1] * w1 + rep_[2 * i2 + 1] * w2;
            } else {
                const size_t idx = (repStart_ + (mode == 2 ? repLen_ - 1 - repK_ : repK_)) % N;
                float g = 1.f;
                if (repK_ < fade) g = (float)repK_ / fade; else if (repK_ + fade > repLen_) g = (float)(repLen_ - repK_) / fade;
                l = rep_[2 * idx] * g; r = rep_[2 * idx + 1] * g;
            }
            x[2 * i] = l; x[2 * i + 1] = r;
            if (++repK_ >= repLen_) repK_ = 0;
        }
    }
    // ---- tape stop, as on the OP-1: the tape winds down over tsDown seconds (the pitch falls with it), and on release it spins up again before the live signal comes back ----
    void tapeStop(float* x, int n) {
        const size_t N = ts_.size() / 2;
        const float brake = p_.tapeStop;
        const float down = 1.f / (std::max(0.05f, p_.tsDown) * (float)sr_), up = p_.tsUp <= 0.f ? 1.f : 1.f / (std::max(0.05f, p_.tsUp) * (float)sr_);   // speed per sample: 1 -> 0 in tsDown s, 0 -> 1 in tsUp s (0 = at once)
        const int xf = 1024;                                                              // hand-over from the spun-up tape to the live signal
        if (!tsOn_ && brake >= 0.02f) { tsOn_ = true; tsPos_ = (double)tsW_; tsSpeed_ = 1.f; tsRel_ = 0; }
        const float target = brake < 0.02f ? 1.f : 1.f - brake;
        for (int i = 0; i < n; i++) {
            const float l0 = x[2 * i], r0 = x[2 * i + 1];
            ts_[2 * tsW_] = l0; ts_[2 * tsW_ + 1] = r0; if (++tsW_ >= N) tsW_ = 0;
            if (!tsOn_) continue;   // live
            if (tsSpeed_ > target) tsSpeed_ = std::max(target, tsSpeed_ - down); else if (tsSpeed_ < target) tsSpeed_ = std::min(target, tsSpeed_ + up);
            tsPos_ += tsSpeed_; if (tsPos_ >= (double)N) tsPos_ -= (double)N;
            double lag = (double)tsW_ - tsPos_; if (lag < 0) lag += (double)N;
            if (lag > (double)N - 2) { tsPos_ = (double)tsW_; lag = 0; }   // the write head lapped the read head: start again
            const size_t i0 = (size_t)tsPos_ % N, i1 = (i0 + 1) % N; const float fr = (float)(tsPos_ - std::floor(tsPos_)), g = std::sqrt(std::max(0.f, tsSpeed_));
            float dl = (ts_[2 * i0] + (ts_[2 * i1] - ts_[2 * i0]) * fr) * g, dr = (ts_[2 * i0 + 1] + (ts_[2 * i1 + 1] - ts_[2 * i0 + 1]) * fr) * g;
            if (brake < 0.02f && tsSpeed_ >= 1.f && tsRel_ == 0) tsRel_ = xf;   // released and spun up: cross over to live
            if (tsRel_ > 0) { const float k = (float)tsRel_ / xf; dl = dl * k + l0 * (1.f - k); dr = dr * k + r0 * (1.f - k); if (--tsRel_ == 0) tsOn_ = false; }
            x[2 * i] = dl; x[2 * i + 1] = dr;
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
    Biquad eqL_, eqM_, eqH_;
    std::vector<float> cho_[2], fla_[2]; int choW_ = 0, flaW_ = 0; float choPh_ = 0, flaPh_ = 0;
    float ph_[2][4] = {}, phFb_[2] = {0, 0}, phA_ = 0, phPh_ = 0;
    float wahEnv_ = 0, wahLow_[2] = {0, 0}, wahBand_[2] = {0, 0}, rmPh_ = 0;
    Svf dj_; bool djOn_ = false; float djHz_ = 0;
    float crHeld_[2] = {0, 0}; int crCnt_[2] = {0, 0};
    float gateG_ = 1.f;
    std::vector<float> rep_; size_t repW_ = 0, repStart_ = 0, repLen_ = 0, repK_ = 0; bool repOn_ = false; int repMode_ = 0;
    std::vector<float> ts_; size_t tsW_ = 0; double tsPos_ = 0; float tsSpeed_ = 1.f; bool tsOn_ = false; int tsRel_ = 0;
    float cmpEnv_ = 0;
    float puF_ = 0, puS_ = 0;                                                   // punch: fast and slow level
    float phHp_[2] = {0, 0}, phHp2_[2] = {0, 0}, phLp1_[2] = {0, 0}, phLp2_[2] = {0, 0}, phHold_[2] = {0, 0}; int phCnt_ = 0; uint32_t phRng_ = 5;   // phone
    static constexpr int kCombLen = 2048, kOctLen = 8192;
    std::array<float, kCombLen> cb_[2]; int cbW_ = 0; float cbZ_[2] = {0, 0}; bool cbOn_ = false;   // comb
    std::array<float, kOctLen> oc_[2]; int ocW_ = 0; float ocPh_ = 0.f; bool ocOn_ = false;   // octaver
    struct Ap2 { float x1 = 0, x2 = 0, y1 = 0, y2 = 0; float run(float x, float a) { const float y = a * (x + y2) - x2; x2 = x1; x1 = x; y2 = y1; y1 = y; return y; } };   // an all-pass of the second order in z^-2
    Ap2 ap_[2][8]; float fsD_[2] = {0, 0}, fsPh_ = 0.f; int fsRamp_ = 0; bool fsOn_ = false;   // frequency shifter
    Svf vw_[3]; bool vwOn_ = false; float vyLp_[2] = {0, 0}, vyPop_ = 0; uint32_t vyRng_ = 11;   // vowel, vinyl
    static constexpr int kSpAp = 20; static constexpr size_t kSpD = 4096;       // spring: allpass stages, loop delay
    std::vector<float> spD_ = std::vector<float>(kSpD, 0.f); float spZ_[kSpAp] = {}; size_t spW_ = 0; float spLp_ = 0; bool spOn_ = false;
};
