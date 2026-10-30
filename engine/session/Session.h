// Session: the audio engine of the sampler app. Eight sampler tracks (arpeggiator, filter, cassette, volume/pan, reverb and delay sends),
// one audio looper whose clips share a tape clock, a microphone sample recorder.
//
// Threads: the audio thread calls process(). Everything else may be called from any other thread; changes reach the audio thread
// through a command queue drained at the start of each block. status() and the peak views are lock-free.
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "../dsp/MasterFx.h"
#include "../dsp/Sequencer.h"

class Session {
public:
    static constexpr int kTracks = 8;
    static constexpr int kVoiceMax = 32;
    static constexpr int kLoopBins = 400;

    struct Status {
        bool playing;
        double pos;              // samples inside the loop
        double loopSamples;      // 0 = no loop yet
        float bpm;
        int loopBars;
        float master_peak;
        float track_peak[kTracks];
        bool track_has_clip[kTracks], track_clip_rec[kTracks];
        uint32_t cpu_permille;   // audio block time / block duration, smoothed
        bool freeRecording; float freeSeconds;
        bool freeArmed;          // free take armed: waiting for the first sound
        int sampleState;         // 0 idle, 1 recording, 2 working, 3 waiting for sound
        bool samplePending;      // a recorded sample waits to be loaded
        int sampleTrack; float sampleSeconds, inputPeak;
        int sampleVersion;       // bumps when a sample was loaded into a track (sampleTrack)
        int sampleFails;         // bumps when a sample could not be loaded
        float clipPos[kTracks];  // 0..1 read position of the loops (follows the loop speed)
        float lfoValue[kTracks]; // -1..1, for the UI
        int voiceN[kTracks]; float voicePos[kTracks][kVoiceMax];   // sounding voices (every grain in granular mode): read positions 0..1 of the whole sample
        double beatPos;          // the beat clock: transport position in beats (free running while stopped)
        bool metro;
        int countBeats;          // > 0 while the count-in runs: beats left
        int seqState[kTracks], seqBars[kTracks], seqCount[kTracks];   // sequencer: 0 mute 1 play 2 record 3 step entry, pattern length in bars, notes in the pattern
        int seqCursor[kTracks];  // step entry cursor, in ticks
        int seqUndoN[kTracks], seqRedoN[kTracks];   // how many steps back / forward are possible
        int bounce;              // 0 idle, 1 waiting for the loop start, 2 recording the pattern to tape
        int bounceDone;          // counts up when a bounce finished
    };
    struct SeqView { int tick, len, note, vel; };
    struct Lfo { float rate = 1.f, depth = 0.f, base = 0.5f; int target = 0 /*0 pitch 1 filter 2 volume 3 pan*/, shape = 0 /*0 sine 1 tri 2 square 3 s&h*/; };

    explicit Session(int sampleRate = 48000);
    ~Session();

    // ---- audio thread ----
    void process(float* out /*interleaved stereo*/, const float* in /*interleaved stereo, may be null*/, int frames);

    // ---- transport and notes ----
    void play(bool on);
    void noteOn(int track, int note, int vel);
    void noteOff(int track, int note);

    // ---- sampler (per track) ----
    void loadSamplePath(const std::string& path, bool kit, int track);          // worker thread builds it, then it replaces the track's sampler
    void setMode(int track, int mode);                                         // 0 melodic, 1 one-shot, 2 looped, 3 kit, 4 granular, 5 paulstretch
    void setEnv(int track, float attack, float decay, float sustain, float release);   // seconds, level
    void setSampleRange(int track, float start, float end);                    // 0..1 of the sample
    void setStretch(int track, float speed, float semitones, bool commit);     // commit: the knob rested, (re)build the stretched copy offline
    void setLfo(int track, const Lfo& l);
    void setArp(int track, bool on, int rate /*0..11*/, int pattern /*0..4*/, int octaves /*1..4*/, float gate /*0.05..0.95*/);
    void setTape(int track, float sat, float tone, float wobble, float hiss, float mix, float bits);

    // ---- mixer and effects ----
    void setVolume(int track, float v);   // 0..1.5
    void setPan(int track, float p);      // -1..1
    void setSend(int track, int which /*0 reverb 1 delay*/, float level);
    void setMasterVolume(float v);
    void setReverb(float size);           // 0..1
    void setDelay(float seconds, float feedback);
    void setRetLevel(int which, float v); // reverb / delay return gain (the effects run 100 % wet)

    // ---- tempo, metronome ----
    void setTempo(float bpm);             // 30..300; ignored while a loop exists (the loop defines the tempo)
    void setMetronome(bool on, float volume);
    void setTakeQuant(int mode);          // 0 off (tempo follows the first take), 1 the take snaps to whole beats, 2 to whole bars (tempo kept)

    // ---- note sequencer (per track pattern on the beat clock, locked to the loop) ----
    void seqSetState(int track, int state);   // 0 mute, 1 play, 2 record (starts the transport; one track records at a time), 3 step entry (keys write at the cursor)
    void seqUndo(int track);                  // takes back the last edit of the pattern (a recording pass, a step, a tap on the grid, clear, a change of the length); 16 steps
    void seqRedo(int track);
    void seqSet(int track, const SeqView* notes, int n);                      // replace the whole pattern (one undo step)
    void seqResize(int track, int tick, int note, int len);                   // touch edit: a note gets a new length in ticks
    void seqToggle(int track, int tick, int len, int note, int vel);   // touch edit: a note under that tick is removed, else one is added
    void seqRest(int track);                  // step entry: skip one step
    void seqCursorMove(int track, int steps); // step entry: move the cursor
    void seqCursorSet(int track, int tick);
    void seqBounce(int track);                // record the pattern onto the tape for one loop cycle (a loop of the pattern length if there is none), then mute it
    void seqSetBars(int track, int bars);     // 1, 2, 4 or 8; a longer pattern repeats its content, a shorter one drops the rest
    void seqClear(int track);
    void setSwing(float s);                                                  // 0.5 = straight .. 0.75: every second grid line of the pattern plays later
    struct TombolaBall { float x, y, hit; int track; };                      // drum coordinates: the drum has radius 1
    void tombolaAdd(int track, int note);                                    // a ball that plays `note` on that track whenever it hits the drum wall
    void tombolaSet(float spin, float gravity, float mass, float shape);
    void tombolaTurn(float radians);
    void tombolaClear();
    int tombolaView(TombolaBall* out, int maxN, float* drumAngle) const;
    void setQuantize(int grid);               // playback grid of all patterns (see Sequencer.h gridName); the recorded timing is kept
    int seqNotes(int track, SeqView* out, int maxNotes) const;

    // ---- timing helpers ----
    void setInputQuant(int grid);             // live quantize: played keys sound on the nearest grid line of the beat clock (0 = off); recording snaps to it too
    void setCountIn(int bars);                // 0..2 bars of clicks before a free take or a sequencer recording starts from a stopped transport

    // ---- effects: FX 2 inserts and LIVE FX on the master, the same set on every track ----
    void setMasterFx(const FxParams& p);
    void setTrackFx(int track, const FxParams& p);

    // ---- looper ----
    void freeRecord(int track, bool on);  // on: arm and wait for the first sound or note; the take defines the loop; off: end the take (or cancel while waiting)
    void clipRecord(int track, bool on);  // overdub on a track while the loop plays
    void clipClear(int track);
    void clipClearAll();                  // every loop: the looper is clean again
    void loopDouble();                    // loop twice as long, the content repeated (up to 3 min)
    void loopHalve();                     // half as long, the second half dropped
    void setLoopSpeed(float speed);       // 0.25..2, tape speed of all loops together; 1 = locked to the transport
    void setLoopSource(int src);          // what the loop records: 0 microphone, 1 the track's sound
    bool saveClip(int track);             // the loop of `track` as sample_*.wav in the data dir
    int loopPeaks(int track /* -1 = all tracks mixed */, float* out, int maxBins) const;

    // ---- microphone sampling ----
    void setMicThreshold(float linear);   // level that starts a take (0.005..0.5)
    void recorderWait(bool waitForSound);
    void recorderStart();
    void recorderStop();                  // trims, normalises and writes sample_*.wav; the result becomes the pending sample
    void recorderDismiss();               // the pending sample was taken over
    std::string lastSamplePath() const;
    int livePeaks(float* out, int maxBins) const;   // one peak per 50 ms of the take in progress

    // ---- library on disk ----
    void setDataDir(const std::string& dir);
    void setSampleDir(const std::string& dir);
    std::string dataDir() const;
    std::string sampleDir() const;
    std::vector<std::string> sampleFolders() const;
    std::vector<std::string> sampleFiles(const std::string& folder) const;
    std::vector<std::string> sampleLibrary() const;   // saved recordings, newest first

    // ---- views ----
    std::vector<float> samplePeaksOf(int track, int bins) const;   // waveform of the track's sample (empty if none)
    std::string trackLabel(int track) const;                       // file name of the track's sample without extension
    int sampleRate() const;
    Status status() const;

    struct Impl;
private:
    std::unique_ptr<Impl> d;
};
