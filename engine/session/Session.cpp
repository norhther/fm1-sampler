#include "Session.h"
#include "Arp.h"
#include "Common.h"
#include "Filter.h"
#include "MasterFx.h"
#include "Reverb.h"
#include "Sequencer.h"
#include "Sampler.h"
#include "Slicer.h"
#include "Stretch.h"
#include "TapeDelay.h"
#include "TapeInsert.h"
#include "Wav.h"
#include <algorithm>
#include "Tombola.h"
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <thread>

namespace {
constexpr int kBlock = 256;
}

struct Session::Impl {
    struct Note { int note, vel; };
    struct Track {
        std::unique_ptr<Sampler> sampler;
        std::shared_ptr<const SampleBuf> prevOrig;   // the sample before the last edit (EDIT: UNDO)
        mutable std::mutex infoMtx; std::string label; std::vector<float> peaks; float rootGuess = 0.f; int frames = 0; std::vector<float> slicePts, sliceEndPts;   // for the UI threads
        // sound shaping
        Arp arp; bool arpOn = false;
        Lowpass filt; bool filtOn = false;
        Session::Lfo lfo; float lfoPhase = 0, lfoSH = 0, lfoVal = 0;
        TapeInsert tape; float tapeP[6] = {0.f, 1.f, 0.f, 0.f, 0.f, 1.f};   // the cassette's settings, as given (a bake runs its own cassette)
        uint32_t bakeTok = 0;   // a bake on its way carries the token it started with; switching print off (or starting another) changes it, and the bake is dropped
        bool printFx = false;   // the tape of this track holds the instrument AFTER its cassette and effects: it records what they make of it and plays back past them
        float vol = 0.8f, pan = 0.f, send[2] = {0.f, 0.f};
        std::vector<Note> pending;   // notes of this block (without the arpeggiator)
        // offline stretch / pitch
        float speed = 1.f, semis = 0.f, rangeA = 0.f, rangeB = 1.f; uint32_t gen = 0;
        // the audio loop of this track
        std::vector<float> clip;     // interleaved stereo, loopSamples frames
        bool clipHas = false, clipRec = false, recWrote = false;   // recWrote: something but silence landed on the tape since the overdub began
        bool recPrevOk = false; double recPrevCp = 0; float recPrevL = 0, recPrevR = 0;   // tape-speed recording: previous write position and input
        std::atomic<float> peak{0.f};
        // note sequencer pattern (audio thread owns it)
        static constexpr int kSlots = 8;
        int slot = 0, pendingSlot = -1;   // pattern banks A..H: which one is the pattern now, which one starts at the next pattern end
        int lockCut = 0;   // the filter lock of the last note of the pattern
        bool pskip[kSeqMax] = {};   // the chance of a note was lost: it stays quiet this pass
        SeqNote sn[kSeqMax]; int snCount = 0, seqState = 0, seqBars = 1; bool sounding[128] = {}; int openIdx[128] = {};
        // play / stop and key trigger: the pattern runs from `seqPhase` (a tick of the loop), transposed by seqTrans; a start waits for the beat or the bar of the metronome
        bool seqRunning = true, oneShot = false, keyTrig = false, startPending = false, restartNow = false; int64_t seqPhase = 0, seqRan = 0;   // oneShot: the pattern plays one pass and stops; seqRan counts the ticks since the start
        float tuneOf[128] = {}; int seqTrans = 0, pendTrans = 0, trigQ = 1, heldKeys = 0; int8_t sndT[128] = {};
        int seqCursor = 0, stepHeld = 0; bool stepDown[128] = {};   // step entry
        struct SeqSnap { SeqNote sn[kSeqMax]; int count = 0, bars = 1; };   // undo / redo: whole patterns
        static constexpr int kHist = 16;
        SeqSnap hist[kHist], redo[kHist]; int histN = 0, redoN = 0;
        SeqSnap bank[kSlots];   // the patterns that are not the current one (the current one lives in sn / snCount / seqBars)
        // live quantize: notes held back to the grid, and the shift each held key got (its release gets the same)
        struct Delayed { int left, note, vel; };
        static constexpr int kDelayMax = 32;
        Delayed delayed[kDelayMax]; int nDelayed = 0; int lastDelay[128] = {}, lastShift[128] = {};
        std::unique_ptr<MasterFx> fx;   // per track effects (DJ filter, chorus, gate, crush ...)
        int idx = 0;   // which track this is (the global undo log tells tracks apart)
        explicit Track(int sr, int i = 0) : idx(i), tape(sr), fx(new MasterFx(sr, 1.f)) {}
    };

    int sr;
    std::vector<std::unique_ptr<Track>> tracks;
    Reverb reverb; TapeDelay delay;
    float masterVol = 0.9f, ret[2] = {0.35f, 0.7f};
    int loopSrc = 1;   // what the loop records: 0 microphone, 1 the track's sound

    MasterFx mfx;
    bool playing = false;
    float bpm = 120.f; int loopBars = 4;
    // beat clock, metronome, sequencer
    double freeBeat = 0;   // beat position while the transport is stopped
    bool metroOn = false; float metroVol = 0.6f, clickEnv = 0, clickFreq = 1000, clickPh = 0;
    int duckSrc = -1; float duckAmt[kTracks] = {}, duckRel = 0.25f, duckEnv = 0.f; bool duckTrig = false;   // sidechain: a note of the source track pushes the other tracks down
    bool limOn = false; float limCeil = 0.9f, limG = 1.f; std::atomic<int> a_clip{0};   // master limiter; the clip light
    std::atomic<int> a_clockPulses{0}, a_clockEv{0}; bool clkWas = false;   // MIDI clock out: 24 pulses per beat while the transport runs, start / stop events
    bool resampling = false; std::vector<float> resBuf; int64_t resLeft = 0;   // the master output being recorded into a sample
    uint32_t rngState = 99; uint32_t tombolaSeed = 1; float swing = 0.5f; std::atomic<int> busy{0};   // file jobs in flight (project save / load)
    int takeBars = 0, takeQuant = 0, seqGrid = 0, seqRecTrack = -1, inQuant = 0;
    // count-in: clicks for countIn bars, then the take (kind 1) or the sequencer recording (kind 2) starts
    int countIn = 0, countKind = 0; bool counting = false; double countLeft = 0;
    // bounce: the pattern of one track is recorded onto its tape clip for exactly one loop cycle
    int bounceTrack = -1, bounceSrc = 1, bounceBar = 0; int64_t bounceLeft = 0, bounceLen = 0; double bounceAt = 0; bool bounceArmed = false, bounceNew = false;   // bounceBar: 0 = the whole loop from its start, n = one pass of the pattern from bar n of the tape
    std::vector<TickHit> hits;
    double pos = 0; int32_t loopSamples = 0;
    std::atomic<float> tapeSpeed{1.f}; double tapePos = 0;   // ONE tape pointer for every loop clip, so they never drift apart

    // free take
    bool freeRec = false, freeArmed = false; int freeTrack = 0; std::vector<float> freeBuf;

    // microphone sampler
    std::string dataDir, sampleDir;
    bool sampling = false, armedWait = false, waitForSound = true;
    std::vector<float> sampleBuf; std::string samplePath; bool pending = false;
    mutable std::mutex sampleMtx;
    std::atomic<float> micThr{0.04f};
    static constexpr int kLive = 2048;
    std::atomic<float> livePk[kLive]; std::atomic<int> liveN{0}; size_t livePos = 0; float liveAcc = 0; int liveCnt = 0;

    // loop overview, refreshed in the background by the audio thread
    std::atomic<float> a_lpk[kLoopBins]; std::atomic<float> a_tpk[kTracks][kLoopBins]; int lpkBin = 0;

    // commands from other threads, run at the start of each block
    std::mutex cmdMtx; std::vector<std::function<void()>> cmds, run;
    // background worker: sample loading, offline stretch, file writes, freeing
    std::thread worker; std::mutex wMtx; std::condition_variable wCv; std::deque<std::function<void()>> jobs; bool wStop = false;

    // status mirror (the audio thread writes)
    std::atomic<double> a_pos{0};
    std::atomic<bool> a_playing{false}, a_free{false}, a_freeArmed{false}, a_pending{false};
    std::atomic<float> a_master{0.f}, a_freeSec{0.f}, a_sampleSec{0.f}, a_inPeak{0.f}, a_bpm{120.f};
    std::atomic<uint32_t> a_cpu{0}, a_cpuPeak{0}, a_clip_mask{0}, a_cliprec_mask{0};
    std::atomic<int> a_bars{4}, a_sampleState{0}, a_sampleTrack{0}, a_sampleVersion{0}, a_sampleFails{0};
    std::atomic<int32_t> a_loop{0};
    std::atomic<float> a_clipPos{0.f}, a_lfo[kTracks];
    std::atomic<double> a_beat{0.0};
    std::atomic<bool> a_metro{false};
    static constexpr int kSlots = Track::kSlots;
    std::atomic<uint64_t> a_bank[kTracks][kSlots][kSeqMax]; std::atomic<int> a_bankN[kTracks][kSlots], a_bankBars[kTracks][kSlots], a_slot[kTracks], a_slotPending[kTracks], a_songPos{0}, a_songOn{0}, a_songLen{0};
    std::vector<uint8_t> song; bool songOn = false, songWas = false; int songPos = 0, songTick = 0, songBase = -1;   // song: a chain of pattern banks, one step after the other, for every track
    std::atomic<int> a_printDone{0}, a_printBusy{0}; std::atomic<uint32_t> a_printMask{0}; std::atomic<bool> aborting{false}; uint64_t tapeGen = 0;   // tapeGen: bumps with every change of the tapes, so a job that worked on a copy knows the copy is stale
    std::atomic<int> a_countBeats{0}, a_bounce{0}, a_bounceDone{0}, a_seqCursor[kTracks], a_seqUndoN[kTracks], a_seqRedoN[kTracks];
    std::atomic<uint64_t> a_seq[kTracks][kSeqMax]; std::atomic<int> a_seqN[kTracks], a_seqBars[kTracks], a_seqState[kTracks];
    std::atomic<int> a_seqFlags[kTracks], a_seqTrans[kTracks], a_seqPos[kTracks];   // a_seqFlags: run | start pending << 1 | key trigger << 2; a_seqPos: tick inside the pattern that is playing, -1 when it is not
    std::atomic<int> a_voiceN[kTracks]; std::atomic<float> a_voicePos[kTracks][kVoiceMax];

    std::vector<float> bus, tmp, sendBus[2], wet;

    explicit Impl(int rate) : sr(rate), reverb(rate), delay(rate), mfx(rate) {
        for (int i = 0; i < kTracks; i++) tracks.emplace_back(new Track(rate, i));
        bus.resize(kBlock * 2); tmp.resize(kBlock * 2); wet.resize(kBlock * 2); sendBus[0].resize(kBlock * 2); sendBus[1].resize(kBlock * 2);
        for (auto& a : a_lpk) a = 0.f;
        for (auto& r : a_tpk) for (auto& a : r) a = 0.f;
        for (auto& a : a_lfo) a = 0.f;
        for (auto& a : a_voiceN) a = 0;
        for (auto& r : a_seq) for (auto& a : r) a = 0;
        for (int i = 0; i < kTracks; i++) { a_seqFlags[i] = 1; a_seqTrans[i] = 0; a_seqPos[i] = -1; a_seqN[i] = 0; a_seqBars[i] = 1; a_slot[i] = 0; a_slotPending[i] = -1; for (int q = 0; q < kSlots; q++) { a_bankN[i][q] = 0; a_bankBars[i][q] = 1; } a_seqState[i] = 0; a_seqCursor[i] = 0; a_seqUndoN[i] = 0; a_seqRedoN[i] = 0; }
        hits.reserve(64);
        worker = std::thread([this] {
            for (;;) {
                std::function<void()> j;
                { std::unique_lock<std::mutex> l(wMtx); wCv.wait(l, [this] { return wStop || !jobs.empty(); }); if (wStop && jobs.empty()) return; j = std::move(jobs.front()); jobs.pop_front(); }
                j();
            }
        });
    }
    // finishes the queued jobs and ends the worker; the jobs use Session::d, so this has to run while that pointer is still set (~Session), not in ~Impl
    void stopWorker() { { std::lock_guard<std::mutex> l(wMtx); wStop = true; } wCv.notify_all(); if (worker.joinable()) worker.join(); }
    ~Impl() { stopWorker(); }
    void job(std::function<void()> f) { { std::lock_guard<std::mutex> l(wMtx); jobs.push_back(std::move(f)); } wCv.notify_one(); }
    void post(std::function<void()> f) { std::lock_guard<std::mutex> l(cmdMtx); cmds.push_back(std::move(f)); }
    template <class T> void bury(std::unique_ptr<T> p) { if (p) { auto sp = std::make_shared<std::unique_ptr<T>>(std::move(p)); job([sp] { sp->reset(); }); } }   // freed on the worker, not in the audio callback

    // ---- notes ----
    void releaseAll(Track& t) {
        std::vector<Arp::Ev> evs; t.arp.clear(evs);
        t.pending.clear();
        for (bool& b : t.sounding) b = false;
        t.nDelayed = 0;
        for (int& d : t.lastDelay) d = 0;
        for (int& d : t.lastShift) d = 0;
        if (t.sampler) t.sampler->allOff();
    }
    // ---- sequencer ----
    double samplesPerBeat() const { return 60.0 * sr / bpm; }
    static constexpr int kMaxBars = 16;
    int patTicks(const Track& t) const { return patternTicks(t.seqBars); }
    void publishSeq(int ti) {
        Track& t = *tracks[(size_t)ti];
        for (int i = 0; i < t.snCount; i++) a_seq[ti][i].store(packNote(t.sn[i]));
        for (int i = 0; i < t.snCount; i++) a_bank[ti][t.slot][i].store(a_seq[ti][i].load());
        a_bankN[ti][t.slot].store(t.snCount); a_bankBars[ti][t.slot].store(t.seqBars); a_slot[ti].store(t.slot); a_slotPending[ti].store(t.pendingSlot);
        a_seqN[ti].store(t.snCount); a_seqBars[ti].store(t.seqBars); a_seqState[ti].store(t.seqState); a_seqUndoN[ti].store(t.histN); a_seqRedoN[ti].store(t.redoN);
        a_seqFlags[ti].store((t.seqRunning ? 1 : 0) | (t.startPending ? 2 : 0) | (t.keyTrig ? 4 : 0) | (t.oneShot ? 8 : 0)); a_seqTrans[ti].store(t.seqTrans);
    }
    void noteTo(Track& t, int note, int vel, int offset, std::vector<Arp::Ev>& evs) {   // a sequencer note: through the arpeggiator when it is on
        if (t.arpOn) { if (vel) t.arp.noteOn(note); else t.arp.noteOff(note); }
        else evs.push_back({offset, note, vel});
    }
    static int clampNote(int n) { return std::min(127, std::max(0, n)); }
    void seqRelease(Track& t) {
        std::vector<Arp::Ev> evs;
        for (int n = 0; n < 128; n++) if (t.sounding[n]) { t.sounding[n] = false; noteTo(t, clampNote(n + t.sndT[n]), 0, 0, evs); }
        for (auto& e : evs) t.pending.push_back({e.note, 0});
        for (int& o : t.openIdx) o = 0;
    }
    // ---- pattern banks and the song ----
    void doSwitch(int ti, int slot) {
        Track& t = *tracks[(size_t)ti]; slot = std::min(kSlots - 1, std::max(0, slot));
        t.pendingSlot = -1;
        if (slot == t.slot) { publishSeq(ti); return; }
        snapOf(t, t.bank[t.slot]);
        const Track::SeqSnap& n = t.bank[slot];
        std::copy(n.sn, n.sn + n.count, t.sn); t.snCount = n.count; t.seqBars = std::max(1, n.bars);
        t.slot = slot; t.histN = t.redoN = 0;
        for (int& o : t.openIdx) o = 0;
        seqRelease(t); publishSeq(ti);
    }
    void selectSlot(int ti, int slot, bool immediate) {
        Track& t = *tracks[(size_t)ti]; slot = std::min(kSlots - 1, std::max(0, slot));
        if (immediate || !(playing && t.seqState > 0)) doSwitch(ti, slot); else { t.pendingSlot = slot == t.slot ? -1 : slot; publishSeq(ti); }   // while it plays the next pattern starts at the end of this one
    }
    void copySlot(int ti, int from, int to) {
        Track& t = *tracks[(size_t)ti]; from = std::min(kSlots - 1, std::max(0, from)); to = std::min(kSlots - 1, std::max(0, to));
        if (from == to) return;
        Track::SeqSnap src; if (from == t.slot) snapOf(t, src); else src = t.bank[from];
        if (to == t.slot) { checkpoint(t); restoreSnap(ti, src); }
        else { t.bank[to] = src; for (int i = 0; i < src.count; i++) a_bank[ti][to][i].store(packNote(src.sn[i])); a_bankN[ti][to].store(src.count); a_bankBars[ti][to].store(src.bars); }
    }
    int slotBars(const Track& t, int slot) const { return slot == t.slot ? (t.snCount ? t.seqBars : 0) : (t.bank[slot].count ? t.bank[slot].bars : 0); }
    int songStepTicks(int slot) const { int bars = 1; for (auto& tr : tracks) bars = std::max(bars, slotBars(*tr, slot)); return patternTicks(bars); }
    void songAdvance() {   // once per block: the song moves on when the length of a step is over; every track gets the pattern bank of the step
        songBase = -1;
        if (!songOn || song.empty()) { songWas = false; a_songOn.store(0); return; }
        a_songOn.store(1); a_songLen.store((int)song.size());
        if (!playing) { songWas = false; return; }
        if (!songWas) { songWas = true; songPos = 0; songTick = 0; for (int ti = 0; ti < kTracks; ti++) doSwitch(ti, song[0]); }
        const int len = songStepTicks(song[(size_t)songPos]);
        if (songTick >= len) { songTick -= len; songPos = (songPos + 1) % (int)song.size(); for (int ti = 0; ti < kTracks; ti++) doSwitch(ti, song[(size_t)songPos]); }
        a_songPos.store(songPos); songBase = songTick;
    }
    // where a note starts when it plays: on the quantize grid, every second grid line pushed back by the swing (only while quantize is on)
    int onTick(const SeqNote& e, int pt) const {
        const int q = snapTick(e.tick, seqGrid, pt), g = gridTicks(seqGrid);
        int t = q;
        if (g != 0 && swing > 0.5f && (q / g) % 2 != 0) t += (int)std::lround((swing - 0.5f) * 2.f * g);
        return ((t + e.nudge) % pt + pt) % pt;
    }
    // ratchet: a note with r repeats plays r equal parts of its length; the repeats before the last are gated to 3/4 of their part
    static int ratchetPart(const SeqNote& e) { return std::max(1, e.len / std::max<int>(1, e.ratchet)); }
    int repeatOn(const SeqNote& e, int pt, int k) const { return (onTick(e, pt) + k * ratchetPart(e)) % pt; }
    int repeatOff(const SeqNote& e, int pt, int k) const {
        const int r = std::max<int>(1, e.ratchet), part = ratchetPart(e);
        return (repeatOn(e, pt, k) + (k == r - 1 ? std::max(1, e.len - (r - 1) * part) : std::max(1, part * 3 / 4))) % pt;
    }
    // audio thread, per block: fire the notes whose tick is hit
    void seqRun(int ti, Track& t, std::vector<Arp::Ev>& evs) {
        int pt = patTicks(t);
        int hi = -1;
        for (const TickHit& h : hits) {
            hi++;
            if (t.startPending && (t.restartNow || t.trigQ == 0 || h.tick % (t.trigQ == 2 ? kPpq * 4 : kPpq) == 0)) {   // the pattern starts from its first tick, on the beat or the bar
                t.startPending = false; t.restartNow = false; t.seqRunning = true; t.seqRan = 0; t.seqPhase = h.tick; t.seqTrans = t.pendTrans; publishSeq(ti);
            }
            if (!t.seqRunning) continue;
            if (t.oneShot && t.seqRan >= pt) { t.seqRunning = false; seqRelease(t); publishSeq(ti); continue; }   // ONE SHOT: the pass is over
            t.seqRan++;
            const int jt = songBase >= 0 ? (songBase + hi) % pt : (int)((((h.tick - t.seqPhase) % pt) + pt) % pt);
            if (t.pendingSlot >= 0 && jt == 0) { doSwitch(ti, t.pendingSlot); pt = patTicks(t); }
            for (int i = 0; i < t.snCount; i++) {   // note offs first, so a repeated note retriggers
                const SeqNote& e = t.sn[i];
                if (!t.sounding[e.note]) continue;
                for (int k = 0; k < std::max<int>(1, e.ratchet); k++) if (repeatOff(e, pt, k) == jt) { t.sounding[e.note] = false; noteTo(t, clampNote(e.note + t.sndT[e.note]), 0, h.offset, evs); break; }
            }
            for (int i = 0; i < t.snCount; i++) {
                const SeqNote& e = t.sn[i];
                for (int k = 0; k < std::max<int>(1, e.ratchet); k++) {
                    if (repeatOn(e, pt, k) != jt) continue;
                    if (k == 0) { rngState = rngState * 1664525u + 1013904223u; t.pskip[i] = e.prob < 100 && (int)((rngState >> 8) % 100u) >= e.prob; }   // the chance is rolled once per note, its repeats follow
                    if (t.pskip[i]) continue;
                    if (t.sounding[e.note]) noteTo(t, clampNote(e.note + t.sndT[e.note]), 0, h.offset, evs);
                    t.lockCut = e.cut;   // a note with a filter lock closes the track filter, a note without one lets it go
                    const bool kit = t.sampler && t.sampler->kitSample(); t.sounding[e.note] = true; t.sndT[e.note] = kit ? 0 : (int8_t)t.seqTrans; t.tuneOf[e.note] = kit ? (float)t.seqTrans : 0.f; noteTo(t, clampNote(e.note + (kit ? 0 : t.seqTrans)), e.vel, h.offset, evs);   // chops: a key moves the PITCH of the pattern, not the chop it plays
                }
            }
        }
    }
    // undo / redo of whole patterns: a snapshot is taken before every edit
    static void snapOf(const Track& t, Track::SeqSnap& s) { std::copy(t.sn, t.sn + t.snCount, s.sn); s.count = t.snCount; s.bars = t.seqBars; }
    // global undo / redo: one log over every track, pattern edits and sample edits in the order they were made (a stale entry whose own history is gone is skipped)
    struct GEntry { int track; int kind; };   // kind: 0 a pattern, 1 a sample edit, 2 the tape
    static constexpr int kGLog = 64;
    GEntry glog[kGLog], gredo[kGLog]; int glogN = 0, gredoN = 0; std::atomic<int> a_gUndoN{0}, a_gRedoN{0};
    static void gPush(GEntry* a, int& n, GEntry e) { if (n == kGLog) { std::move(a + 1, a + kGLog, a); n--; } a[n++] = e; }
    // what the UNDO / REDO buttons show: only the steps that can really be taken (the tape keeps the last few snapshots, a sample edit needs its old sample)
    int gValid(const GEntry* a, int n, size_t tapeSteps) const {
        int c = 0;
        for (int i = n - 1; i >= 0; i--) {
            if (a[i].kind == 2) { if (tapeSteps == 0) continue; tapeSteps--; }
            else if (a[i].kind == 1 && !tracks[(size_t)a[i].track]->prevOrig) continue;
            c++;
        }
        return c;
    }
    void gPublish() { a_gUndoN = gValid(glog, glogN, tapeHist.size()); a_gRedoN = gValid(gredo, gredoN, tapeRedo.size()); }
    void gReset() {   // a new or a loaded project: nothing before it can be undone
        glogN = gredoN = 0; tapeHist.clear(); tapeRedo.clear();
        for (auto& t : tracks) { t->histN = 0; t->redoN = 0; t->prevOrig.reset(); }
        gPublish();
    }
    void gRecord(int track, int kind) { gPush(glog, glogN, {track, kind}); gredoN = 0; tapeRedo.clear(); gPublish(); }
    // tape undo: the clips (and the loop length) as they were before a first take, an overdub, a pattern bounce or a change of the tempo
    struct TapeSnap { int track = -1; int32_t loop = 0; int bars = 1; float bpm = 120.f; std::vector<float> clip[kTracks]; bool print[kTracks] = {}; };   // track -1: every clip and the loop length
    static constexpr int kTapeHist = 6;
    std::deque<TapeSnap> tapeHist, tapeRedo;
    TapeSnap tapeSnapOf(int track) const {
        TapeSnap s; s.track = track; s.loop = loopSamples; s.bars = loopBars; s.bpm = bpm;
        for (int i = 0; i < kTracks; i++) { s.print[i] = tracks[(size_t)i]->printFx; if ((track < 0 || track == i) && tracks[(size_t)i]->clipHas) s.clip[i] = tracks[(size_t)i]->clip; }
        return s;
    }
    void tapeApply(const TapeSnap& s) {
        tapeGen++;
        for (int i = 0; i < kTracks; i++) if (s.track < 0 || s.track == i) { Track& t = *tracks[(size_t)i]; t.clipRec = false; t.recPrevOk = false; t.clip = s.clip[i]; t.clipHas = !t.clip.empty(); t.printFx = s.print[i]; }
        if (s.track < 0 || (loopSamples == 0 && s.loop > 0)) { if (loopSamples != s.loop) { pos = 0; tapePos = 0; } loopSamples = s.loop; a_loop = s.loop; loopBars = s.bars; a_bars = s.bars; bpm = s.bpm; a_bpm = s.bpm; }
        clearIfEmpty();
    }
    void tapePush(TapeSnap s) { tapeGen++; const int tr = s.track; tapeHist.push_back(std::move(s)); if ((int)tapeHist.size() > kTapeHist) tapeHist.pop_front(); gRecord(tr, 2); }
    void tapeCheckpoint(int track) { tapePush(tapeSnapOf(track)); }
    void dropTapeCheckpoint(int track) {   // an overdub that recorded only silence leaves no undo step
        if (glogN > 0 && glog[glogN - 1].kind == 2 && glog[glogN - 1].track == track && !tapeHist.empty() && tapeHist.back().track == track) { glogN--; tapeHist.pop_back(); gPublish(); }
    }
    void tapeCheckpointEmpty() { TapeSnap s; s.bpm = bpm; for (int i = 0; i < kTracks; i++) s.print[i] = tracks[(size_t)i]->printFx; tapePush(std::move(s)); }   // a first take: before it there was no tape
    bool tapeUndoStep() {
        if (tapeHist.empty()) return false;
        TapeSnap s = std::move(tapeHist.back()); tapeHist.pop_back();
        TapeSnap cur = tapeSnapOf(s.track); tapeApply(s);
        tapeRedo.push_back(std::move(cur)); if ((int)tapeRedo.size() > kTapeHist) tapeRedo.pop_front();
        return true;
    }
    bool tapeRedoStep() {
        if (tapeRedo.empty()) return false;
        TapeSnap s = std::move(tapeRedo.back()); tapeRedo.pop_back();
        TapeSnap cur = tapeSnapOf(s.track); tapeApply(s);
        tapeHist.push_back(std::move(cur)); if ((int)tapeHist.size() > kTapeHist) tapeHist.pop_front();
        return true;
    }
    void checkpoint(Track& t) {
        gRecord(t.idx, 0);
        if (t.histN == Track::kHist) { std::move(t.hist + 1, t.hist + Track::kHist, t.hist); t.histN--; }
        snapOf(t, t.hist[t.histN++]); t.redoN = 0;
    }
    void restoreSnap(int ti, const Track::SeqSnap& s) {
        Track& t = *tracks[(size_t)ti];
        std::copy(s.sn, s.sn + s.count, t.sn); t.snCount = s.count; t.seqBars = s.bars;
        for (int& o : t.openIdx) o = 0;
        seqRelease(t); publishSeq(ti);
    }
    bool seqUndoStep(int ti) {
        Track& t = *tracks[(size_t)ti];
        if (t.histN == 0) return false;
        if (t.redoN == Track::kHist) { std::move(t.redo + 1, t.redo + Track::kHist, t.redo); t.redoN--; }
        snapOf(t, t.redo[t.redoN++]);
        restoreSnap(ti, t.hist[--t.histN]);
        return true;
    }
    bool seqRedoStep(int ti) {
        Track& t = *tracks[(size_t)ti];
        if (t.redoN == 0) return false;
        if (t.histN == Track::kHist) { std::move(t.hist + 1, t.hist + Track::kHist, t.hist); t.histN--; }
        snapOf(t, t.hist[t.histN++]);
        restoreSnap(ti, t.redo[--t.redoN]);
        return true;
    }
    int stepTicks() const { const int g = gridTicks(seqGrid); return g ? g : kPpq / 4; }   // step entry advances by the quantize grid (1/16 when it is off)
    void seqRemove(Track& t, int i) { for (int k = i; k + 1 < t.snCount; k++) t.sn[k] = t.sn[k + 1]; t.snCount--; for (int& o : t.openIdx) if (o > i + 1) o--; else if (o == i + 1) o = 0; }
    void seqCursorTo(int ti, int tick) {
        Track& t = *tracks[(size_t)ti]; const int pt = patTicks(t);
        t.seqCursor = ((tick % pt) + pt) % pt; a_seqCursor[ti].store(t.seqCursor);
    }
    void seqStepMove(int ti, int steps) {
        Track& t = *tracks[(size_t)ti]; const int st = stepTicks();
        seqCursorTo(ti, t.seqCursor / st * st + steps * st);
    }
    // step entry: a key writes at the cursor (the same key on the same step erases it); the cursor moves on when the last key of a chord is up
    void seqStepNote(int ti, int note, int vel) {
        Track& t = *tracks[(size_t)ti];
        if (vel > 0) {
            if (t.stepHeld == 0) checkpoint(t);   // one step = one undo
            t.stepHeld++; t.stepDown[note] = true;
            for (int i = 0; i < t.snCount; i++) if (t.sn[i].note == note && t.sn[i].tick == t.seqCursor) { seqRemove(t, i); seqRelease(t); publishSeq(ti); return; }
            if (t.snCount < kSeqMax) { t.sn[t.snCount++] = {(uint16_t)t.seqCursor, (uint16_t)std::max(6, stepTicks() * 3 / 4), (uint8_t)note, (uint8_t)vel}; publishSeq(ti); }
        } else if (t.stepDown[note]) {
            t.stepDown[note] = false;
            if (--t.stepHeld <= 0) { t.stepHeld = 0; seqStepMove(ti, 1); }
        }
    }
    void seqToggleNote(int ti, int tick, int len, int note, int vel) {
        Track& t = *tracks[(size_t)ti]; const int pt = patTicks(t); tick = ((tick % pt) + pt) % pt;
        checkpoint(t);
        for (int i = 0; i < t.snCount; i++) if (t.sn[i].note == note && tick >= t.sn[i].tick && tick < t.sn[i].tick + std::max<int>(1, t.sn[i].len)) { seqRemove(t, i); seqRelease(t); publishSeq(ti); return; }
        if (t.snCount >= kSeqMax) return;
        t.sn[t.snCount++] = {(uint16_t)tick, (uint16_t)std::max(1, len), (uint8_t)note, (uint8_t)std::min(127, std::max(1, vel))};
        publishSeq(ti);
    }
    void seqSetNotes(int ti, const std::vector<SeqNote>& list) {
        Track& tr = *tracks[(size_t)ti]; const int pt = patTicks(tr);
        checkpoint(tr); tr.snCount = 0;
        for (auto sn : list) if (sn.tick < pt && tr.snCount < kSeqMax) tr.sn[tr.snCount++] = sn;
        seqRelease(tr); publishSeq(ti);
    }
    // a selection of notes: delete, transpose, shift, duplicate, velocity / probability / ratchet / nudge (one undo step)
    void seqApplyOp(int ti, const std::vector<Session::SeqRef>& refs, int op, int arg) {
        Track& t = *tracks[(size_t)ti]; const int pt = patTicks(t);
        auto sel = [&](const SeqNote& n) { for (auto& r : refs) if (r.tick == n.tick && r.note == n.note) return true; return false; };
        checkpoint(t);
        if (op == Session::OP_DELETE) { int w = 0; for (int i = 0; i < t.snCount; i++) if (!sel(t.sn[i])) t.sn[w++] = t.sn[i]; t.snCount = w; }
        else if (op == Session::OP_DUP) {
            const int n0 = t.snCount;
            for (int i = 0; i < n0 && t.snCount < kSeqMax; i++) if (sel(t.sn[i])) { SeqNote c = t.sn[i]; c.tick = (uint16_t)(((int)c.tick + arg) % pt + pt) % pt; t.sn[t.snCount++] = c; }
        } else for (int i = 0; i < t.snCount; i++) {
            if (!sel(t.sn[i])) continue; SeqNote& n = t.sn[i];
            switch (op) {
                case Session::OP_TRANSPOSE: n.note = (uint8_t)std::min(127, std::max(0, (int)n.note + arg)); break;
                case Session::OP_SHIFT: n.tick = (uint16_t)((((int)n.tick + arg) % pt + pt) % pt); break;
                case Session::OP_VEL: n.vel = (uint8_t)std::min(127, std::max(1, arg)); break;
                case Session::OP_PROB: n.prob = (uint8_t)std::min(100, std::max(0, arg)); break;
                case Session::OP_RATCHET: n.ratchet = (uint8_t)std::min(8, std::max(1, arg)); break;
                case Session::OP_NUDGE: n.nudge = (int8_t)std::min(15, std::max(-16, arg)); break;
                case Session::OP_CUT: n.cut = (uint8_t)std::min(127, std::max(0, arg)); break;
            }
        }
        for (int& o : t.openIdx) o = 0;
        seqRelease(t); publishSeq(ti);
    }
    void seqAddBarOp(int ti, bool copyLast) {
        Track& t = *tracks[(size_t)ti]; if (t.seqBars >= kMaxBars) return;
        const int lastLo = (t.seqBars - 1) * 4 * kPpq;
        seqBarsSet(ti, t.seqBars + 1);
        if (copyLast) { const int n0 = t.snCount; for (int i = 0; i < n0 && t.snCount < kSeqMax; i++) if (t.sn[i].tick >= lastLo && t.sn[i].tick < lastLo + 4 * kPpq) { SeqNote c = t.sn[i]; c.tick = (uint16_t)(c.tick + 4 * kPpq); t.sn[t.snCount++] = c; } publishSeq(ti); }
    }
    // touch edit: the note at (tick, note) gets a new length (at least 1 tick, at most to the end of the pattern)
    void seqResizeNote(int ti, int tick, int note, int len) {
        Track& t = *tracks[(size_t)ti]; const int pt = patTicks(t);
        for (int i = 0; i < t.snCount; i++) if (t.sn[i].note == note && t.sn[i].tick == tick) {
            const int nl = std::max(1, std::min(len, pt - tick));
            if (nl == t.sn[i].len) return;
            checkpoint(t); t.sn[i].len = (uint16_t)nl; seqRelease(t); publishSeq(ti); return;
        }
    }
    // ---- bounce: pattern -> tape ----
    void bounceStart(int ti, int bar) {
        Track& t = *tracks[(size_t)ti];
        if (t.snCount == 0 || freeRec || freeArmed || counting || bounceTrack >= 0) return;
        bounceSrc = loopSrc; loopSrc = 1;   // the tape takes the sound of the track, not the microphone
        bounceTrack = ti; bounceNew = loopSamples == 0;
        bounceBar = 0; bounceLen = 0; bounceAt = 0;
        if (!bounceNew && bar > 0) {   // one pass of the pattern from a bar of the tape (it must fit before the loop end)
            const double bs = 4 * samplesPerBeat();
            const int pb = std::min(t.seqBars, std::max(1, loopBars));
            bounceBar = std::min(bar, loopBars - pb + 1);
            bounceAt = (bounceBar - 1) * bs; bounceLen = (int64_t)std::lround(pb * bs);
        }
        if (t.seqState != 1) seqState(ti, 1);
        for (auto& tr : tracks) releaseAll(*tr);
        if (bounceNew) {   // no loop yet: it gets the length of the pattern
            tapeCheckpointEmpty();
            const double spb = samplesPerBeat();
            loopSamples = (int32_t)std::lround(t.seqBars * 4 * spb); a_loop = loopSamples;
            loopBars = std::min(64, t.seqBars); a_bars = loopBars;
            t.clip.assign((size_t)loopSamples * 2, 0.f); t.clipHas = true; t.clipRec = true;
            pos = 0; tapePos = 0; playing = true; bounceLeft = loopSamples; a_bounce = 2;
        } else { bounceArmed = true; playing = true; a_bounce = 1; }   // recording starts when the loop comes round
    }
    void bounceFinish() {
        if (bounceTrack < 0) return;
        Track& t = *tracks[(size_t)bounceTrack];
        t.clipRec = false; t.recPrevOk = false; loopSrc = bounceSrc;
        seqState(bounceTrack, 0);   // the tape holds the pattern now: it is muted so it does not play twice
        bounceTrack = -1; bounceArmed = false; bounceLeft = 0; a_bounce = 0; a_bounceDone++;
    }
    void bounceCancel() {
        if (bounceTrack < 0) return;
        Track& t = *tracks[(size_t)bounceTrack];
        t.clipRec = false; loopSrc = bounceSrc;
        if (bounceNew) { t.clip.clear(); t.clipHas = false; clearIfEmpty(); }
        bounceTrack = -1; bounceArmed = false; bounceLeft = 0; a_bounce = 0;
    }
    void seqCapture(int ti, int note, int vel, int shift) {   // shift: samples to the quantized moment (live quantize), else 0
        Track& t = *tracks[(size_t)ti];
        const int pt = patTicks(t);
        const int cur = (int)std::fmod(std::max(0.0, (pos + shift) / samplesPerBeat() * kPpq - (double)(t.seqRunning ? t.seqPhase : 0)) + 0.5 + (double)pt * 64, (double)pt);
        if (vel > 0) {
            if (t.snCount >= kSeqMax) return;
            t.sn[t.snCount] = {(uint16_t)cur, (uint16_t)(kPpq / 4), (uint8_t)note, (uint8_t)vel};
            t.openIdx[note] = ++t.snCount;
        } else if (t.openIdx[note]) {
            SeqNote& n = t.sn[t.openIdx[note] - 1];
            n.len = (uint16_t)std::max(1, (cur - n.tick + pt) % pt);
            t.openIdx[note] = 0;
        } else return;
        publishSeq(ti);
    }
    void seqState(int ti, int state) {
        Track& t = *tracks[(size_t)ti];
        state = std::min(3, std::max(0, state));
        if (state == 2 && t.seqState != 2) checkpoint(t);   // one recording pass = one undo step
        t.stepHeld = 0; for (bool& b : t.stepDown) b = false;
        const bool wasPlaying = playing;
        if (state == 2) {
            if (seqRecTrack >= 0 && seqRecTrack != ti) { Track& o = *tracks[(size_t)seqRecTrack]; if (o.seqState == 2) { o.seqState = 1; for (int& x : o.openIdx) x = 0; publishSeq(seqRecTrack); } }
            seqRecTrack = ti;
            if (!playing && !counting) { if (countIn > 0 && loopSamples == 0) startCount(2); else { playing = true; if (loopSamples == 0) pos = 0; } }   // from a stopped transport the pattern starts at bar 1
        }
        if (state != 2) for (int& x : t.openIdx) x = 0;
        if (state == 0) { seqRelease(t); if (counting && countKind == 2) { counting = false; countKind = 0; } }
        if (state == 2 && !t.keyTrig) { if (!wasPlaying) { t.seqRunning = true; t.seqPhase = 0; t.seqRan = 0; } else if (!t.seqRunning) t.startPending = true; }   // recording: from a stopped transport the pattern starts with it, from its first tick; while the transport runs a stopped pattern starts on the next beat
        t.seqState = state; publishSeq(ti);
    }
    void seqBarsSet(int ti, int bars) {
        Track& t = *tracks[(size_t)ti];
        bars = std::min(kMaxBars, std::max(1, bars));
        if (bars == t.seqBars) return;
        checkpoint(t);
        const int oldPt = patTicks(t);
        t.seqBars = bars;
        const int newPt = patTicks(t);
        if (newPt < oldPt) {   // shorter: what lies behind the new end is dropped
            int w = 0; for (int i = 0; i < t.snCount; i++) if (t.sn[i].tick < newPt) t.sn[w++] = t.sn[i];
            t.snCount = w;
        }   // longer: the new bars are empty (copy a bar by hand, or with the dice)
        for (int& x : t.openIdx) x = 0;
        seqRelease(t); publishSeq(ti);
    }
    // samples from now to the nearest grid line of the live quantize (negative: it just passed)
    int quantShift() const {
        const double spb = samplesPerBeat(), tl = spb / kPpq, g = gridTicks(inQuant), tick = (playing ? pos : freeBeat * spb) / tl;
        return (int)std::lround((std::round(tick / g) * g - tick) * tl);
    }
    // tombola: the balls play the notes they carry when they hit a wall; the UI reads a snapshot of the balls
    Tombola tomb; std::vector<Tombola::Hit> tombHits; struct TombOff { int track, note; float left; }; std::vector<TombOff> tombOffs;
    std::atomic<uint32_t> a_ball[Tombola::kMaxBalls][4]; std::atomic<int> a_ballN{0}; std::atomic<uint32_t> a_drum{0};
    static uint32_t bitsOf(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }
    static float floatOf(uint32_t u) { float f; std::memcpy(&f, &u, 4); return f; }
    void runTombola(int n) {
        for (size_t i = tombOffs.size(); i-- > 0;) if ((tombOffs[i].left -= (float)n / sr) <= 0.f) { liveNote(tombOffs[i].track, tombOffs[i].note, 0); tombOffs.erase(tombOffs.begin() + (long)i); }
        if (tomb.balls().empty() && a_ballN.load() == 0) return;
        tombHits.clear(); tomb.run((float)n / sr, tombHits);
        for (auto& h : tombHits) { liveNote(h.track, h.note, h.vel); tombOffs.push_back({h.track, h.note, h.len / 1000.f}); }
        const auto& bl = tomb.balls(); const int cnt = (int)bl.size();
        for (int i = 0; i < cnt; i++) { a_ball[i][0] = bitsOf(bl[(size_t)i].x); a_ball[i][1] = bitsOf(bl[(size_t)i].y); a_ball[i][2] = bitsOf(bl[(size_t)i].hit); a_ball[i][3] = (uint32_t)bl[(size_t)i].track; }
        a_ballN = cnt; a_drum = bitsOf(tomb.angle());
    }
    void liveNote(int ti, int note, int vel) {
        Track& t = *tracks[ti];
        bool startsTake = false;
        if (freeArmed && !counting && ti == freeTrack && loopSrc == 1 && vel > 0) { freeArmed = false; a_freeArmed = false; freeRec = true; playing = true; pos = 0; clickEnv = 0; startsTake = true; }   // the take starts with the first note
        if (t.keyTrig) {   // the key is the pattern: it starts it, the other keys move it up or down by the distance from C4, the last key let go stops it
            if (vel > 0) { t.heldKeys++; t.pendTrans = std::min(48, std::max(-48, note - 60)); if (t.seqRunning) t.seqTrans = t.pendTrans; else t.startPending = true; if (!playing) { playing = true; pos = 0; } }
            else if (--t.heldKeys <= 0) { t.heldKeys = 0; t.seqRunning = false; t.startPending = false; seqRelease(t); }
            publishSeq(ti); return;
        }
        int delay = 0, shift = 0;
        if (inQuant > 0 && !startsTake) {
            if (vel > 0) { shift = quantShift(); delay = std::max(0, shift); t.lastShift[note] = shift; t.lastDelay[note] = delay; }
            else { shift = t.lastShift[note]; delay = t.lastDelay[note]; t.lastShift[note] = t.lastDelay[note] = 0; }
        }
        if (t.seqState == 2 && playing) seqCapture(ti, note, vel, shift);
        else if (t.seqState == 3) seqStepNote(ti, note, vel);
        if (delay > 0 && t.nDelayed < Track::kDelayMax) { t.delayed[t.nDelayed++] = {delay, note, vel}; return; }   // the key sounds on the grid line
        if (t.arpOn) { if (vel) t.arp.noteOn(note); else t.arp.noteOff(note); }
        else t.pending.push_back({note, vel});
    }
    void startCount(int kind) {
        counting = true; countKind = kind; countLeft = countIn * 4.0 * samplesPerBeat(); freeBeat = 0; playing = false; clickEnv = 0;
    }
    void endCount() {
        counting = false;
        if (countKind == 1 && freeArmed) { freeArmed = false; a_freeArmed = false; freeRec = true; playing = true; pos = 0; clickEnv = 0; }
        else if (countKind == 2) { playing = true; if (loopSamples == 0) pos = 0; }
        countKind = 0;
    }

    // ---- free take ----
    void freeStart(int track) {
        for (auto& t : tracks) { releaseAll(*t); t->clip.clear(); t->clip.shrink_to_fit(); t->clipHas = false; t->clipRec = false; }
        loopSamples = 0; a_loop = 0;
        freeArmed = true; a_freeArmed = true; freeRec = false; freeTrack = track;
        freeBuf.clear(); freeBuf.reserve((size_t)sr * 2 * 30);
        pos = 0; playing = false;
        if (countIn > 0) startCount(1);
    }
    void freeStop() {
        if (freeArmed) { freeArmed = false; a_freeArmed = false; counting = false; countKind = 0; return; }   // cancelled before any sound
        if (!freeRec) return;
        freeRec = false;
        const double len = pos;
        if (len < 0.25 * sr) { pos = 0; freeBuf.clear(); return; }   // too short: treat as cancelled
        tapeCheckpointEmpty();   // UNDO of a take: the tape is empty again
        if (takeBars > 0) {   // the user said how many bars: the tempo stays and the loop is exactly that long
            const double spb = samplesPerBeat();
            const int fit = std::max(1, (int)(180.0 * sr / (4 * spb)));   // the tape holds 3 minutes: at a slow tempo fewer bars fit
            loopBars = std::min(takeBars, fit); a_bars = loopBars;
            loopSamples = (int32_t)std::lround(loopBars * 4 * spb); a_loop = loopSamples;
        } else if (takeQuant > 0) {   // the tempo stays: the take is cut or padded to whole beats / bars
            const double spb = samplesPerBeat(), unit = takeQuant == 1 ? spb : 4 * spb;
            const double units = std::max(1.0, std::round(len / unit));
            loopSamples = (int32_t)std::lround(std::min(units * unit, 180.0 * sr)); a_loop = loopSamples;
            loopBars = std::min(64, std::max(1, (int)std::lround(loopSamples / (4 * spb)))); a_bars = loopBars;
        } else {
            // bars and tempo that fit the take (shown to the user; the loop length is exactly the take)
            const double barSec = 240.0 / bpm;
            int n = std::min(64, std::max(1, (int)std::lround(len / sr / barSec)));
            float nb = (float)(n * 240.0 * sr / len);
            while (nb > 300.f && n > 1) { n--; nb = (float)(n * 240.0 * sr / len); }
            while (nb < 30.f && n < 64) { n++; nb = (float)(n * 240.0 * sr / len); }
            bpm = std::min(300.f, std::max(30.f, nb)); a_bpm = bpm; loopBars = n; a_bars = n;
            loopSamples = (int32_t)std::lround(len); a_loop = loopSamples;
        }
        Track& ft = *tracks[freeTrack];
        if (!freeBuf.empty()) { freeBuf.resize((size_t)loopSamples * 2, 0.f); ft.clip = std::move(freeBuf); ft.clipHas = true; }
        freeBuf = {};
        pos = 0; tapePos = 0;
    }
    void clearIfEmpty() {
        for (auto& t : tracks) if (t->clipHas) return;
        loopSamples = 0; a_loop = 0; pos = 0; tapePos = 0;
    }

    // ---- samples ----
    static std::vector<float> slicePoints(const Sampler& sm) {   // slice starts as 0..1 of the sample (for the waveform)
        std::vector<float> o; auto b = sm.original(); if (!b || b->frames <= 0) return o;
        for (int p : sm.slices()) o.push_back((float)p / (float)b->frames);
        return o;
    }
    static std::vector<float> sliceEndPoints(const Sampler& sm) {   // where each chop stops, 0..1 (its own end, else the next start)
        std::vector<float> o; auto b = sm.original(); if (!b || b->frames <= 0) return o;
        const auto& st = sm.slices(); const auto& en = sm.sliceEnds();
        for (size_t i = 0; i < st.size(); i++) o.push_back((float)(en.size() == st.size() ? en[i] : i + 1 < st.size() ? st[i + 1] : b->frames) / (float)b->frames);
        return o;
    }
    static std::string baseName(const std::string& p) { size_t s = p.find_last_of("/\\"); return p.substr(s == std::string::npos ? 0 : s + 1); }
    void installNew(const std::string& path, int track, bool kit) {
        auto sm = std::make_unique<Sampler>(sr);
        if (!sm->load(path)) { a_sampleFails++; a_sampleState = 0; return; }
        auto holder = std::make_shared<std::unique_ptr<Sampler>>(std::move(sm));
        post([this, holder, track, kit] {
            Track& t = *tracks[track];
            releaseAll(t);
            std::swap(t.sampler, *holder);
            { std::lock_guard<std::mutex> l(t.infoMtx); t.label = t.sampler->label(); t.peaks = t.sampler->peaks(); t.rootGuess = t.sampler->rootGuess(); t.slicePts = slicePoints(*t.sampler); t.sliceEndPts = sliceEndPoints(*t.sampler); t.frames = t.sampler->original() ? t.sampler->original()->frames : 0; }
            (void)kit;
            bury(std::move(*holder));
            a_sampleTrack = track; a_sampleVersion++; a_sampleState = 0;
            startProcess(track);   // the stretch / pitch that was set for this track applies to the new sample too
        });
    }
    // audio thread: build the stretched copy of the track's range on the worker, then swap it in
    void startProcess(int ti) {
        Track& t = *tracks[ti];
        if (!t.sampler) return;
        const uint32_t gen = ++t.gen;
        auto orig = t.sampler->original();
        if (!orig) return;   // a synth has nothing to stretch
        const float a = t.rangeA, b = t.rangeB, speed = t.speed, semis = t.semis;
        if (std::fabs(speed - 1.f) < 0.005f && std::fabs(semis) < 0.005f) { t.sampler->setProcessed(nullptr, 0.f, 1.f); return; }
        job([this, ti, gen, orig, a, b, speed, semis] {
            const int f0 = (int)(a * orig->frames), f1 = std::max(f0 + 64, (int)(b * orig->frames));
            std::vector<float> seg(orig->d.begin() + 2 * (size_t)f0, orig->d.begin() + 2 * (size_t)std::min(f1, orig->frames));
            auto out = std::make_shared<SampleBuf>();
            out->d = stretchPitch(seg, orig->sr, speed, semis); out->frames = (int)(out->d.size() / 2); out->sr = orig->sr;
            post([this, ti, gen, orig, a, b, out] {
                Track& t = *tracks[ti];
                if (t.gen == gen && t.sampler && t.sampler->original() == orig) t.sampler->setProcessed(out, a, b);
            });
        });
    }

    // ---- microphone sampler ----
    void liveReset() { liveN = 0; livePos = 0; liveAcc = 0; liveCnt = 0; }
    void liveFeed() {
        const int bin = sr / 20;
        for (; livePos < sampleBuf.size(); livePos++) {
            liveAcc = std::max(liveAcc, std::fabs(sampleBuf[livePos]));
            if (++liveCnt >= bin && liveN < kLive) { livePk[liveN.load()].store(liveAcc); liveN++; liveAcc = 0; liveCnt = 0; }
        }
    }
    // mono capture; while waiting for sound only a short look-ahead is kept, and the take starts when the level crosses the threshold
    void captureMono(const float* x, int n) {
        float pk = 0;
        for (int i = 0; i < n; i++) { const float m = 0.5f * (x[2 * i] + x[2 * i + 1]); sampleBuf.push_back(m); pk = std::max(pk, std::fabs(m)); }
        if (armedWait) {
            if (pk > micThr.load()) { armedWait = false; sampling = true; a_sampleState = 1; }
            else if (sampleBuf.size() > 2048) sampleBuf.erase(sampleBuf.begin(), sampleBuf.end() - 2048);
        }
        if (sampling) liveFeed();
    }
    // worker thread: trim, normalise, write the WAV; the result becomes the pending sample
    void finishSample(std::vector<float> st /*interleaved stereo*/) {
        const size_t frames = st.size() / 2;
        size_t a = 0, b = frames;
        while (a < frames && std::fabs(st[2 * a]) < 0.02f && std::fabs(st[2 * a + 1]) < 0.02f) a++;
        while (b > a && std::fabs(st[2 * (b - 1)]) < 0.01f && std::fabs(st[2 * (b - 1) + 1]) < 0.01f) b--;
        a = a > (size_t)(sr / 200) ? a - sr / 200 : 0; b = std::min(frames, b + (size_t)sr / 50);
        if (b <= a + (size_t)(sr / 50)) { a_sampleState = 0; return; }
        std::vector<float> o(st.begin() + 2 * a, st.begin() + 2 * b);
        float pk = 0; for (float v : o) pk = std::max(pk, std::fabs(v));
        if (pk > 1e-4f) { const float g = 0.9f / pk; for (float& v : o) v *= g; }
        const size_t fade = std::min<size_t>(o.size() / 4, (size_t)sr / 200 * 2);
        for (size_t i = 0; i < fade; i += 2) { const float k = (float)i / fade; o[i] *= k; o[i + 1] *= k; o[o.size() - 2 - i] *= k; o[o.size() - 1 - i] *= k; }
        char name[64]; snprintf(name, sizeof name, "/sample_%lld.wav", (long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        const std::string path = (dataDir.empty() ? "." : dataDir) + name;
        writeWav16(path, o, sr);
        { std::lock_guard<std::mutex> l(sampleMtx); samplePath = path; pending = true; a_pending = true; }
        a_sampleState = 0;
    }

    // ---- loop overview ----
    void updateLoopPeaks() {
        const int32_t L = loopSamples;
        for (int r = 0; r < 8; r++) {
            const int b = lpkBin; lpkBin = (lpkBin + 1) % kLoopBins;
            if (L <= 0) { a_lpk[b].store(0.f); for (int t = 0; t < kTracks; t++) a_tpk[t][b].store(0.f); continue; }
            const int32_t a = (int32_t)((int64_t)b * L / kLoopBins), e = (int32_t)((int64_t)(b + 1) * L / kLoopBins), step = std::max<int32_t>(1, (e - a) / 24);
            float mix = 0, tp[kTracks] = {};
            for (int32_t f = a; f < e; f += step) {
                float sum = 0;
                for (int t = 0; t < kTracks; t++) {
                    const auto& c = tracks[(size_t)t]->clip;
                    if (!tracks[(size_t)t]->clipHas || c.size() != (size_t)L * 2) continue;
                    const float v = 0.5f * (c[(size_t)2 * f] + c[(size_t)2 * f + 1]); sum += v; tp[t] = std::max(tp[t], std::fabs(v));
                }
                mix = std::max(mix, std::fabs(sum));
            }
            a_lpk[b].store(mix); for (int t = 0; t < kTracks; t++) a_tpk[t][b].store(tp[t]);
        }
    }

    // PRINT NOW: the tape of a track played through its cassette and effects once, offline, in place. The last second runs through first (its output is
    // thrown away) so a tail that would run over the end of the loop is already in the effects when the loop starts: the loop closes without a seam.
    // False when it was told to stop (the session is going away).
    static bool bakeInPlace(std::vector<float>& clip, int sr, const float* tp, const FxParams& fp, double spb, const std::atomic<bool>& abort) {
        const size_t frames = clip.size() / 2; if (!frames) return true;
        TapeInsert tape(sr); tape.setParams(tp[0], tp[1], tp[2], tp[3], tp[4], tp[5]);
        MasterFx fx(sr, 1.f); fx.set(fp);
        auto run = [&](float* x, size_t first, size_t count) {
            for (size_t f = 0; f < count; f += 256) {
                const int n = (int)std::min<size_t>(256, count - f);
                tape.process(x + 2 * f, n);
                if (fx.active()) fx.process(x + 2 * f, n, (double)(first + f) / spb, 1.0 / spb); else fx.feedOnly(x + 2 * f, n);
                if (abort.load()) return false;
            }
            return true;
        };
        const size_t warm = std::min<size_t>(frames, (size_t)sr);
        std::vector<float> w(clip.end() - (long)(warm * 2), clip.end());
        return run(w.data(), frames - warm, warm) && run(clip.data(), 0, frames);
    }

    // ---- one block of `n` frames ----
    void block(float* out, const float* in, int n) {
        std::fill(out, out + n * 2, 0.f);
        for (auto& s : sendBus) std::fill(s.begin(), s.begin() + n * 2, 0.f);
        const size_t freeBase = freeBuf.size();
        if (freeRec && loopSrc == 1) freeBuf.resize(freeBase + (size_t)n * 2, 0.f);   // the track's sound is added below
        if (in) {
            float ip = 0;
            for (int i = 0; i < n * 2; i++) ip = std::max(ip, std::fabs(in[i]));
            a_inPeak.store(std::max(ip, a_inPeak.load() * 0.9f));
            if (freeArmed && !counting && loopSrc == 0 && ip > micThr.load()) { freeArmed = false; a_freeArmed = false; freeRec = true; playing = true; pos = 0; clickEnv = 0; }   // first sound: the take starts
            if (freeRec && loopSrc == 0) freeBuf.insert(freeBuf.end(), in, in + n * 2);
            if (sampling || armedWait) captureMono(in, n);
        }
        const int32_t p0 = (int32_t)pos;
        // beat clock: the transport while it runs, a free running clock while it is stopped; the sequencer and metronome fire on its ticks
        const double spb = samplesPerBeat();
        tickHits(playing ? pos : freeBeat * spb, n, spb / kPpq, playing ? (double)loopSamples : 0.0, hits);
        const double beat0 = playing ? pos / spb : freeBeat;
        const bool tl = playing && !freeRec && loopSamples > 0;
        runTombola(n);
        updateLoopPeaks();
        const double spd = tapeSpeed.load();
        const bool locked = std::fabs(spd - 1.0) < 0.01;
        const double tb = loopSamples > 0 ? (locked ? (double)(p0 % loopSamples) : tapePos) : 0.0;
        if (loopSamples > 0) tapePos = !tl ? (double)(p0 % loopSamples) : locked ? (double)((p0 + n) % loopSamples) : std::fmod(tb + n * spd, (double)loopSamples);
        bool anyEvents;
        std::vector<Arp::Ev> evs;
        songAdvance();
        if (playing) for (auto& h : hits) if (h.tick % (kPpq / 24) == 0) a_clockPulses.fetch_add(1);
        if (playing != clkWas) { a_clockEv.fetch_or(playing ? 1 : 2); clkWas = playing; }
        const float duckEnv0 = duckEnv; duckTrig = false;
        for (int ti = 0; ti < kTracks; ti++) {
            Track& t = *tracks[(size_t)ti];
            if (!t.sampler && !t.clipHas && !t.clipRec && t.pending.empty() && t.nDelayed == 0) { t.peak.store(t.peak.load() * 0.9f); a_voiceN[ti].store(0); continue; }
            std::fill(bus.begin(), bus.begin() + n * 2, 0.f);
            // LFO: one value per block (about 170 Hz)
            float lfoGain = 1.f, lfoPan = 0.f, bend = 0.f; bool filterOn = false;
            if (t.lfo.depth > 0.001f) {
                t.lfoPhase += t.lfo.rate * n / sr; if (t.lfoPhase >= 1.f) { t.lfoPhase -= std::floor(t.lfoPhase); t.lfoSH = (float)(rand() % 2001 - 1000) / 1000.f; }
                const float ph = t.lfoPhase;
                const float v = t.lfo.shape == 0 ? std::sin(2.f * kPi * ph) : t.lfo.shape == 1 ? (ph < 0.5f ? 4 * ph - 1 : 3 - 4 * ph) : t.lfo.shape == 2 ? (ph < 0.5f ? 1.f : -1.f) : t.lfoSH;
                t.lfoVal = v;
                if (t.lfo.target == 0) bend = v * t.lfo.depth * t.lfo.depth * 12.f;   // +-12 semitones at full depth
                else if (t.lfo.target == 1) { filterOn = true; t.filt.set(20.f * std::pow(1000.f, clampf(t.lfo.base + 0.5f * v * t.lfo.depth, 0.f, 1.f)), (float)sr); }
                else if (t.lfo.target == 2) lfoGain = 1.f - t.lfo.depth * (0.5f - 0.5f * v);
                else lfoPan = v * t.lfo.depth;
            } else t.lfoVal = 0.f;
            if (!playing || t.seqState == 0) t.lockCut = 0;
            if (t.lockCut > 0) { filterOn = true; t.filt.set(80.f * std::pow(150.f, (t.lockCut - 1) / 126.f), (float)sr); }
            if (filterOn != t.filtOn) { t.filtOn = filterOn; t.filt.reset(); }
            a_lfo[ti].store(t.lfoVal);
            // notes of the block: live ones at the start, the arpeggiator's with sample accurate offsets
            evs.clear();
            for (auto& nt : t.pending) evs.push_back({0, nt.note, nt.vel});
            t.pending.clear();
            for (int k = 0, w = 0; k < t.nDelayed; k++) {   // held back keys: due in this block -> sample accurate on the grid line
                Track::Delayed d = t.delayed[k];
                if (d.left < n) noteTo(t, d.note, d.vel, std::max(0, d.left), evs);
                else { d.left -= n; t.delayed[w++] = d; }
                if (k == t.nDelayed - 1) t.nDelayed = w;
            }
            if (playing && t.seqState > 0 && !hits.empty()) seqRun(ti, t, evs);
            if (t.arpOn) t.arp.run(n, bpm, sr, evs);
            std::stable_sort(evs.begin(), evs.end(), [](const Arp::Ev& a, const Arp::Ev& b) { return a.offset < b.offset; });
            if (t.sampler) {
                t.sampler->setBend(bend);
                int cur = 0;
                for (auto& e : evs) {
                    const int off = std::min(n, std::max(cur, e.offset));
                    if (off > cur) { t.sampler->render(bus.data() + 2 * cur, off - cur); cur = off; }
                    if (e.vel) { t.sampler->setTune(t.tuneOf[e.note & 127]); t.sampler->noteOn(e.note, e.vel); if (ti == duckSrc) duckTrig = true; } else { t.sampler->noteOff(e.note); t.tuneOf[e.note & 127] = 0.f; }
                }
                if (cur < n) t.sampler->render(bus.data() + 2 * cur, n - cur);
                float vp[kVoiceMax]; const int vn = t.sampler->voicePositions(vp, kVoiceMax);
                for (int i = 0; i < vn; i++) a_voicePos[ti][i].store(vp[i]);
                a_voiceN[ti].store(vn);
            } else a_voiceN[ti].store(0);
            if (t.filtOn) for (int i = 0; i < n; i++) { bus[2 * i] = t.filt.process(0, bus[2 * i]); bus[2 * i + 1] = t.filt.process(1, bus[2 * i + 1]); }
            // the sound of the track itself (before the loop is added), for the free take
            if (freeRec && loopSrc == 1 && ti == freeTrack && !t.printFx) for (int i = 0; i < n * 2; i++) freeBuf[freeBase + (size_t)i] += bus[(size_t)i];
            // the tape of this track: one pass over the block that adds the clip to the bus and records into it. A normal track runs it here (the
            // instrument dry; the cassette and the effects come after and treat the tape too). A PRINTED track runs it after its effects instead:
            // what they make of the instrument lands on the tape, and the tape plays back past them (nothing is treated twice)
            auto tapePass = [&] {
            if (tl) {
                for (int i = 0; i < n; i++) {
                    const int32_t k0 = (p0 + i) % loopSamples;
                    const double cp = locked ? (double)k0 : std::fmod(tb + i * spd, (double)loopSamples);
                    const float src0 = bus[2 * i], src1 = bus[2 * i + 1];
                    const int32_t k = (int32_t)cp % loopSamples;
                    if (t.clipHas) {
                        auto& c = t.clip;
                        if (locked) { bus[2 * i] += c[(size_t)2 * k]; bus[2 * i + 1] += c[(size_t)2 * k + 1]; }
                        else {   // tape speed: its own read pointer, linear interpolation
                            const int32_t k1 = (k + 1) % loopSamples; const float fr = (float)(cp - std::floor(cp));
                            bus[2 * i] += c[(size_t)2 * k] + (c[(size_t)2 * k1] - c[(size_t)2 * k]) * fr; bus[2 * i + 1] += c[(size_t)2 * k + 1] + (c[(size_t)2 * k1 + 1] - c[(size_t)2 * k + 1]) * fr;
                        }
                    }
                    if (t.clipRec) {
                        if (t.clip.size() != (size_t)loopSamples * 2) t.clip.assign((size_t)loopSamples * 2, 0.f);
                        auto& c = t.clip;
                        // recording works at any tape speed, as the head of a tape machine: the input lands on the moving tape position.
                        // Slower than 1x the input is spread over the two neighbouring cells weighted by the speed (no pile-up);
                        // faster than 1x the cells the tape skipped since the last sample are filled by interpolating the input (no holes).
                        const float il = loopSrc == 0 && in ? in[2 * i] : loopSrc == 1 ? src0 : 0.f, ir = loopSrc == 0 && in ? in[2 * i + 1] : loopSrc == 1 ? src1 : 0.f;
                        if (locked) { c[(size_t)2 * k] += il; c[(size_t)2 * k + 1] += ir; t.recPrevOk = false; }
                        else if (spd >= 1.0) {
                            if (t.recPrevOk) {
                                const double p = t.recPrevCp; double cur = cp; if (cur < p) cur += (double)loopSamples;
                                for (int64_t cell = (int64_t)std::floor(p) + 1; cell <= (int64_t)std::floor(cur); cell++) {
                                    const float u = (float)((cell - p) / (cur - p)); const size_t idx = (size_t)(cell % loopSamples);
                                    c[2 * idx] += t.recPrevL + (il - t.recPrevL) * u; c[2 * idx + 1] += t.recPrevR + (ir - t.recPrevR) * u;
                                }
                            } else { c[(size_t)2 * k] += il; c[(size_t)2 * k + 1] += ir; }
                            t.recPrevCp = cp; t.recPrevL = il; t.recPrevR = ir; t.recPrevOk = true;
                        } else {
                            const float fr = (float)(cp - std::floor(cp)), w = (float)spd; const size_t k1 = (size_t)((k + 1) % loopSamples);
                            c[(size_t)2 * k] += il * w * (1.f - fr); c[(size_t)2 * k + 1] += ir * w * (1.f - fr);
                            c[2 * k1] += il * w * fr; c[2 * k1 + 1] += ir * w * fr;
                            t.recPrevOk = false;
                        }
                        if (il != 0.f || ir != 0.f) t.recWrote = true;
                        t.clipHas = true;
                        if (bounceTrack == ti && bounceLeft > 0 && --bounceLeft == 0) bounceFinish();
                    } else t.recPrevOk = false;
                }
            }
            };
            if (!t.printFx) tapePass();
            t.tape.process(bus.data(), n);
            if (t.fx->active()) t.fx->process(bus.data(), n, beat0, 1.0 / spb); else t.fx->feedOnly(bus.data(), n);
            if (t.printFx) {
                if (freeRec && loopSrc == 1 && ti == freeTrack) for (int i = 0; i < n * 2; i++) freeBuf[freeBase + (size_t)i] += bus[(size_t)i];   // the first take, printed
                tapePass();
            }
            const float dkR = std::exp(-1.f / (std::max(0.02f, duckRel) * (float)sr)); float de = std::max(duckEnv0, duckTrig && ti != duckSrc ? 1.f : 0.f);
            const float da = ti == duckSrc ? 0.f : duckAmt[ti];
            const float g = t.vol * lfoGain;
            const float ang = (clampf(t.pan + lfoPan, -1.f, 1.f) * 0.5f + 0.5f) * 1.5707963f;
            const float gl = g * std::cos(ang) * 1.4142f, gr = g * std::sin(ang) * 1.4142f;
            float pk = 0.f;
            for (int i = 0; i < n; i++) {
                const float dg = da > 0.f ? 1.f - da * de : 1.f; de *= dkR;
                const float l = bus[2 * i] * gl * dg, r = bus[2 * i + 1] * gr * dg;
                out[2 * i] += l; out[2 * i + 1] += r;
                for (int sd = 0; sd < 2; sd++) if (t.send[sd] > 0.f) { sendBus[sd][2 * i] += l * t.send[sd]; sendBus[sd][2 * i + 1] += r * t.send[sd]; }
                pk = std::max(pk, std::max(std::fabs(l), std::fabs(r)));
            }
            t.peak.store(std::max(pk, t.peak.load() * 0.92f));
        }
        if (songBase >= 0) songTick += (int)hits.size();
        duckEnv = std::max(duckEnv0, duckTrig ? 1.f : 0.f) * std::exp(-(float)n / (std::max(0.02f, duckRel) * (float)sr));
        (void)anyEvents;
        a_clipPos.store(loopSamples > 0 ? (float)(tapePos / loopSamples) : 0.f);
        reverb.process(sendBus[0].data(), wet.data(), n);
        for (int i = 0; i < n * 2; i++) out[i] += wet[(size_t)i] * ret[0];
        delay.process(sendBus[1].data(), wet.data(), n);
        for (int i = 0; i < n * 2; i++) out[i] += wet[(size_t)i] * ret[1];
        mfx.process(out, n, beat0, 1.0 / spb);
        if (metroOn || counting || clickEnv > 0.001f) {
            size_t hi = 0;
            for (int i = 0; i < n; i++) {
                for (; hi < hits.size() && hits[hi].offset <= i; hi++)
                    if ((metroOn || counting) && hits[hi].tick % kPpq == 0) { clickEnv = 1.f; clickPh = 0.f; clickFreq = (hits[hi].tick / kPpq) % 4 == 0 ? 1600.f : 1000.f; }
                if (clickEnv > 0.001f) {
                    const float v = std::sin(clickPh) * clickEnv * metroVol * 0.5f;
                    out[2 * i] += v; out[2 * i + 1] += v;
                    clickPh += 2.f * kPi * clickFreq / sr; clickEnv *= 0.9965f;
                }
            }
        }
        float mp = 0.f;
        if (limOn) {   // master limiter: the gain drops at once when a sample would pass the ceiling, and comes back over about 100 ms
            const float rel = 1.f - std::exp(-1.f / (0.1f * (float)sr));
            for (int i = 0; i < n; i++) {
                const float pk = std::max(std::fabs(out[2 * i]), std::fabs(out[2 * i + 1])) * masterVol;
                if (pk * limG > limCeil) limG = limCeil / pk; else limG += (1.f - limG) * rel;
                out[2 * i] *= limG; out[2 * i + 1] *= limG;
            }
        } else limG = 1.f;
        { float pre = 0.f; for (int i = 0; i < n * 2; i++) pre = std::max(pre, std::fabs(out[i]) * masterVol); if (pre > 1.1f) a_clip.store(40); else if (a_clip.load() > 0) a_clip.store(a_clip.load() - 1); }
        for (int i = 0; i < n * 2; i++) { const float v = std::tanh(out[i] * masterVol); out[i] = v; mp = std::max(mp, std::fabs(v)); }
        a_master.store(std::max(mp, a_master.load() * 0.92f));
        if (resampling) {   // RESAMPLE: what you hear becomes a sample
            const int64_t take = std::min<int64_t>(n, resLeft);
            resBuf.insert(resBuf.end(), out, out + take * 2); resLeft -= take;
            if (resLeft <= 0) { resampling = false; a_sampleState = 2; auto b = std::make_shared<std::vector<float>>(std::move(resBuf)); resBuf = {}; job([this, b] { finishSample(std::move(*b)); }); }
        }
        if (playing) {
            const double pos0 = pos;
            pos += n;
            if (freeRec) { a_freeSec = (float)(pos / sr); if (takeBars > 0 && pos >= takeBars * 4 * samplesPerBeat()) { freeStop(); } }   // the bars asked for are full: the take ends by itself
            else if (loopSamples > 0 && pos >= loopSamples) pos -= loopSamples;
            if (bounceArmed && loopSamples > 0 && (bounceBar == 0 ? pos < pos0 : (pos0 < bounceAt && bounceAt <= pos) || (bounceAt == 0 && pos < pos0))) { bounceArmed = false; { Track& bt = *tracks[(size_t)bounceTrack]; if (!bt.keyTrig || bt.seqRunning) { bt.startPending = true; bt.restartNow = true; bt.pendTrans = bt.seqTrans; } } tapeCheckpoint(bounceTrack); tracks[(size_t)bounceTrack]->clipRec = true; bounceLeft = bounceBar ? bounceLen : loopSamples; a_bounce = 2; }   // recording covers every position of the loop once
        }
        freeBeat = playing ? pos / spb : freeBeat + n / spb;
        a_beat.store(freeBeat);
        for (int ti = 0; ti < kTracks; ti++) {   // where each pattern really is: it started on a beat of its own, so it is not the beat clock mod its length
            const Track& t = *tracks[(size_t)ti];
            if (!playing || !t.seqRunning || t.seqState == 0) { a_seqPos[ti].store(-1); continue; }
            const int pt = patTicks(t);
            a_seqPos[ti].store((int)((((int64_t)(freeBeat * kPpq) - t.seqPhase) % pt + pt) % pt));
        }
        if (counting) { countLeft -= n; a_countBeats.store((int)std::ceil(std::max(0.0, countLeft) / spb)); if (countLeft <= 0) { endCount(); a_countBeats.store(0); } }
        if (sampling) a_sampleSec = (float)sampleBuf.size() / sr;
    }
};

Session::Session(int sampleRate) : d(new Impl(sampleRate)) {}
Session::~Session() { d->aborting = true; d->stopWorker(); }

void Session::process(float* out, const float* in, int frames) {
    const auto t0 = std::chrono::steady_clock::now();
    {
        std::unique_lock<std::mutex> l(d->cmdMtx, std::try_to_lock);
        if (l.owns_lock() && !d->cmds.empty()) d->run.swap(d->cmds);
    }
    for (auto& f : d->run) f();
    d->run.clear();
    for (int done = 0; done < frames; done += kBlock) {
        const int n = std::min(kBlock, frames - done);
        d->block(out + 2 * done, in ? in + 2 * done : nullptr, n);
    }
    d->a_pos = d->pos; d->a_playing = d->playing; d->a_free = d->freeRec;
    uint32_t cm = 0, rm = 0;
    for (int i = 0; i < kTracks; i++) { cm |= (uint32_t)d->tracks[(size_t)i]->clipHas << i; rm |= (uint32_t)d->tracks[(size_t)i]->clipRec << i; }
    d->a_clip_mask = cm; d->a_cliprec_mask = rm;
    { uint32_t pm = 0; for (int i = 0; i < kTracks; i++) pm |= (uint32_t)d->tracks[(size_t)i]->printFx << i; d->a_printMask = pm; }
    const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count(), budget = frames * 1e6 / d->sr;
    const uint32_t cur = (uint32_t)(1000.0 * us / budget);
    d->a_cpu = (d->a_cpu.load() * 7 + cur) / 8;
    d->a_cpuPeak = std::max(cur, d->a_cpuPeak.load() * 998 / 1000);
}

// ---- transport and notes ----
void Session::play(bool on) {
    d->post([this, on] {
        if (!on) { d->counting = false; d->countKind = 0; d->bounceCancel(); }
        if (!on && (d->freeRec || d->freeArmed)) d->freeStop();
        d->playing = on;
        if (!on) for (auto& t : d->tracks) d->releaseAll(*t);
    });
}
void Session::noteOn(int t, int note, int vel) { if (t >= 0 && t < kTracks) d->post([=] { d->liveNote(t, note, std::max(1, vel)); }); }
void Session::noteOff(int t, int note) { if (t >= 0 && t < kTracks) d->post([=] { d->liveNote(t, note, 0); }); }

// ---- sampler ----
void Session::loadSamplePath(const std::string& path, bool kit, int track) {
    if (track < 0 || track >= kTracks || (path.rfind("synth:", 0) != 0 && !std::filesystem::exists(path))) return;
    d->a_sampleState = 2;
    d->job([this, path, track, kit] { d->installNew(path, track, kit); });
}
void Session::setChopFull(int t, bool on) { if (t >= 0 && t < kTracks) d->post([=] { if (d->tracks[(size_t)t]->sampler) d->tracks[(size_t)t]->sampler->setChopFull(on); }); }
void Session::setMode(int t, int m) { if (t >= 0 && t < kTracks) d->post([=] { if (d->tracks[(size_t)t]->sampler) d->tracks[(size_t)t]->sampler->setMode(m); }); }
void Session::setEnv(int t, float a, float dc, float s, float r) { if (t >= 0 && t < kTracks) d->post([=] { if (d->tracks[(size_t)t]->sampler) d->tracks[(size_t)t]->sampler->setEnv(a, dc, s, r); }); }
void Session::setSampleRange(int t, float a, float b) {
    if (t < 0 || t >= kTracks) return;
    d->post([=] { auto& tr = *d->tracks[(size_t)t]; tr.rangeA = clampf(a, 0.f, 1.f); tr.rangeB = clampf(b, 0.f, 1.f); if (tr.sampler) tr.sampler->setRange(a, b); });
}
void Session::setStretch(int t, float speed, float semis, bool commit) {
    if (t < 0 || t >= kTracks) return;
    d->post([=] { auto& tr = *d->tracks[(size_t)t]; tr.speed = speed; tr.semis = semis; if (commit) d->startProcess(t); });
}
void Session::setLfo(int t, const Lfo& l) { if (t >= 0 && t < kTracks) d->post([=] { d->tracks[(size_t)t]->lfo = l; }); }
void Session::setArp(int t, bool on, int rate, int pattern, int octaves, float gate) {
    if (t < 0 || t >= kTracks) return;
    d->post([=] {
        auto& tr = *d->tracks[(size_t)t];
        tr.arp.setup(rate, pattern, octaves, gate);
        if (on != tr.arpOn) { d->releaseAll(tr); tr.arpOn = on; }
    });
}
void Session::setTape(int t, float sat, float tone, float wobble, float hiss, float mix, float bits) {
    if (t >= 0 && t < kTracks) d->post([=] { auto& tr = *d->tracks[(size_t)t]; const float v[6] = {sat, tone, wobble, hiss, mix, bits}; std::copy(v, v + 6, tr.tapeP); tr.tape.setParams(sat, tone, wobble, hiss, mix, bits); });
}
// PRINT FX: the tape of the track holds the sound after its cassette and effects. Switching on with a tape already there runs that tape through them once (a bake,
// one undo step) and flips the switch when it is done; switching off only flips it
void Session::setPrintFx(int t, bool on, bool bake) {
    if (t < 0 || t >= kTracks) return;
    d->post([=] {
        auto& tr = *d->tracks[(size_t)t];
        if (!on) { if (tr.printFx || tr.bakeTok) { tr.printFx = false; tr.bakeTok = 0; } return; }   // off: also drops a bake on its way
        if (tr.printFx) return;
        if (!bake || !tr.clipHas || tr.clipRec) { tr.printFx = true; return; }
        auto clip = std::make_shared<std::vector<float>>(tr.clip);   // the worker prints this copy
        FxParams fp = tr.fx->params(); float tp[6]; std::copy(tr.tapeP, tr.tapeP + 6, tp);
        fp.tapeStop = 0.f; fp.repeat = 0; fp.reverse = 0; fp.freeze = 0;   // what is held right now (a stop, a repeat, a freeze) is a gesture, not a setting: it is not baked across the loop
        const double spb = d->samplesPerBeat(); const uint64_t gen0 = d->tapeGen; const uint32_t tok = ++tr.bakeTok ? tr.bakeTok : (tr.bakeTok = 1);
        d->a_printBusy++;
        d->job([=] {
            bool ok = false;
            try { ok = Impl::bakeInPlace(*clip, d->sr, tp, fp, spb, d->aborting); } catch (...) { ok = false; }   // out of memory on a phone: the tape stays as it was
            d->post([=] {
                auto& t2 = *d->tracks[(size_t)t];
                if (ok && t2.bakeTok == tok && d->tapeGen == gen0 && t2.clipHas && !t2.clipRec && t2.clip.size() == clip->size()) {   // nothing touched the tape meanwhile
                    d->tapeCheckpoint(t); t2.clip = std::move(*clip); t2.printFx = true; d->a_printDone++;
                }
                if (t2.bakeTok == tok) t2.bakeTok = 0;
                d->a_printBusy--;
            });
        });
    });
}

// ---- mixer ----
void Session::setVolume(int t, float v) { if (t >= 0 && t < kTracks) d->post([=] { d->tracks[(size_t)t]->vol = clampf(v, 0.f, 1.5f); }); }
void Session::setPan(int t, float p) { if (t >= 0 && t < kTracks) d->post([=] { d->tracks[(size_t)t]->pan = clampf(p, -1.f, 1.f); }); }
void Session::setSend(int t, int w, float l) { if (t >= 0 && t < kTracks && (w == 0 || w == 1)) d->post([=] { d->tracks[(size_t)t]->send[w] = clampf(l, 0.f, 1.f); }); }
void Session::setMasterVolume(float v) { d->post([=] { d->masterVol = clampf(v, 0.f, 1.5f); }); }
void Session::setReverb(float size) { d->post([=] { d->reverb.setSize(size); }); }
void Session::setDelay(float sec, float fb) { d->post([=] { d->delay.setTime(sec); d->delay.setFeedback(fb); }); }
void Session::setRetLevel(int w, float v) { d->post([=] { d->ret[w & 1] = clampf(v, 0.f, 1.5f); }); }

// ---- tempo, metronome, sequencer, master fx ----
void Session::setTempo(float b) { d->post([=] { if (d->loopSamples == 0 && !d->freeRec) { d->bpm = clampf(b, 30.f, 300.f); d->a_bpm = d->bpm; } }); }
void Session::setMetronome(bool on, float vol) { d->post([=] { d->metroOn = on; d->metroVol = clampf(vol, 0.f, 1.f); d->a_metro = on; }); }
void Session::setTakeBars(int b) { d->post([=] { d->takeBars = std::min(64, std::max(0, b)); }); }
void Session::setTakeQuant(int m) { d->post([=] { d->takeQuant = std::min(2, std::max(0, m)); }); }
void Session::seqRun(int t, bool run) {   // PLAY: from the first tick on the next beat / bar; STOP: at once
    if (t >= 0 && t < kTracks) d->post([=] { auto& tr = *d->tracks[(size_t)t];
        if (run) { if (!d->playing) { d->playing = true; d->pos = 0; } tr.startPending = true; tr.pendTrans = tr.seqTrans; if (tr.seqState == 0) d->seqState(t, 1); }
        else { tr.seqRunning = false; tr.startPending = false; d->seqRelease(tr); }
        d->publishSeq(t); });
}
void Session::seqKeyTrigger(int t, bool on) {   // the keys trigger the pattern instead of playing the sound
    if (t >= 0 && t < kTracks) d->post([=] { auto& tr = *d->tracks[(size_t)t]; if (tr.keyTrig == on) return; tr.keyTrig = on; tr.heldKeys = 0; tr.seqTrans = 0; tr.pendTrans = 0; tr.startPending = false; if (on) { tr.seqRunning = false; d->seqRelease(tr); if (tr.seqState == 0) d->seqState(t, 1); } else tr.seqRunning = true; d->publishSeq(t); });
}
void Session::seqOneShot(int t, bool on) { if (t >= 0 && t < kTracks) d->post([=] { auto& tr = *d->tracks[(size_t)t]; tr.oneShot = on; d->publishSeq(t); }); }   // the pattern plays one pass and stops, instead of looping
void Session::seqTrigQuant(int t, int q) { if (t >= 0 && t < kTracks) d->post([=] { d->tracks[(size_t)t]->trigQ = std::min(2, std::max(0, q)); }); }
void Session::seqNudge(int t, int ticks) {   // every note moves by `ticks` round the pattern: the first beat lines up with the metronome (one undo step)
    if (t < 0 || t >= kTracks) return;
    d->post([=] { auto& tr = *d->tracks[(size_t)t]; if (tr.snCount == 0 || ticks == 0) return;
        const int pt = d->patTicks(tr); d->checkpoint(tr);
        for (int i = 0; i < tr.snCount; i++) tr.sn[i].tick = (uint16_t)((((tr.sn[i].tick + ticks) % pt) + pt) % pt);
        d->seqRelease(tr); d->publishSeq(t); });
}
void Session::seqSetState(int t, int s) { if (t >= 0 && t < kTracks) d->post([=] { d->seqState(t, s); }); }
void Session::seqSetBars(int t, int b) { if (t >= 0 && t < kTracks) d->post([=] { d->seqBarsSet(t, b); }); }
void Session::seqClear(int t) {
    if (t >= 0 && t < kTracks) d->post([=] { auto& tr = *d->tracks[(size_t)t]; if (tr.snCount > 0) d->checkpoint(tr); d->seqRelease(tr); tr.snCount = 0; d->publishSeq(t); });
}
// the user's UNDO / REDO button: the last thing done on any track, pattern or sample (a sample edit is taken back by swapping the old sample in, so the same call redoes it)
// with a loop on the tape the tempo moves by stretching every loop (the pitch stays); one undo step
void Session::setLoopTempo(float newBpm) {
    d->post([this, newBpm] {
        if (d->loopSamples <= 0 || d->freeRec || d->freeArmed || d->bounceTrack >= 0) return;
        const float nb = clampf(newBpm, 30.f, 300.f), old = d->bpm;
        if (std::fabs(nb - old) < 0.05f) return;
        const double ratio = std::min(2.0, std::max(0.5, (double)nb / old));
        const int32_t L = d->loopSamples; const int bars = d->loopBars; const uint64_t gen0 = d->tapeGen;
        std::vector<std::pair<int, std::vector<float>>> in;
        for (int i = 0; i < kTracks; i++) if (d->tracks[(size_t)i]->clipHas && d->tracks[(size_t)i]->clip.size() == (size_t)L * 2) in.push_back({i, d->tracks[(size_t)i]->clip});
        d->job([this, in = std::move(in), ratio, L, bars, gen0]() mutable {
            const int32_t L2 = (int32_t)std::lround(L / ratio);
            for (auto& c : in) { c.second = stretchPitch(c.second, d->sr, ratio, 0.0); c.second.resize((size_t)L2 * 2, 0.f); }
            d->post([this, in = std::move(in), L, L2, bars, gen0]() mutable {
                if (d->loopSamples != L || d->tapeGen != gen0) return;   // the tape changed while it was being stretched (a longer loop, an overdub, a print)
                d->tapeCheckpoint(-1);
                for (auto& t : d->tracks) { t->clipRec = false; t->recPrevOk = false; }
                for (auto& c : in) { Impl::Track& t = *d->tracks[(size_t)c.first]; t.clip = std::move(c.second); t.clipHas = true; }
                d->pos = d->pos * L2 / L; d->tapePos = d->tapePos * L2 / L; d->loopSamples = L2; d->a_loop = L2;
                d->bpm = std::min(300.f, std::max(30.f, (float)(bars * 240.0 * d->sr / L2))); d->a_bpm = d->bpm;
            });
        });
    });
}
void Session::resetUndo() { d->post([this] { d->gReset(); }); }
void Session::globalUndo() {
    d->post([this] {
        while (d->glogN > 0) {
            const Impl::GEntry e = d->glog[--d->glogN];
            const bool ok = e.kind == 1 ? (bool)d->tracks[(size_t)e.track]->prevOrig : e.kind == 2 ? d->tapeUndoStep() : d->seqUndoStep(e.track);
            if (!ok) continue;
            if (e.kind == 1) undoEdit(e.track);
            Impl::gPush(d->gredo, d->gredoN, e); break;
        }
        d->gPublish();
    });
}
void Session::globalRedo() {
    d->post([this] {
        while (d->gredoN > 0) {
            const Impl::GEntry e = d->gredo[--d->gredoN];
            const bool ok = e.kind == 1 ? (bool)d->tracks[(size_t)e.track]->prevOrig : e.kind == 2 ? d->tapeRedoStep() : d->seqRedoStep(e.track);
            if (!ok) continue;
            if (e.kind == 1) undoEdit(e.track);
            Impl::gPush(d->glog, d->glogN, e); break;
        }
        d->gPublish();
    });
}
void Session::seqUndo(int t) { if (t >= 0 && t < kTracks) d->post([=] { d->seqUndoStep(t); }); }
void Session::seqRedo(int t) { if (t >= 0 && t < kTracks) d->post([=] { d->seqRedoStep(t); }); }
void Session::seqToggle(int t, int tick, int len, int note, int vel) { if (t >= 0 && t < kTracks && note >= 0 && note < 128) d->post([=] { d->seqToggleNote(t, tick, len, note, vel); }); }
void Session::seqSet(int t, const SeqView* nv, int n) {
    if (t < 0 || t >= kTracks) return;
    std::vector<SeqNote> list; for (int i = 0; i < n && i < kSeqMax; i++) list.push_back({(uint16_t)std::max(0, nv[i].tick), (uint16_t)std::max(1, nv[i].len), (uint8_t)std::min(127, std::max(0, nv[i].note)), (uint8_t)std::min(127, std::max(1, nv[i].vel)), (uint8_t)std::min(100, std::max(0, nv[i].prob)), (uint8_t)std::min(8, std::max(1, nv[i].ratchet)), (int8_t)std::min(15, std::max(-16, nv[i].nudge)), (uint8_t)std::min(127, std::max(0, nv[i].cut))});
    d->post([=] { d->seqSetNotes(t, list); });
}
void Session::seqResize(int t, int tick, int note, int len) { if (t >= 0 && t < kTracks) d->post([=] { d->seqResizeNote(t, tick, note, len); }); }
void Session::tombolaAdd(int t, int note) { if (t >= 0 && t < kTracks && note >= 0 && note < 128) d->post([=] { d->tomb.add(t, note, (uint32_t)(d->tombolaSeed++ + note)); }); }
void Session::tombolaSet(float spin, float grav, float mass, float shape) { d->post([=] { d->tomb.spin = spin; d->tomb.grav = grav; d->tomb.mass = mass; d->tomb.shape = shape; }); }
void Session::tombolaTurn(float rad) { d->post([=] { d->tomb.turn(rad); }); }
void Session::tombolaClear() { d->post([=] { d->tomb.clear(); d->a_ballN = 0; }); }
int Session::tombolaView(TombolaBall* out, int maxN, float* drumAngle) const {
    const int n = std::min(maxN, std::min(Tombola::kMaxBalls, d->a_ballN.load()));
    for (int i = 0; i < n; i++) out[i] = {Impl::floatOf(d->a_ball[i][0]), Impl::floatOf(d->a_ball[i][1]), Impl::floatOf(d->a_ball[i][2]), (int)d->a_ball[i][3].load()};
    if (drumAngle) *drumAngle = Impl::floatOf(d->a_drum.load());
    return n;
}
void Session::seqApply(int t, const SeqRef* notes, int n, int op, int arg) {
    if (t < 0 || t >= kTracks || n <= 0) return;
    std::vector<SeqRef> refs(notes, notes + n);
    d->post([=] { d->seqApplyOp(t, refs, op, arg); });
}
void Session::seqAddBar(int t, bool copyLast) { if (t >= 0 && t < kTracks) d->post([=] { d->seqAddBarOp(t, copyLast); }); }
void Session::seqSelectPattern(int t, int slot, bool immediate) { if (t >= 0 && t < kTracks) d->post([=] { d->selectSlot(t, slot, immediate); }); }
void Session::seqCopyPattern(int t, int from, int to) { if (t >= 0 && t < kTracks) d->post([=] { d->copySlot(t, from, to); }); }
int Session::seqSlotNotes(int t, int slot, SeqView* out, int maxNotes, int* bars) const {
    if (t < 0 || t >= kTracks || slot < 0 || slot >= Impl::kSlots) return 0;
    const int n = std::min(maxNotes, std::min(kSeqMax, d->a_bankN[t][slot].load()));
    for (int i = 0; i < n; i++) { const SeqNote nn = unpackNote(d->a_bank[t][slot][i].load()); out[i] = {nn.tick, nn.len, nn.note, nn.vel, nn.prob, nn.ratchet, nn.nudge, nn.cut}; }
    if (bars) *bars = d->a_bankBars[t][slot].load();
    return n;
}
// a whole pattern into a bank (a project is loaded): the current bank is replaced in place, the others are stored
void Session::seqSetSlot(int t, int slot, const SeqView* nv, int n, int bars) {
    if (t < 0 || t >= kTracks || slot < 0 || slot >= Impl::kSlots) return;
    std::vector<SeqNote> list; for (int i = 0; i < n && i < kSeqMax; i++) list.push_back({(uint16_t)std::max(0, nv[i].tick), (uint16_t)std::max(1, nv[i].len), (uint8_t)std::min(127, std::max(0, nv[i].note)), (uint8_t)std::min(127, std::max(1, nv[i].vel)), (uint8_t)std::min(100, std::max(0, nv[i].prob)), (uint8_t)std::min(8, std::max(1, nv[i].ratchet)), (int8_t)std::min(15, std::max(-16, nv[i].nudge)), (uint8_t)std::min(127, std::max(0, nv[i].cut))});
    d->post([=] {
        Impl::Track& tr = *d->tracks[(size_t)t];
        if (slot == tr.slot) { tr.seqBars = std::min(Impl::kMaxBars, std::max(1, bars)); d->seqSetNotes(t, list); return; }
        Impl::Track::SeqSnap& b = tr.bank[slot]; b.count = 0; b.bars = std::min(Impl::kMaxBars, std::max(1, bars));
        for (auto& e : list) if (b.count < kSeqMax) b.sn[b.count++] = e;
        for (int i = 0; i < b.count; i++) d->a_bank[t][slot][i].store(packNote(b.sn[i]));
        d->a_bankN[t][slot].store(b.count); d->a_bankBars[t][slot].store(b.bars);
    });
}
void Session::setSong(const uint8_t* chain, int n, bool on) {
    std::vector<uint8_t> c(chain, chain + std::max(0, std::min(n, 64))); for (auto& x : c) x = std::min<int>(Impl::kSlots - 1, x);
    d->post([=] { d->song = c; d->songOn = on && !c.empty(); d->songWas = false; d->songTick = 0; d->songPos = 0; if (!d->songOn) { d->a_songOn.store(0); d->songBase = -1; } d->a_songLen.store((int)c.size()); });
}
void Session::seqRest(int t) { if (t >= 0 && t < kTracks) d->post([=] { d->seqStepMove(t, 1); }); }
void Session::seqCursorMove(int t, int steps) { if (t >= 0 && t < kTracks) d->post([=] { d->seqStepMove(t, steps); }); }
void Session::seqCursorSet(int t, int tick) { if (t >= 0 && t < kTracks) d->post([=] { d->seqCursorTo(t, tick); }); }
void Session::seqBounceCancel() { d->post([=] { d->bounceCancel(); }); }
void Session::seqBounce(int t, int bar) { if (t >= 0 && t < kTracks) d->post([=] { d->bounceStart(t, bar); }); }
void Session::setSwing(float s) { d->post([=] { d->swing = clampf(s, 0.5f, 0.75f); }); }
void Session::setQuantize(int g) { d->post([=] { d->seqGrid = std::min(kGrids - 1, std::max(0, g)); }); }
int Session::seqNotes(int t, SeqView* out, int maxNotes) const {
    if (t < 0 || t >= kTracks) return 0;
    const int n = std::min(maxNotes, std::min(kSeqMax, d->a_seqN[t].load()));
    for (int i = 0; i < n; i++) { const SeqNote nn = unpackNote(d->a_seq[t][i].load()); out[i] = {nn.tick, nn.len, nn.note, nn.vel, nn.prob, nn.ratchet, nn.nudge, nn.cut}; }
    return n;
}
void Session::setInputQuant(int g) { d->post([=] { d->inQuant = std::min(kGrids - 1, std::max(0, g)); }); }
void Session::setCountIn(int bars) { d->post([=] { d->countIn = std::min(2, std::max(0, bars)); }); }
void Session::setTrackFx(int t, const FxParams& p) { if (t >= 0 && t < kTracks) d->post([=] { d->tracks[(size_t)t]->fx->set(p); }); }
void Session::setMasterFx(const FxParams& p) { d->post([=] { d->mfx.set(p); }); }

// ---- looper ----
void Session::freeRecord(int t, bool on) { if (t >= 0 && t < kTracks) d->post([=] { if (on) d->freeStart(t); else d->freeStop(); }); }
void Session::clipRecord(int t, bool on) { if (t >= 0 && t < kTracks) d->post([=] { auto& tr = *d->tracks[(size_t)t]; if (on && !tr.clipRec) { d->tapeCheckpoint(t); tr.recWrote = false; } else if (!on && tr.clipRec && !tr.recWrote) d->dropTapeCheckpoint(t); tr.clipRec = on; }); }
void Session::clipClear(int t) {
    if (t >= 0 && t < kTracks) d->post([=] { auto& tr = *d->tracks[(size_t)t]; if (tr.clipHas) d->tapeCheckpoint(t); tr.clipRec = false; tr.clip.clear(); tr.clipHas = false; d->clearIfEmpty(); });
}
void Session::clipClearAll() {
    d->post([this] { bool any = false; for (auto& t : d->tracks) any |= t->clipHas; if (any) d->tapeCheckpoint(-1); for (auto& t : d->tracks) { t->clipRec = false; t->clip.clear(); t->clipHas = false; } d->clearIfEmpty(); });
}
void Session::loopDouble() {
    d->post([this] {
        const int32_t L = d->loopSamples;
        if (L <= 0 || (int64_t)L * 2 > (int64_t)d->sr * 180) return;
        for (auto& t : d->tracks) {
            if (t->clipHas && t->clip.size() == (size_t)L * 2) { auto& c = t->clip; c.resize((size_t)L * 4); std::copy(c.begin(), c.begin() + (size_t)L * 2, c.begin() + (size_t)L * 2); }
            else { t->clip.clear(); t->clipHas = false; }
        }
        d->loopSamples = L * 2; d->a_loop = d->loopSamples;
        d->loopBars = std::min(64, d->loopBars * 2); d->a_bars = d->loopBars;
    });
}
void Session::loopHalve() {
    d->post([this] {
        const int32_t L = d->loopSamples, h = L / 2;
        if (L <= 0 || h < d->sr / 4) return;
        for (auto& t : d->tracks) if (t->clipHas && t->clip.size() == (size_t)L * 2) t->clip.resize((size_t)h * 2);
        d->loopSamples = h; d->a_loop = h;
        d->loopBars = std::max(1, d->loopBars / 2); d->a_bars = d->loopBars;
        if (d->pos >= h) d->pos = std::fmod(d->pos, (double)h);
        if (d->tapePos >= h) d->tapePos = std::fmod(d->tapePos, (double)h);
    });
}
void Session::setLoopSpeed(float v) { d->tapeSpeed.store(clampf(v, 0.25f, 2.f)); }
void Session::setLoopSource(int src) { d->post([=] { d->loopSrc = src ? 1 : 0; }); }
// ---- projects: tape audio on disk ----
// the loop clips of every track as tape_N.wav (16 bit, the length of the loop) and the mix of them as mix.wav; written on the worker thread
bool Session::writeTape(const std::string& dir) {
    d->busy++;
    d->post([this, dir] {
        std::vector<std::pair<int, std::vector<float>>> clips; std::vector<float> mix; const int32_t L = d->loopSamples;
        if (L > 0) {
            mix.assign((size_t)L * 2, 0.f);
            for (int i = 0; i < kTracks; i++) {
                const Impl::Track& t = *d->tracks[(size_t)i];
                if (!t.clipHas || t.clip.size() != (size_t)L * 2) continue;
                clips.push_back({i, t.clip});
                const float a = (t.pan + 1.f) * kPi / 4.f, gl = std::cos(a) * t.vol * 1.414f, gr = std::sin(a) * t.vol * 1.414f;
                for (int k = 0; k < L; k++) { mix[(size_t)k * 2] += t.clip[(size_t)k * 2] * gl; mix[(size_t)k * 2 + 1] += t.clip[(size_t)k * 2 + 1] * gr; }
            }
            float pk = 0.f; for (float v : mix) pk = std::max(pk, std::fabs(v)); if (pk > 1.f) for (float& v : mix) v /= pk;
        }
        d->job([this, dir, clips = std::move(clips), mix = std::move(mix)]() mutable {
            std::filesystem::create_directories(dir);
            for (int i = 0; i < kTracks; i++) std::filesystem::remove(dir + "/tape_" + std::to_string(i) + ".wav");   // stale clips of an older save
            std::filesystem::remove(dir + "/mix.wav");
            for (auto& c : clips) writeWav16(dir + "/tape_" + std::to_string(c.first) + ".wav", c.second, d->sr);
            if (!mix.empty()) writeWav16(dir + "/mix.wav", mix, d->sr);
            d->busy--;
        });
    });
    return true;
}
// replaces the loops by the tape_N.wav files of a project (resampled when the project was made at another rate)
void Session::loadTape(const std::string& dir, int bars, float bpm) {
    d->busy++;
    d->job([this, dir, bars, bpm] {
        const int sr = d->sr;
        std::vector<std::pair<int, std::vector<float>>> clips; size_t frames = 0;
        for (int i = 0; i < kTracks; i++) {
            std::vector<float> st; int fsr = 0;
            const std::string path = dir + "/tape_" + std::to_string(i) + ".wav";
            if (!std::filesystem::exists(path) || !readWav(path, st, fsr) || st.size() < 4) continue;
            if (fsr != sr) {   // linear resample
                const size_t n = (size_t)((double)(st.size() / 2) * sr / fsr); std::vector<float> o(n * 2);
                for (size_t k = 0; k < n; k++) { const double x = (double)k * fsr / sr; const size_t i0 = std::min((size_t)x, st.size() / 2 - 2); const float f = (float)(x - (double)i0);
                    o[2 * k] = st[2 * i0] + (st[2 * i0 + 2] - st[2 * i0]) * f; o[2 * k + 1] = st[2 * i0 + 1] + (st[2 * i0 + 3] - st[2 * i0 + 1]) * f; }
                st = std::move(o);
            }
            frames = std::max(frames, st.size() / 2); clips.push_back({i, std::move(st)});
        }
        d->post([this, clips = std::move(clips), frames, bars, bpm]() mutable {
            for (auto& t : d->tracks) { t->clipRec = false; t->clip.clear(); t->clipHas = false; }
            if (clips.empty() || frames == 0) { d->clearIfEmpty(); if (bpm > 0.f && d->loopSamples == 0) { d->bpm = std::min(300.f, std::max(30.f, bpm)); d->a_bpm = d->bpm; } d->busy--; return; }
            for (auto& c : clips) { c.second.resize(frames * 2, 0.f); d->tracks[(size_t)c.first]->clip = std::move(c.second); d->tracks[(size_t)c.first]->clipHas = true; }
            d->loopSamples = (int32_t)frames; d->a_loop = d->loopSamples; d->loopBars = std::min(64, std::max(1, bars)); d->a_bars = d->loopBars;
            d->bpm = std::min(300.f, std::max(30.f, bpm)); d->a_bpm = d->bpm;
            d->pos = 0; d->tapePos = 0;
            d->busy--;
        });
    });
}
void Session::setSynthMacros(int t, float tone, float body, float motion, float grit) {
    if (t < 0 || t >= kTracks) return;
    SynthMacros m; m.tone = clampf(tone, -1.f, 1.f); m.body = clampf(body, -1.f, 1.f); m.motion = clampf(motion, -1.f, 1.f); m.grit = clampf(grit, -1.f, 1.f);
    d->post([=] { if (d->tracks[(size_t)t]->sampler) d->tracks[(size_t)t]->sampler->setSynthMacros(m); });
}
void Session::setSynthEdit(int t, int w1, int w2, float det2Cents, float cutOctaves, float resAdd, int filterType, int unison, bool mono, float glide) {
    if (t < 0 || t >= kTracks) return;
    SynthEdit e; e.w1 = w1; e.w2 = w2; e.det2 = clampf(det2Cents, -100.f, 100.f); e.cutOct = clampf(cutOctaves, -4.f, 4.f); e.res = clampf(resAdd, -1.f, 8.f); e.ftype = std::min(2, std::max(0, filterType)); e.uni = unison; e.mono = mono; e.glide = clampf(glide, 0.f, 1.f);
    d->post([=] { if (d->tracks[(size_t)t]->sampler) d->tracks[(size_t)t]->sampler->setSynthEdit(e); });
}
void Session::setRoot(int t, int midi) { if (t >= 0 && t < kTracks) d->post([=] { if (d->tracks[(size_t)t]->sampler) d->tracks[(size_t)t]->sampler->setRoot(midi); }); }
// re-cuts the sample into `count` slices (4..32); `sens` 0.25..2 decides how soft a hit may be (built on the worker, then swapped in)
void Session::setSlices(int t, int count, float sens) {
    if (t < 0 || t >= kTracks) return;
    d->post([=] {
        Impl::Track& tr = *d->tracks[(size_t)t]; if (!tr.sampler) return;
        auto orig = tr.sampler->original(); if (!orig) return;
        d->job([=] {
            auto en = std::make_shared<std::vector<int>>();
            auto sl = std::make_shared<std::vector<int>>(findSlices(orig->d, orig->frames, orig->sr, std::min(32, std::max(2, count)), sens, en.get()));
            d->post([=] {
                Impl::Track& tr2 = *d->tracks[(size_t)t];
                if (!tr2.sampler || tr2.sampler->original() != orig) return;
                tr2.sampler->allOff(); tr2.sampler->setSlices(*sl, *en);
                std::lock_guard<std::mutex> l(tr2.infoMtx); tr2.slicePts = Impl::slicePoints(*tr2.sampler); tr2.sliceEndPts = Impl::sliceEndPoints(*tr2.sampler);
            });
        });
    });
}
int Session::sampleFramesOf(int t) const { if (t < 0 || t >= kTracks) return 0; std::lock_guard<std::mutex> l(d->tracks[(size_t)t]->infoMtx); return d->tracks[(size_t)t]->frames; }
float Session::rootGuessOf(int t) const { if (t < 0 || t >= kTracks) return 0.f; std::lock_guard<std::mutex> l(d->tracks[(size_t)t]->infoMtx); return d->tracks[(size_t)t]->rootGuess; }
std::vector<float> Session::slicesOf(int t) const { if (t < 0 || t >= kTracks) return {}; std::lock_guard<std::mutex> l(d->tracks[(size_t)t]->infoMtx); return d->tracks[(size_t)t]->slicePts; }
void Session::setPad(int t, int pad, float vol, float pitchSemis, float pan, float decay, int group) {
    if (t < 0 || t >= kTracks || pad < 0 || pad >= 16) return;
    Sampler::Pad p; p.vol = clampf(vol, 0.f, 2.f); p.pitch = clampf(pitchSemis, -24.f, 24.f); p.pan = clampf(pan, -1.f, 1.f); p.decay = clampf(decay, 0.05f, 8.f); p.group = std::min(8, std::max(0, group));
    d->post([=] { if (d->tracks[(size_t)t]->sampler) d->tracks[(size_t)t]->sampler->setPad(pad, p); });
}
// RESAMPLE: records `bars` bars of the master output from the start of the pattern into a new sample (the sample pending is picked up like a microphone take)
void Session::resample(int bars) {
    d->post([this, bars] {
        if (d->resampling || d->sampling) return;
        d->resBuf.clear(); d->resBuf.reserve((size_t)d->sr * 40);
        d->resLeft = (int64_t)std::lround(std::min(16, std::max(1, bars)) * 4.0 * d->samplesPerBeat());
        d->pos = 0; d->tapePos = 0; d->playing = true; d->resampling = true; d->a_sampleState = 1;
    });
}
// offline sample edits (REVERSE, NORMALIZE, TRIM to the window, FADE IN / OUT) on the worker; the old sample is kept for EDIT UNDO
void Session::editSample(int t, int op) {
    if (t < 0 || t >= kTracks) return;
    d->post([this, t, op] {
        Impl::Track& tr = *d->tracks[(size_t)t]; if (!tr.sampler) return;
        auto orig = tr.sampler->original(); if (!orig) return;
        const float a = tr.rangeA, b = tr.rangeB; const int sr = d->sr;
        d->job([this, t, op, orig, a, b, sr] {
            auto out = std::make_shared<SampleBuf>(); out->sr = orig->sr;
            int f0 = 0, f1 = orig->frames; std::vector<float> data = orig->d;
            if (op == EDIT_TRIM) { f0 = std::min(orig->frames - 64, (int)(a * orig->frames)); f1 = std::max(f0 + 64, std::min(orig->frames, (int)(b * orig->frames))); data.assign(orig->d.begin() + 2 * (size_t)f0, orig->d.begin() + 2 * (size_t)f1); }
            const int n = (int)(data.size() / 2);
            if (op == EDIT_REVERSE) for (int i = 0; i < n / 2; i++) { std::swap(data[2 * (size_t)i], data[2 * (size_t)(n - 1 - i)]); std::swap(data[2 * (size_t)i + 1], data[2 * (size_t)(n - 1 - i) + 1]); }
            if (op == EDIT_NORMALIZE) { float pk = 1e-6f; for (float v : data) pk = std::max(pk, std::fabs(v)); const float g = 0.95f / pk; for (float& v : data) v *= g; }
            if (op == EDIT_FADE_IN || op == EDIT_FADE_OUT) { const int len = std::min(n / 2, orig->sr * 30 / 1000); for (int i = 0; i < len; i++) { const float k = (float)i / len; const int j = op == EDIT_FADE_IN ? i : n - 1 - i; data[2 * (size_t)j] *= k; data[2 * (size_t)j + 1] *= k; } }
            out->d = std::move(data); out->frames = n;
            auto peaks = Sampler::overview(*out); auto slices = findSlices(out->d, out->frames, out->sr, 16);
            (void)sr;
            d->post([this, t, op, orig, out, peaks = std::move(peaks), slices = std::move(slices)]() mutable {
                Impl::Track& tr = *d->tracks[(size_t)t]; if (!tr.sampler || tr.sampler->original() != orig) return;
                tr.prevOrig = orig; d->gRecord(t, 1);
                if (op == EDIT_TRIM) { tr.rangeA = 0.f; tr.rangeB = 1.f; tr.sampler->setRange(0.f, 1.f); }
                { std::lock_guard<std::mutex> l(tr.infoMtx); tr.peaks = peaks; tr.frames = out->frames; }
                tr.sampler->replaceOriginal(out, std::move(peaks), std::move(slices));
                { std::lock_guard<std::mutex> l(tr.infoMtx); tr.slicePts = Impl::slicePoints(*tr.sampler); tr.sliceEndPts = Impl::sliceEndPoints(*tr.sampler); }
                d->startProcess(t); d->a_sampleVersion++; d->a_sampleTrack = t;
            });
        });
    });
}
void Session::undoEdit(int t) {
    if (t < 0 || t >= kTracks) return;
    d->post([this, t] {
        Impl::Track& tr = *d->tracks[(size_t)t]; if (!tr.sampler || !tr.prevOrig) return;
        auto prev = tr.prevOrig; tr.prevOrig = tr.sampler->original();
        d->job([this, t, prev] {
            auto peaks = Sampler::overview(*prev); auto slices = findSlices(prev->d, prev->frames, prev->sr, 16);
            d->post([this, t, prev, peaks = std::move(peaks), slices = std::move(slices)]() mutable {
                Impl::Track& tr = *d->tracks[(size_t)t]; if (!tr.sampler) return;
                { std::lock_guard<std::mutex> l(tr.infoMtx); tr.peaks = peaks; tr.frames = prev->frames; }
                tr.sampler->replaceOriginal(prev, std::move(peaks), std::move(slices));
                { std::lock_guard<std::mutex> l(tr.infoMtx); tr.slicePts = Impl::slicePoints(*tr.sampler); tr.sliceEndPts = Impl::sliceEndPoints(*tr.sampler); }
                d->startProcess(t); d->a_sampleVersion++; d->a_sampleTrack = t;
            });
        });
    });
}
// chops placed by hand, 0..1 of the sample: each has its own start and end (`ends` empty = every chop runs to the next start)
void Session::setSliceStarts(int t, const std::vector<float>& pts, const std::vector<float>& ends) {
    if (t < 0 || t >= kTracks || pts.size() < 2) return;
    d->post([=] {
        Impl::Track& tr = *d->tracks[(size_t)t]; if (!tr.sampler) return; auto o = tr.sampler->original(); if (!o) return;
        const bool own = ends.size() == pts.size();
        std::vector<std::pair<int, int>> c;   // (start, end) sorted by start
        for (size_t i = 0; i < pts.size(); i++) {
            const int s = std::min(o->frames - 64, std::max(0, (int)(clampf(pts[i], 0.f, 1.f) * o->frames)));
            c.push_back({s, own ? std::min(o->frames, std::max(s + 16, (int)(clampf(ends[i], 0.f, 1.f) * o->frames))) : 0});
        }
        std::stable_sort(c.begin(), c.end(), [](auto& a, auto& b) { return a.first < b.first; });
        c.erase(std::unique(c.begin(), c.end(), [](auto& a, auto& b) { return a.first == b.first; }), c.end()); if (c.size() < 2) return;
        std::vector<int> st, en; for (auto& x : c) { st.push_back(x.first); if (own) en.push_back(x.second); }
        tr.sampler->allOff(); tr.sampler->setSlices(st, en);
        std::lock_guard<std::mutex> l(tr.infoMtx); tr.slicePts = Impl::slicePoints(*tr.sampler); tr.sliceEndPts = Impl::sliceEndPoints(*tr.sampler);
    });
}
std::vector<float> Session::sliceEndsOf(int t) const { if (t < 0 || t >= kTracks) return {}; std::lock_guard<std::mutex> l(d->tracks[(size_t)t]->infoMtx); return d->tracks[(size_t)t]->sliceEndPts; }
void Session::setSampleLevel(int t, float level, float fineCents) { if (t >= 0 && t < kTracks) d->post([=] { if (d->tracks[(size_t)t]->sampler) { d->tracks[(size_t)t]->sampler->setLevel(level); d->tracks[(size_t)t]->sampler->setFine(fineCents); } }); }
int Session::clockPulses() { return d->a_clockPulses.exchange(0); }
int Session::clockEvents() { return d->a_clockEv.exchange(0); }
void Session::setLimiter(bool on, float ceilingDb) { d->post([=] { d->limOn = on; d->limCeil = std::pow(10.f, clampf(ceilingDb, -12.f, 0.f) / 20.f); }); }
void Session::setDuck(int sourceTrack, float release) { d->post([=] { d->duckSrc = sourceTrack; d->duckRel = clampf(release, 0.02f, 1.5f); }); }
void Session::setDuckAmount(int t, float amount) { if (t >= 0 && t < kTracks) d->post([=] { d->duckAmt[t] = clampf(amount, 0.f, 1.f); }); }
void Session::unloadSample(int t) {
    if (t < 0 || t >= kTracks) return;
    d->post([this, t] {
        Impl::Track& tr = *d->tracks[(size_t)t]; d->releaseAll(tr);
        d->bury(std::move(tr.sampler));
        std::lock_guard<std::mutex> l(tr.infoMtx); tr.label.clear(); tr.peaks.clear();
    });
}
int Session::busyJobs() const { return d->busy.load(); }
bool Session::saveClip(int t) {
    if (t < 0 || t >= kTracks || !d->tracks[(size_t)t]->clipHas) return false;
    std::vector<float> c = d->tracks[(size_t)t]->clip;   // a copy, written on the worker
    d->job([this, c = std::move(c)]() mutable { d->finishSample(std::move(c)); d->a_pending = false; });
    return true;
}
int Session::loopPeaks(int track, float* out, int maxBins) const {
    const int n = std::min(maxBins, kLoopBins);
    for (int i = 0; i < n; i++) out[i] = track < 0 ? d->a_lpk[i].load() : d->a_tpk[std::min(track, kTracks - 1)][i].load();
    return d->a_loop.load() > 0 ? n : 0;
}

// ---- microphone sampling ----
void Session::setMicThreshold(float v) { d->micThr.store(clampf(v, 0.005f, 0.5f)); }
void Session::recorderWait(bool w) { d->post([this, w] { if (!d->sampling && !d->armedWait) d->waitForSound = w; }); }
void Session::recorderStart() {
    d->post([this] {
        if (d->sampling || d->armedWait) return;
        d->sampleBuf.clear(); d->sampleBuf.reserve((size_t)d->sr * 30); d->a_sampleSec = 0; d->liveReset();
        if (d->waitForSound) { d->armedWait = true; d->a_sampleState = 3; } else { d->sampling = true; d->a_sampleState = 1; }
    });
}
void Session::recorderStop() {
    d->post([this] {
        if (d->armedWait) { d->armedWait = false; d->sampleBuf.clear(); d->a_sampleState = 0; return; }
        if (!d->sampling) return;
        d->sampling = false; d->a_sampleState = 2;
        std::vector<float> st(d->sampleBuf.size() * 2);
        for (size_t i = 0; i < d->sampleBuf.size(); i++) st[2 * i] = st[2 * i + 1] = d->sampleBuf[i];
        d->sampleBuf.clear();
        d->job([this, st = std::move(st)]() mutable { d->finishSample(std::move(st)); });
    });
}
void Session::recorderDismiss() { std::lock_guard<std::mutex> l(d->sampleMtx); d->pending = false; d->a_pending = false; }
std::string Session::lastSamplePath() const { std::lock_guard<std::mutex> l(d->sampleMtx); return d->samplePath; }
int Session::livePeaks(float* out, int maxBins) const {
    const int n = std::min(maxBins, d->liveN.load());
    for (int i = 0; i < n; i++) out[i] = d->livePk[i].load();
    return n;
}

// ---- library ----
void Session::setDataDir(const std::string& dir) { d->dataDir = dir; }
void Session::setSampleDir(const std::string& dir) { d->sampleDir = dir; }
std::string Session::dataDir() const { return d->dataDir; }
std::string Session::sampleDir() const { return d->sampleDir; }
std::vector<std::string> Session::sampleFolders() const {
    std::vector<std::string> v; std::error_code ec;
    for (auto& e : std::filesystem::directory_iterator(d->sampleDir, ec)) if (e.is_directory()) v.push_back(e.path().filename().string());
    std::sort(v.begin(), v.end()); return v;
}
std::vector<std::string> Session::sampleFiles(const std::string& folder) const {
    std::vector<std::string> v; std::error_code ec;
    for (auto& e : std::filesystem::directory_iterator(d->sampleDir + "/" + folder, ec)) if (e.path().extension() == ".wav") v.push_back(e.path().filename().string());
    std::sort(v.begin(), v.end()); return v;
}
std::vector<std::string> Session::sampleLibrary() const {
    namespace fs = std::filesystem;
    std::vector<std::pair<fs::file_time_type, std::string>> v; std::error_code ec;
    if (d->dataDir.empty()) return {};
    for (auto& e : fs::directory_iterator(d->dataDir, ec)) {
        const auto n = e.path().filename().string();
        if (n.rfind("sample_", 0) == 0 && e.path().extension() == ".wav") v.push_back({e.last_write_time(ec), e.path().string()});
    }
    std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.first > b.first; });
    std::vector<std::string> out; for (auto& p : v) out.push_back(p.second);
    return out;
}

// ---- views ----
std::vector<float> Session::samplePeaksOf(int t, int bins) const {
    std::vector<float> out;
    if (t < 0 || t >= kTracks || bins <= 0) return out;
    const auto& tr = *d->tracks[(size_t)t];
    std::lock_guard<std::mutex> l(tr.infoMtx);
    if (tr.peaks.empty()) return out;
    out.assign((size_t)bins, 0.f);
    for (size_t i = 0; i < tr.peaks.size(); i++) { const size_t b = i * (size_t)bins / tr.peaks.size(); out[b] = std::max(out[b], tr.peaks[i]); }
    return out;
}
std::string Session::trackLabel(int t) const {
    if (t < 0 || t >= kTracks) return "";
    std::lock_guard<std::mutex> l(d->tracks[(size_t)t]->infoMtx);
    return d->tracks[(size_t)t]->label;
}
int Session::sampleRate() const { return d->sr; }
Session::Status Session::status() const {
    Status s{};
    s.playing = d->a_playing; s.pos = d->a_pos; s.loopSamples = d->a_loop;
    s.clip = d->a_clip.load() > 0; s.songOn = d->a_songOn.load() != 0; s.songPos = d->a_songPos.load(); s.songLen = d->a_songLen.load();
    for (int i = 0; i < kTracks; i++) { s.seqSlot[i] = d->a_slot[i].load(); s.seqSlotPending[i] = d->a_slotPending[i].load(); }
    s.bpm = d->a_bpm; s.loopBars = d->a_bars; s.master_peak = d->a_master; s.cpu_permille = d->a_cpu; s.cpu_peak_permille = d->a_cpuPeak;
    const uint32_t cm = d->a_clip_mask, rm = d->a_cliprec_mask;
    for (int i = 0; i < kTracks; i++) {
        s.track_peak[i] = d->tracks[(size_t)i]->peak;
        s.track_has_clip[i] = cm >> i & 1; s.track_clip_rec[i] = rm >> i & 1;
        s.clipPos[i] = d->a_clipPos; s.lfoValue[i] = d->a_lfo[i];
        s.voiceN[i] = std::min(kVoiceMax, d->a_voiceN[i].load()); for (int j = 0; j < s.voiceN[i]; j++) s.voicePos[i][j] = d->a_voicePos[i][j];
    }
    s.beatPos = d->a_beat; s.metro = d->a_metro; s.countBeats = d->a_countBeats; s.bounce = d->a_bounce; s.bounceDone = d->a_bounceDone; s.printBusy = d->a_printBusy; s.printDone = d->a_printDone; s.printMask = d->a_printMask;
    for (int i = 0; i < kTracks; i++) { s.seqCursor[i] = d->a_seqCursor[i]; s.seqUndoN[i] = d->a_seqUndoN[i]; s.seqRedoN[i] = d->a_seqRedoN[i]; }
    for (int i = 0; i < kTracks; i++) { s.seqState[i] = d->a_seqState[i]; { const int f = d->a_seqFlags[i]; s.seqRun[i] = f & 1; s.seqPend[i] = f >> 1 & 1; s.keyTrig[i] = f >> 2 & 1; s.seqOne[i] = f >> 3 & 1; s.seqTrans[i] = d->a_seqTrans[i]; s.seqPos[i] = d->a_seqPos[i]; } s.seqBars[i] = d->a_seqBars[i]; s.seqCount[i] = d->a_seqN[i]; }
    s.globalUndoN = d->a_gUndoN; s.globalRedoN = d->a_gRedoN;
    s.freeRecording = d->a_free; s.freeArmed = d->a_freeArmed; s.freeSeconds = d->a_freeSec;
    s.sampleState = d->a_sampleState; s.sampleTrack = d->a_sampleTrack; s.sampleSeconds = d->a_sampleSec; s.inputPeak = d->a_inPeak;
    s.sampleVersion = d->a_sampleVersion; s.sampleFails = d->a_sampleFails; s.samplePending = d->a_pending;
    return s;
}
