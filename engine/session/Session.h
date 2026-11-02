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
    static constexpr int kTracks = 16;
    static constexpr int kVoiceMax = 32;
    static constexpr int kLoopBins = 400;

    struct Status {
        bool playing;
        double pos;              // samples inside the loop
        double loopSamples;      // 0 = no loop yet
        float bpm;
        int loopBars;
        int seqSlot[kTracks], seqSlotPending[kTracks]; bool songOn; int songPos, songLen;   // pattern banks A..H (index 0..7): the current one, the one queued for the pattern end (-1 none); the song
        float master_peak; bool clip;   // clip: the master was pushed past full level a moment ago

        float track_peak[kTracks];
        bool track_has_clip[kTracks], track_clip_rec[kTracks];
        uint32_t cpu_permille;   // audio block time / block duration, smoothed
        uint32_t cpu_peak_permille;   // the slowest call lately (falls off over a few seconds): a spike is what you hear as a crackle
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
        bool seqRun[kTracks], seqPend[kTracks], keyTrig[kTracks], seqOne[kTracks]; int seqTrans[kTracks], seqPos[kTracks];   // seqPos: tick inside the pattern, -1 when it is not playing
          // pattern play / stop, a start waiting for the beat, the keys trigger the pattern, its transposition
        int seqCursor[kTracks];  // step entry cursor, in ticks
        int globalUndoN, globalRedoN;   // the UNDO / REDO button: steps back / forward over every track
        int seqUndoN[kTracks], seqRedoN[kTracks];   // how many steps back / forward are possible
        int bounce;              // 0 idle, 1 waiting for the loop start, 2 recording the pattern to tape
        int bounceDone;          // counts up when a bounce finished
        int printDone, printBusy;   // PRINT FX: counts up when a bake finished; > 0 while one runs
        uint32_t printMask;         // PRINT FX: the tracks whose tape holds the sound after the effects (bit per track)
    };
    struct SeqView { int tick, len, note, vel, prob = 100, ratchet = 1, nudge = 0, cut = 0; };
    struct SeqRef { int tick, note; };   // a note of a pattern is told by its start and its pitch
    enum SeqOp { OP_DELETE, OP_TRANSPOSE, OP_SHIFT, OP_DUP, OP_VEL, OP_PROB, OP_RATCHET, OP_NUDGE, OP_CUT };
    struct Lfo { float rate = 1.f, depth = 0.f, base = 0.5f; int target = 0 /*0 pitch 1 filter 2 volume 3 pan*/, shape = 0 /*0 sine 1 tri 2 square 3 s&h*/; };

    explicit Session(int sampleRate = 48000);
    ~Session();

    // ---- audio thread ----
    void process(float* out /*interleaved stereo*/, const float* in /*interleaved stereo, may be null*/, int frames);

    // ---- transport and notes ----
    void play(bool on);
    void setTrackSpace(int track, bool own, float size /*0..1*/, float sec, float fb);   // OWN SPACE: this track gets its own reverb and delay
    void newTape(int bars);                // an empty tape of that many bars at the current tempo (no sound on it yet); only when there is no tape
    void seekPos(double samples);          // tape head to an exact place (samples from the start of the tape; 0 = the start)
    void moveTo(double samples, bool listen);   // the head goes there like a tape (fast, heard when `listen`, backwards too); while the transport plays it jumps
    void setPunchLen(double bars);         // a record or overdub that ends by itself: 0 = when told to, n = after n bars, -1 = at the end of the tape
    void recordFrom(int track, int countBars);   // record over the tape from the head, after countBars bars of metronome (0 = at once)
    void silence();                       // STOP ALL: transport off, every voice and held note released, reverb and delay tails cleared
    void noteOn(int track, int note, int vel);
    void noteOff(int track, int note);

    // ---- sampler (per track) ----
    void loadSamplePath(const std::string& path, bool kit, int track);          // worker thread builds it, then it replaces the track's sampler
    void setSynthMacros(int track, float tone, float body, float motion, float grit);   // -1..1 each; only a built-in synth listens
    void setSynthEdit(int track, int wave1, int wave2, float detune2Cents, float cutoffOctaves, float resonanceAdd, int filterType, int unison, bool mono, float glideSeconds);   // a built-in synth on top of its patch (-1 = keep the patch's)
    void setPad(int track, int pad, float volume, float pitchSemitones, float pan, float decay, int chokeGroup);   // KIT mode and the synth drum kits, pad 0..15
    enum EditOp { EDIT_REVERSE, EDIT_NORMALIZE, EDIT_TRIM, EDIT_FADE_IN, EDIT_FADE_OUT, EDIT_UNDO };
    void editSample(int track, int op);                                       // REVERSE, NORMALIZE, TRIM to the play window, FADE IN / OUT (30 ms); UNDO takes the last edit back
    void undoEdit(int track);
    void setChopFull(int track, bool on);                                     // KIT: a key plays its whole chop (true) / the chop sounds while the key is held (false)
    void setSliceStarts(int track, const std::vector<float>& starts, const std::vector<float>& ends = {});   // KIT: chops placed by hand (0..1 of the sample), each with its own end
    void setSampleLevel(int track, float level, float fineCents);            // level 0..3, fine tune in cents
    void resample(int bars);                                                  // the master output of `bars` bars becomes a new sample (loaded like a microphone take)
    void setRoot(int track, int midiNote);                                     // the key that plays the sample at its own pitch (60 = the library's tuning)
    void setSlices(int track, int count, float sensitivity);                   // KIT: cut the sample into `count` pads again
    int sampleFramesOf(int track) const;                                       // length of the track's sample in frames (0 = none)
    float rootGuessOf(int track) const;                                        // pitch of the sample as a MIDI note, 0 = not a pitched sound
    std::vector<float> sliceEndsOf(int track) const;                           // where each chop stops, 0..1 (same order as slicesOf)
    std::vector<float> slicesOf(int track) const;                              // slice starts, 0..1 of the sample
    void unloadSample(int track);                                              // the track is empty again
    void setMode(int track, int mode);                                         // 0 melodic, 1 one-shot, 2 looped, 3 kit, 4 granular, 5 paulstretch
    void setEnv(int track, float attack, float decay, float sustain, float release);   // seconds, level
    void setSampleRange(int track, float start, float end);                    // 0..1 of the sample
    void setStretch(int track, float speed, float semitones, bool commit);     // commit: the knob rested, (re)build the stretched copy offline
    void setLfo(int track, const Lfo& l);
    void setArp(int track, bool on, int rate /*0..11*/, int pattern /*0..4*/, int octaves /*1..4*/, float gate /*0.05..0.95*/);
    void setTapeOnly(int track, bool on);   // TAPE ONLY: the cassette and the track effects treat the tape of the track only, the live sound stays dry
    void setPrintFx(int track, bool on, bool bake = true);   // the tape of the track holds the sound after its cassette and effects (see Session.cpp)
    void setTape(int track, float sat, float tone, float wobble, float hiss, float mix, float bits);

    // ---- mixer and effects ----
    void setVolume(int track, float v);   // 0..1.5
    void setPan(int track, float p);      // -1..1
    void setSend(int track, int which /*0 reverb 1 delay*/, float level);
    void setMasterVolume(float v);
    int clockPulses();                          // MIDI clock out: the pulses (24 per beat) since the last call
    int clockEvents();                          // bit 0: the transport started, bit 1: it stopped, since the last call
    void setLimiter(bool on, float ceilingDb);   // master limiter: nothing passes the ceiling (-12 .. 0 dB)
    void setDuck(int sourceTrack, float release);   // sidechain: every note of this track (-1 = off) pushes the tracks with a duck amount down, releasing over `release` seconds
    void setDuckAmount(int track, float amount);    // 0..1
    void setReverb(float size);           // 0..1
    void setDelay(float seconds, float feedback);
    void setRetLevel(int which, float v); // reverb / delay return gain (the effects run 100 % wet)

    // ---- tempo, metronome ----
    void setTempo(float bpm);             // 30..300; ignored while a loop exists (the loop defines the tempo)
    void setMetronome(bool on, float volume);
    void setTakeQuant(int mode);          // 0 off (tempo follows the first take), 1 the take snaps to whole beats, 2 to whole bars (tempo kept)

    // ---- note sequencer (per track pattern on the beat clock, locked to the loop) ----
    void seqRun(int track, bool run);         // play the pattern from its first tick (on the next beat or bar) / stop it, the transport keeps going
    void seqKeyTrigger(int track, bool on);   // OP-1 style: the keys start the pattern (C4 = as written, every other key moves it by its distance), release stops it
    void seqOneShot(int track, bool on);      // the pattern plays one pass and stops (false: it loops)
    void seqTrigQuant(int track, int q);      // when a key starts the pattern: 0 at once, 1 on the beat, 2 on the bar
    void seqNudge(int track, int ticks);      // shift the whole pattern round its length (one undo step)
    void seqSetState(int track, int state);   // 0 mute, 1 play, 2 record (starts the transport; one track records at a time), 3 step entry (keys write at the cursor)
    void setLoopTempo(float bpm);             // a loop is on the tape: every clip is time-stretched to this tempo (the pitch stays; one undo step)
    void resetUndo();                         // forget every undo / redo step (a new or a loaded project)
    void globalUndo();                        // takes back the last edit on any track (pattern or sample edit)
    void globalRedo();
    void seqUndo(int track);                  // takes back the last edit of the pattern (a recording pass, a step, a tap on the grid, clear, a change of the length); 16 steps
    void seqRedo(int track);
    void seqSet(int track, const SeqView* notes, int n);                      // replace the whole pattern (one undo step)
    void seqApply(int track, const SeqRef* notes, int n, int op, int arg);    // edits a selection in one undo step: delete, transpose (semitones), shift / duplicate by `arg` ticks, set velocity / probability / ratchet / nudge
    void seqSelectPattern(int track, int slot, bool immediate);               // pattern bank 0..7 (A..H): at once, or (while it plays) when the current pattern ends
    void seqCopyPattern(int track, int fromSlot, int toSlot);
    int seqSlotNotes(int track, int slot, SeqView* out, int maxNotes, int* bars) const;   // the notes of any bank
    void seqSetSlot(int track, int slot, const SeqView* notes, int n, int bars);          // fills a bank (a project is loaded)
    void setSong(const uint8_t* chain, int n, bool on);                       // the song: banks one after the other for every track, each as long as its longest pattern
    void seqAddBar(int track, bool copyLast);                                 // one more bar at the end (up to 16), empty or a copy of the last one
    void seqResize(int track, int tick, int note, int len);                   // touch edit: a note gets a new length in ticks
    void seqToggle(int track, int tick, int len, int note, int vel);   // touch edit: a note under that tick is removed, else one is added
    void seqRest(int track);                  // step entry: skip one step
    void seqCursorMove(int track, int steps); // step entry: move the cursor
    void seqCursorSet(int track, int tick);
    void seqBounceCancel();                   // stop a bounce that waits or records (a new tape clip it made is dropped)
    void seqBounce(int track, int bar = 0);                // record the pattern onto the tape for one loop cycle (a loop of the pattern length if there is none), then mute it (bar > 0: one pass of the pattern from that bar of an existing tape)
    void seqSetBars(int track, int bars);     // 1..16; a longer pattern gets empty bars, a shorter one drops the notes behind its new end
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
    void setTakeBars(int bars);               // 0: the take ends when told to; n: it ends by itself after n bars, at the current tempo
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
    bool writeTape(const std::string& dir);   // projects: every loop as dir/tape_N.wav plus the mix as dir/mix.wav (written in the background, see busyJobs)
    void loadTape(const std::string& dir, int bars, float bpm);   // projects: replaces the loops by dir/tape_N.wav
    int busyJobs() const;                     // > 0 while a project is being written or read
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
    static constexpr int kScope = 128;
    void scope(float* hi, float* lo) const;   // the master out as a scope: kScope bins of 256 frames (about 0.75 s), oldest first, highest and lowest sample of each

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
