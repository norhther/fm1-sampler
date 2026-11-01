// SPDX-License-Identifier: GPL-3.0-only
// ORACLE of Tessera (GPL-3.0, Theo Dainis, from Felucca / Melodee): two oscillators, Poly-Mod and a four-pole ladder after the Prophet-5, as floats.
// The patch is the panel (OrParam, 0..127). The amplitude envelope is the host's (the SynthPatch's ADSR is made from the patch's); the filter
// envelope runs here: attack, decay to sustain (no note-off reaches a voice, so its release is the amplitude's).
#pragma once
#include "FelCommon.h"
#include "OracleSounds.h"
namespace oracle {

inline float envMs(int v) { return std::pow(10000.f, v / 127.f); }   // a time knob: 1 ms .. 10 s
inline float lfoHz(int v) {
    float x = -3.1005f + 0.05387f * v;
    if (v > 100) x += (std::log(500.f) - (-3.1005f + 0.05387f * 127.f)) * std::pow((v - 100) / 27.f, 2.f);
    if (v < 20) x -= (-3.1005f - std::log(.022f)) * std::pow((20 - v) / 20.f, 2.f);
    return std::exp(x);
}
inline void fill(int sound, uint8_t* p) {   // INIT, then the sound's few values
    for (int i = 0; i < kOracleN; i++) p[i] = 0;
    p[OR_FREQ_A] = p[OR_FREQ_B] = 24; p[OR_SAW_A] = p[OR_KEY_B] = 1; p[OR_PW_A] = p[OR_PW_B] = 64; p[OR_LEVEL_A] = p[OR_CUTOFF] = 127; p[OR_FILTER_REV] = 1;
    p[OR_DECAY_FILTER] = p[OR_DECAY_AMP] = 60; p[OR_SUSTAIN_FILTER] = p[OR_SUSTAIN_AMP] = 127; p[OR_RELEASE_FILTER] = p[OR_RELEASE_AMP] = 40;
    p[OR_RELEASE_ON] = 1; p[OR_LFO_RATE] = 60; p[OR_LFO_TRI] = 1;
    if (sound < 0 || sound >= kOracleSoundCount) return;
    const OracleSound& s = kOracleSounds[sound];
    for (int i = 0; i < s.n; i++) p[s.set[i].k] = s.set[i].v;
}

class Voice {
public:
    void start(int sound, int note, float vel, float sr, uint32_t seed) {
        fill(sound, p_); sr_ = sr; vel_ = vel; rng_ = 0x5A1793u + seed * 17u; ph_[0] = fel::unit(rng_); ph_[1] = p_[OR_LOW_B] ? fel::unit(rng_) : 0.f;
        tri_ = 1.f; tail_ = 0.f; for (float& z : z_) z = 0.f; lfo_ = 0.f; env_ = 0.f; stage_ = 1; blk_ = 0; note_ = note; dcx_ = dcy_ = 0.f;
        const int vint = p_[OR_VINTAGE];   // VINTAGE: each voice's tuning, cutoff and level drift as an old unit's
        drift_ = ((int)(fel::xorshift(rng_) & 255u) - 128) * vint / 1024.f; fdrift_ = ((int)(fel::xorshift(rng_) & 255u) - 128) * vint / 32.f / 256.f;
        adrift_ = ((int)(fel::xorshift(rng_) & 255u) - 128) * vint / 16.f / 32768.f; edrift_ = ((int)(fel::xorshift(rng_) & 255u) - 128) * vint / 128.f / 32768.f;
        const int res = p_[OR_RESONANCE]; const bool late = p_[OR_FILTER_REV];
        if (res >= 110 && !p_[OR_LEVEL_A] && !p_[OR_LEVEL_B] && !p_[OR_NOISE]) { z_[0] = 0.5f; z_[2] = -0.5f; }   // thermal excitation: it self-oscillates with the mixer shut
        const int cut = p_[OR_CUTOFF];
        cut_ = (1618.f + cut * 236.f + (late ? 0.f : std::min(127 - cut, 66) * 38.f)) / 256.f;
        k_ = res * (late ? 19200.f : 18800.f) / 127.f / 4096.f; fgain_ = (32767.f - res * (late ? 100.f : 45.f)) / 32768.f;
        envAmt_ = p_[OR_ENV_FILTER] * 165.f / 256.f;
        paEnv_ = p_[OR_POLY_ENV] * 85.f / 4096.f; paB_ = p_[OR_POLY_B] * 35.f / 4096.f; pfEnv_ = p_[OR_POLY_ENV] * 226.f / 256.f; pfB_ = p_[OR_POLY_B] * 226.f / 256.f;
        pwEnv_ = p_[OR_POLY_ENV] * 64.f / 32768.f; pwB_ = p_[OR_POLY_B] * 258.f / 32768.f; pwmA_ = p_[OR_PW_A] * 258.f / 32768.f; pwmB_ = p_[OR_PW_B] * 258.f / 32768.f;
        la_ = p_[OR_LEVEL_A] * 258.f / 32768.f; lb_ = p_[OR_LEVEL_B] * 258.f / 32768.f; ln_ = p_[OR_NOISE] * 258.f / 32768.f;
        const float amt = std::pow(p_[OR_LFO_INITIAL] / 127.f, 1.737f); lfoAmt_ = amt; lfoInc_ = lfoHz(p_[OR_LFO_RATE]) / sr_; mix_ = p_[OR_WHEEL_MIX] / 127.f;
        fv_ = p_[OR_VEL_FILTER] ? clampf(vel, 0.f, 1.f) : 1.f;
        const float blockT = 32.f / sr_;
        atk_ = blockT / std::min(envMs(p_[OR_ATTACK_FILTER]) / 1000.f, 0.25f);   // ponytail: no filter swell slower than 0.25 s, so a pad speaks on a short note dec_ = 1.f - std::exp(-blockT / (envMs(p_[OR_DECAY_FILTER]) / 1000.f));
        susT_ = p_[OR_SUSTAIN_FILTER] / 127.f; ssm_ = blockT / (3.6f * envMs(p_[OR_DECAY_FILTER]) / 1000.f);
    }
    float next(float hz) {
        if (blk_ == 0) { envPrev_ = env_; envTick(); }
        const float fe = envPrev_ + (env_ - envPrev_) * (float)blk_ / 32.f; blk_ = (blk_ + 1) & 31;
        const float noise = fel::unit(rng_) - 0.5f;
        lfoWave_ = lfoWave();
        const float lfo = (lfoWave_ * (1.f - mix_) + noise * mix_) * lfoAmt_; lfo_ += lfoInc_; lfo_ -= std::floor(lfo_);
        const float wp = std::exp2(lfo * 470.f / 1200.f);
        const float pitch = fel::pitchOf(hz);
        const float semiA = (std::min((int)p_[OR_FREQ_A], 96) / 2 - 12) + drift_ / 100.f;
        float fB = p_[OR_KEY_B] ? hz * std::exp2(((std::min((int)p_[OR_FREQ_B], 96) / 2 - 12) + (p_[OR_FINE_B] * 100.f / 128.f - drift_) / 100.f) / 12.f)
                                : 440.f * std::exp2(((12.f + p_[OR_FREQ_B] * 108.f / 127.f) - 69.f + (p_[OR_FINE_B] * 100.f / 128.f - drift_) / 100.f) / 12.f);
        if (p_[OR_LOW_B]) fB /= 128.f;
        const float fA = hz * std::exp2(semiA / 12.f);
        float dB = fB / sr_; if (p_[OR_WHEEL_FREQ_B]) dB *= wp;
        const float bw = wave(ph_[1], dB, clampf(pwmB_ + (p_[OR_WHEEL_PW_B] ? lfo : 0.f), 0.03125f, 0.968f), p_[OR_SAW_B], p_[OR_PULSE_B], p_[OR_TRI_B], &tri_);
        const float bn = bw * 2.f;
        float dA = fA / sr_;
        if (p_[OR_POLY_FREQ]) dA *= std::exp2(clampf(fe * paEnv_ + bn * paB_, -8.f, 6.f));
        if (p_[OR_WHEEL_FREQ_A]) dA *= wp;
        const float pwA = clampf(pwmA_ + (p_[OR_POLY_PW] ? fe * pwEnv_ + bw * pwB_ : 0.f) + (p_[OR_WHEEL_PW_A] ? lfo : 0.f), 0.03125f, 0.968f);
        float unused = 0.f;
        float a = wave(ph_[0], dA, pwA, p_[OR_SAW_A], p_[OR_PULSE_A], 0, &unused) + tail_; tail_ = 0.f;
        const float nb = ph_[1] + dB; float na = ph_[0] + dA; na -= std::floor(na);
        if (p_[OR_SYNC] && nb >= 1.f && dB > 0.f) {   // B wrapped: A restarts, the jump band-limited
            const float frac = std::min(1.f, (1.f - ph_[1]) / dB), remain = 1.f - frac;
            float at = ph_[0] + dA * frac; at -= std::floor(at);
            const float before = wave(at, dA, pwA, p_[OR_SAW_A], p_[OR_PULSE_A], 0, &unused), after = wave(0.f, dA, pwA, p_[OR_SAW_A], p_[OR_PULSE_A], 0, &unused);
            const float jump = (after - before) * 0.5f;
            a += jump * remain * remain; tail_ = -jump * frac * frac; na = dA * remain;
        }
        ph_[0] = na; ph_[1] = nb - std::floor(nb);
        const float x = a * la_ + bw * lb_ + noise * ln_;
        float fc = cut_ + fdrift_ + (pitch - 36.f) * 16.f * std::min((int)p_[OR_KEY_FILTER], 2) * 1196.f / 128.f / 256.f + fe * fv_ * envAmt_;
        if (p_[OR_POLY_FILTER]) fc += fe * pfEnv_ + bn * pfB_;
        if (p_[OR_WHEEL_FILTER]) fc += lfo * 112.f;
        const float y = ladder(std::tanh(x), fc);
        const float out = y * (1.f + adrift_) * 2.f * 0.7324f * (p_[OR_VEL_AMP] ? clampf(vel_, 0.f, 1.f) : 1.f);
        dcy_ = out - dcx_ + dcy_ * (1.f - 6.2831853f * 5.f / sr_); dcx_ = out;   // the output's ~5 Hz coupling: the oscillators are unipolar
        return dcy_ * 2.5f;
    }
    // the amplitude envelope, as SynthPatch ADSR seconds (attack, decay, sustain 0..1, release)
    static void adsr(int sound, float& a, float& d, float& s, float& r) {
        uint8_t p[kOracleN]; fill(sound, p);
        a = clampf(envMs(p[OR_ATTACK_AMP]) / 1000.f, 0.002f, 0.2f); d = std::max(0.01f, envMs(p[OR_DECAY_AMP]) / 1000.f); s = p[OR_SUSTAIN_AMP] / 127.f;
        r = std::max(0.02f, (p[OR_RELEASE_ON] ? envMs(p[OR_RELEASE_AMP]) : envMs(16)) / 1000.f);
    }
private:
    uint8_t p_[kOracleN]; float sr_ = 44100.f, vel_ = 1.f, ph_[2] = {0, 0}, tri_ = 1.f, tail_ = 0.f, z_[4] = {0, 0, 0, 0}, lfo_ = 0.f, lfoWave_ = 0.f, env_ = 0.f, envPrev_ = 0.f;
    float drift_ = 0.f, fdrift_ = 0.f, adrift_ = 0.f, edrift_ = 0.f, cut_ = 0.f, k_ = 0.f, fgain_ = 1.f, envAmt_ = 0.f, paEnv_ = 0.f, paB_ = 0.f, pfEnv_ = 0.f, pfB_ = 0.f, pwEnv_ = 0.f, pwB_ = 0.f;
    float pwmA_ = 0.5f, pwmB_ = 0.5f, la_ = 0.f, lb_ = 0.f, ln_ = 0.f, lfoAmt_ = 0.f, lfoInc_ = 0.f, mix_ = 0.f, fv_ = 1.f, atk_ = 0.f, dec_ = 0.f, susT_ = 1.f, ssm_ = 0.f, dcx_ = 0.f, dcy_ = 0.f;
    int blk_ = 0, stage_ = 1, note_ = 60; uint32_t rng_ = 1;
        void envTick() {   // the filter envelope once a block: linear attack, then EARLY's linear decay or LATE's exponential
        if (stage_ == 1) { env_ += atk_; if (env_ >= 1.f) { env_ = 1.f; stage_ = 2; } }
        else if (stage_ == 2) {
            if (!p_[OR_FILTER_REV]) env_ += clampf(susT_ - env_, -ssm_, ssm_);
            else env_ += (susT_ - env_) * std::min(dec_ * (1.f + edrift_), 1.f);
        }
    }
    float lfoWave() const {
        float y = 0.f; int n = 0; const float pos = lfo_;
        if (p_[OR_LFO_SAW]) { y += pos; n++; }
        if (p_[OR_LFO_TRI]) { y += pos < 0.5f ? pos * 2.f - 0.5f : 1.5f - pos * 2.f; n++; }
        if (p_[OR_LFO_PULSE]) { y += pos < 0.5f ? 1.f : 0.f; n++; }
        return n == 1 ? y : n == 2 ? y * 0.5f : n == 3 ? y / 3.f : 0.f;
    }
    static float blep(float ph, float dt) {
        if (dt <= 0.f) return 0.f;
        if (ph < dt) { const float x = ph / dt; return x + x - x * x - 1.f; }
        if (ph > 1.f - dt) { const float x = (ph - 1.f) / dt; return x * x + x + x + 1.f; }
        return 0.f;
    }
    static float sawB(float ph, float dt) { return ph * 2.f - 1.f - blep(ph, dt); }
    // one oscillator sample at ph (which it then advances by dt); saw, pulse and the triangle integrated from the square
    static float wave(const float at, float dt, float pw, int saw, int pulse, int tri, float* triangle) {
        float y = 0.f;
        if (saw || pulse || tri) {
            const float a = sawB(at, dt); float sq = 0.f;
            if (saw) y += a * 0.5f;
            if (pulse) { float q = at + pw; q -= std::floor(q); sq = (a - sawB(q, dt)) * 0.5f; y += sq; }
            if (tri) {
                if (!pulse || pw != 0.5f) { float q = at + 0.5f; q -= std::floor(q); sq = (a - sawB(q, dt)) * 0.5f; }
                *triangle = clampf(*triangle * (32767.f / 32768.f) + sq * dt * 8.f, -1.2207f, 1.2207f);
                y += *triangle * 0.5f;
            }
        }
        return y;
    }
    float ladder(float in, float cutIdx) {   // four trapezoidal one-poles, the feedback solved algebraically; LATE linear stages, EARLY each saturating
        const float c = clampf(cutIdx, 0.f, 127.f), hz = std::min(30.f * std::pow(16000.f / 30.f, c / 127.f), 0.45f * sr_);
        const float t = std::tan(kPi * hz / sr_), g = t / (1.f + t), g2 = g * g, g3 = g2 * g, g4 = g3 * g, rec = 1.f / (1.f + k_ * g4);
        const float sigma = (1.f - g) * (g3 * z_[0] + g2 * z_[1] + g * z_[2] + z_[3]);
        float u = std::tanh((in * fgain_ - k_ * sigma) * rec);
        const bool late = p_[OR_FILTER_REV];
        for (int j = 0; j < 4; j++) {
            const float delta = g * (u - z_[j]), y = z_[j] + delta;
            z_[j] = clampf(y + delta, -1.831f, 1.831f); if (std::fabs(z_[j]) < 1e-20f) z_[j] = 0.f;
            u = late ? y : std::tanh(y);
        }
        return std::tanh(u);
    }
};
}  // namespace oracle
