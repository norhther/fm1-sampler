// The sampler instrument: one sample, up to eight voices, six playback algorithms.
//   MELODIC  pitched by the note, ADSR, plays the window once
//   ONESHOT  pitched by the note, always plays the whole window (note off is ignored)
//   LOOPED   pitched by the note, ADSR, the window loops while the key is held
//   KIT      the sample is cut at its hits into 16 slices; a note plays one slice unpitched (pad = note - 60)
//   GRAIN    a cloud of short overlapping grains picked at random inside the window
//   PAUL     the window smeared into an endless pad (spectral time stretch)
// A sampler can also hold a built-in synth instead of a sample (path "synth:N", see Synth.h); the playback algorithms do not apply then.
#pragma once
#include "Adsr.h"
#include "Common.h"
#include "Paul.h"
#include "Synth.h"
#include <memory>
#include <string>
#include <vector>

struct SampleBuf {
    std::vector<float> d;   // interleaved stereo
    int frames = 0, sr = SAMPLERATE;
};

class Sampler {
public:
    enum Mode { MELODIC, ONESHOT, LOOPED, KIT, GRAIN, PAUL, kModes };
    static constexpr int kVoices = 10;   // 8 sounding (4 in PAUL mode) + room for the voices that are fading out because a new note took their place

    Sampler(int engineRate);
    ~Sampler();
    // worker thread: reads a WAV, normalises it, finds the slices and the overview peaks
    bool load(const std::string& path);   // a WAV, or "synth:N" for built-in synth patch N
    bool isSynth() const { return synth_ >= 0; }
    float rootGuess() const { return rootGuess_; }   // pitch of the sample as a MIDI note (0 = not pitched)
    void setRoot(int midi) { root_ = std::min(108, std::max(12, midi)); }   // the key that plays the sample at its own pitch (60 = C4, how the library is tuned)
    void setChopFull(bool on) { chopFull_ = on; }   // KIT: a pad plays its whole chop when the key goes down (true), or only as long as the key is held (false)
    const std::vector<int>& slices() const { return slices_; }
    void setSlices(std::vector<int> s) { if (s.size() >= 2) slices_ = std::move(s); }
    // per pad (KIT mode and the synth drum kits: pad = note - 60 mod 16): level, tuning in semitones, decay length, pan, choke group (0 = none; a pad cuts the sounding pads of its group)
    struct Pad { float vol = 1.f, pitch = 0.f, decay = 1.f, pan = 0.f; int group = 0; };
    void setPad(int pad, const Pad& p) { if (pad >= 0 && pad < 16) pads_[pad] = p; }
    bool padMode() const { return mode_ == KIT || (synth_ >= 0 && kSynths[synth_].engine == 3); }
    void setLevel(float g) { level_ = clampf(g, 0.f, 3.f); }           // sample level
    void setFine(float cents) { fine_ = clampf(cents, -100.f, 100.f); }   // fine tuning
    // audio thread: the sample was edited (reversed, trimmed ...): the new buffer with its overview and slices, built on the worker
    void replaceOriginal(std::shared_ptr<const SampleBuf> b, std::vector<float> peaks, std::vector<int> slices) { allOff(); orig_ = std::move(b); proc_.reset(); peaks_ = std::move(peaks); if (slices.size() >= 2) slices_ = std::move(slices); }
    static std::vector<float> overview(const SampleBuf& b);
    void setSynthMacros(const SynthMacros& m) { mac_ = m; }
    void setSynthEdit(const SynthEdit& e) { edit_ = e; }   // TONE, BODY, MOTION, GRIT of a built-in synth
    const std::string& label() const { return label_; }
    const std::vector<float>& peaks() const { return peaks_; }   // 512 bins, 0..1
    std::shared_ptr<const SampleBuf> original() const { return orig_; }

    // audio thread
    void setMode(int m);
    void setEnv(float attack, float decay, float sustain, float release);
    void setRange(float a, float b);   // 0..1 of the original sample
    // the offline stretched / pitched copy of the range [a, b] (null: play the original)
    void setProcessed(std::shared_ptr<const SampleBuf> p, float a, float b);
    void setBend(float semitones) { bend_ = semitones; }
    void noteOn(int note, int vel);
    void noteOff(int note);
    void allOff();
    void render(float* out /*stereo, added*/, int n);
    int voicePositions(float* out, int maxN) const;   // read positions 0..1 of the original sample
    bool hasVoices() const;

private:
    struct Grain { bool on = false; double pos = 0, inc = 0; int age = 0, len = 1; };
    struct Voice {
        bool on = false, gate = false, fading = false, paulReady = false; int note = 0, slice = 0, group = 0; float vel = 1.f, padGain = 1.f, panL = 1.f, panR = 1.f, padPitch = 0.f; uint32_t born = 0;
        double pos = 0, rate = 1.0, ws = 0, we = 0;   // window of the played buffer, frames
        const SampleBuf* buf = nullptr;
        Adsr env; Grain grain[12]; double spawn = 0; int paulIdx = -1;
    };
    void resolveWindow(Voice& v, const SampleBuf*& buf, double& ws, double& we) const;
    float orginalPosOf(const Voice& v) const;
    void renderVoice(Voice& v, float* out, int n);
    void pumpPaul();   // tops up the Paulstretch streams: a little work per block, never all voices at once
    void fadeOut(Voice& v);
    float rnd() { rng_ = rng_ * 1664525u + 1013904223u; return (rng_ >> 8) / 16777216.f; }

    int sr_;
    std::shared_ptr<const SampleBuf> orig_, proc_;
    float procA_ = 0, procB_ = 1, rangeA_ = 0, rangeB_ = 1, bend_ = 0, atk_ = 0.01f, dec_ = 0.3f, sus_ = 0.8f, rel_ = 0.3f;
    bool chopFull_ = false; int mode_ = MELODIC; std::vector<int> slices_; std::string label_; std::vector<float> peaks_;
    Voice voices_[kVoices]; std::vector<std::unique_ptr<PaulStream>> paul_; bool paulBusy_[kVoices] = {};
    uint32_t bornCounter_ = 0, rng_ = 7;
    bool loadSynth(int idx);
    static float detectPitch(const std::vector<float>& st, int frames, int sr);
    void renderSynth(Voice& v, SynthVoice& sv, float* out, int n);
    float level_ = 1.f, fine_ = 0.f; Pad pads_[16]; SynthEdit edit_; int lastNote_ = -1; int root_ = 60; float rootGuess_ = 0.f;
    SynthMacros mac_; int synth_ = -1; float synthNorm_ = 1.f; SynthVoice sv_[kVoices];
};
