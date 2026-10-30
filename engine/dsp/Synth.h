// Built-in synthesizers: a table of patches played by one voice engine (no sample needed).
// A voice = oscillator 1 (optionally a stack of detuned saws, or an FM carrier) + oscillator 2 + sub + noise -> low-pass with its own envelope.
// A plucked patch uses a Karplus-Strong string instead of the oscillators. The amplitude envelope is the track's ADSR (the patch sets its defaults).
#pragma once
#include "Common.h"
#include "Filter.h"

struct SynthPatch {
    const char* name;
    int w1; float l1;                       // oscillator 1: wave (0 sine 1 saw 2 square 3 triangle), level
    int w2; float semi2, det2, l2;          // oscillator 2: wave, offset in semitones and cents, level
    float sub, noise, noiseDec;             // sine one octave down, white noise (decay in s, 0 = continuous)
    float pw; int uni; float uniDet;        // pulse width; unison saw stack (voices, spread in cents)
    float fmRatio, fmIdx, fmDec;            // FM: modulator ratio, index, decay in s (0 = constant); osc 1 must be a sine
    float cut, res, fenv, fdec, key;        // filter: cutoff Hz, resonance (Q), envelope amount in octaves, its decay in s, key tracking 0..1
    float pitchEnv, pitchDec;               // start this many semitones above the note and glide down
    float vib;                              // vibrato depth in cents (fades in)
    bool pluck; float pluckDec;             // plucked string; level left after one second
    float a, d, s, r;                       // default amplitude envelope
};

//   name        w1 l1   w2 semi det  l2   sub  noi  nDec  pw   uni det   fmR  fmI  fmD   cut   res  fenv fdec key   pEnv pDec vib  pluck dec   a     d     s     r
static const SynthPatch kSynths[] = {
    {"Sub Bass",   0, 1.0f, 3, 0, 0,   0.3f, 0.6f, 0.f, 0.f, 0.5f, 1, 0,   1, 0.f, 0.f, 900,  0.9f, 1.0f, 0.25f, 0.2f, 0, 0, 0, false, 0, 0.005f, 0.3f, 0.9f, 0.15f},
    {"Saw Bass",   1, 0.8f, 1, 0, 8,   0.6f, 0.4f, 0.f, 0.f, 0.5f, 1, 0,   1, 0.f, 0.f, 500,  1.2f, 2.5f, 0.3f,  0.3f, 0, 0, 0, false, 0, 0.004f, 0.35f, 0.7f, 0.12f},
    {"Acid",       1, 1.0f, 2, 0, 6,   0.3f, 0.f,  0.f, 0.f, 0.5f, 1, 0,   1, 0.f, 0.f, 350,  6.0f, 3.0f, 0.22f, 0.25f, 0, 0, 0, false, 0, 0.003f, 0.25f, 0.6f, 0.08f},
    {"FM Bass",    0, 1.0f, 0, 0, 0,   0.f,  0.4f, 0.f, 0.f, 0.5f, 1, 0,   1, 2.5f, 0.35f, 6000, 0.7f, 0.f, 0.1f,  0.f, 0, 0, 0, false, 0, 0.003f, 0.4f, 0.55f, 0.15f},
    {"Mono Lead",  1, 0.8f, 2, 0, 7,   0.5f, 0.2f, 0.f, 0.f, 0.5f, 1, 0,   1, 0.f, 0.f, 3500, 1.5f, 1.5f, 0.35f, 0.4f, 0, 0, 7, false, 0, 0.01f, 0.3f, 0.8f, 0.2f},
    {"Pulse Lead", 2, 0.8f, 2, 0, 9,   0.5f, 0.f,  0.f, 0.f, 0.35f,1, 0,   1, 0.f, 0.f, 4000, 1.0f, 1.0f, 0.3f,  0.5f, 0, 0, 5, false, 0, 0.008f, 0.3f, 0.8f, 0.18f},
    {"Chip Square",2, 1.0f, 2, 12, 0,  0.25f,0.f,  0.f, 0.f, 0.125f,1, 0,  1, 0.f, 0.f, 9000, 0.7f, 0.f, 0.1f,  0.f, 0, 0, 0, false, 0, 0.002f, 0.12f, 0.6f, 0.06f},
    {"Super Saw",  1, 1.0f, 0, 0, 0,   0.f,  0.f,  0.f, 0.f, 0.5f, 5, 22,  1, 0.f, 0.f, 4500, 0.9f, 0.8f, 0.4f,  0.4f, 0, 0, 0, false, 0, 0.01f, 0.4f, 0.8f, 0.35f},
    {"Warm Pad",   1, 1.0f, 3, 0, 5,   0.5f, 0.f,  0.f, 0.f, 0.5f, 3, 12,  1, 0.f, 0.f, 1300, 0.8f, 0.7f, 1.2f,  0.3f, 0, 0, 3, false, 0, 0.7f,  0.8f, 0.85f, 1.2f},
    {"String Ens", 1, 1.0f, 1, 0, 6,   0.f,  0.f,  0.f, 0.f, 0.5f, 5, 11,  1, 0.f, 0.f, 2600, 0.8f, 0.4f, 1.0f,  0.4f, 0, 0, 5, false, 0, 0.35f, 0.5f, 0.9f, 0.7f},
    {"Glass Pad",  0, 0.8f, 0, 12, 3,  0.4f, 0.f,  0.f, 0.f, 0.5f, 1, 0,   2, 0.6f, 0.f, 8000, 0.7f, 0.f, 0.1f,  0.2f, 0, 0, 3, false, 0, 0.5f,  0.8f, 0.8f, 1.1f},
    {"Brass",      1, 0.9f, 1, 0, 5,   0.f,  0.f,  0.f, 0.f, 0.5f, 1, 0,   1, 0.f, 0.f, 800,  1.1f, 2.6f, 0.25f, 0.5f, 0, 0, 4, false, 0, 0.07f, 0.3f, 0.8f, 0.2f},
    {"Pulse Pad",  2, 0.8f, 2, 0, 11,  0.5f, 0.f,  0.f, 0.f, 0.4f, 1, 0,   1, 0.f, 0.f, 1800, 0.9f, 0.6f, 1.0f,  0.3f, 0, 0, 4, false, 0, 0.5f,  0.7f, 0.85f, 1.0f},
    {"FM Piano",   0, 1.0f, 0, 12, 0,  0.1f, 0.f,  0.f, 0.f, 0.5f, 1, 0,   1, 1.6f, 0.7f, 9000, 0.7f, 0.f, 0.1f,  0.f, 0, 0, 0, false, 0, 0.003f, 1.4f, 0.0f, 0.3f},
    {"FM Bell",    0, 1.0f, 0, 0, 0,   0.f,  0.f,  0.f, 0.f, 0.5f, 1, 0,   3.5f,3.0f,1.6f, 9000, 0.7f, 0.f, 0.1f,  0.f, 0, 0, 0, false, 0, 0.002f, 2.4f, 0.0f, 0.8f},
    {"Organ",      0, 1.0f, 0, 12, 0,  0.7f, 0.5f, 0.f, 0.f, 0.5f, 1, 0,   1, 0.f, 0.f, 6000, 0.7f, 0.f, 0.1f,  0.f, 0, 0, 5, false, 0, 0.005f, 0.1f, 1.0f, 0.06f},
    {"Pluck",      0, 1.0f, 0, 0, 0,   0.f,  0.f,  0.f, 0.f, 0.5f, 1, 0,   1, 0.f, 0.f, 9000, 0.7f, 0.f, 0.1f,  0.f, 0, 0, 0, true,  0.12f, 0.002f, 1.5f, 0.0f, 0.3f},
    {"Harp",       0, 1.0f, 0, 0, 0,   0.f,  0.f,  0.f, 0.f, 0.5f, 1, 0,   1, 0.f, 0.f, 9000, 0.7f, 0.f, 0.1f,  0.f, 0, 0, 0, true,  0.45f, 0.002f, 3.0f, 0.0f, 0.6f},
    {"Muted Bass", 0, 1.0f, 0, 0, 0,   0.f,  0.f,  0.f, 0.f, 0.5f, 1, 0,   1, 0.f, 0.f, 9000, 0.7f, 0.f, 0.1f,  0.f, 0, 0, 0, true,  0.02f, 0.002f, 0.8f, 0.0f, 0.15f},
    {"Wind",       1, 0.f,  0, 0, 0,   0.f,  1.0f, 0.f, 0.f, 0.5f, 1, 0,   1, 0.f, 0.f, 700,  5.0f, 1.5f, 1.5f,  0.f, 0, 0, 0, false, 0, 0.9f,  1.0f, 0.9f, 1.5f},
    {"Zap Kick",   0, 1.0f, 0, 0, 0,   0.f,  0.15f,0.02f,0.f,0.5f, 1, 0,   1, 0.f, 0.f, 5000, 0.7f, 0.f, 0.1f,  0.f, 30, 0.05f, 0, false, 0, 0.001f, 0.35f, 0.0f, 0.1f},
    {"Laser Zap",  1, 0.8f, 2, 0, 5,   0.f,  0.f,  0.f, 0.f, 0.5f, 1, 0,   1, 0.f, 0.f, 3000, 2.5f, 1.5f, 0.2f,  0.f, 36, 0.15f, 0, false, 0, 0.001f, 0.3f, 0.0f, 0.1f},
    {"Hoover",     1, 1.0f, 1, 0, 0,   0.f,  0.f,  0.f, 0.f, 0.5f, 5, 38,  1, 0.f, 0.f, 2800, 2.0f, 1.0f, 0.5f,  0.2f, 5, 0.12f, 6, false, 0, 0.01f, 0.4f, 0.85f, 0.3f},
};
constexpr int kSynthCount = (int)(sizeof(kSynths) / sizeof(kSynths[0]));

class SynthVoice {
public:
    void start(const SynthPatch& p, int note, float vel, float sr, uint32_t seed) {
        *this = SynthVoice();
        p_ = &p; sr_ = sr; note_ = (float)note; vel_ = vel; rng_ = seed * 2654435761u + 12345u;
        for (int k = 0; k < 5; k++) ph_[k] = rnd() ;   // free-running phases: a stack does not sound like one thick oscillator
        if (p.pluck) {
            const int n = std::min(kDl - 2, (int)(sr / freq(0.f)));
            float z = 0.f;
            for (int i = 0; i < n; i++) { const float x = rnd() * 2.f - 1.f; z += 0.55f * (x - z); dl_[i] = z; }   // a little low-passed noise burst
            plN_ = std::max(2.f, sr / freq(0.f) - 0.5f);
            plDec_ = std::pow(std::max(0.001f, p.pluckDec), 1.f / freq(0.f));
            plW_ = n;
        }
        lp_.reset(); coefCount_ = 0;
    }
    // adds one sample (mono -> l, r)
    void next(float bend, float& l, float& r) {
        const SynthPatch& p = *p_;
        const float dt1 = 1.f / sr_;
        t_ += dt1;
        const float vibDeg = p.vib > 0.f ? p.vib * std::min(1.f, t_ / 0.4f) * std::sin(2.f * kPi * 5.2f * t_) : 0.f;
        const float pe = p.pitchEnv > 0.f ? p.pitchEnv * std::exp(-t_ / p.pitchDec) : 0.f;
        const float f = freq(bend + pe + vibDeg / 100.f);
        float oL = 0.f, oR = 0.f;
        if (p.pluck) {
            // read the string plN_ samples back (linear interpolation), damp, write
            float rp = (float)plW_ - plN_; if (rp < 0) rp += kDl;
            const int i0 = (int)rp, i1 = (i0 + 1) % kDl; const float fr = rp - i0;
            const float x = dl_[i0 % kDl] * (1.f - fr) + dl_[i1] * fr;
            const float y = 0.5f * (x + plZ_); plZ_ = x;
            dl_[plW_] = y * plDec_; plW_ = (plW_ + 1) % kDl;
            oL = oR = x * 1.4f;
        } else {
            const float dt = f * dt1;
            if (p.fmIdx > 0.f && p.w1 == 0) {
                const float idx = p.fmIdx * (p.fmDec > 0.f ? std::exp(-t_ / p.fmDec) : 1.f) * (0.5f + 0.5f * vel_);
                phm_ += dt * p.fmRatio; phm_ -= std::floor(phm_);
                ph_[0] += dt; ph_[0] -= std::floor(ph_[0]);
                oL = oR = std::sin(2.f * kPi * (ph_[0] + idx * std::sin(2.f * kPi * phm_))) * p.l1;
            } else if (p.uni > 1) {
                const int n = std::min(5, p.uni);
                for (int k = 0; k < n; k++) {
                    const float x = n > 1 ? (float)k / (n - 1) * 2.f - 1.f : 0.f;   // -1 .. 1
                    const float d = dt * std::pow(2.f, x * p.uniDet / 1200.f);
                    ph_[k] += d; ph_[k] -= std::floor(ph_[k]);
                    const float w = wave(p.w1, ph_[k], d, p.pw) * p.l1 / std::sqrt((float)n);
                    const float pan = 0.5f + 0.5f * x;   // spread over the stereo field
                    oL += w * (1.f - pan * 0.8f); oR += w * (0.2f + pan * 0.8f);
                }
            } else {
                ph_[0] += dt; ph_[0] -= std::floor(ph_[0]);
                oL = oR = wave(p.w1, ph_[0], dt, p.pw) * p.l1;
            }
            if (p.l2 > 0.f) {
                const float d2 = dt * std::pow(2.f, (p.semi2 + p.det2 / 100.f) / 12.f);
                ph2_ += d2; ph2_ -= std::floor(ph2_);
                const float w = wave(p.w2, ph2_, d2, p.pw) * p.l2; oL += w; oR += w;
            }
            if (p.sub > 0.f) { phs_ += dt * 0.5f; phs_ -= std::floor(phs_); const float w = std::sin(2.f * kPi * phs_) * p.sub; oL += w; oR += w; }
            if (p.noise > 0.f) { const float w = (rnd() * 2.f - 1.f) * p.noise * (p.noiseDec > 0.f ? std::exp(-t_ / p.noiseDec) : 1.f); oL += w; oR += w; }
        }
        // filter: the coefficients are renewed every 8 samples
        if (!p.pluck) {
            if ((coefCount_++ & 7) == 0) {
                const float env = p.fdec > 0.f ? std::exp(-t_ / p.fdec) : 0.f;
                const float hz = p.cut * std::pow(2.f, p.fenv * env + p.key * (note_ - 60.f) / 12.f + (vel_ - 0.7f) * 0.8f);
                lp_.set(hz, sr_, p.res);
            }
            oL = lp_.process(0, oL); oR = lp_.process(1, oR);
        }
        l = oL; r = oR;
    }

private:
    static constexpr int kDl = 2048;
    float freq(float semis) const { return 440.f * std::pow(2.f, (note_ + semis - 69.f) / 12.f); }
    float rnd() { rng_ = rng_ * 1664525u + 1013904223u; return (rng_ >> 8) / 16777216.f; }
    static float blep(float t, float dt) {   // band-limits the saw / pulse edges
        if (t < dt) { t /= dt; return t + t - t * t - 1.f; }
        if (t > 1.f - dt) { t = (t - 1.f) / dt; return t * t + t + t + 1.f; }
        return 0.f;
    }
    static float wave(int w, float ph, float dt, float pw) {
        switch (w) {
            case 1: return 2.f * ph - 1.f - blep(ph, dt);
            case 2: { float s = ph < pw ? 1.f : -1.f; s += blep(ph, dt); float q = ph - pw; q -= std::floor(q); s -= blep(q, dt); return s; }
            case 3: return 4.f * std::fabs(ph - 0.5f) - 1.f;
            default: return std::sin(2.f * kPi * ph);
        }
    }
    const SynthPatch* p_ = nullptr;
    float sr_ = 44100.f, note_ = 60.f, vel_ = 1.f, t_ = 0.f, ph_[5] = {}, ph2_ = 0.f, phs_ = 0.f, phm_ = 0.f;
    Lowpass lp_; int coefCount_ = 0; uint32_t rng_ = 1;
    float dl_[kDl] = {}; int plW_ = 0; float plN_ = 100.f, plDec_ = 0.99f, plZ_ = 0.f;
};
