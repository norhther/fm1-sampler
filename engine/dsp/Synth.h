// Built-in synthesizers: a table of patches played by one voice engine (no sample needed).
// A voice = oscillator 1 (optionally a stack of detuned saws, or an FM carrier) + oscillator 2 + sub + noise -> low-pass with its own envelope.
// A plucked patch uses a Karplus-Strong string instead of the oscillators. The amplitude envelope is the track's ADSR (the patch sets its defaults).
#pragma once
#include "Common.h"
#include "Filter.h"
#include "DungeonSynth.h"
#include "SloopKits.h"
#include "Acid303.h"
#include "Tr808Voice.h"
#include "Tr909Voice.h"
#include "AnimalSynth.h"
#include "Fm6Voice.h"
#include "FelVoice.h"

inline bool isKitEngine(int e) { return e == 3 || e == 8 || e == 10 || e == 11; }   // 16 pads, note 60 + pad
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
    float x[12] = {};                           // engines 5 gurdy, 6 wheel, 7 voice (GHOULBOX ports, dsp/DungeonSynth.h): their eight knobs; 9 the 303 (dsp/Acid303.h): its twelve pots
};
// a GHOULBOX-engine patch: engine 5 gurdy / 6 wheel / 7 voice, its eight knobs, the envelope (a d s r), and the filter that follows (cut Hz)
static inline SynthPatch dungeonPatch(const char* name, int engine, const float (&k)[8], float a, float d, float s, float r, float cut = 9000.f) {
    SynthPatch p{name, 0, 1.f, 0, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.5f, 1, 0.f, 1.f, 0.f, 0.f, cut, 0.7f, 0.f, 0.1f, 0.f, 0.f, 0.f, 0.f, false, 0.f, a, d, s, r, engine};
    for (int i = 0; i < 8; i++) p.x[i] = k[i];
    return p;
}

// the four macro knobs of a synth (-1 .. 1, 0 = the patch as it is): TONE brightens, BODY adds weight and length, MOTION adds movement, GRIT adds drive
struct SynthMacros { float tone = 0, body = 0, motion = 0, grit = 0; };
// what the user changed on top of a patch (SYNTH pages); the defaults leave the patch as it is
struct SynthEdit {
    int w1 = -1, w2 = -1;       // oscillator waves (-1 = the patch's): 0 sine 1 saw 2 square 3 triangle
    float det2 = 0.f;           // oscillator 2 detune, cents added
    float cutOct = 0.f, res = 0.f;   // filter cutoff in octaves, resonance added
    int ftype = 0;              // 0 low-pass, 1 high-pass, 2 band-pass
    int uni = -1;               // unison voices (-1 = the patch's)
    bool mono = false; float glide = 0.f;   // one voice at a time; time in s to slide from the last note
};
static inline SynthPatch dungeonOsc(const char* name, int w1, float l1, int w2, float semi2, float det2, float l2, float sub, float noise, int uni, float uniDet, float cut, float res, float fenv, float fdec, float key, float vib, float a, float d, float s, float r) {
    return SynthPatch{name, w1, l1, w2, semi2, det2, l2, sub, noise, 0.f, 0.5f, uni, uniDet, 1.f, 0.f, 0.f, cut, res, fenv, fdec, key, 0.f, 0.f, vib, false, 0.f, a, d, s, r};
}
static inline SynthPatch dungeonPluck(const char* name, float pluckDec, float d, float r) {
    return SynthPatch{name, 0, 1.f, 0, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.5f, 1, 0.f, 1.f, 0.f, 0.f, 9000.f, 0.7f, 0.f, 0.1f, 0.f, 0.f, 0.f, 0.f, true, pluckDec, 0.002f, d, 0.f, r};
}
static inline SynthPatch acidPatch(const char* name, const float (&k)[12], float a, float d, float s, float r) {   // X0X's 303: engine 9, its twelve pots
    SynthPatch p = dungeonOsc(name, 0, 1.f, 0, 0.f, 0.f, 0.f, 0.f, 0.f, 1, 0.f, 9000.f, 0.7f, 0.f, 0.1f, 0.f, 0.f, a, d, s, r);
    p.engine = 9; for (int i = 0; i < 12; i++) p.x[i] = k[i]; p.l1 = 1.4f; return p;
}
static inline SynthPatch animalPatch(const char* name, const float (&k)[8], float a, float d, float s, float r) {   // JIANT's ANIMAL: engine 12, its eight knobs
    SynthPatch p = dungeonOsc(name, 0, 1.f, 0, 0.f, 0.f, 0.f, 0.f, 0.f, 1, 0.f, 9000.f, 0.7f, 0.f, 0.1f, 0.f, 0.f, a, d, s, r);
    p.engine = 12; for (int i = 0; i < 8; i++) p.x[i] = k[i]; return p;
}
static inline SynthPatch fm6Patch(const char* name, int idx) {   // a six-operator FM patch (engine 13: dsp/Fm6Voice.h), x[0] = its number in dsp/Fm6Patches.h
    SynthPatch p = dungeonOsc(name, 0, 1.f, 0, 0.f, 0.f, 0.f, 0.f, 0.f, 1, 0.f, 9000.f, 0.7f, 0.f, 0.1f, 0.f, 0.f, 0.002f, 1.f, 1.f, 0.35f);
    p.engine = 13; p.x[0] = (float)idx; return p;
}
// Felucca-Engines: engine 14 ANALOG, 15 TRIO, 16 LOFI, 17 NOISE. x = the eight knobs as the Felucca editor has them (0..127); env = its A D S R, 0..127
static inline SynthPatch felPatch(const char* name, int engine, const float (&k)[8], const float (&e)[4], float l1 = 0.7f) {
    const auto t = [](float v, float lo, float span) { const float u = v / 127.f; return lo + u * u * span; };
    SynthPatch p = dungeonOsc(name, 0, 1.f, 0, 0.f, 0.f, 0.f, 0.f, 0.f, 1, 0.f, 9000.f, 0.7f, 0.f, 0.1f, 0.f, 0.f, t(e[0], 0.002f, 2.f), t(e[1], 0.03f, 3.f), e[2] / 127.f, t(e[3], 0.03f, 3.f));
    p.engine = engine; for (int i = 0; i < 8; i++) p.x[i] = k[i]; p.l1 = l1; return p;
}
static inline SynthPatch x0xKitPatch(const char* name, int engine, float gain = 1.f) {   // X0X's circuit-modelled kits: engine 10 = TR-808, 16 pads (note 60 + pad)
    SynthPatch p = dungeonOsc(name, 0, gain, 0, 0.f, 0.f, 0.f, 0.f, 0.f, 1, 0.f, 9000.f, 0.7f, 0.f, 0.1f, 0.f, 0.f, 0.001f, 0.01f, 1.f, 0.03f);
    p.engine = engine; p.kit = 20; return p;
}
static inline SynthPatch sloopKitPatch(const char* name, int kit) {   // SLOOP's synthesised kits: engine 8, 16 pads like the other kits (note 60 + pad)
    SynthPatch p = dungeonOsc(name, 0, 1.f, 0, 0.f, 0.f, 0.f, 0.f, 0.f, 1, 0.f, 9000.f, 0.7f, 0.f, 0.1f, 0.f, 0.f, 0.001f, 0.01f, 1.f, 0.03f);
    p.engine = 8; p.kit = kit + 1; return p;
}

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
    // ---- more patches (appended: a saved project names a synth by its number, so the old ones keep theirs) ----
    {"Neuro Bass", 1, 0.7f, 1, 0.f, 14.f, 0.7f, 0.3f, 0.f, 0.f, 0.5f, 1, 0.f, 1.f, 0.f, 0.f, 380.f, 3.5f, 2.2f, 0.35f, 0.25f, 0.f, 0.f, 0.f, false, 0.f, 0.004f, 0.4f, 0.8f, 0.1f},
    {"Pulse Bass", 2, 0.8f, 0, 0.f, 0.f, 0.f, 0.6f, 0.f, 0.f, 0.3f, 1, 0.f, 1.f, 0.f, 0.f, 450.f, 2.f, 2.6f, 0.2f, 0.3f, 0.f, 0.f, 0.f, false, 0.f, 0.003f, 0.25f, 0.6f, 0.1f},
    {"Mellow Bass", 3, 1.f, 0, 0.f, 0.f, 0.f, 0.5f, 0.f, 0.f, 0.5f, 1, 0.f, 1.f, 0.f, 0.f, 700.f, 0.8f, 1.2f, 0.25f, 0.3f, 0.f, 0.f, 0.f, false, 0.f, 0.006f, 0.3f, 0.8f, 0.15f},
    {"Sub Drop", 0, 1.f, 0, 0.f, 0.f, 0.f, 0.4f, 0.f, 0.f, 0.5f, 1, 0.f, 1.f, 0.f, 0.f, 900.f, 0.7f, 0.f, 0.1f, 0.f, 36.f, 0.35f, 0.f, false, 0.f, 0.001f, 0.9f, 0.f, 0.3f},
    {"Rave Stab", 1, 0.7f, 1, 7.f, 8.f, 0.5f, 0.f, 0.f, 0.f, 0.5f, 3, 14.f, 1.f, 0.f, 0.f, 1500.f, 1.2f, 2.8f, 0.22f, 0.4f, 0.f, 0.f, 0.f, false, 0.f, 0.003f, 0.25f, 0.15f, 0.15f},
    {"Funk Stab", 1, 0.8f, 2, 12.f, 0.f, 0.35f, 0.f, 0.f, 0.f, 0.5f, 1, 0.f, 1.f, 0.f, 0.f, 1100.f, 2.2f, 2.4f, 0.15f, 0.5f, 0.f, 0.f, 0.f, false, 0.f, 0.002f, 0.18f, 0.f, 0.1f},
    {"Trance Lead", 1, 0.9f, 2, 0.f, 4.f, 0.25f, 0.f, 0.f, 0.f, 0.5f, 5, 18.f, 1.f, 0.f, 0.f, 5200.f, 0.9f, 0.8f, 0.5f, 0.4f, 0.f, 0.f, 5.f, false, 0.f, 0.01f, 0.3f, 0.8f, 0.25f},
    {"Bit Lead", 2, 0.9f, 2, 12.f, 0.f, 0.3f, 0.f, 0.f, 0.f, 0.25f, 1, 0.f, 1.f, 0.f, 0.f, 7000.f, 0.7f, 0.f, 0.1f, 0.f, 0.f, 0.f, 8.f, false, 0.f, 0.004f, 0.2f, 0.7f, 0.12f},
    {"Reso Lead", 1, 0.8f, 1, 0.f, 6.f, 0.4f, 0.f, 0.f, 0.f, 0.5f, 1, 0.f, 1.f, 0.f, 0.f, 1800.f, 5.5f, 2.f, 0.3f, 0.4f, 0.f, 0.f, 4.f, false, 0.f, 0.006f, 0.3f, 0.8f, 0.2f},
    {"Whistle", 0, 0.9f, 0, 12.f, 0.f, 0.08f, 0.f, 0.03f, 0.f, 0.5f, 1, 0.f, 1.f, 0.f, 0.f, 6000.f, 0.7f, 0.f, 0.1f, 0.f, 0.f, 0.f, 14.f, false, 0.f, 0.08f, 0.2f, 0.9f, 0.3f},
    {"Space Pad", 1, 0.8f, 2, 0.f, 9.f, 0.4f, 0.3f, 0.f, 0.f, 0.5f, 5, 16.f, 1.f, 0.f, 0.f, 900.f, 2.4f, 1.8f, 2.5f, 0.3f, 0.f, 0.f, 3.f, false, 0.f, 0.55f, 1.f, 0.9f, 2.f},
    {"Sweep Pad", 1, 0.8f, 1, 7.f, 6.f, 0.5f, 0.f, 0.f, 0.f, 0.5f, 3, 10.f, 1.f, 0.f, 0.f, 400.f, 3.2f, 3.2f, 3.f, 0.2f, 0.f, 0.f, 0.f, false, 0.f, 0.5f, 1.5f, 0.8f, 1.6f},
    {"Ambient Drone", 1, 0.8f, 3, -12.f, 5.f, 0.6f, 0.f, 0.f, 0.f, 0.5f, 5, 25.f, 1.f, 0.f, 0.f, 600.f, 1.6f, 1.f, 3.f, 0.2f, 0.f, 0.f, 2.f, false, 0.f, 0.5f, 1.f, 1.f, 3.f},
    {"Detune Keys", 2, 0.8f, 1, 0.f, 12.f, 0.6f, 0.f, 0.f, 0.f, 0.35f, 1, 0.f, 1.f, 0.f, 0.f, 2800.f, 1.1f, 1.6f, 0.5f, 0.5f, 0.f, 0.f, 0.f, false, 0.f, 0.004f, 0.8f, 0.35f, 0.35f},
    {"Saw Pluck", 1, 0.9f, 1, 0.f, 7.f, 0.5f, 0.f, 0.f, 0.f, 0.5f, 1, 0.f, 1.f, 0.f, 0.f, 800.f, 1.6f, 3.f, 0.22f, 0.5f, 0.f, 0.f, 0.f, false, 0.f, 0.002f, 0.35f, 0.f, 0.2f},
    {"Riser", 1, 0.f, 0, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.5f, 1, 0.f, 1.f, 0.f, 0.f, 6000.f, 4.f, -4.f, 2.5f, 0.f, 0.f, 0.f, 0.f, false, 0.f, 0.45f, 1.f, 0.9f, 0.6f},
    {"Digi Bell", 1, 1.f, 0, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.5f, 1, 0.f, 1.f, 0.f, 0.f, 9000.f, 0.7f, 0.f, 0.1f, 0.f, 0.f, 0.f, 0.f, false, 0.f, 0.002f, 1.8f, 0.f, 0.8f, 1, 0.5f, 0.5f, 0.6f, 0},
    {"Choir Pad", 1, 1.f, 0, 0.f, 8.f, 0.3f, 0.f, 0.f, 0.f, 0.5f, 1, 0.f, 1.f, 0.f, 0.f, 5500.f, 0.7f, 0.f, 0.1f, 0.f, 0.f, 0.f, 4.f, false, 0.f, 0.35f, 0.8f, 0.9f, 1.4f, 1, 0.6f, 0.1f, 1.f, 0},
    {"Wave Pluck", 1, 1.f, 0, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.5f, 1, 0.f, 1.f, 0.f, 0.f, 6000.f, 0.9f, 0.f, 0.1f, 0.f, 0.f, 0.f, 0.f, false, 0.f, 0.002f, 0.5f, 0.f, 0.2f, 1, 0.1f, 0.6f, 0.15f, 0},
    {"Morph Lead", 1, 1.f, 0, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.5f, 1, 0.f, 1.f, 0.f, 0.f, 6000.f, 1.f, 0.6f, 0.4f, 0.f, 0.f, 0.f, 6.f, false, 0.f, 0.01f, 0.3f, 0.8f, 0.2f, 1, 0.2f, 0.6f, 1.2f, 0},
    {"Hollow Keys", 1, 1.f, 0, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.5f, 1, 0.f, 1.f, 0.f, 0.f, 4500.f, 0.8f, 1.f, 0.4f, 0.4f, 0.f, 0.f, 0.f, false, 0.f, 0.004f, 0.9f, 0.3f, 0.3f, 1, 0.95f, -0.35f, 0.5f, 0},
    {"Steel Drum", 1, 1.f, 0, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.5f, 1, 0.f, 1.f, 0.f, 0.f, 9000.f, 0.7f, 0.f, 0.1f, 0.f, 0.f, 0.f, 0.f, false, 0.f, 0.002f, 0.2f, 1.f, 0.3f, 2, 0.f, 0.f, 0.5f, 0, {1.f, 2.01f, 3.02f, 4.06f}, {1.f, 0.6f, 0.35f, 0.2f}, {1.4f, 0.9f, 0.5f, 0.3f}},
    {"Gamelan", 1, 1.f, 0, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.5f, 1, 0.f, 1.f, 0.f, 0.f, 9000.f, 0.7f, 0.f, 0.1f, 0.f, 0.f, 0.f, 0.f, false, 0.f, 0.002f, 0.2f, 1.f, 0.5f, 2, 0.f, 0.f, 0.5f, 0, {1.f, 2.76f, 5.4f, 8.93f, 13.34f}, {1.f, 0.7f, 0.5f, 0.3f, 0.2f}, {2.5f, 1.6f, 1.f, 0.6f, 0.4f}},
    {"Wood Block", 1, 1.f, 0, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.5f, 1, 0.f, 1.f, 0.f, 0.f, 9000.f, 0.7f, 0.f, 0.1f, 0.f, 0.f, 0.f, 0.f, false, 0.f, 0.001f, 0.2f, 1.f, 0.1f, 2, 0.f, 0.f, 0.5f, 0, {1.f, 2.3f, 4.1f}, {1.f, 0.5f, 0.25f}, {0.06f, 0.04f, 0.025f}},
    {"Singing Bowl", 1, 1.f, 0, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.5f, 1, 0.f, 1.f, 0.f, 0.f, 9000.f, 0.7f, 0.f, 0.1f, 0.f, 0.f, 0.f, 0.f, false, 0.f, 0.002f, 0.2f, 1.f, 1.5f, 2, 0.f, 0.f, 0.5f, 0, {1.f, 2.71f, 5.15f, 8.4f}, {1.f, 0.6f, 0.4f, 0.2f}, {6.f, 4.5f, 3.f, 2.f}},
    {"Xylophone", 1, 1.f, 0, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.5f, 1, 0.f, 1.f, 0.f, 0.f, 9000.f, 0.7f, 0.f, 0.1f, 0.f, 0.f, 0.f, 0.f, false, 0.f, 0.001f, 0.2f, 1.f, 0.1f, 2, 0.f, 0.f, 0.5f, 0, {1.f, 3.f, 6.f}, {1.f, 0.5f, 0.2f}, {0.35f, 0.12f, 0.06f}},
    {"Crotales", 1, 1.f, 0, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.5f, 1, 0.f, 1.f, 0.f, 0.f, 9000.f, 0.7f, 0.f, 0.1f, 0.f, 0.f, 0.f, 0.f, false, 0.f, 0.001f, 0.2f, 1.f, 0.4f, 2, 0.f, 0.f, 0.5f, 0, {2.f, 5.4f, 10.6f}, {1.f, 0.6f, 0.3f}, {2.2f, 1.1f, 0.5f}},
    {"Nylon Gtr", 0, 1.f, 0, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.5f, 1, 0.f, 1.f, 0.f, 0.f, 9000.f, 0.7f, 0.f, 0.1f, 0.f, 0.f, 0.f, 0.f, true, 0.22f, 0.002f, 2.f, 0.f, 0.5f},
    {"Steel Gtr", 0, 1.f, 0, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.5f, 1, 0.f, 1.f, 0.f, 0.f, 9000.f, 0.7f, 0.f, 0.1f, 0.f, 0.f, 0.f, 0.f, true, 0.35f, 0.002f, 2.5f, 0.f, 0.5f},
    {"Banjo", 0, 1.f, 0, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.5f, 1, 0.f, 1.f, 0.f, 0.f, 9000.f, 0.7f, 0.f, 0.1f, 0.f, 0.f, 0.f, 0.f, true, 0.15f, 0.002f, 1.2f, 0.f, 0.2f},
    {"Upright Bass", 0, 1.f, 0, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.5f, 1, 0.f, 1.f, 0.f, 0.f, 9000.f, 0.7f, 0.f, 0.1f, 0.f, 0.f, 0.f, 0.f, true, 0.06f, 0.002f, 1.f, 0.f, 0.2f},
    {"Synth Kit 909", 1, 1.f, 0, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.5f, 1, 0.f, 1.f, 0.f, 0.f, 9000.f, 0.7f, 0.f, 0.1f, 0.f, 0.f, 0.f, 0.f, false, 0.f, 0.001f, 0.01f, 1.f, 0.03f, 3, 0.f, 0.f, 0.5f, 4},
    {"Synth Kit Trap", 1, 1.f, 0, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.5f, 1, 0.f, 1.f, 0.f, 0.f, 9000.f, 0.7f, 0.f, 0.1f, 0.f, 0.f, 0.f, 0.f, false, 0.f, 0.001f, 0.01f, 1.f, 0.03f, 3, 0.f, 0.f, 0.5f, 5},
    {"PD Bass", 0, 1.f, 0, 0.f, 0.f, 0.f, 0.4f, 0.f, 0.f, 0.5f, 1, 0.f, 1.f, 0.f, 0.f, 1800.f, 1.f, 0.8f, 0.2f, 0.f, 0.f, 0.f, 0.f, false, 0.f, 0.003f, 0.3f, 0.8f, 0.1f, 4, 0.55f, 0.4f, 0.25f, 0},
    {"PD Lead", 1, 1.f, 0, 0.f, 7.f, 0.25f, 0.f, 0.f, 0.f, 0.5f, 1, 0.f, 1.f, 0.f, 0.f, 5000.f, 0.8f, 0.f, 0.1f, 0.f, 0.f, 0.f, 6.f, false, 0.f, 0.008f, 0.3f, 0.8f, 0.2f, 4, 0.5f, 0.3f, 0.5f, 0},
    {"PD Reso", 3, 1.f, 0, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.5f, 1, 0.f, 1.f, 0.f, 0.f, 7000.f, 0.7f, 0.f, 0.1f, 0.f, 0.f, 0.f, 0.f, false, 0.f, 0.01f, 0.4f, 0.7f, 0.3f, 4, 0.25f, 0.6f, 0.8f, 0},
    {"PD Pad", 0, 1.f, 0, 0.f, 9.f, 0.5f, 0.f, 0.f, 0.f, 0.5f, 1, 0.f, 1.f, 0.f, 0.f, 3500.f, 0.8f, 0.f, 0.1f, 0.f, 0.f, 0.f, 3.f, false, 0.f, 0.4f, 1.f, 0.85f, 1.1f, 4, 0.3f, 0.2f, 2.f, 0},
    {"PD Pluck", 2, 1.f, 0, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.5f, 1, 0.f, 1.f, 0.f, 0.f, 4000.f, 0.8f, 0.f, 0.1f, 0.f, 0.f, 0.f, 0.f, false, 0.f, 0.002f, 0.5f, 0.f, 0.2f, 4, 0.2f, 0.75f, 0.12f, 0},
    {"PD Bell", 3, 1.f, 0, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.5f, 1, 0.f, 1.f, 0.f, 0.f, 9000.f, 0.7f, 0.f, 0.1f, 0.f, 0.f, 0.f, 0.f, false, 0.f, 0.002f, 2.f, 0.f, 0.8f, 4, 0.8f, -0.5f, 1.2f, 0},
    // ---- GHOULBOX engines (engine 5 gurdy, 6 wheel organ, 7 formant voice: dsp/DungeonSynth.h) and its dungeon patches ----
    dungeonPatch("Hurdy Gurdy", 5, {2.f, 90.f, 50.f, 80.f, 85.f, 2.f, 70.f, 40.f}, 0.005f, 0.86f, 1.00f, 0.13f),
    dungeonPatch("Drone Wheel", 5, {2.f, 127.f, 110.f, 45.f, 0.f, 0.f, 105.f, 70.f}, 0.046f, 0.86f, 1.00f, 1.13f),
    dungeonPatch("Dance Gurdy", 5, {2.f, 45.f, 0.f, 120.f, 127.f, 4.f, 35.f, 20.f}, 0.003f, 0.86f, 1.00f, 0.07f),
    dungeonPatch("Trompette", 5, {2.f, 60.f, 0.f, 95.f, 110.f, 0.f, 60.f, 30.f}, 0.004f, 0.86f, 1.00f, 0.13f),
    dungeonPatch("Vielle", 5, {2.f, 0.f, 0.f, 70.f, 0.f, 0.f, 100.f, 45.f}, 0.020f, 0.86f, 0.94f, 0.38f),
    dungeonPatch("Bagpipe", 5, {2.f, 127.f, 90.f, 127.f, 0.f, 0.f, 25.f, 0.f}, 0.004f, 0.86f, 1.00f, 0.09f),
    dungeonPatch("Full Organ", 6, {15.f, 0.f, 0.f, 0.f, 0.f, 30.f, 20.f, 1.f}, 0.003f, 0.86f, 1.00f, 0.15f),
    dungeonPatch("Jazz Perc Organ", 6, {4.f, 0.f, 0.f, 0.f, 2.f, 50.f, 8.f, 1.f}, 0.003f, 0.86f, 1.00f, 0.13f),
    dungeonPatch("Gospel Organ", 6, {6.f, 0.f, 0.f, 0.f, 1.f, 60.f, 40.f, 2.f}, 0.003f, 0.86f, 1.00f, 0.15f),
    dungeonPatch("Soft Flute Organ", 6, {1.f, 0.f, -2.f, 0.f, 0.f, 10.f, 0.f, 1.f}, 0.003f, 0.86f, 1.00f, 0.22f),
    dungeonPatch("Rock Drive", 6, {7.f, 0.f, 0.f, 0.f, 0.f, 70.f, 100.f, 2.f}, 0.003f, 0.86f, 1.00f, 0.13f),
    dungeonPatch("Crypt Organ", 6, {12.f, 2.f, 0.f, -3.f, 0.f, 5.f, 15.f, 1.f}, 0.009f, 0.86f, 1.00f, 0.38f),
    dungeonPatch("Cathedral", 6, {15.f, 3.f, 0.f, -2.f, 0.f, 0.f, 10.f, 0.f}, 0.015f, 0.86f, 1.00f, 0.78f),
    dungeonPatch("Harmonium", 6, {10.f, 0.f, 1.f, -4.f, 0.f, 10.f, 25.f, 1.f}, 0.007f, 0.86f, 1.00f, 0.18f),
    dungeonPatch("Tower Flute", 6, {0.f, 0.f, -3.f, -6.f, 0.f, 0.f, 30.f, 1.f}, 0.012f, 0.86f, 1.00f, 0.26f),
    dungeonPatch("Choir Aah", 7, {0.f, 0.f, 0.f, 0.f, 40.f, 22.f, 60.f, 0.f}, 0.305f, 1.12f, 0.91f, 0.94f),
    dungeonPatch("Vox Lead", 7, {32.f, 0.f, 0.f, 2.f, 90.f, 8.f, 72.f, 0.f}, 0.004f, 0.92f, 0.83f, 0.22f),
    dungeonPatch("Wow Bass", 7, {95.f, 0.f, 68.f, 0.f, 100.f, 0.f, 80.f, 0.f}, 0.003f, 0.92f, 0.55f, 0.09f),
    dungeonPatch("Whisper", 7, {0.f, 95.f, 88.f, 3.f, 50.f, 120.f, 50.f, 0.f}, 0.046f, 1.12f, 0.87f, 0.78f),
    dungeonPatch("Monks", 7, {105.f, 120.f, 0.f, -5.f, 75.f, 18.f, 70.f, 6.f}, 0.135f, 1.12f, 0.94f, 1.13f),
    dungeonPatch("Crypt Choir", 7, {95.f, 0.f, 100.f, -2.f, 45.f, 30.f, 55.f, 10.f}, 0.691f, 1.12f, 0.94f, 1.62f),
    dungeonPatch("Wraith", 7, {127.f, 64.f, 110.f, 0.f, 20.f, 100.f, 40.f, 30.f}, 1.190f, 1.12f, 0.87f, 1.94f),
    dungeonPatch("Low Drone", 7, {127.f, 95.f, 0.f, -8.f, 95.f, 15.f, 85.f, 4.f}, 0.401f, 1.12f, 1.00f, 1.62f),
    // ---- GHOULBOX dungeon sounds on the oscillator engine, strings and noise (dungeonOsc: wave 0 sine 1 saw 2 square 3 tri) ----
    dungeonOsc("Crypt Pad", 1, 1.f, 3, 0.f, 8.f, 0.6f, 0.f, 0.f, 3, 14.f, 1400.f, 0.8f, 0.4f, 1.5f, 0.3f, 3.f, 0.9f, 0.9f, 0.85f, 1.6f),
    dungeonOsc("Frost Strings", 1, 1.f, 1, 0.f, 6.f, 0.5f, 0.f, 0.f, 5, 12.f, 3200.f, 0.8f, 0.3f, 1.2f, 0.4f, 5.f, 0.5f, 0.9f, 0.85f, 1.2f),
    dungeonOsc("War Horn", 1, 0.9f, 1, 0.f, 6.f, 0.5f, 0.f, 0.f, 1, 0.f, 900.f, 1.1f, 2.4f, 0.3f, 0.5f, 4.f, 0.06f, 0.3f, 0.8f, 0.4f),
    dungeonOsc("Dirge Bass", 1, 0.8f, 2, 0.f, 4.f, 0.4f, 0.5f, 0.f, 1, 0.f, 420.f, 1.2f, 1.8f, 0.4f, 0.3f, 0.f, 0.01f, 0.4f, 0.8f, 0.3f),
    dungeonOsc("Fantasy Pad", 1, 0.9f, 3, 12.f, 7.f, 0.5f, 0.f, 0.f, 3, 14.f, 2200.f, 0.9f, 0.6f, 1.4f, 0.3f, 4.f, 0.8f, 0.9f, 0.85f, 1.5f),
    dungeonOsc("Mourn Horn", 1, 0.9f, 1, -12.f, 5.f, 0.5f, 0.f, 0.f, 1, 0.f, 1100.f, 1.2f, 1.6f, 0.4f, 0.5f, 5.f, 0.12f, 0.3f, 0.8f, 0.5f),
    dungeonOsc("String Machine", 1, 1.f, 1, 12.f, 18.f, 0.6f, 0.f, 0.f, 3, 16.f, 3500.f, 0.7f, 0.f, 0.1f, 0.2f, 4.f, 0.25f, 0.5f, 0.9f, 0.8f),
    dungeonOsc("Shawm", 1, 0.9f, 1, 12.f, 4.f, 0.6f, 0.f, 0.f, 1, 0.f, 2600.f, 3.5f, 0.8f, 0.2f, 0.6f, 5.f, 0.01f, 0.2f, 0.9f, 0.15f),
    dungeonOsc("Tower Lead", 2, 0.9f, 2, 0.f, 6.f, 0.3f, 0.f, 0.f, 1, 0.f, 4200.f, 0.9f, 0.5f, 0.3f, 0.4f, 6.f, 0.005f, 0.2f, 0.85f, 0.2f),
    // wind and fire: filtered noise
    dungeonOsc("Cave Wind", 1, 0.f, 0, 0.f, 0.f, 0.f, 0.f, 1.f, 1, 0.f, 600.f, 6.f, 1.2f, 2.f, 0.3f, 0.f, 1.2f, 1.f, 0.9f, 2.f),
    dungeonOsc("Torch", 1, 0.f, 0, 0.f, 0.f, 0.f, 0.f, 0.8f, 1, 0.f, 1800.f, 2.5f, 0.8f, 0.15f, 0.f, 0.f, 0.05f, 0.4f, 0.8f, 0.5f),
    // GHOULBOX's plucked strings (the Karplus-Strong voice): lute, harpsichord, dungeon harp
    dungeonPluck("Lute", 0.30f, 2.2f, 0.4f),
    dungeonPluck("Harpsichord", 0.55f, 1.6f, 0.3f),
    dungeonPluck("Dungeon Harp", 0.6f, 3.5f, 0.8f),
    // ---- SLOOP's synthesised drum kits (engine 8: dsp/SloopDrum.h, tables from tools/gen_sloop_kits.py) ----
    sloopKitPatch("Sloop 808", 0),
    sloopKitPatch("Sloop 909", 1),
    sloopKitPatch("Sloop 606", 2),
    sloopKitPatch("Sloop 80S", 3),
    sloopKitPatch("Sloop VINTAGE", 4),
    sloopKitPatch("Sloop TRAP", 5),
    sloopKitPatch("Sloop DRILL", 6),
    sloopKitPatch("Sloop BOOMBAP", 7),
    sloopKitPatch("Sloop LO-FI", 8),
    sloopKitPatch("Sloop PHONK", 9),
    sloopKitPatch("Sloop HOUSE", 10),
    sloopKitPatch("Sloop D.HOUSE", 11),
    sloopKitPatch("Sloop TECHNO", 12),
    sloopKitPatch("Sloop MINIMAL", 13),
    sloopKitPatch("Sloop ELECTRO", 14),
    sloopKitPatch("Sloop DISCO", 15),
    sloopKitPatch("Sloop GARAGE", 16),
    sloopKitPatch("Sloop JUNGLE", 17),
    sloopKitPatch("Sloop DUBSTEP", 18),
    sloopKitPatch("Sloop DEMBOW", 19),
    sloopKitPatch("Sloop AMAPIANO", 20),
    sloopKitPatch("Sloop AFRO", 21),
    sloopKitPatch("Sloop LATIN", 22),
    sloopKitPatch("Sloop TRIBAL", 23),
    sloopKitPatch("Sloop SYNTHWV", 24),
    sloopKitPatch("Sloop CHIP", 25),
    sloopKitPatch("Sloop ARCADE", 26),
    sloopKitPatch("Sloop GLITCH", 27),
    sloopKitPatch("Sloop INDUSTR", 28),
    sloopKitPatch("Sloop HYPER", 29),
    sloopKitPatch("Sloop AMBIENT", 30),
    sloopKitPatch("Sloop JAZZ", 31),
    // ---- X0X's TB-303 (engine 9: dsp/Acid303.h, dsp/bass303.cpp), pots: CUTOFF RESO ENVMOD DECAY ACCENT WAVE TUNE VOLUME DRIVE DRVTYPE SLIDE ACCDEC ----
    acidPatch("303 Classic",  {64, 90, 90, 40, 90, 0, 64, 96, 0, 1, 21, 7}, 0.002f, 0.4f, 0.9f, 0.08f),
    acidPatch("303 Squelch",  {40, 120, 110, 30, 100, 0, 64, 96, 20, 1, 21, 7}, 0.002f, 0.3f, 0.9f, 0.08f),
    acidPatch("303 Square",   {70, 80, 80, 50, 80, 1, 64, 96, 0, 1, 21, 7}, 0.002f, 0.4f, 0.9f, 0.08f),
    acidPatch("303 Rubber",   {55, 100, 70, 90, 70, 0, 64, 96, 0, 1, 80, 20}, 0.002f, 0.5f, 0.9f, 0.1f),
    acidPatch("303 Devilfish",{80, 110, 100, 20, 127, 0, 64, 96, 40, 2, 10, 4}, 0.002f, 0.3f, 0.9f, 0.06f),
    acidPatch("303 RAT Drive",{60, 95, 85, 45, 90, 0, 64, 90, 80, 2, 21, 7}, 0.002f, 0.4f, 0.9f, 0.08f),
    acidPatch("303 Sub",      {20, 40, 40, 80, 60, 1, 64, 100, 10, 1, 21, 30}, 0.002f, 0.6f, 0.9f, 0.12f),
    acidPatch("303 Open",     {110, 70, 60, 100, 80, 0, 64, 96, 0, 0, 21, 7}, 0.002f, 0.6f, 0.9f, 0.12f),
    x0xKitPatch("X0X 808 Kit", 10),
    x0xKitPatch("X0X 909 Kit", 11),
    // ---- JIANT FM's ANIMAL engine (engine 12: dsp/AnimalSynth.h): CMOS bird machine, pulses (cricket, frog), syrinx (songbirds), wings, larynx ----
    animalPatch("CMOS Bird", {0.f, 90.f, 0.f, 37.f, 16.f, 74.f, 0.f, 20.f}, 0.003f, 0.86f, 1.00f, 0.13f),
    animalPatch("CMOS Trill", {0.f, 70.f, 0.f, 50.f, 6.f, 94.f, 0.f, 6.f}, 0.003f, 0.86f, 1.00f, 0.13f),
    animalPatch("CMOS Monkey", {0.f, 110.f, 20.f, 80.f, -40.f, 70.f, 5.f, 28.f}, 0.003f, 0.86f, 1.00f, 0.18f),
    animalPatch("CMOS Bug", {0.f, 60.f, 90.f, 20.f, 12.f, 104.f, 0.f, 16.f}, 0.003f, 0.86f, 1.00f, 0.09f),
    animalPatch("Cricket", {1.f, 127.f, 100.f, 26.f, 0.f, 119.f, 7.f, 22.f}, 0.003f, 0.86f, 1.00f, 0.09f),
    animalPatch("Tree Cricket", {1.f, 127.f, 110.f, 32.f, 4.f, 102.f, 1.f, 22.f}, 0.003f, 0.86f, 1.00f, 0.13f),
    animalPatch("Katydid", {1.f, 10.f, 20.f, 24.f, 50.f, 80.f, 7.f, 22.f}, 0.003f, 0.86f, 1.00f, 0.09f),
    animalPatch("Cicada", {1.f, 38.f, 85.f, 28.f, 14.f, 60.f, 0.f, 24.f}, 0.009f, 0.86f, 1.00f, 0.26f),
    animalPatch("Treefrog", {1.f, 106.f, 49.f, 41.f, 6.f, 127.f, 15.f, 26.f}, 0.003f, 0.86f, 1.00f, 0.13f),
    animalPatch("Toad", {1.f, 127.f, 80.f, 44.f, 8.f, 116.f, 1.f, 22.f}, 0.004f, 0.86f, 1.00f, 0.18f),
    animalPatch("Bullfrog", {1.f, 70.f, 30.f, 53.f, 10.f, 52.f, 7.f, 22.f}, 0.003f, 0.86f, 1.00f, 0.18f),
    animalPatch("Canary", {2.f, 100.f, 20.f, 38.f, -30.f, 112.f, 14.f, 22.f}, 0.003f, 0.86f, 1.00f, 0.09f),
    animalPatch("Cardinal", {2.f, 110.f, 6.f, 46.f, -40.f, 77.f, 10.f, 22.f}, 0.003f, 0.86f, 1.00f, 0.09f),
    animalPatch("Dove", {2.f, 70.f, 10.f, 66.f, 15.f, 60.f, 9.f, 22.f}, 0.004f, 0.86f, 1.00f, 0.18f),
    animalPatch("Owl", {2.f, 80.f, 8.f, 68.f, -8.f, 68.f, 9.f, 22.f}, 0.004f, 0.86f, 1.00f, 0.18f),
    animalPatch("Peeper", {2.f, 100.f, 4.f, 34.f, 30.f, 80.f, 3.f, 22.f}, 0.003f, 0.86f, 1.00f, 0.09f),
    animalPatch("Bat", {2.f, 120.f, 0.f, 40.f, -64.f, 127.f, 3.f, 22.f}, 0.003f, 0.86f, 1.00f, 0.06f),
    animalPatch("Mosquito", {3.f, 30.f, 50.f, 64.f, 20.f, 40.f, 2.f, 22.f}, 0.005f, 0.86f, 1.00f, 0.13f),
    animalPatch("Bee", {3.f, 90.f, 30.f, 75.f, 10.f, 40.f, 2.f, 22.f}, 0.005f, 0.86f, 1.00f, 0.13f),
    animalPatch("Fly", {3.f, 70.f, 90.f, 78.f, 25.f, 50.f, 1.f, 22.f}, 0.005f, 0.86f, 1.00f, 0.13f),
    animalPatch("Wolf", {4.f, 0.f, 8.f, 64.f, 40.f, 50.f, 0.f, 5.f}, 0.009f, 0.86f, 1.00f, 0.26f),
    animalPatch("Dog Bark", {4.f, 100.f, 70.f, 64.f, -15.f, 85.f, 5.f, 6.f}, 0.003f, 0.86f, 1.00f, 0.09f),
    animalPatch("Roar", {4.f, 110.f, 100.f, 100.f, -20.f, 50.f, 3.f, 9.f}, 0.004f, 0.86f, 1.00f, 0.18f),
    animalPatch("Purr", {4.f, 10.f, 60.f, 50.f, 0.f, 60.f, 1.f, 19.f}, 0.005f, 0.86f, 1.00f, 0.13f),
    // ---- FM6: Felucca's msfa six-operator FM (engine 13: dsp/Fm6Voice.h), its factory patches ----
    fm6Patch("FM6 Tine Ep", 0),
    fm6Patch("FM6 Bell", 1),
    fm6Patch("FM6 Fm Bass", 2),
    fm6Patch("FM6 Brass", 3),
    fm6Patch("FM6 Pad", 4),
    fm6Patch("FM6 Marimba", 5),
    fm6Patch("FM6 Organ", 6),
    fm6Patch("FM6 Pluck", 7),
    fm6Patch("FM6 French Horn", 8),
    fm6Patch("FM6 Harpsichord", 9),
    fm6Patch("FM6 Glass Choir", 10),
    fm6Patch("FM6 Dark Strings", 11),
    fm6Patch("FM6 Lofi Flute", 12),
    fm6Patch("FM6 Crypt Organ", 13),
    fm6Patch("FM6 Tuba", 14),
    // ---- Felucca-Engines (MIT): ANALOG TRIO LOFI NOISE, their factory sounds ----
    felPatch("Fel Saw Lead", 14, {0, 12, 64, 0, 90, 30, 10, 64}, {4, 70, 100, 50}),
    felPatch("Fel Soft Pad", 14, {0, 20, 64, 4, 60, 10, 0, 32}, {80, 90, 110, 95}),
    felPatch("Fel Sqr Bass", 14, {1, 0, 0, 0, 50, 70, 40, 64}, {0, 60, 40, 30}),
    felPatch("Fel Pwm Str", 14, {4, 8, 40, 0, 75, 20, 0, 48}, {60, 80, 110, 85}),
    felPatch("Fel Acid", 14, {0, 0, 0, 0, 50, 100, 25, 64}, {0, 55, 20, 30}),
    felPatch("Fel Sine Key", 14, {3, 6, 50, 0, 127, 0, 0, 0}, {2, 80, 30, 70}),
    felPatch("Fel Rave", 14, {4, 30, 64, 0, 85, 20, 30, 64}, {20, 80, 110, 60}),
    felPatch("Fel Sub Bass", 14, {3, 0, 0, 0, 40, 0, 20, 0}, {0, 60, 100, 20}),
    felPatch("Fel Pluck", 14, {0, 8, 50, 0, 30, 40, 0, 64}, {0, 88, 0, 60}),
    felPatch("Fel Brass", 14, {0, 10, 64, 0, 45, 20, 10, 64}, {35, 70, 90, 45}),
    felPatch("Fel Wind", 14, {0, 0, 0, 90, 30, 90, 0, 0}, {60, 90, 60, 80}),
    felPatch("Fel Strings", 14, {0, 25, 64, 0, 70, 10, 0, 32}, {70, 90, 115, 90}),
    felPatch("Fel Sync Saw", 14, {5, 40, 64, 0, 100, 25, 0, 64}, {2, 70, 100, 40}),
    felPatch("Fel Sub Saw", 14, {6, 0, 80, 0, 70, 20, 10, 64}, {0, 60, 90, 30}),
    felPatch("Trio Fat Bass", 15, {0, 0, -12, 9, 0, 62, 45, 64}, {0, 62, 60, 25}),
    felPatch("Trio Arp Lead", 15, {2, 12, 0, 4, 0, 88, 25, 32}, {0, 60, 90, 30}),
    felPatch("Trio Sync Lead", 15, {9, 9, 0, 0, 0, 82, 30, 64}, {2, 70, 100, 40}),
    felPatch("Trio Ring Bell", 15, {13, 0, 18, 6, 1, 96, 30, 64}, {0, 92, 0, 80}),
    felPatch("Trio Chip Choir", 15, {1, 0, 12, 7, 1, 62, 95, 40}, {70, 90, 110, 85}),
    felPatch("Lofi Pulse Lead", 16, {0, 0, 32, 0, 0, 20, 0, 127}, {0, 60, 90, 30}),
    felPatch("Lofi Wave Bass", 16, {1, 1, 0, 0, 0, 0, 0, 90}, {0, 50, 70, 20}),
    felPatch("Lofi Arp 8bit", 16, {2, 0, 96, 0, 0, 0, 2, 110}, {0, 60, 80, 40}),
    felPatch("Lofi Wave Lead", 16, {0, 4, 92, 0, 0, 18, 0, 110}, {0, 70, 90, 30}),
    felPatch("Lofi Step Lead", 16, {4, 0, 40, 34, 0, 16, 0, 127}, {0, 64, 127, 55}),
    felPatch("Noise Wind", 17, {0, 64, 70, 100, 127, 0, 70, 0}, {90, 90, 110, 90}),
    felPatch("Noise Rain", 17, {1, 30, 88, 80, 64, 105, 40, 0}, {10, 90, 127, 70}),
    felPatch("Noise Arcade", 17, {2, 0, 127, 0, 127, 112, 0, 84}, {0, 80, 0, 50}),
    felPatch("Noise Metal", 17, {3, 0, 100, 40, 100, 32, 0, 0}, {0, 75, 40, 60}),
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
    void start(const SynthPatch& p, int note, float vel, float sr, uint32_t seed, const SynthMacros& m, float padPitch = 0.f, float padDecay = 1.f, float glideSemis = 0.f, float glideTime = 0.f) {
        *this = SynthVoice();
        gl_ = glideSemis; glT_ = std::max(0.005f, glideTime);
        p_ = &p; sr_ = sr; note_ = (float)note; vel_ = vel; rng_ = seed * 2654435761u + 12345u;
        for (int k = 0; k < 5; k++) ph_[k] = rnd();   // free-running phases: a stack does not sound like one thick oscillator
        if (p.engine == 8) { pad_ = ((note - 60) % 16 + 16) % 16; if (p.kit >= 1 && p.kit <= kSloopKitCount) dk_.start(kSloopKits[p.kit - 1].s[pad_], kSloopKits[p.kit - 1].crush, vel, sr, padPitch, seed); else done_ = true; return; }
        if (p.engine == 3) { pad_ = ((note - 60) % 16 + 16) % 16; padPitch_ = padPitch; padDecay_ = padDecay; return; }
        if (p.engine >= 14 && p.engine <= 17) { fl_.start(p.engine - 14, p.x, sr, seed); return; }
        if (p.engine == 13) { fm_.start((int)p.x[0], (int)note_, vel, sr); return; }
        if (p.engine == 12) { an_.start(p.x, vel, sr, seed); return; }
        if (p.engine == 11) { pad_ = ((note - 60) % 16 + 16) % 16; d9_.start(pad_, vel, sr); return; }
        if (p.engine == 10) { pad_ = ((note - 60) % 16 + 16) % 16; d8_.start(pad_, vel, sr); return; }
        if (p.engine == 9) { ac_.start(p.x, (int)note_, vel, sr); return; }
        if (p.engine >= 5 && p.engine <= 7) { dv_.start(p.x, p.engine, vel, sr, seed); return; }
        if (p.engine == 2) {
            const float f = freq(0.f);
            for (int k = 0; k < 6; k++) { mph_[k] = 0.f; mAmp_[k] = p.ma[k] > 0.f ? p.ma[k] : 0.f; }
            mf_ = f; return;
        }
        if (p.pluck) {
            const int n = std::min(kDl - 2, (int)(sr / freq(0.f)));
            float z = 0.f;
            for (int i = 0; i < n; i++) { const float x = rnd() * 2.f - 1.f; z += 0.55f * (x - z); dl_[i] = z; }   // a little low-passed noise burst
            plN_ = clampf(sr / freq(0.f) - 0.5f, 2.f, (float)kDl - 2.f);   // a very low note plays a little sharp instead of reading outside the delay line
            plDec_ = std::pow(std::max(0.001f, std::pow(p.pluckDec, 1.f - 0.5f * m.body)), 1.f / freq(0.f));
            plW_ = n;
        }
        lp_.reset(); coefCount_ = 0;
    }
    bool finished() const { return done_; }
    // adds one sample (mono -> l, r)
    void next(float bend, const SynthMacros& m, const SynthEdit& ed, float& l, float& r) {
        const SynthPatch& p = *p_;
        const float dt1 = 1.f / sr_;
        t_ += dt1;
        float oL = 0.f, oR = 0.f;
        if (p.engine == 3) drum(m, oL, oR);
        else if (p.engine >= 14 && p.engine <= 17) { oL = oR = fl_.next(freq(bend), m.tone, m.motion) * p.l1; }
        else if (p.engine == 13) { oL = oR = fm_.next(bend) * p.l1; if (t_ > 0.1f && fm_.done()) done_ = true; }
        else if (p.engine == 12) { oL = oR = an_.next(freq(bend), m.tone) * p.l1; }
        else if (p.engine == 11) { oL = oR = d9_.next() * p.l1; if (!d9_.alive() && t_ > 0.05f) done_ = true; }
        else if (p.engine == 10) { oL = oR = d8_.next() * p.l1; if (!d8_.alive() && t_ > 0.05f) done_ = true; }
        else if (p.engine == 9) { oL = oR = ac_.next() * p.l1; }
        else if (p.engine == 8) { oL = oR = dk_.next(); if (!dk_.alive()) done_ = true; }
        else if (p.engine == 2) modal(bend, m, oL, oR);
        else if (p.engine >= 5 && p.engine <= 7) { oL = oR = dv_.next(freq(bend), m.tone, m.motion) * p.l1; }
        else {
            const float vib = p.vib + std::max(0.f, m.motion) * 12.f;
            const float vibDeg = vib > 0.f ? vib * std::min(1.f, t_ / 0.4f) * std::sin(2.f * kPi * 5.2f * t_) : 0.f;
            const float pe = (p.pitchEnv > 0.f ? p.pitchEnv * std::exp(-t_ / p.pitchDec) : 0.f) + (gl_ != 0.f ? gl_ * std::exp(-t_ / glT_) : 0.f);
            const int w1 = ed.w1 >= 0 ? ed.w1 : p.w1, w2 = ed.w2 >= 0 ? ed.w2 : p.w2, uniN = ed.uni > 0 ? ed.uni : p.uni;
            const float f = freq(bend + pe + vibDeg / 100.f);
            if (p.pluck) {
                float rp = (float)plW_ - plN_; while (rp < 0.f) rp += (float)kDl;
                const int i0 = (int)rp, i1 = (i0 + 1) % kDl; const float fr = rp - (float)i0;
                const float x = dl_[i0 % kDl] * (1.f - fr) + dl_[i1] * fr;
                const float k = clampf(0.5f + 0.4f * m.tone, 0.15f, 0.95f);   // the loop filter: TONE opens or darkens the string
                const float y = k * x + (1.f - k) * plZ_; plZ_ = x;
                dl_[plW_] = y * plDec_; plW_ = (plW_ + 1) % kDl;
                oL = oR = x * 1.4f;
            } else {
                const float dt = f * dt1;
                if (p.engine == 4) {   // phase distortion (the Casio CZ idea): a cosine read through a bent phase; how far it is bent (DCW) follows the envelope
                    ph_[0] += dt; ph_[0] -= std::floor(ph_[0]);
                    const float ph = ph_[0];
                    float dcw = clampf(p.wtPos + p.wtEnv * std::exp(-t_ / std::max(0.03f, p.wtDec)) + 0.3f * m.tone + 0.2f * m.motion * std::sin(2.f * kPi * 0.4f * t_), 0.f, 1.f);
                    dcw *= clampf(2400.f / std::max(f, 1.f), 0.25f, 1.f);   // the sharp shapes alias on high notes: they get less of the bend
                    float y;
                    if (w1 == 1) { const float k = 0.5f * (1.f - 0.97f * dcw); const float q = ph < 0.5f ? std::min(1.f, ph / k) * 0.5f : 0.5f + std::min(1.f, (ph - 0.5f) / k) * 0.5f; y = std::cos(2.f * kPi * q); }   // square
                    else if (w1 == 2) { const float k = 1.f - 0.93f * dcw; y = (ph < k ? std::cos(2.f * kPi * ph / k) : 1.f) - (1.f - k); }                                                                 // pulse (the mean is taken off)
                    else if (w1 == 3) { const float res = 1.f + 14.f * dcw; y = std::sin(2.f * kPi * res * ph) * (1.f - dcw * ph) * (1.f - ph * ph * ph * ph * ph * ph); }                                                                          // resonant: a faster sine hard-synced to the note, faded out over the period (to zero at its end, so there is no step)
                    else { const float k = 0.5f * (1.f - 0.98f * dcw); const float q = ph < k ? ph * 0.5f / k : 0.5f + (ph - k) * 0.5f / (1.f - k); y = std::cos(2.f * kPi * q); }                       // saw
                    oL = oR = y * p.l1;
                } else if (p.engine == 1) {   // wavetable
                    ph_[0] += dt; ph_[0] -= std::floor(ph_[0]);
                    const float pos = clampf(p.wtPos + p.wtEnv * std::exp(-t_ / std::max(0.05f, p.wtDec)) + 0.25f * m.tone + 0.15f * m.motion * std::sin(2.f * kPi * 0.35f * t_), 0.f, 1.f) * (WaveTable::kFrames - 1);
                    const WaveTable& wt = WaveTable::get(); const int f0 = std::min(WaveTable::kFrames - 2, (int)pos); const float ff = pos - (float)f0;
                    const float idx = ph_[0] * WaveTable::kLen; const int i0 = (int)idx & (WaveTable::kLen - 1), i1 = (i0 + 1) & (WaveTable::kLen - 1); const float fi = idx - std::floor(idx);
                    const float (*tb)[WaveTable::kLen] = f > 500.f ? wt.lite : wt.full;
                    const float a = tb[f0][i0] + (tb[f0][i1] - tb[f0][i0]) * fi, b = tb[f0 + 1][i0] + (tb[f0 + 1][i1] - tb[f0 + 1][i0]) * fi;
                    oL = oR = (a + (b - a) * ff) * p.l1;
                } else if (p.fmIdx > 0.f && w1 == 0) {
                    const float idx = p.fmIdx * (1.f + 0.8f * m.tone) * (p.fmDec > 0.f ? std::exp(-t_ / p.fmDec) : 1.f) * (0.5f + 0.5f * vel_);
                    phm_ += dt * p.fmRatio; phm_ -= std::floor(phm_);
                    ph_[0] += dt; ph_[0] -= std::floor(ph_[0]);
                    oL = oR = std::sin(2.f * kPi * (ph_[0] + std::max(0.f, idx) * std::sin(2.f * kPi * phm_))) * p.l1;
                } else if (uniN > 1) {
                    const int n = std::min(5, uniN); const float spread = p.uniDet * (1.f + 1.2f * m.motion);
                    for (int k = 0; k < n; k++) {
                        const float x = n > 1 ? (float)k / (n - 1) * 2.f - 1.f : 0.f;   // -1 .. 1
                        const float d = dt * std::pow(2.f, x * std::max(0.f, spread) / 1200.f);
                        ph_[k] += d; ph_[k] -= std::floor(ph_[k]);
                        const float w = wave(w1, ph_[k], d, p.pw) * p.l1 / std::sqrt((float)n);
                        const float pan = 0.5f + 0.5f * x;   // spread over the stereo field
                        oL += w * (1.f - pan * 0.8f); oR += w * (0.2f + pan * 0.8f);
                    }
                } else {
                    ph_[0] += dt; ph_[0] -= std::floor(ph_[0]);
                    oL = oR = wave(w1, ph_[0], dt, p.pw) * p.l1;
                }
                const float body = std::max(-0.9f, m.body);
                if (p.l2 > 0.f) {
                    const float d2 = dt * std::pow(2.f, (p.semi2 + (p.det2 + ed.det2) / 100.f) / 12.f);
                    ph2_ += d2; ph2_ -= std::floor(ph2_);
                    const float w = wave(w2, ph2_, d2, p.pw) * p.l2 * (1.f + 0.7f * body); oL += w; oR += w;
                }
                const float sub = p.sub * (1.f + body) + (p.sub <= 0.f && body > 0.f ? 0.25f * body : 0.f);
                if (sub > 0.f) { phs_ += dt * 0.5f; phs_ -= std::floor(phs_); const float w = std::sin(2.f * kPi * phs_) * sub; oL += w; oR += w; }
                if (p.noise > 0.f) { const float w = (rnd() * 2.f - 1.f) * p.noise * (p.noiseDec > 0.f ? std::exp(-t_ / p.noiseDec) : 1.f); oL += w; oR += w; }
            }
            // filter: the coefficients are renewed every 8 samples
            if (!p.pluck) {
                if ((coefCount_++ & 7) == 0) {
                    const float env = p.fdec > 0.f ? std::exp(-t_ / p.fdec) : 0.f;
                    const float hz = p.cut * std::pow(2.f, p.fenv * (1.f + 0.6f * m.motion) * env + p.key * (note_ - 60.f) / 12.f + (vel_ - 0.7f) * 0.8f + 2.f * m.tone + ed.cutOct);
                    lp_.set(hz, sr_, std::max(0.5f, p.res + ed.res));
                }
                oL = lp_.processT(0, oL, ed.ftype); oR = lp_.processT(1, oR, ed.ftype);
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
        else if (kit == 4) { d.dec *= 0.85f; d.f0 *= 1.1f; d.f1 *= 1.15f; d.noise *= 1.2f; d.pdec *= 0.8f; d.hp = std::min(0.95f, d.hp + 0.08f); }   // 909: tighter, brighter, harder
        else if (kit == 5) {   // trap: a long deep kick, tiny hats, loud snares and claps
            if (pad_ < 2) { d.dec *= 1.7f; d.f1 *= 0.8f; d.pdec *= 1.3f; }
            else if (pad_ == 6) { d.dec *= 0.55f; d.hp = std::min(0.95f, d.hp + 0.1f); }
            else if (pad_ == 7) d.dec *= 0.8f;
            else if (pad_ >= 2 && pad_ <= 4) { d.noise *= 1.25f; d.f0 *= 1.2f; }
        }
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
            float mt = 0.f; const float base = p_->kit == 3 ? 280.f : p_->kit == 4 ? 360.f : p_->kit == 5 ? 400.f : 330.f * (pad_ == 11 ? 1.7f : pad_ == 14 ? 1.1f : 1.f);
            for (int k = 0; k < 6; k++) { ph_[k % 5] += 0.f; mphd_[k] += base * r[k] / sr_; mphd_[k] -= std::floor(mphd_[k]); mt += mphd_[k] < 0.5f ? 1.f : -1.f; }
            s += mt / 6.f * d.met * std::exp(-t_ / (d.dec * (pad_ == 11 ? 1.f : 0.7f))) * (pad_ == 11 ? 0.8f : 0.5f);
        }
        if (t_ > 6.f * d.dec + 0.05f) done_ = true;
        s = std::tanh(s * 1.4f) * (0.4f + 0.6f * vel_);
        oL = oR = s;
    }
    const SynthPatch* p_ = nullptr;
    float sr_ = 44100.f, note_ = 60.f, vel_ = 1.f, t_ = 0.f, ph_[5] = {}, ph2_ = 0.f, phs_ = 0.f, phm_ = 0.f;
    dungeon::Voice dv_; SloopDrumVoice dk_; Acid303 ac_; Drum808Voice d8_; Tr909Voice d9_; animal::Voice an_; fm6::Voice fm_; fel::Voice fl_;
    Lowpass lp_; int coefCount_ = 0; uint32_t rng_ = 1; bool done_ = false;
    float dl_[kDl] = {}; int plW_ = 0; float plN_ = 100.f, plDec_ = 0.99f, plZ_ = 0.f;
    float mph_[6] = {}, mAmp_[6] = {}, mf_ = 261.6f;                            // modal
    float gl_ = 0.f, glT_ = 0.1f;   // glide: semitones to slide, over how long
    int pad_ = 0; float padPitch_ = 0.f, padDecay_ = 1.f; float hpz_ = 0.f, nlp_ = 0.f, mphd_[6] = {};                  // drums
};
