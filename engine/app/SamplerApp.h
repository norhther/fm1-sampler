// SamplerApp: the whole sampler + audio looper on top of Session, and its FM-1 side.
// Bytes in (MIDI of the FM-1), bytes out (SysEx draw commands for its 240x240 screen and LEDs), no platform code: runs unchanged on the host
// (tools/app_sim) and in the Android app. The phone UI (ui/SamplerUI) reads the same knobs and edits them by touch.
//
// FM-1 map:  encoders  SELECT = track (1..8) | PRESETS = sample library with audition | ALGORITHM = sampler algorithm | K1..K4 = page knobs
//            buttons   HOME sample (start, end, speed, pitch) | FX | ENV | LFO | ARP | SEQ tape (loop speed, exciter, lo-fi, wobble) | EDIT mixer
//                      GLO key and scale | SAVE loop save (tap) / clear (hold) | PLAY | REC loop record / overdub
#pragma once
#include "../dsp/Synth.h"
#include "../session/Session.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

struct SurfaceCell { std::string text; uint16_t fg = 0, bg = 0; bool known = false; };

class SamplerApp {
public:
    using Send = std::function<void(const uint8_t*, size_t)>;
    static constexpr int kTracks = 8;
    enum Button { OCT_DOWN, OCT_UP, FX, SEL, ENV, LFO, EDIT, GLO, HOME, SAVE, ARP, SEQ, PLAY, REC };
    enum Page { P_HOME, P_FX, P_ENV, P_LFO, P_ARP, P_SEQ, P_MIX, P_KEY, P_SAVE, P_LEN, P_FX2, P_LIVE, P_SQ, P_TEMPO, P_TFX, P_TIMING, P_TONE, P_MOD, P_LIVE2, P_TOMB, P_FX3, P_SHAPE, P_PADS, P_SMPED, P_TEQ, P_DUCK, P_SYN1, P_SYN2, kPages };
    struct Knob {
        std::string name, value;
        float norm = 0;
        bool valid = false, isChoice = false;
        std::function<void(int)> step;       // encoder detents
        std::function<void(float)> set;      // absolute 0..1 (touch)
    };
    using Knobs = std::array<Knob, 4>;
    struct Entry { std::string label, group, path; bool kit; };
    struct ScaleState { int scale = 3, root = 0, octave = 0, mode = 0; };

    SamplerApp(Session& s, Send send);

    void onMidi(const uint8_t* msg, size_t n);
    void onSysex(const uint8_t* msg, size_t n);
    void tick(uint32_t nowMs);                    // ~30 Hz

    // ---- state for the phone UI (all of it can also be edited from there) ----
    Page page() const { return page_; }
    void gotoPage(Page p);
    void gotoMode(int m) { gotoPage(lastPage_[m & 3]); }   // the page the mode was left on
    static const char* pageName(Page p);
    // OP-1 style: four modes on the buttons HOME, FX, SEQ, EDIT, each with up to four pages (the same button steps through them, dots on the screen show where you are)
    static constexpr int kModes = 4;
    static const char* modeName(int mode);        // SOUND, FX, SEQ, MIX
    static const Page* modePages(int mode);       // the pages of a mode (4, the FX mode has 7)
    static int modePageCount(int mode);
    static int modeOf(Page p);
    static int pageSlot(Page p);                  // index of a page inside its mode
    int track() const { return track_; }
    void selectTrack(int t);
    Knobs knobs();
    Knobs knobs(Page p);
    static const char* algoName(int i);
    int algo() const { return ts_[track_].algo; }
    void setAlgo(int a);
    const std::vector<Entry>& library() const { return lib_; }
    int libraryCursor() const { return libCursor_; }
    void browse(int delta);                       // PRESETS: moves the cursor, audition after a short pause
    void loadEntry(int index);                    // at once, on the selected track
    void refreshLibrary();
    void importFile(const std::string& wavPath);  // a file picked from the disk (already copied into the data dir as a WAV)
    bool wantFile() { bool w = wantFile_; wantFile_ = false; return w; }   // the UI asked for the disk picker: Java polls this
    void requestFile() { wantFile_ = true; }
    // looper and recorder
    void transportPlay();
    void loopRecord();                            // REC: arm / end the first take, or toggle overdub once a loop exists
    // sequencer (also used by the phone's piano roll)
    void seqUndo() { s_.seqUndo(track_); say("UNDO"); }
    void seqRedo() { s_.seqRedo(track_); say("REDO"); }
    void seqClear() { s_.seqClear(track_); say("PATTERN CLEARED"); }
    void seqBounce();                             // pattern -> tape for one loop cycle (what REC does while the pattern plays)
    void setSeqState(int s) { ts_[track_].seqState = s; applyPattern(track_); }   // 0 mute, 1 play, 2 rec, 3 step
    void seqToggleAt(int tick, int note, int len = 0);   // touch: add / remove a note (length in ticks, 0 = one step)
    void tapeStopHold(bool on) { tapeStopHeld_ = on; applyMasterFx(); }   // live: while held the tape runs down, on release it spins up again
    bool tapeStopHeld() const { return tapeStopHeld_; }
    std::string padName(int pad) const;                   // KICK, SNARE ... for the synth kits, the number for a sliced sample
    bool isKit(int t) const { return ts_[t].algo == 3; }
    void padHit(int pad, int vel, bool down) {   // a pad of the PADS page: the key 60 + pad with a velocity (records into a pattern that is recording)
        const int note = 60 + std::min(15, std::max(0, pad));
        if (down) { heldNote_[note] = track_; s_.noteOn(track_, note, std::min(127, std::max(1, vel))); } else if (heldNote_[note] >= 0) { s_.noteOff(heldNote_[note], note); heldNote_[note] = -1; }
    }
    void selectPad(int pad) { ts_[track_].selPad = std::min(15, std::max(0, pad)); }
    void resampleNow();                                   // the sound you hear (bars of the loop or of the pattern) becomes a sample on this track
    void applyEdit(int t);
    void setSliceStarts(const std::vector<float>& pts) { s_.setSliceStarts(track_, pts); }
    // pattern banks A..H and the song (a chain of banks played one after the other on every track)
    int patternSlot(int t) const { return st_.seqSlot[t]; }
    void selectPattern(int slot) { s_.seqSelectPattern(track_, slot, false); }
    void copyPattern(int toSlot) { s_.seqCopyPattern(track_, st_.seqSlot[track_], toSlot); }
    const std::vector<int>& songChain() const { return songChain_; }
    bool songOn() const { return songOn_; }
    void songAdd(int slot) { if (songChain_.size() < 64) { songChain_.push_back(std::min(7, std::max(0, slot))); applySong(); } }
    void songPop() { if (!songChain_.empty()) { songChain_.pop_back(); applySong(); } }
    void songClear() { songChain_.clear(); songOn_ = false; applySong(); }
    void songSet(bool on) { songOn_ = on && !songChain_.empty(); applySong(); }
    void tombolaDrop();                                   // a ball with a random note of the scale
    void tombolaReverse() { tSpin_ = 1.f - tSpin_; applyTomb(); }
    void tombolaClear() { s_.tombolaClear(); }
    int tombolaSides() const { return 3 + (int)std::lround(tShape_ * 5.f); }
    void seqDice();                                       // a random pattern in the current scale
    void seqResizeAt(int tick, int note, int len);        // touch: stretch the note that starts at `tick`
    int stepTicks() const { const int g = gridTicks(seqGrid_); return g ? g : kPpq / 4; }
    int seqGridIndex() const { return seqGrid_; }
    void cycleSeqGrid() { seqGrid_ = seqGrid_ >= kGrids - 1 || seqGrid_ < 1 ? 1 : seqGrid_ + 1; applyTempo(); }   // 1/4, 1/8, 1/8T ... 1/32T (the grid of the roll and of the quantize)
    int seqBarsOf(int t) const { return ts_[t].seqBars + 1; }
    void seqAddBarCopy() { if (ts_[track_].seqBars < 15) { ts_[track_].seqBars++; barsLocalAt_[track_] = nowMs_; s_.seqAddBar(track_, true); } }   // one more bar holding a copy of the last one
    void seqSetBarCount(int bars) { ts_[track_].seqBars = std::min(15, std::max(0, bars - 1)); barsLocalAt_[track_] = nowMs_; applyPattern(track_); }   // add or remove bars (1..16)
    void lenAction(bool dbl);
    void loopSave();
    void loopClear();
    void micSample();                             // toggle: record the microphone into a new sample for the selected track
    float micThreshold() const { return 0.005f * std::pow(100.f, micThr_); }   // linear level that starts a take
    void setMicThreshold01(float v) { micThr_ = std::min(1.f, std::max(0.f, v)); s_.setMicThreshold(micThreshold()); }
    float micThreshold01() const { return micThr_; }
    int micState() const { return mic_; }         // 0 idle, 1 waiting for sound, 2 recording, 3 saving
    bool loopExists() const { return loopAny_; }
    std::string loopText() const;
    std::string toast() const { return nowMs_ < toastUntil_ ? toast_ : ""; }
    void say(const std::string& t) { toast_ = t; toastUntil_ = nowMs_ + 1800; }
    // mixer and per-track state
    struct TrackState {
        int algo = 0;
        float a = 0.1f, d = 0.35f, s = 0.8f, r = 0.4f;
        float lfoRate = 0.3f, lfoDepth = 0.f; int lfoTarget = 0, lfoShape = 0;
        float start = 0.f, end = 1.f, speed = 0.5f, pitch = 0.5f;
        int arpRate = 0, arpPatt = 0, arpOct = 0; float arpLen = 0.7f;
        
        float vol = 0.8f, pan = 0.5f, sendR = 0.5f, sendD = 0.2f;
        int entry = -1;
        float tfDj = 0.5f, tfChorus = 0.f, tfCrush = 0.f; int tfGate = 0;   // per track effects
        float rootN = 0.5f, synTone = 0.5f, synBody = 0.5f, synMotion = 0.5f, synGrit = 0.5f, sliceSens = 0.5f; int sliceCount = 3;   // SHAPE page: root key (24..96), synth macros, KIT slices (4 8 12 16 24 32)
        int editOp = 0; float fineN = 0.5f, levelN = 1.f / 3.f;   // SAMPLE EDIT page: the edit to apply, fine tune (+-50 cents), sample level (0..3)
        int edW1 = 0, edW2 = 0, edType = 0, edUni = 0, edVoice = 0; float edDet2 = 0.5f, edCut = 0.5f, edRes = 0.f;   // SYNTH pages: the patch edited (0 = as the patch)
        float tfEqLow = 0.5f, tfEqMid = 0.5f, tfEqHigh = 0.5f, tfComp = 0.f, duckAmt = 0.f;   // MIX mode: track EQ and compressor, how far the sidechain pushes this track down
        float padVol[16], padPitch[16], padDecay[16]; int padGroup[16] = {}; int selPad = 0;   // per pad of a kit (PADS page)
        TrackState() { for (int i = 0; i < 16; i++) { padVol[i] = 2.f / 3.f; padPitch[i] = 0.5f; padDecay[i] = 0.5f; } }
        int seqState = 0, seqBars = 3;   // sequencer: OFF / PLAY / REC, pattern length - 1 (1..16 bars)
    };
    const TrackState& state(int t) const { return ts_[t]; }
    void setVolume(int t, float v); void setPan01(int t, float v); void setSend(int t, int which, float v);
    Session::Status status() const { return st_; }
    bool isSynth(int t) const { const int e = ts_[t].entry; return e >= 0 && e < (int)lib_.size() && (lib_[(size_t)e].path.rfind("synth:", 0) == 0 || lib_[(size_t)e].path.rfind("patch:", 0) == 0); }
    std::string savePatch();                              // the synth of this track with its envelope, macros and edits as a patch in the library (group My Patches)
    std::string resolvePath(int entry) const;             // what the session loads: a patch entry stands for its synth
    bool hasSample(int t) const { return !s_.trackLabel(t).empty(); }
    ScaleState scale() const { return scale_; }
    void setScale(int scale, int root, int octave, int mode);
    static const char* scaleName(int i);
    static const char* noteName(int r);
    static int scaleSteps(int scale, int* out12);              // semitone steps of a scale (0 for OFF), returns their count
    static int snapNote(int note, int scale, int root);        // nearest note of the scale (ties down)
    void playNote(int note, bool down);
    void keyEvent(int rawNote, bool down);        // FM-1 key: chromatic note from the firmware
    bool linkSeen() const { return linkSeen_; }
    std::vector<float> peaks(int t, int bins);    // waveform of a track's sample, cached
    std::string sampleLabel(int t) const { return s_.trackLabel(t); }
    Session& session() { return s_; }
    // ---- look: the colour theme (see Theme.h) and the full screen; both are kept in dataDir/settings.txt ----
    int themeId() const { return theme_; }
    void setTheme(int id);
    bool fullscreen() const { return fullscreen_; }
    void setFullscreen(bool on) { fullscreen_ = on; saveSettings(); }
    // ---- projects: everything (settings, patterns, samples used, tape audio) in dataDir/projects/<name>/ ----
    std::vector<std::string> projectList() const;      // newest first
    std::string newProjectName() const;                // "Project 1", "Project 2" ...
    bool saveProject(const std::string& name);         // the tape audio is written in the background
    bool loadProject(const std::string& name);
    void deleteProject(const std::string& name);
    void autosave(bool force = false);                 // "autosave": every 20 s from tick(), and the UI calls it when the app goes to the background
    void restoreLast();                                // start of the app: back to where it was (skipped once if the last restore never finished)
    std::string currentProject() const { return project_; }
    bool exportMidi();                                 // the patterns as a .mid in dataDir/exports; the file is offered through sharePath()
    bool exportMix();                                  // the tape mix as a .wav (needs a loop)
    bool importMidi(const std::string& path);          // notes of a .mid file become the patterns of the tracks (one track each)
    std::string sharePath() { std::string p = sharePath_; sharePath_.clear(); return p; }   // Java polls: a file to hand to the share sheet
    void requestMidi() { wantMidi_ = true; }
    bool wantMidi() { bool w = wantMidi_; wantMidi_ = false; return w; }

private:
    // ---- FM-1 drawing ----
    void sysex(int cmd, const std::vector<uint8_t>& payload, bool urgent = false, int key = -1);
    void clear(uint16_t c);
    bool rect(int id, int x, int y, int w, int h, uint16_t c);   // true when it was queued (the cached copy on the FM-1 changed)
    void drawText(int id, int x, int y, int scale, uint16_t fg, uint16_t bg, const std::string& s, int width_chars);
    void draw(uint32_t now);
    void leds();
    void flush(uint32_t now);
    void heal();
    void hero(uint32_t now, const Knobs& k);
    // ---- input ----
    void button(int b, bool down);
    void encoder(int i, int delta);
    // ---- parameters ----
    void applyAll(int t);
    void applyShape(int t, bool reslice = false); void applySynthEdit(int t); void applyPad(int t, int pad); void applyPads(int t); void autoTune(); void applyAlgo(int t); void applyEnv(int t); void applyLfo(int t); void applyHome(int t, bool commit = false); void applyArp(int t); void applySeq(int t); void applyMix(int t);
    void applyFx();
    void applyMasterFx();
    void applyTempo(); void applyTomb();
    void applyPattern(int t);
    void applyTrackFx(int t);
    void setTempo(float bpm);
    void queueCommit(int t);
    Knob cont(const char* name, float* v, std::function<std::string(float)> fmt, std::function<void()> apply, float stepSize = 0.02f, bool commit = false);
    Knob choice(const char* name, int* v, std::vector<std::string> opts, std::function<void()> apply);

    std::string projectDir(const std::string& name) const;
    std::string projectsRoot() const;
    static std::string cleanName(const std::string& n);
    int theme_ = 0; bool fullscreen_ = false; void loadSettings(); void saveSettings();
    std::string project_ = "autosave"; uint32_t autosaveAt_ = 0, loadedAt_ = 0; bool tapeDirty_ = false, loading_ = false, wantMidi_ = false, anyRec_ = false;
    int lastBounceSeen_ = 0; double lastLoopSeen_ = 0; std::string sharePath_, pendingShare_; uint32_t pendingShareAt_ = 0;
    void projectTick();
    std::string stateText();                            // the settings as key=value lines
    void applyStateText(const std::string& text);

    Session& s_;
    Send send_;
    Page page_ = P_HOME;
    int track_ = 0;
    std::array<TrackState, kTracks> ts_;
    // global effects
    float reverbSize_ = 0.7f, reverbDw_ = 0.35f, delayTime_ = 0.4f, delayFb_ = 0.45f;
    int loopSrc_ = 1;
    // master fx (FX 2, LIVE FX), tempo page, sequencer grid
    float chorus_ = 0.f, phaser_ = 0.f, comp_ = 0.f, width_ = 0.5f, djFilter_ = 0.5f, crush_ = 0.f;
    float flanger_ = 0.f, drive_ = 0.f, eqLow_ = 0.5f, eqMid_ = 0.5f, eqHigh_ = 0.5f, tremolo_ = 0.f, wah_ = 0.f, ringmod_ = 0.f, tapeStop_ = 0.f;
    std::vector<int> songChain_; bool songOn_ = false; void applySong(); uint32_t barsLocalAt_[kTracks] = {};
    int duckSrc_ = 0, limiter_ = 0; float duckRel_ = 0.3f;   // sidechain source (0 off, 1..8) and its release, master limiter (0 off, then ceilings)
    void applyMaster();
    int reverse_ = 0, freeze_ = 0; bool tapeStopHeld_ = false;
    float spring_ = 0.f, springDamp_ = 0.5f, phone_ = 0.f, punch_ = 0.f;   // OP-1 FX page
    int repeat_ = 0, gate_ = 0, metro_ = 0, takeQuant_ = 0, seqGrid_ = 4, liveGrid_ = 0, countIn_ = 0;
    float swing_ = 0.f, tSpin_ = 0.557f, tGrav_ = 0.55f, tMass_ = 0.72f, tShape_ = 0.6f;   // swing 0..1 = 50..75 %, tombola knobs   // the pattern plays on a 1/16 grid unless told otherwise
    float metroVol_ = 0.6f, tempo_ = 120.f; double lastLoopSamples_ = 0;
    float exciter_ = 0.f, lofi_ = 0.f, wobble_ = 0.f, loopSpeed_ = 2.f / 3.f, micThr_ = 0.452f;   // loop speed is global
    uint32_t repaintAt_ = 0; int repaintCount_ = 0;
    // library
    std::vector<Entry> lib_;
    int libCursor_ = 0, loadedCursor_ = -1;
    uint32_t auditionAt_ = 0, previewOffAt_ = 0; int previewNote_ = -1, previewTrack_ = -1;
    bool wantFile_ = false, previewAfterLoad_ = false;
    // loop / recorder
    Session::Status st_{};
    bool loopAny_ = false;
    int mic_ = 0, micTrack_ = 0;
    uint32_t saveDownAt_ = 0; bool saveHeld_ = false, saveHoldDone_ = false;
    Page lastPage_[kModes] = {P_HOME, P_FX, P_SQ, P_MIX};   // each mode comes back to the page it was left on
    uint32_t seqDownAt_ = 0; bool seqHeld_ = false, seqRecStarted_ = false;   // SEQ held = record the pattern while the keys are played
    void modeButton(int mode);
    int seqSeen_[kTracks] = {}, lastBounceDone_ = 0;
    int lastSampleVersion_ = 0, lastSampleFails_ = 0;
    uint32_t commitAt_ = 0; int commitTrack_ = -1;
    std::string toast_; uint32_t toastUntil_ = 0;
    std::array<std::vector<float>, kTracks> peaks_; std::array<uint32_t, kTracks> peaksAt_{}; std::array<std::string, kTracks> peaksLabel_;
    // FM-1 link
    struct Frame { int key; std::vector<uint8_t> bytes; };
    std::deque<Frame> pending_; size_t pendingBytes_ = 0;
    uint32_t gap_ = 16, lastSend_ = 0, lastPing_ = 0, lastLoss_ = 0, lastRaise_ = 0, dropped_ = 0, healAt_ = 0, lastDraw_ = 0, nowMs_ = 0;
    bool haveDropBase_ = false, needClear_ = true, needResend_ = false, linkSeen_ = false;
    int pingId_ = 0, healCursor_ = 0;
    uint64_t lastLed_ = ~0ull;
    int knobMsb_ = 0;
    int heldNote_[128];
    int keyNotes_[128][3] = {}; int keyCount_[128] = {};
    bool fwDirty_ = true; uint32_t lenAt_ = 0;
    uint32_t held_ = 0;
    ScaleState scale_;
    uint32_t touchedAt_ = 0; int touchedKnob_ = -1;
    std::vector<SurfaceCell> cells_;
    std::vector<uint64_t> rects_;
};
