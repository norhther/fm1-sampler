// SPDX-License-Identifier: GPL-3.0-only
// Float ports of three GHOULBOX engines (https://github.com/jasonpersinger/ghoulbox-fm1-dungeon-synth, GPL-3.0-only,
// itself a fork of Felucca by Leo Kuroshita, Hugelton Instruments): GURDY (hurdy-gurdy), WHEEL (tonewheel organ),
// VOICE (Klatt formant singer). The firmware runs them in fixed point; the algorithms and the constants are the same,
// the knobs keep Ghoulbox's meaning and range (SynthPatch::x, 0..127 unless noted).
//   engine 5 GURDY  x: DRN 0..11, DLVL, 5TH, WHL, BUZZ, COUP 0..4 (HOLD 1/4 1/8 1/8T 1/16), BODY, WOBL
//   engine 6 WHEEL  x: REG 0..15, SUB, BODY, TOP (-8..8), PERC 0..6, CLICK, DRIVE, ROTR 0..2
//   engine 7 VOICE  x: VOWL, VOWL2, TALK, SHIFT (semitones), BUZZ, BRTH, Q, RAND
#pragma once
#include "Common.h"

namespace dungeon {

constexpr float kBpm = 120.f;   // ponytail: the strokes of COUP run at a fixed 120 BPM; pass the song tempo in when it matters

struct Bp {   // TPT state-variable band-pass
    float s1 = 0.f, s2 = 0.f, g = 0.f, k = 1.f;
    void set(float hz, float q, float sr) { g = std::tan(kPi * std::min(hz, sr * 0.45f) / sr); k = 1.f / q; }
    float run(float x) {
        const float a1 = 1.f / (1.f + g * (g + k)), v1 = a1 * (s1 + g * (x - s2)), v2 = s2 + g * v1;
        s1 = 2.f * v1 - s1; s2 = 2.f * v2 - s2;
        return v1;
    }
};

inline float saw(float ph, float dt) {   // polyBLEP saw, -1..1
    float y = 2.f * ph - 1.f;
    if (ph < dt) { const float t = ph / dt; y -= t + t - t * t - 1.f; }
    else if (ph > 1.f - dt) { const float t = (ph - 1.f) / dt; y -= t * t + t + t + 1.f; }
    return y;
}
inline float softKnee(float x) { return std::fabs(x) < 0.6f ? x : std::tanh((std::fabs(x) - 0.6f) / 0.4f) * 0.4f * (x < 0 ? -1.f : 1.f) + 0.6f * (x < 0 ? -1.f : 1.f); }

class Voice {
public:
    // x: the eight knobs; engine 5, 6 or 7; hz: the note; vel 0..1
    void start(const float* x, int engine, float vel, float sr, uint32_t seed) {
        *this = Voice();
        for (int i = 0; i < 8; i++) x_[i] = x[i];
        eng_ = engine; sr_ = sr; vel_ = vel; rng_ = seed * 2654435761u + 977u;
        for (int k = 0; k < 9; k++) ph_[k] = rnd();   // free-running wheels / phases
        bar_.set(270.f, 0.7f, sr); nose_.set(1100.f, 0.9f, sr);
        if (eng_ == 6) setupWheel();
    }
    float next(float hz, float tone, float motion) {
        t_ += 1.f / sr_;
        switch (eng_) { case 5: return gurdy(hz); case 6: return wheel(hz, motion); default: return formant(hz, tone); }
    }
private:
    float x_[8] = {}; int eng_ = 5; float sr_ = 48000.f, vel_ = 1.f, t_ = 0.f; uint32_t rng_ = 1;
    float ph_[9] = {}, lp_ = 0.f, wob_ = 0.f, wobN_ = 0.f; int wobC_ = 0;
    Bp bar_, nose_;
    // wheel
    float g_[9] = {}, pk_ = 0.f, pdec_ = 0.f, plev_ = 0.f, perc_ = 1.f, clk_ = 1.f, hp_ = 0.f, drvG_ = 0.f, drvC_ = 1.f, rph_[2] = {0.f, 0.3f}, rsp_[2] = {};
    int pidx_ = 9;
    // formant
    float y_[8] = {}, tilt_ = 0.f, talk_ = 0.f, rv_ = 0.f;

    float rnd() { rng_ = rng_ * 1664525u + 1013904223u; return (rng_ >> 8) / 16777216.f; }

    // ---------- GURDY ----------
    float gurdy(float hz) {
        const int drn = 36 + (int)clampf(x_[0], 0.f, 11.f);
        const float dlvl = x_[1] * 200.f / 32768.f, flvl = x_[2] * 170.f / 32768.f, whl = x_[3], buzz = x_[4] * 258.f / 32768.f, body = x_[6] * 258.f / 32768.f, wobl = x_[7];
        const int coup = (int)clampf(x_[5], 0.f, 4.f);
        const float dh = 440.f * std::pow(2.f, (drn - 69) / 12.f), fh = dh * 1.5f;
        if ((wobC_++ & 31) == 0) { wobN_ += rnd() - 0.5f; wobN_ -= wobN_ / 64.f; }   // the wheel's wander
        const float w = wobN_ * wobl / 32.f;
        const float inc = hz / sr_ * (1.f + w * 0.0012f), dinc = dh / sr_, finc = fh / sr_;
        float e;   // the trompette's strokes
        if (buzz <= 0.f) e = 0.f;
        else if (coup == 0) e = 1.f;
        else {
            static const float div[5] = {0.f, 1.f, 0.5f, 1.f / 3.f, 0.25f};   // beats
            const float per = div[coup] * 60.f / kBpm, a = std::fmod(t_, per), fall = per * 0.4f;
            e = a < fall ? 1.f - a / fall : 0.f;
        }
        ph_[0] += inc; ph_[0] -= std::floor(ph_[0]);
        ph_[1] += dinc; ph_[1] -= std::floor(ph_[1]);
        ph_[2] += finc; ph_[2] -= std::floor(ph_[2]);
        const float nz = rnd() - 0.5f, kp = (6000.f + whl * 200.f) / 32768.f;
        lp_ += (saw(ph_[0], inc) - lp_) * std::min(1.f, kp);   // the chanterelle through the pressure low-pass, and the rosin
        float x = lp_ + nz * (whl * 24.f / 32768.f) * 2.f;
        x += saw(ph_[1], dinc) * dlvl + saw(ph_[2], finc) * flvl;   // bourdon and mouche
        if (e > 0.f) {   // the buzzing bridge: the dog chatters at each period's start
            float p2 = ph_[1] * 2.f; p2 -= std::floor(p2);
            float tr = clampf(saw(p2, dinc * 2.f) * 3.f, -0.73f, 0.73f) + (p2 < 0.1875f ? nz * 2.f : 0.f);
            x += tr * e * buzz;
        }
        x *= 0.5f;
        const float r = bar_.run(x) + 0.5f * nose_.run(x);
        x += (r - x) * body;
        return softKnee(x) * 1.3f;
    }

    // ---------- WHEEL ----------
    static const char* reg(int i) {
        static const char* R[16] = {"008000000", "008400000", "800800000", "838000000", "888000000", "888800000", "888600006", "888000888",
                                    "800000888", "008080800", "008765432", "006876540", "004432000", "008888000", "858000000", "888888888"};
        return R[i & 15];
    }
    void setupWheel() {
        static const float LV[9] = {0.f, 0.089f, 0.126f, 0.178f, 0.251f, 0.355f, 0.501f, 0.708f, 1.f};   // 3 dB a step
        const int perc = (int)x_[4] % 7;
        float lv[9], s = 0.f;
        for (int k = 0; k < 9; k++) {
            int d = reg((int)x_[0])[k] - '0'; d += k < 2 ? (int)x_[1] : k < 4 ? (int)x_[2] : (int)x_[3];
            lv[k] = (perc && k == 8) ? 0.f : LV[(int)clampf((float)d, 0.f, 8.f)]; s += lv[k];
        }
        float norm = 2.f * 1.2f / (s + 1.5f);   // the louder the registration, the less each bar gets
        if (perc == 1 || perc == 2 || perc == 5 || perc == 6) norm *= 0.7071f;
        for (int k = 0; k < 9; k++) g_[k] = lv[k] * norm;
        pidx_ = perc ? 3 + ((perc - 1) & 1) : 9;
        plev_ = (perc == 3 || perc == 4) ? 0.37f : 0.73f;
        pdec_ = std::exp(-1.f / (sr_ * (perc >= 5 ? 0.6f : 0.15f)));
        if (x_[6] > 0.f) { drvG_ = 1.f + x_[6] / 127.f * 3.f; drvC_ = 6.f / (2.f + drvG_); }
        rsp_[0] = x_[7] < 1.f ? 0.f : x_[7] < 2.f ? 0.8f : 6.7f; rsp_[1] = x_[7] < 1.f ? 0.f : x_[7] < 2.f ? 0.7f : 5.8f;
    }
    float wheel(float hz, float motion) {
        static const float R[9] = {0.5f, 1.5f, 1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 8.f};
        const float amH = x_[7] >= 1.f ? 0.4f : 0.f, amL = x_[7] >= 1.f ? 0.21f : 0.f;
        rph_[0] += rsp_[0] / sr_; rph_[0] -= std::floor(rph_[0]); rph_[1] += rsp_[1] / sr_; rph_[1] -= std::floor(rph_[1]);
        const float cH = std::cos(2.f * kPi * rph_[0]), cL = std::cos(2.f * kPi * rph_[1]);
        const float amp[2] = {1.f - amH * (1.f - cH) * 0.5f, 1.f - amL * (1.f - cL) * 0.5f};
        const float fm[2] = {std::sin(2.f * kPi * rph_[0]) * 0.005f * (rsp_[0] / 6.7f), std::sin(2.f * kPi * rph_[1]) * 0.002f * (rsp_[1] / 5.8f)};
        perc_ *= pdec_;
        float acc = 0.f;
        for (int k = 0; k < 9; k++) {
            float f = hz * R[k]; while (f > 6000.f) f *= 0.5f;   // above ~6 kHz it folds an octave, as the top of a tonewheel set does
            const int r = f < 800.f;
            float g = g_[k]; if (k == pidx_) g += plev_ * perc_;
            if (g <= 0.f) { ph_[k] += f / sr_; ph_[k] -= std::floor(ph_[k]); continue; }
            ph_[k] += f * (1.f + fm[r]) / sr_; ph_[k] -= std::floor(ph_[k]);
            acc += std::sin(2.f * kPi * ph_[k]) * g * amp[r];
        }
        if (x_[5] > 0.f && clk_ > 0.001f) { const float z = rnd() * 2.f - 1.f, y = z - hp_; hp_ += y / 8.f; acc += y * clk_ * (x_[5] * 180.f / 32768.f); clk_ *= std::exp(-1.f / (sr_ * 0.0025f)); }
        if (drvG_ > 0.f) acc = std::tanh(acc * drvG_ / 2.f) * drvC_;
        return acc * 0.5f * (0.6f + 0.4f * vel_) * (1.f + 0.2f * motion);
    }

    // ---------- VOICE (Klatt cascade of four resonators on a Rosenberg glottal pulse) ----------
    static void vowelAt(float v, float* f, float* b) {   // v 0..127: A E I O U, interpolated
        static const float F[5][3] = {{768, 1333, 2522}, {580, 1799, 2605}, {342, 2322, 3000}, {497, 910, 2459}, {378, 997, 2343}};
        static const float B[5][3] = {{90, 100, 160}, {70, 100, 170}, {50, 120, 200}, {70, 80, 150}, {60, 80, 150}};
        static const float P[5] = {0.f, 32.f, 64.f, 95.f, 127.f};
        v = clampf(v, 0.f, 127.f); int i = 0; while (i < 3 && v > P[i + 1]) i++;
        const float t = (v - P[i]) / (P[i + 1] - P[i]);
        for (int j = 0; j < 3; j++) { f[j] = F[i][j] + (F[i + 1][j] - F[i][j]) * t; b[j] = B[i][j] + (B[i + 1][j] - B[i][j]) * t; }
    }
    struct Res { float a, b, c; };
    static Res res(float f, float bw, float sr) { const float r = std::exp(-kPi * bw / sr), B = 2.f * r * std::cos(2.f * kPi * f / sr), C = -r * r; return {1.f - B - C, B, C}; }
    float formant(float hz, float tone) {
        if (rv_ == 0.f) rv_ = rnd() * 127.f + 0.001f;   // this note's random vowel
        float k = 0.f;
        if (x_[2] > 0.f) { talk_ = std::min(1.f, talk_ + 1.f / (sr_ * (0.05f + (127.f - x_[2]) / 127.f * 3.f))); k = talk_ * (2.f - talk_); }   // VOWL -> VOWL2 over TALK
        const float rnd01 = x_[7] * 258.f / 32768.f;
        const float v0 = x_[0] + (rv_ - x_[0]) * rnd01, v1 = x_[1] + (rv_ - x_[1]) * rnd01;
        float fa[3], ba[3], fb[3], bb[3]; vowelAt(v0, fa, ba); vowelAt(v1, fb, bb);
        const float ratio = std::pow(2.f, x_[3] / 12.f + 30.f / 127.f * std::log2(hz / 261.63f));   // SHIFT, and key tracking around C4
        const float bws = std::pow(2.f, (64.f - x_[6]) * 4.f / 192.f);
        Res rs[4]; float f[3], b[3];
        for (int j = 0; j < 3; j++) {
            f[j] = (fa[j] + (fb[j] - fa[j]) * k) * ratio; b[j] = (ba[j] + (bb[j] - ba[j]) * k) * ratio * bws;
            if (j == 0 && f[j] < hz * 1.0625f) { b[j] *= hz * 1.0625f / f[j]; f[j] = hz * 1.0625f; }   // sung high: F1 follows F0
            rs[j] = res(clampf(f[j], 60.f, 7000.f), clampf(b[j], 20.f, 1500.f), sr_);
        }
        rs[3] = res(clampf(3300.f * ratio, 60.f, 7000.f), clampf(250.f * ratio * bws, 20.f, 1500.f), sr_);
        const float buzz = clampf(x_[4] - 64.f + (vel_ * 127.f - 96.f) * 0.15f + 64.f + 20.f * tone, 0.f, 127.f);
        const float oq = 0.9f - 0.55f * buzz / 127.f, tilt = (2500.f + buzz * 109.f / 256.f * 2.f) / 16384.f * 8.f;   // one-pole: dark .. open
        const float te = oq, tp = te * 0.65f;
        const float dt = hz / sr_; ph_[0] += dt; ph_[0] -= std::floor(ph_[0]);
        const float ph = ph_[0];
        float e = 0.f;
        if (ph < tp) e = std::sin(kPi * ph / tp) * 0.54f; else if (ph < te) e = -std::sin(0.5f * kPi * (ph - tp) / (te - tp));
        { float u = ph - te; if (u < 0.f) u += 1.f; if (u < dt) { const float t = u / dt; e += 0.5f * (t + t - t * t - 1.f) * -1.f; } }   // the closure step, band-limited
        lp_ += (e - lp_) * clampf(tilt, 0.02f, 1.f);
        const float brth = x_[5], gv = 1.f - brth * brth * 2.f / 32768.f, gn = brth * 258.f / 32768.f;
        float x = lp_ * std::max(0.f, gv);
        if (gn > 0.f) { const float nz = (rnd() - 0.5f) * 2.f * gn; x += ph < te ? nz : nz * 0.5f; }
        float a = x;
        for (int j = 0; j < 4; j++) { const float y = rs[j].a * a + rs[j].b * y_[j * 2] + rs[j].c * y_[j * 2 + 1]; y_[j * 2 + 1] = y_[j * 2]; y_[j * 2] = y; a = y; }
        const float og = (13000.f - (x_[6] - 64.f) * 50.f) / 32768.f * std::pow(hz / 261.63f, -0.25f);   // low notes are sparse pulses: 1.5 dB / octave
        const float pres = 1.f + buzz / 127.f * 3.9f;
        const float out = (a + 0.25f * pres * (a - lastY_)) * og * 18.f; lastY_ = a;   // PRES: a first-difference shelf
        return softKnee(out);
    }
    float lastY_ = 0.f;
};

}  // namespace dungeon
