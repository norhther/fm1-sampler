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
    int engine = 0;                         // 0 oscillators + filter (above), 1 wavetable, 2 modal (struck bar / bell), 3 drum kit
    float wtPos = 0, wtEnv = 0, wtDec = 0.5f;   // wavetable: frame 0..1, how far the envelope moves it, its decay in s
    int kit = 0;                            // drum kit: 1 808, 2 punch, 3 lo-fi (16 pads, note 60 + pad)
    float mr[6] = {}, ma[6] = {}, md[6] = {};   // modal: partial ratios, levels, decay times in s
};

// the four macro knobs of a synth (-1 .. 1, 0 = the patch as it is): TONE brightens, BODY adds weight and length, MOTION adds movement, GRIT adds drive
struct SynthMacros { float tone = 0, body = 0, motion = 0, grit = 0; };

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
    // ---- wavetable ----  (engine 1: osc 1 reads a table of 8 morphing frames; wtPos is where it sits, the envelope moves it)
    {"Wave Pad",   0, 1.0f, 0, 0, 0,   0.f,  0.f,  0.f, 0.f, 0.5f, 1, 0,   1, 0.f, 0.f, 5000, 0.8f, 0.5f, 1.2f,  0.3f, 0, 0, 3, false, 0, 0.45f, 0.8f, 0.85f, 1.2f,  1, 0.15f, 0.6f, 1.5f},
    {"Wave Lead",  0, 1.0f, 0, 0, 0,   0.f,  0.f,  0.f, 0.f, 0.5f, 1, 0,   1, 0.f, 0.f, 6000, 1.0f, 0.8f, 0.4f,  0.4f, 0, 0, 6, false, 0, 0.01f, 0.3f, 0.8f, 0.2f,  1, 0.35f, 0.3f, 0.4f},
    {"Vocal Wave", 0, 1.0f, 0, 0, 0,   0.f,  0.f,  0.f, 0.f, 0.5f, 1, 0,   1, 0.f, 0.f, 7000, 0.8f, 0.f, 0.5f,   0.2f, 0, 0, 4, false, 0, 0.12f, 0.3f, 0.9f, 0.35f, 1, 0.55f, 0.15f, 0.6f},
    {"Wave Bass",  0, 1.0f, 0, 0, 0,   0.f,  0.5f, 0.f, 0.f, 0.5f, 1, 0,   1, 0.f, 0.f, 1800, 1.1f, 1.5f, 0.3f,  0.2f, 0, 0, 0, false, 0, 0.004f, 0.3f, 0.7f, 0.15f, 1, 0.25f, 0.4f, 0.25f},
    // ---- modal ----  (engine 2: a bar or bell struck once; partial ratios, levels and decays in the last three lists)
    {"Marimba",    0, 1.0f, 0, 0, 0,   0.f,  0.f,  0.f, 0.f, 0.5f, 1, 0,   1, 0.f, 0.f, 9000, 0.7f, 0.f, 0.1f,  0.f, 0, 0, 0, false, 0, 0.002f, 0.2f, 1.0f, 0.25f, 2, 0.f, 0.f, 0.5f, 0, {1.f, 3.93f, 9.3f, 15.6f}, {1.f, 0.5f, 0.22f, 0.1f}, {0.7f, 0.25f, 0.1f, 0.05f}},
    {"Vibes",      0, 1.0f, 0, 0, 0,   0.f,  0.f,  0.f, 0.f, 0.5f, 1, 0,   1, 0.f, 0.f, 9000, 0.7f, 0.f, 0.1f,  0.f, 0, 0, 4, false, 0, 0.002f, 0.2f, 1.0f, 0.3f, 2, 0.f, 0.f, 0.5f, 0, {1.f, 4.f, 10.f}, {1.f, 0.4f, 0.12f}, {3.5f, 1.2f, 0.5f}},
    {"Glockenspiel",0,1.0f, 0, 0, 0,   0.f,  0.f,  0.f, 0.f, 0.5f, 1, 0,   1, 0.f, 0.f, 9000, 0.7f, 0.f, 0.1f,  0.f, 0, 0, 0, false, 0, 0.001f, 0.2f, 1.0f, 0.3f, 2, 0.f, 0.f, 0.5f, 0, {2.f, 5.52f, 10.8f, 17.86f}, {1.f, 0.6f, 0.35f, 0.2f}, {1.8f, 1.1f, 0.7f, 0.4f}},
    {"Tubular Bell",0,1.0f, 0, 0, 0,   0.f,  0.f,  0.f, 0.f, 0.5f, 1, 0,   1, 0.f, 0.f, 9000, 0.7f, 0.f, 0.1f,  0.f, 0, 0, 0, false, 0, 0.001f, 0.2f, 1.0f, 0.6f, 2, 0.f, 0.f, 0.5f, 0, {0.5f, 1.f, 1.19f, 1.56f, 2.f, 2.51f}, {0.5f, 1.f, 0.7f, 0.6f, 0.5f, 0.35f}, {5.f, 4.f, 3.5f, 3.f, 2.5f, 2.f}},
    {"Kalimba",    0, 1.0f, 0, 0, 0,   0.f,  0.f,  0.f, 0.f, 0.5f, 1, 0,   1, 0.f, 0.f, 9000, 0.7f, 0.f, 0.1f,  0.f, 0, 0, 0, false, 0, 0.001f, 0.2f, 1.0f, 0.2f, 2, 0.f, 0.f, 0.5f, 0, {1.f, 5.4f, 12.f}, {1.f, 0.35f, 0.08f}, {1.2f, 0.18f, 0.06f}},
    // ---- drum kits ----  (engine 3: 16 pads, note 60 + pad: kick, long kick, snare, snare 2, clap, rim, hat, open hat, tom lo / mid / hi, cowbell, clave, shaker, crash, zap)
    {"Synth Kit 808",  0, 1.0f, 0, 0, 0,   0.f, 0.f, 0.f, 0.f, 0.5f, 1, 0,   1, 0.f, 0.f, 9000, 0.7f, 0.f, 0.1f,  0.f, 0, 0, 0, false, 0, 0.001f, 0.01f, 1.0f, 0.03f, 3, 0.f, 0.f, 0.5f, 1},
    {"Synth Kit Punch",0, 1.0f, 0, 0, 0,   0.f, 0.f, 0.f, 0.f, 0.5f, 1, 0,   1, 0.f, 0.f, 9000, 0.7f, 0.f, 0.1f,  0.f, 0, 0, 0, false, 0, 0.001f, 0.01f, 1.0f, 0.03f, 3, 0.f, 0.f, 0.5f, 2},
    {"Synth Kit Lo-Fi",0, 1.0f, 0, 0, 0,   0.f, 0.f, 0.f, 0.f, 0.5f, 1, 0,   1, 0.f, 0.f, 9000, 0.7f, 0.f, 0.1f,  0.f, 0, 0, 0, false, 0, 0.001f, 0.01f, 1.0f, 0.03f, 3, 0.f, 0.f, 0.5f, 3},
};
constexpr int kSynthCount = (int)(sizeof(kSynths) / sizeof(kSynths[0]));

// The 8 frames of the wavetable (2048 points, two sets: full and for high notes), built once from harmonic recipes.
struct WaveTable {
    static constexpr int kFrames = 8, kLen = 2048;
    float full[kFrames][kLen], lite[kFrames][kLen];
    WaveTable() {
        for (int f = 0; f < kFrames; f++) for (int set = 0; set < 2; set++) {
            const int maxH = set == 0 ? 40 : 10; float* t = set == 0 ? full[f] : lite[f];
            std::fill(t, t + kLen, 0.f);
            for (int h = 1; h <= maxH; h++) {
                float amp = 0.f; const float hf = (float)h;
                switch (f) {
                    case 0: amp = h == 1 ? 1.f : 0.f; break;                                                        // sine
                    case 1: amp = h == 1 ? 1.f : h == 2 ? 0.45f : h == 3 ? 0.2f : 0.f; break;                      // a little body
                    case 2: amp = 1.f / hf; break;                                                                  // saw
                    case 3: amp = (h & 1) ? 1.f / hf : 0.f; break;                                                  // square
                    case 4: amp = std::exp(-0.5f * std::pow((hf - 5.f) / 1.6f, 2.f)) + 0.7f * std::exp(-0.5f * std::pow((hf - 11.f) / 2.f, 2.f)); break;   // "ah"
                    case 5: amp = std::exp(-0.5f * std::pow((hf - 2.f) / 0.8f, 2.f)) + 0.6f * std::exp(-0.5f * std::pow((hf - 18.f) / 2.5f, 2.f)); break;   // "ee"
                    case 6: amp = h <= 16 ? 1.f / std::sqrt(hf) : 0.f; break;                                      // bright, buzzy
                    default: amp = (h % 3 == 1) ? 1.f / std::sqrt(hf) : 0.3f / hf; break;                           // hollow, metallic
                }
                const float ph = (f == 7 ? 0.9f * h * h : 0.f);
                for (int i = 0; i < kLen; i++) t[i] += amp * std::sin(2.f * kPi * ((float)h * i / kLen) + ph);
            }
            float pk = 1e-6f; for (int i = 0; i < kLen; i++) pk = std::max(pk, std::fabs(t[i]));
            for (int i = 0; i < kLen; i++) t[i] /= pk;
        }
    }
    static const WaveTable& get() { static const WaveTable w; return w; }
};

class SynthVoice {
public:
    void start(const SynthPatch& p, int note, float vel, float sr, uint32_t seed, const SynthMacros& m, float padPitch = 0.f, float padDecay = 1.f) {
        *this = SynthVoice();
        p_ = &p; sr_ = sr; note_ = (float)note; vel_ = vel; rng_ = seed * 2654435761u + 12345u;
        for (int k = 0; k < 5; k++) ph_[k] = rnd();   // free-running phases: a stack does not sound like one thick oscillator
        if (p.engine == 3) { pad_ = ((note - 60) % 16 + 16) % 16; padPitch_ = padPitch; padDecay_ = padDecay; return; }
        if (p.engine == 2) {
            const float f = freq(0.f);
            for (int k = 0; k < 6; k++) { mph_[k] = 0.f; mAmp_[k] = p.ma[k] > 0.f ? p.ma[k] : 0.f; }
            mf_ = f; return;
        }
        if (p.pluck) {
            const int n = std::min(kDl - 2, (int)(sr / freq(0.f)));
            float z = 0.f;
            for (int i = 0; i < n; i++) { const float x = rnd() * 2.f - 1.f; z += 0.55f * (x - z); dl_[i] = z; }   // a little low-passed noise burst
            plN_ = std::max(2.f, sr / freq(0.f) - 0.5f);
            plDec_ = std::pow(std::max(0.001f, std::pow(p.pluckDec, 1.f - 0.5f * m.body)), 1.f / freq(0.f));
            plW_ = n;
        }
        lp_.reset(); coefCount_ = 0;
    }
    bool finished() const { return done_; }
    // adds one sample (mono -> l, r)
    void next(float bend, const SynthMacros& m, float& l, float& r) {
        const SynthPatch& p = *p_;
        const float dt1 = 1.f / sr_;
        t_ += dt1;
        float oL = 0.f, oR = 0.f;
        if (p.engine == 3) drum(m, oL, oR);
        else if (p.engine == 2) modal(bend, m, oL, oR);
        else {
            const float vib = p.vib + std::max(0.f, m.motion) * 12.f;
            const float vibDeg = vib > 0.f ? vib * std::min(1.f, t_ / 0.4f) * std::sin(2.f * kPi * 5.2f * t_) : 0.f;
            const float pe = p.pitchEnv > 0.f ? p.pitchEnv * std::exp(-t_ / p.pitchDec) : 0.f;
            const float f = freq(bend + pe + vibDeg / 100.f);
            if (p.pluck) {
                float rp = (float)plW_ - plN_; if (rp < 0) rp += kDl;
                const int i0 = (int)rp, i1 = (i0 + 1) % kDl; const float fr = rp - (float)i0;
                const float x = dl_[i0 % kDl] * (1.f - fr) + dl_[i1] * fr;
                const float k = clampf(0.5f + 0.4f * m.tone, 0.15f, 0.95f);   // the loop filter: TONE opens or darkens the string
                const float y = k * x + (1.f - k) * plZ_; plZ_ = x;
                dl_[plW_] = y * plDec_; plW_ = (plW_ + 1) % kDl;
                oL = oR = x * 1.4f;
            } else {
                const float dt = f * dt1;
                if (p.engine == 1) {   // wavetable
                    ph_[0] += dt; ph_[0] -= std::floor(ph_[0]);
                    const float pos = clampf(p.wtPos + p.wtEnv * std::exp(-t_ / std::max(0.05f, p.wtDec)) + 0.25f * m.tone + 0.15f * m.motion * std::sin(2.f * kPi * 0.35f * t_), 0.f, 1.f) * (WaveTable::kFrames - 1);
                    const WaveTable& wt = WaveTable::get(); const int f0 = std::min(WaveTable::kFrames - 2, (int)pos); const float ff = pos - (float)f0;
                    const float idx = ph_[0] * WaveTable::kLen; const int i0 = (int)idx & (WaveTable::kLen - 1), i1 = (i0 + 1) & (WaveTable::kLen - 1); const float fi = idx - std::floor(idx);
                    const float (*tb)[WaveTable::kLen] = f > 500.f ? wt.lite : wt.full;
                    const float a = tb[f0][i0] + (tb[f0][i1] - tb[f0][i0]) * fi, b = tb[f0 + 1][i0] + (tb[f0 + 1][i1] - tb[f0 + 1][i0]) * fi;
                    oL = oR = (a + (b - a) * ff) * p.l1;
                } else if (p.fmIdx > 0.f && p.w1 == 0) {
                    const float idx = p.fmIdx * (1.f + 0.8f * m.tone) * (p.fmDec > 0.f ? std::exp(-t_ / p.fmDec) : 1.f) * (0.5f + 0.5f * vel_);
                    phm_ += dt * p.fmRatio; phm_ -= std::floor(phm_);
                    ph_[0] += dt; ph_[0] -= std::floor(ph_[0]);
                    oL = oR = std::sin(2.f * kPi * (ph_[0] + std::max(0.f, idx) * std::sin(2.f * kPi * phm_))) * p.l1;
                } else if (p.uni > 1) {
                    const int n = std::min(5, p.uni); const float spread = p.uniDet * (1.f + 1.2f * m.motion);
                    for (int k = 0; k < n; k++) {
                        const float x = n > 1 ? (float)k / (n - 1) * 2.f - 1.f : 0.f;   // -1 .. 1
                        const float d = dt * std::pow(2.f, x * std::max(0.f, spread) / 1200.f);
                        ph_[k] += d; ph_[k] -= std::floor(ph_[k]);
                        const float w = wave(p.w1, ph_[k], d, p.pw) * p.l1 / std::sqrt((float)n);
                        const float pan = 0.5f + 0.5f * x;   // spread over the stereo field
                        oL += w * (1.f - pan * 0.8f); oR += w * (0.2f + pan * 0.8f);
                    }
                } else {
                    ph_[0] += dt; ph_[0] -= std::floor(ph_[0]);
                    oL = oR = wave(p.w1, ph_[0], dt, p.pw) * p.l1;
                }
                const float body = std::max(-0.9f, m.body);
                if (p.l2 > 0.f) {
                    const float d2 = dt * std::pow(2.f, (p.semi2 + p.det2 / 100.f) / 12.f);
                    ph2_ += d2; ph2_ -= std::floor(ph2_);
                    const float w = wave(p.w2, ph2_, d2, p.pw) * p.l2 * (1.f + 0.7f * body); oL += w; oR += w;
                }
                const float sub = p.sub * (1.f + body) + (p.sub <= 0.f && body > 0.f ? 0.25f * body : 0.f);
                if (sub > 0.f) { phs_ += dt * 0.5f; phs_ -= std::floor(phs_); const float w = std::sin(2.f * kPi * phs_) * sub; oL += w; oR += w; }
                if (p.noise > 0.f) { const float w = (rnd() * 2.f - 1.f) * p.noise * (p.noiseDec > 0.f ? std::exp(-t_ / p.noiseDec) : 1.f); oL += w; oR += w; }
            }
            // filter: the coefficients are renewed every 8 samples
            if (!p.pluck) {
                if ((coefCount_++ & 7) == 0) {
                    const float env = p.fdec > 0.f ? std::exp(-t_ / p.fdec) : 0.f;
                    const float hz = p.cut * std::pow(2.f, p.fenv * (1.f + 0.6f * m.motion) * env + p.key * (note_ - 60.f) / 12.f + (vel_ - 0.7f) * 0.8f + 2.f * m.tone);
                    lp_.set(hz, sr_, p.res);
                }
                oL = lp_.process(0, oL); oR = lp_.process(1, oR);
            }
        }
        if (m.grit > 0.01f) { const float g = 1.f + 4.f * m.grit, mk = 1.f / std::tanh(g) * 0.9f; oL = std::tanh(oL * g) * mk; oR = std::tanh(oR * g) * mk; }   // GRIT: drive
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
    // a struck bar or bell: damped sines at the partial ratios, a short mallet tick; TONE tilts the partials, BODY lengthens them
    void modal(float bend, const SynthMacros& m, float& oL, float& oR) {
        const SynthPatch& p = *p_; const float f = mf_ * std::pow(2.f, bend / 12.f), decScale = std::pow(2.f, m.body) * std::pow(f / 261.6f, -0.35f);
        float sum = 0.f, sumR = 0.f, loud = 0.f;
        for (int k = 0; k < 6; k++) {
            if (p.ma[k] <= 0.f || p.mr[k] <= 0.f) continue;
            const float fk = f * p.mr[k]; if (fk > sr_ * 0.45f) continue;
            mph_[k] += fk / sr_; mph_[k] -= std::floor(mph_[k]);
            const float env = std::exp(-t_ / std::max(0.02f, p.md[k] * decScale)), a = p.ma[k] * env * std::pow(p.mr[k], 0.45f * m.tone);
            const float s = std::sin(2.f * kPi * mph_[k]) * a; loud = std::max(loud, env * p.ma[k]);
            (k & 1 ? sumR : sum) += s; (k & 1 ? sum : sumR) += s * 0.5f;   // odd partials lean right
        }
        const float tick = (rnd() * 2.f - 1.f) * std::exp(-t_ / 0.003f) * 0.25f * (1.f + std::max(0.f, m.tone));
        oL = (sum + tick) * 0.6f * (0.5f + 0.5f * vel_); oR = (sumR + tick) * 0.6f * (0.5f + 0.5f * vel_);
        if (t_ > 0.05f && loud < 0.0004f) done_ = true;
    }
    // drum kit voice: a pitched body (with a fall), noise through a high-pass, metallic squares, claps as a few noise bursts; table driven
    void drum(const SynthMacros& m, float& oL, float& oR) {
        struct D { float f0, f1, pdec, tone, noise, hp, dec, met, claps; };
        //                           f0    f1   pdec  tone noise   hp   dec   met  claps
        static const D d808[16] = {
            {140, 48, 0.07f, 1.0f, 0.02f, 0.05f, 0.55f, 0.f, 0},     // kick
            {120, 40, 0.12f, 1.0f, 0.f,   0.05f, 1.1f,  0.f, 0},     // long kick
            {220, 160, 0.03f, 0.6f, 0.9f, 0.35f, 0.2f,  0.f, 0},     // snare
            {260, 190, 0.02f, 0.5f, 0.8f, 0.5f,  0.14f, 0.f, 0},     // snare 2
            {0,   0,  0.f,   0.f,  1.0f, 0.45f, 0.22f, 0.f, 3},      // clap
            {1700, 1500, 0.01f, 0.7f, 0.5f, 0.6f, 0.04f, 0.f, 0},    // rim
            {0,   0,  0.f,   0.f,  0.6f, 0.85f, 0.045f, 0.9f, 0},    // closed hat
            {0,   0,  0.f,   0.f,  0.6f, 0.85f, 0.4f,  0.9f, 0},     // open hat
            {160, 85, 0.1f,  1.0f, 0.05f, 0.1f, 0.4f,  0.f, 0},      // tom lo
            {210, 120, 0.09f, 1.0f, 0.05f, 0.1f, 0.35f, 0.f, 0},     // tom mid
            {290, 170, 0.08f, 1.0f, 0.05f, 0.1f, 0.3f,  0.f, 0},     // tom hi
            {560, 560, 0.f,  0.0f, 0.f,   0.5f, 0.28f, 1.0f, 0},     // cowbell
            {2500, 2400, 0.01f, 0.8f, 0.f, 0.5f, 0.05f, 0.f, 0},     // clave
            {0,   0,  0.f,   0.f,  0.8f, 0.8f,  0.12f, 0.f, 0},      // shaker
            {0,   0,  0.f,   0.f,  0.7f, 0.8f,  1.2f,  0.8f, 0},     // crash
            {900, 120, 0.15f, 1.0f, 0.1f, 0.3f, 0.3f,  0.f, 0}};    // zap
        D d = d808[pad_];
        const int kit = p_->kit;
        if (kit == 2) { d.dec *= 0.6f; d.f0 *= 1.15f; d.noise *= 1.1f; d.pdec *= 0.7f; }          // punch: tighter and higher
        else if (kit == 3) { d.dec *= 1.3f; d.f0 *= 0.8f; d.f1 *= 0.85f; d.noise *= 1.4f; d.hp *= 0.6f; }   // lo-fi: lower, noisier, duller
        d.dec *= std::pow(2.f, 0.8f * m.body) * std::max(0.05f, padDecay_); const float pit = std::pow(2.f, 0.3f * m.tone + padPitch_ / 12.f); d.f0 *= pit; d.f1 *= pit; d.hp = clampf(d.hp + 0.25f * m.tone, 0.02f, 0.97f);
        float s = 0.f;
        if (d.tone > 0.f) {
            const float f = d.f1 + (d.f0 - d.f1) * std::exp(-t_ / std::max(0.003f, d.pdec));
            ph_[0] += f / sr_; ph_[0] -= std::floor(ph_[0]);
            s += std::sin(2.f * kPi * ph_[0]) * d.tone * std::exp(-t_ / d.dec);
        }
        if (d.noise > 0.f) {
            float env = std::exp(-t_ / (d.dec * 0.8f));
            if (d.claps > 0) { const float tt = t_ * 1000.f; float e = 0.f; for (int k = 0; k < d.claps; k++) { const float st = k * 11.f; if (tt >= st) e += std::exp(-(tt - st) / 3.f); } env = std::min(1.f, e) * (tt > 11.f * d.claps ? 1.f : 1.f) * (tt < 11.f * d.claps ? 1.f : std::exp(-(tt - 11.f * d.claps) / (d.dec * 700.f))); }
            const float n = rnd() * 2.f - 1.f; nlp_ += (1.f - 0.5f * d.hp) * 0.0f; hpz_ += d.hp * 0.9f * (n - hpz_);   // high-pass = input minus a low-passed copy
            s += (n - hpz_) * d.noise * env;
        }
        if (d.met > 0.f) {
            static const float r[6] = {1.f, 1.4471f, 1.617f, 1.9265f, 2.5028f, 2.6637f};
            float mt = 0.f; const float base = p_->kit == 3 ? 280.f : 330.f * (pad_ == 11 ? 1.7f : pad_ == 14 ? 1.1f : 1.f);
            for (int k = 0; k < 6; k++) { ph_[k % 5] += 0.f; mphd_[k] += base * r[k] / sr_; mphd_[k] -= std::floor(mphd_[k]); mt += mphd_[k] < 0.5f ? 1.f : -1.f; }
            s += mt / 6.f * d.met * std::exp(-t_ / (d.dec * (pad_ == 11 ? 1.f : 0.7f))) * (pad_ == 11 ? 0.8f : 0.5f);
        }
        if (t_ > 6.f * d.dec + 0.05f) done_ = true;
        s = std::tanh(s * 1.4f) * (0.4f + 0.6f * vel_);
        oL = oR = s;
    }
    const SynthPatch* p_ = nullptr;
    float sr_ = 44100.f, note_ = 60.f, vel_ = 1.f, t_ = 0.f, ph_[5] = {}, ph2_ = 0.f, phs_ = 0.f, phm_ = 0.f;
    Lowpass lp_; int coefCount_ = 0; uint32_t rng_ = 1; bool done_ = false;
    float dl_[kDl] = {}; int plW_ = 0; float plN_ = 100.f, plDec_ = 0.99f, plZ_ = 0.f;
    float mph_[6] = {}, mAmp_[6] = {}, mf_ = 261.6f;                            // modal
    int pad_ = 0; float padPitch_ = 0.f, padDecay_ = 1.f; float hpz_ = 0.f, nlp_ = 0.f, mphd_[6] = {};                  // drums
};
