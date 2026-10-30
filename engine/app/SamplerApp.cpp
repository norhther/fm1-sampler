#include "SamplerApp.h"
#include <Common.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
constexpr uint16_t rgb(int r, int g, int b) { return (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3)); }
// The palette of the FM-1 screens (and of the phone): ink, paper, one amber accent.
constexpr uint16_t BLACK = 0, PAPER = rgb(237, 235, 228), DIM = rgb(120, 126, 140), ACCENT = rgb(232, 163, 61), BAR = rgb(46, 54, 72), PANEL = rgb(22, 27, 38);
const char* kPage[] = {"SAMPLE", "FX", "ENVELOPE", "LFO", "ARP", "TAPE", "MIXER", "KEY", "LOOP", "LENGTH", "FX 2", "LIVE FX", "SEQUENCER", "TEMPO", "TRACK FX", "TIMING", "TONE", "MODULATE", "LIVE FX 2"};
const char* kAlgo[] = {"MELODIC", "ONE-SHOT", "LOOPED", "KIT/SLICE", "GRANULAR", "PAULSTRETCH"};
const char* kScaleName[] = {"CHROM", "MIX", "MAJ", "MIN", "HMIN", "PHRYG", "MIN+", "OFF", "DORIAN", "LYDIAN", "LOCR", "PENT+", "PENT-", "BLUES", "WHOLE", "DIM", "PHRDOM", "HMAJ", "MELMIN", "HUNG", "HIRA", "DHARM"};
const char* kModeName[] = {"KEY", "CHORD", "SNAP"};
const char* kRepeat[] = {"OFF", "1/4", "1/8", "1/16", "1/32"};
const char* kOffOn[] = {"OFF", "ON"};
const char* kGate[] = {"OFF", "1/8", "1/16", "1/32"};
const char* kTakeQ[] = {"OFF", "BEAT", "BAR"};
const char* kCountIn[] = {"OFF", "1 BAR", "2 BARS"};
const char* kNote[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
const char* kArpRate[] = {"OFF", "1", "2", "2T", "4", "4T", "8", "8T", "16", "16T", "32", "32T", "64"};
const char* kArpPatt[] = {"UP", "DOWN", "UPDN", "CONV", "RAND"};
const char* kLfoTarget[] = {"PITCH", "FILTER", "VOLUME", "PAN"};
const char* kLfoShape[] = {"SINE", "TRI", "SQUARE", "S&H"};
std::vector<uint8_t> n14(int v) { return {(uint8_t)(v >> 7 & 127), (uint8_t)(v & 127)}; }
std::vector<uint8_t> col(uint16_t c) { return {(uint8_t)(c >> 14 & 3), (uint8_t)(c >> 7 & 127), (uint8_t)(c & 127)}; }
void cat(std::vector<uint8_t>& a, const std::vector<uint8_t>& b) { a.insert(a.end(), b.begin(), b.end()); }
std::string pad(std::string s, size_t n) { if (s.size() > n) s.resize(n); while (s.size() < n) s += ' '; return s; }
float cl(float v, float lo = 0.f, float hi = 1.f) { return std::min(hi, std::max(lo, v)); }
std::string fmt(const char* f, double v) { char b[48]; snprintf(b, sizeof b, f, v); return b; }
std::string pct(float v) { return fmt("%.0f%%", v * 100); }
// Tonality. The firmware is kept on a plain chromatic keyboard (scale OFF, notes 53+key) so keys outside the scale stay playable; the app only
// lights the keys that belong to the scale, shifts the notes by the octave and builds chords itself.
constexpr int kScaleCount = 22, kScaleOff = 7;   // ids 0..6 are the firmware's tables, 7 = OFF (the firmware's value), 8.. only exist in the app
const int kScaleN[kScaleCount] = {12, 7, 7, 7, 7, 7, 8, 0, 7, 7, 7, 5, 5, 6, 6, 8, 7, 7, 7, 7, 5, 7};
const int kScaleStep[kScaleCount][12] = {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}, {0, 2, 4, 5, 7, 9, 10}, {0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 8, 10},
                               {0, 2, 3, 5, 7, 8, 11}, {0, 1, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 8, 10, 11}, {0},
                               {0, 2, 3, 5, 7, 9, 10}, {0, 2, 4, 6, 7, 9, 11}, {0, 1, 3, 5, 6, 8, 10}, {0, 2, 4, 7, 9}, {0, 3, 5, 7, 10}, {0, 3, 5, 6, 7, 10},
                               {0, 2, 4, 6, 8, 10}, {0, 1, 3, 4, 6, 7, 9, 10}, {0, 1, 4, 5, 7, 8, 10}, {0, 2, 4, 5, 7, 8, 11}, {0, 2, 3, 5, 7, 9, 11},
                               {0, 2, 3, 6, 7, 8, 11}, {0, 2, 3, 7, 8}, {0, 1, 4, 5, 7, 8, 11}};
// the order of the SCALE knob: OFF last
const int kScaleOrder[kScaleCount] = {0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 7};
inline bool hasScale(int s) { return s != kScaleOff; }
int snapToScale(int note, int scale, int root) {   // nearest note of the scale (ties go down)
    if (!hasScale(scale)) return note;
    for (int d = 0; d <= 6; d++) {
        for (int sgn : {-1, 1}) {
            const int n = note + sgn * d, pc = (((n - root) % 12) + 12) % 12;
            for (int i = 0; i < kScaleN[scale]; i++) if (kScaleStep[scale][i] == pc) return n;
        }
    }
    return note;
}
int scaleIndex(int pc, int scale) { for (int i = 0; i < kScaleN[scale]; i++) if (kScaleStep[scale][i] == pc) return i; return -1; }   // degree of a pitch class, -1 outside
int degreeNote(int d, int scale, int root) { const int n = kScaleN[scale], o = d >= 0 ? d / n : -((-d + n - 1) / n); return root + 12 * o + kScaleStep[scale][d - o * n]; }   // degree 0 = the root of octave 0
float speedOf(float n) { return 0.25f * std::pow(16.f, n); }          // 0.25..4
float micThrOf(float v) { return 0.005f * std::pow(100.f, v); }       // -46..-6 dBFS
float loopSpeedOf(float n) { return 0.25f * std::pow(8.f, n); }       // 0.25..2
float lfoHz(float n) { return 0.1f * std::pow(200.f, n); }            // 0.1..20
}  // namespace

const char* SamplerApp::pageName(Page p) { return kPage[p]; }
const char* SamplerApp::algoName(int i) { return kAlgo[std::min(5, std::max(0, i))]; }
const char* SamplerApp::scaleName(int i) { return kScaleName[std::min(kScaleCount - 1, std::max(0, i))]; }
const char* SamplerApp::modeName(int m) { static const char* n[] = {"SOUND", "FX", "SEQ", "MIX"}; return n[m & 3]; }
const SamplerApp::Page* SamplerApp::modePages(int m) {
    static const Page p[kModes][7] = {{P_HOME, P_ENV, P_LFO, P_ARP}, {P_FX, P_FX2, P_TONE, P_MOD, P_LIVE, P_LIVE2, P_TFX}, {P_SQ, P_SEQ, P_TEMPO, P_TIMING}, {P_MIX, P_KEY, P_SAVE, P_LEN}};
    return p[m & 3];
}
int SamplerApp::modePageCount(int m) { return (m & 3) == 1 ? 7 : 4; }
int SamplerApp::modeOf(Page pg) { for (int m = 0; m < kModes; m++) for (int i = 0; i < modePageCount(m); i++) if (modePages(m)[i] == pg) return m; return 0; }
int SamplerApp::pageSlot(Page pg) { const int m = modeOf(pg); for (int i = 0; i < modePageCount(m); i++) if (modePages(m)[i] == pg) return i; return 0; }
int SamplerApp::scaleSteps(int scale, int* out) {
    scale = std::min(kScaleCount - 1, std::max(0, scale));
    for (int i = 0; i < kScaleN[scale]; i++) out[i] = kScaleStep[scale][i];
    return kScaleN[scale];
}
int SamplerApp::snapNote(int note, int scale, int root) { return snapToScale(note, scale, root); }
const char* SamplerApp::noteName(int r) { return kNote[((r % 12) + 12) % 12]; }

SamplerApp::SamplerApp(Session& s, Send send) : s_(s), send_(std::move(send)) {
    cells_.resize(320); rects_.assign(320, ~0ull);
    for (int& h : heldNote_) h = -1;
    for (int i = 0; i < kTracks; i++) { ts_[i].vol = 0.8f; }
    refreshLibrary();
    applyFx();
    s_.setRetLevel(1, 0.7f);
    s_.setLoopSource(loopSrc_);
    // a first session to play with: keys on 1, a drum kit on 2, bass on 3
    auto first = [&](const char* group, int track) {
        for (size_t i = 0; i < lib_.size(); i++) if (lib_[i].group == group) { ts_[track].entry = (int)i; if (group == std::string("Drums")) ts_[track].algo = 3; s_.loadSamplePath(lib_[i].path, lib_[i].kit, track); return; }
    };
    // the default project: Tape Key, Acoustic bass, Shiny, Trap Kit
    auto byLabel = [&](const char* label, int track) {
        for (size_t i = 0; i < lib_.size(); i++) if (lib_[i].label == label) { ts_[track].entry = (int)i; if (lib_[i].kit) ts_[track].algo = 3; s_.loadSamplePath(lib_[i].path, lib_[i].kit, track); return; }
    };
    byLabel("Tape Key", 0); byLabel("Acoustic", 1); byLabel("Shiny", 2); byLabel("TrapKit", 3); (void)first;
    for (int t = 0; t < kTracks; t++) { applyMix(t); applyPattern(t); }
    applyMasterFx(); applyTempo();
    if (ts_[0].entry >= 0) libCursor_ = ts_[0].entry;
}

// ---- library ----
void SamplerApp::refreshLibrary() {
    lib_.clear();
    const std::string dir = s_.sampleDir();
    for (auto& f : s_.sampleFolders())
        for (auto& file : s_.sampleFiles(f)) {
            std::string stem = file.substr(0, file.rfind('.'));
            lib_.push_back({stem, f, dir + "/" + f + "/" + file, f == "Drums"});
        }
    for (auto& p : s_.sampleLibrary()) {
        std::string nm = p.substr(p.find_last_of('/') + 1); nm = nm.substr(0, nm.rfind('.'));
        if (nm.rfind("sample_", 0) == 0) nm = nm.substr(7);
        lib_.push_back({nm, "User", p, false});
    }
    if (libCursor_ >= (int)lib_.size()) libCursor_ = 0;
}
void SamplerApp::browse(int d) {
    if (lib_.empty()) return;
    libCursor_ = ((libCursor_ + (d > 0 ? 1 : -1)) % (int)lib_.size() + (int)lib_.size()) % (int)lib_.size();
    auditionAt_ = nowMs_ + 250;
}
void SamplerApp::loadEntry(int i) {
    if (i < 0 || i >= (int)lib_.size()) return;
    libCursor_ = i; auditionAt_ = 0;
    const Entry& e = lib_[(size_t)i];
    TrackState& t = ts_[track_];
    t.entry = i; t.start = 0.f; t.end = 1.f;
    if (e.kit) t.algo = 3; else if (t.algo == 3) t.algo = 0;
    previewAfterLoad_ = true;
    s_.loadSamplePath(e.path, e.kit, track_);
}
void SamplerApp::importFile(const std::string& wav) {
    refreshLibrary();
    for (size_t i = 0; i < lib_.size(); i++) if (lib_[i].path == wav) { loadEntry((int)i); say("LOADED FROM DISK"); return; }
    say("FILE NOT READABLE");
}

// ---- parameters ----
void SamplerApp::applyAll(int t) {
    applyAlgo(t); applyEnv(t); applyLfo(t); applyHome(t); applyArp(t); applySeq(t); applyMix(t);
}
void SamplerApp::applyAlgo(int t) { s_.setMode(t, ts_[t].algo); }
void SamplerApp::applyEnv(int t) {
    const TrackState& x = ts_[t];
    s_.setEnv(t, 3.f * x.a * x.a, x.d, x.s, 5.f * x.r * x.r);   // seconds (as shown on the screens), sustain level
}
void SamplerApp::applyLfo(int t) {
    const TrackState& x = ts_[t];
    Session::Lfo l; l.rate = lfoHz(x.lfoRate); l.depth = x.lfoDepth; l.target = x.lfoTarget; l.shape = x.lfoShape; l.base = 0.5f;
    s_.setLfo(t, l);   // the engine runs the low-pass only while the LFO sweeps the filter
}
void SamplerApp::applyHome(int t, bool commit) {
    const TrackState& x = ts_[t];
    s_.setSampleRange(t, x.start, x.end);
    s_.setStretch(t, speedOf(x.speed), std::round((x.pitch - 0.5f) * 48.f), commit);
}
void SamplerApp::applyArp(int t) {
    const TrackState& x = ts_[t];
    s_.setArp(t, x.arpRate > 0, std::max(0, x.arpRate - 1), x.arpPatt, x.arpOct + 1, x.arpLen);
}
void SamplerApp::applySeq(int) {   // loop speed and tape colour are global: every track gets them
    s_.setLoopSpeed(loopSpeedOf(loopSpeed_));
    for (int i = 0; i < kTracks; i++)
        s_.setTape(i, exciter_, 1.f - 0.4f * lofi_, wobble_, 0.25f * lofi_, cl(std::max(exciter_ * 3.f, std::max(wobble_, lofi_))), 1.f - 0.6f * lofi_);
}
void SamplerApp::applyMix(int t) {
    const TrackState& x = ts_[t];
    s_.setVolume(t, x.vol * 1.2f); s_.setPan(t, x.pan * 2.f - 1.f); s_.setSend(t, 0, x.sendR); s_.setSend(t, 1, x.sendD);
}
void SamplerApp::applyFx() {
    s_.setReverb(reverbSize_);
    s_.setRetLevel(0, reverbDw_);
    s_.setDelay(0.05f + delayTime_ * 1.45f, delayFb_ * 0.95f);
}
void SamplerApp::applyMasterFx() {
    FxParams p; p.chorus = chorus_; p.flanger = flanger_; p.phaser = phaser_; p.comp = comp_; p.width = width_; p.filter = djFilter_; p.crush = crush_; p.repeat = repeat_; p.gate = gate_;
    p.drive = drive_; p.eqLow = eqLow_; p.eqMid = eqMid_; p.eqHigh = eqHigh_; p.tremolo = tremolo_; p.wah = wah_; p.ringmod = ringmod_; p.tapeStop = tapeStop_; p.reverse = reverse_; p.freeze = freeze_;
    s_.setMasterFx(p);
}
void SamplerApp::applyTempo() {
    s_.setMetronome(metro_ != 0, metroVol_);
    s_.setTakeQuant(takeQuant_);
    s_.setQuantize(seqGrid_);
    s_.setInputQuant(liveGrid_);
    s_.setCountIn(countIn_);
}
void SamplerApp::applyTrackFx(int t) {
    const TrackState& x = ts_[t];
    FxParams p; p.filter = x.tfDj; p.chorus = x.tfChorus; p.crush = x.tfCrush; p.gate = x.tfGate;
    s_.setTrackFx(t, p);
}
void SamplerApp::applyPattern(int t) { s_.seqSetBars(t, 1 << ts_[t].seqBars); s_.seqSetState(t, ts_[t].seqState); }
void SamplerApp::queueCommit(int t) { commitAt_ = nowMs_ + 350; commitTrack_ = t; }   // the offline stretch / pitch runs once the knob rests
void SamplerApp::setAlgo(int a) { ts_[track_].algo = ((a % 6) + 6) % 6; applyAlgo(track_); }
void SamplerApp::setVolume(int t, float v) { ts_[t].vol = cl(v); applyMix(t); }
void SamplerApp::setPan01(int t, float v) { ts_[t].pan = cl(v); applyMix(t); }
void SamplerApp::setSend(int t, int w, float v) { (w ? ts_[t].sendD : ts_[t].sendR) = cl(v); applyMix(t); }
void SamplerApp::setScale(int scale, int root, int octave, int mode) {
    scale_ = {std::min(kScaleCount - 1, std::max(0, scale)), ((root % 12) + 12) % 12, std::min(3, std::max(-3, octave)), std::min(2, std::max(0, mode))};
    sysex(6, {7, (uint8_t)scale_.root, (uint8_t)(scale_.octave + 3), (uint8_t)(scale_.mode == 1)}, true);
}
void SamplerApp::selectTrack(int t) {
    t = std::min(kTracks - 1, std::max(0, t));
    if (t == track_) return;
    if (st_.track_clip_rec[track_]) s_.clipRecord(track_, false);   // an overdub belongs to the track it was started on
    track_ = t;
    if (ts_[t].entry >= 0) libCursor_ = ts_[t].entry;   // PRESETS starts from the sample this track plays
}
void SamplerApp::gotoPage(Page p) { page_ = p; lastPage_[modeOf(p)] = p; }
void SamplerApp::modeButton(int m) {   // a mode button: enter the mode on the page it was left on, press again for the next page
    gotoPage(modeOf(page_) == m ? modePages(m)[(pageSlot(page_) + 1) % modePageCount(m)] : lastPage_[m]);
}

SamplerApp::Knob SamplerApp::cont(const char* name, float* v, std::function<std::string(float)> f, std::function<void()> apply, float stepSize, bool commit) {
    Knob k; k.name = name; k.valid = true; k.norm = *v; k.value = f(*v);
    const int t = track_;
    k.step = [=](int d) { *v = cl(*v + d * stepSize); apply(); if (commit) queueCommit(t); };
    k.set = [=](float n) { *v = cl(n); apply(); if (commit) queueCommit(t); };
    return k;
}
SamplerApp::Knob SamplerApp::choice(const char* name, int* v, std::vector<std::string> opts, std::function<void()> apply) {
    Knob k; k.name = name; k.valid = true; k.isChoice = true;
    const int n = (int)opts.size();
    *v = std::min(n - 1, std::max(0, *v));
    k.value = opts[(size_t)*v]; k.norm = n > 1 ? *v / (float)(n - 1) : 0.f;
    k.step = [=](int d) { *v = std::min(n - 1, std::max(0, *v + (d > 0 ? 1 : -1))); apply(); };
    k.set = [=](float x) { *v = std::min(n - 1, std::max(0, (int)std::lround(x * (n - 1)))); apply(); };
    return k;
}

SamplerApp::Knobs SamplerApp::knobs() { return knobs(page_); }
SamplerApp::Knobs SamplerApp::knobs(Page p) {
    Knobs k;
    TrackState& x = ts_[track_];
    const int t = track_;
    auto sec = [](float s) { return fmt(s < 1 ? "%.2fS" : "%.1fS", s); };
    switch (p) {
    case P_HOME:
        k[0] = cont("START", &x.start, [](float v) { return pct(v); }, [=] { if (ts_[t].start > ts_[t].end - 0.02f) ts_[t].start = ts_[t].end - 0.02f; applyHome(t); }, 0.01f, true);
        k[1] = cont("END", &x.end, [](float v) { return pct(v); }, [=] { if (ts_[t].end < ts_[t].start + 0.02f) ts_[t].end = ts_[t].start + 0.02f; applyHome(t); }, 0.01f, true);
        k[2] = cont("SPEED", &x.speed, [](float v) { return fmt("%.2fX", speedOf(v)); }, [=] { applyHome(t); }, 0.01f, true);
        k[3] = cont("PITCH", &x.pitch, [](float v) { return fmt("%+.0f ST", std::round((v - 0.5f) * 48.f)); }, [=] { applyHome(t); }, 1.f / 48.f, true);
        break;
    case P_FX:
        k[0] = cont("REVERB SIZE", &reverbSize_, [](float v) { return pct(v); }, [=] { applyFx(); });
        k[1] = cont("REVERB D/W", &reverbDw_, [](float v) { return pct(v); }, [=] { applyFx(); });
        k[2] = cont("DELAY TIME", &delayTime_, [](float v) { return fmt("%.2fS", 0.05 + v * 1.45); }, [=] { applyFx(); });
        k[3] = cont("DELAY FEEDBACK", &delayFb_, [](float v) { return pct(v); }, [=] { applyFx(); });
        break;
    case P_ENV:
        k[0] = cont("ATTACK", &x.a, [=](float v) { return sec(3 * v * v); }, [=] { applyEnv(t); });
        k[1] = cont("DECAY", &x.d, [=](float v) { return sec(v); }, [=] { applyEnv(t); });
        k[2] = cont("SUSTAIN", &x.s, [](float v) { return pct(v); }, [=] { applyEnv(t); });
        k[3] = cont("RELEASE", &x.r, [=](float v) { return sec(5 * v * v); }, [=] { applyEnv(t); });
        break;
    case P_LFO:
        k[0] = cont("RATE", &x.lfoRate, [](float v) { return fmt("%.1fHZ", lfoHz(v)); }, [=] { applyLfo(t); });
        k[1] = cont("DEPTH", &x.lfoDepth, [](float v) { return pct(v); }, [=] { applyLfo(t); });
        k[2] = choice("TARGET", &x.lfoTarget, {kLfoTarget[0], kLfoTarget[1], kLfoTarget[2], kLfoTarget[3]}, [=] { applyLfo(t); });
        k[3] = choice("SHAPE", &x.lfoShape, {kLfoShape[0], kLfoShape[1], kLfoShape[2], kLfoShape[3]}, [=] { applyLfo(t); });
        break;
    case P_ARP: {
        std::vector<std::string> rates(kArpRate, kArpRate + 13), patt(kArpPatt, kArpPatt + 5);
        k[0] = choice("RATE", &x.arpRate, rates, [=] { applyArp(t); });
        k[1] = choice("PATTERN", &x.arpPatt, patt, [=] { applyArp(t); });
        k[2] = choice("OCTAVES", &x.arpOct, {"1", "2", "3", "4"}, [=] { applyArp(t); });
        k[3] = cont("LENGTH", &x.arpLen, [](float v) { return pct(v); }, [=] { applyArp(t); });
        break;
    }
    case P_SEQ:
        k[0] = cont("LOOP SPEED", &loopSpeed_, [](float v) { return fmt("%.2fX", loopSpeedOf(v)); }, [=] { applySeq(t); }, 0.02f);
        k[1] = cont("EXCITER", &exciter_, [](float v) { return pct(v); }, [=] { applySeq(t); });
        k[2] = cont("LO-FI", &lofi_, [](float v) { return pct(v); }, [=] { applySeq(t); });
        k[3] = cont("WOBBLE", &wobble_, [](float v) { return pct(v); }, [=] { applySeq(t); });
        break;
    case P_MIX:
        k[0] = cont("VOLUME", &x.vol, [](float v) { return pct(v); }, [=] { applyMix(t); });
        k[1] = cont("PAN", &x.pan, [](float v) { int p100 = (int)std::lround((v - 0.5f) * 200); return p100 == 0 ? std::string("C") : fmt(p100 < 0 ? "L%.0f" : "R%.0f", std::abs(p100)); }, [=] { applyMix(t); });
        k[2] = cont("REVERB SEND", &x.sendR, [](float v) { return pct(v); }, [=] { applyMix(t); });
        k[3] = cont("DELAY SEND", &x.sendD, [](float v) { return pct(v); }, [=] { applyMix(t); });
        break;
    case P_KEY: {
        auto mk = [this](const char* name, std::vector<std::string> opts, int cur, std::function<void(int)> setter) {
            Knob b; b.name = name; b.valid = true; b.isChoice = true;
            const int n = (int)opts.size(); cur = std::min(n - 1, std::max(0, cur));
            b.value = opts[(size_t)cur]; b.norm = cur / (float)(n - 1);
            b.step = [=](int d) { setter(std::min(n - 1, std::max(0, cur + (d > 0 ? 1 : -1)))); };
            b.set = [=](float v) { setter(std::min(n - 1, std::max(0, (int)std::lround(v * (n - 1))))); };
            return b;
        };
        const ScaleState sc = scale_;
        int pos = 0; for (int i = 0; i < kScaleCount; i++) if (kScaleOrder[i] == sc.scale) pos = i;
        std::vector<std::string> names; for (int i = 0; i < kScaleCount; i++) names.push_back(kScaleName[kScaleOrder[i]]);
        k[0] = mk("SCALE", names, pos, [=](int v) { setScale(kScaleOrder[v], sc.root, sc.octave, sc.mode); });
        k[1] = mk("ROOT", std::vector<std::string>(kNote, kNote + 12), sc.root, [=](int v) { setScale(sc.scale, v, sc.octave, sc.mode); });
        k[2] = mk("OCTAVE", {"-3", "-2", "-1", "0", "+1", "+2", "+3"}, sc.octave + 3, [=](int v) { setScale(sc.scale, sc.root, v - 3, sc.mode); });
        k[3] = mk("MODE", {"KEY", "CHORD", "SNAP"}, sc.mode, [=](int v) { setScale(sc.scale, sc.root, sc.octave, v); });
        break;
    }
    case P_SAVE: {
        static const char* micTxt[] = {"READY", "WAITING", "RECORDING", "SAVING"};
        Knob m; m.name = "MIC SAMPLE"; m.valid = true; m.isChoice = true; m.value = micTxt[mic_]; m.norm = mic_ == 0 ? 0.f : 1.f;
        m.step = [this](int d) { if ((d > 0) == (mic_ == 0)) micSample(); };
        m.set = [this](float v) { if ((v > 0.5f) == (mic_ == 0)) micSample(); };
        k[0] = m;
        k[1] = choice("LOOP SOURCE", &loopSrc_, {"MIC", "SOUND"}, [this] { s_.setLoopSource(loopSrc_); });
        Knob sv; sv.name = "SAVE LOOP"; sv.valid = true; sv.isChoice = true; sv.value = st_.track_has_clip[track_] ? "TURN" : "EMPTY"; sv.step = [this](int) { loopSave(); }; sv.set = [this](float) { loopSave(); };
        k[2] = sv;
        k[3] = cont("MIC THRESHOLD", &micThr_, [](float v) { return fmt("%.0f DB", 20 * std::log10(micThrOf(v))); }, [this] { s_.setMicThreshold(micThrOf(micThr_)); }, 0.02f);
        break;
    }
    case P_LEN: {
        const double sec = st_.loopSamples > 0 ? st_.loopSamples / (double)SAMPLERATE : 0.0;
        const float cur = st_.loopSamples > 0 ? cl((float)(std::log2(std::max(1.0, (double)st_.loopBars)) / 5.0)) : 0.f;
        Knob len; len.name = "LENGTH"; len.valid = true; len.isChoice = true;
        len.value = st_.loopSamples > 0 ? fmt("%.1fS", sec) : "NO LOOP"; len.norm = cur;
        len.step = [this](int d) { lenAction(d > 0); };
        len.set = [this, cur](float v) { if (v > cur + 0.05f) lenAction(true); else if (v < cur - 0.05f) lenAction(false); };
        auto act = [this](const char* name, const char* what, bool dbl) {
            Knob a; a.name = name; a.valid = true; a.isChoice = true; a.value = st_.loopSamples > 0 ? what : "NO LOOP";
            a.step = [this, dbl](int) { lenAction(dbl); }; a.set = [this, dbl](float) { lenAction(dbl); };
            return a;
        };
        k[0] = len;
        k[1] = act("DUPLICATE", "TURN: X2", true);
        k[2] = act("HALVE", "TURN: /2", false);
        Knob b; b.name = "BARS / BPM"; b.valid = true; b.isChoice = true; b.value = fmt("%.0f", st_.loopBars) + " / " + fmt("%.0f", st_.bpm); b.step = [](int) {}; b.set = [](float) {};
        k[3] = b;
        break;
    }
    case P_FX2:
        k[0] = cont("CHORUS", &chorus_, [](float v) { return pct(v); }, [=] { applyMasterFx(); });
        k[1] = cont("FLANGER", &flanger_, [](float v) { return pct(v); }, [=] { applyMasterFx(); });
        k[2] = cont("PHASER", &phaser_, [](float v) { return pct(v); }, [=] { applyMasterFx(); });
        k[3] = cont("COMPRESSOR", &comp_, [](float v) { return pct(v); }, [=] { applyMasterFx(); });
        break;
    case P_TONE: {
        auto db = [](float v) { return std::fabs(v - 0.5f) < 0.02f ? std::string("FLAT") : fmt("%+.0f DB", (v - 0.5f) * 24.0); };
        k[0] = cont("DRIVE", &drive_, [](float v) { return pct(v); }, [=] { applyMasterFx(); });
        k[1] = cont("EQ LOW", &eqLow_, db, [=] { applyMasterFx(); }, 0.02f);
        k[2] = cont("EQ MID", &eqMid_, db, [=] { applyMasterFx(); }, 0.02f);
        k[3] = cont("EQ HIGH", &eqHigh_, db, [=] { applyMasterFx(); }, 0.02f);
        break;
    }
    case P_MOD:
        k[0] = cont("TREMOLO", &tremolo_, [](float v) { return pct(v); }, [=] { applyMasterFx(); });
        k[1] = cont("AUTO WAH", &wah_, [](float v) { return pct(v); }, [=] { applyMasterFx(); });
        k[2] = cont("RING MOD", &ringmod_, [](float v) { return v < 0.005f ? std::string("OFF") : fmt("%.0f HZ", 20.0 * std::pow(150.0, v)); }, [=] { applyMasterFx(); });
        k[3] = cont("WIDTH", &width_, [](float v) { return v < 0.01f ? std::string("MONO") : pct(v * 2.f); }, [=] { applyMasterFx(); });
        break;
    case P_LIVE2:
        k[0] = choice("REVERSE", &reverse_, {kRepeat[0], kRepeat[1], kRepeat[2], kRepeat[3], kRepeat[4]}, [=] { applyMasterFx(); });
        k[1] = cont("TAPE STOP", &tapeStop_, [](float v) { return v < 0.02f ? std::string("OFF") : pct(v); }, [=] { applyMasterFx(); });
        k[2] = choice("FREEZE", &freeze_, {kOffOn[0], kOffOn[1]}, [=] { applyMasterFx(); });
        break;
    case P_LIVE:
        k[0] = cont("DJ FILTER", &djFilter_, [](float v) {
            if (std::fabs(v - 0.5f) < 0.02f) return std::string("OFF");
            return v < 0.5f ? "LP " + pct(v * 2.f) : "HP " + pct((v - 0.5f) * 2.f); }, [=] { applyMasterFx(); }, 0.02f);
        k[1] = choice("BEAT REPEAT", &repeat_, {kRepeat[0], kRepeat[1], kRepeat[2], kRepeat[3], kRepeat[4]}, [=] { applyMasterFx(); });
        k[2] = choice("TRANCE GATE", &gate_, {kGate[0], kGate[1], kGate[2], kGate[3]}, [=] { applyMasterFx(); });
        k[3] = cont("CRUSH", &crush_, [](float v) { return pct(v); }, [=] { applyMasterFx(); });
        break;
    case P_SQ: {
        std::vector<std::string> grids; for (int g = 0; g < kGrids; g++) grids.push_back(gridName(g));
        k[0] = choice("PATTERN", &x.seqState, {"OFF", "PLAY", "REC"}, [=] { applyPattern(t); });
        k[1] = choice("BARS", &x.seqBars, {"1", "2", "4", "8"}, [=] { applyPattern(t); });
        k[2] = choice("QUANTIZE", &seqGrid_, grids, [=] { applyTempo(); });
        Knob c; c.name = "CLEAR"; c.valid = true; c.isChoice = true;
        c.value = st_.seqCount[t] ? std::to_string(st_.seqCount[t]) + " NOTES" : "EMPTY";
        c.step = [this](int) { seqClear(); }; c.set = [this](float) { seqClear(); };
        k[3] = c;
        break;
    }
    case P_TFX:
        k[0] = cont("DJ FILTER", &x.tfDj, [](float v) {
            if (std::fabs(v - 0.5f) < 0.02f) return std::string("OFF");
            return v < 0.5f ? "LP " + pct(v * 2.f) : "HP " + pct((v - 0.5f) * 2.f); }, [=] { applyTrackFx(t); }, 0.02f);
        k[1] = cont("CHORUS", &x.tfChorus, [](float v) { return pct(v); }, [=] { applyTrackFx(t); });
        k[2] = choice("TRANCE GATE", &x.tfGate, {kGate[0], kGate[1], kGate[2], kGate[3]}, [=] { applyTrackFx(t); });
        k[3] = cont("CRUSH", &x.tfCrush, [](float v) { return pct(v); }, [=] { applyTrackFx(t); });
        break;
    case P_TIMING: {
        std::vector<std::string> grids; for (int g = 0; g < kGrids; g++) grids.push_back(gridName(g));
        k[0] = choice("LIVE QUANTIZE", &liveGrid_, grids, [=] { applyTempo(); });
        k[1] = choice("SEQ QUANTIZE", &seqGrid_, grids, [=] { applyTempo(); });
        k[2] = choice("COUNT-IN", &countIn_, {kCountIn[0], kCountIn[1], kCountIn[2]}, [=] { applyTempo(); });
        break;
    }
    case P_TEMPO: {
        const bool locked = st_.loopSamples > 0;
        Knob b; b.name = "TEMPO"; b.valid = true; b.norm = cl((tempo_ - 30.f) / 270.f);
        b.value = locked ? fmt("%.0f LOCK", st_.bpm) : fmt("%.0f BPM", tempo_);
        b.step = [this](int d) { setTempo(tempo_ + d); }; b.set = [this](float v) { setTempo(30.f + v * 270.f); };
        k[0] = b;
        k[1] = choice("METRONOME", &metro_, {"OFF", "ON"}, [=] { applyTempo(); });
        k[2] = cont("CLICK LEVEL", &metroVol_, [](float v) { return pct(v); }, [=] { applyTempo(); });
        k[3] = choice("TAKE SNAP", &takeQuant_, {kTakeQ[0], kTakeQ[1], kTakeQ[2]}, [=] { applyTempo(); });
        break;
    }
    default: break;
    }
    return k;
}
void SamplerApp::setTempo(float bpm) {
    if (st_.loopSamples > 0) { say("TEMPO LOCKED BY THE LOOP"); return; }
    tempo_ = cl(std::round(bpm), 30.f, 300.f);
    s_.setTempo(tempo_);
}

// ---- looper, recorder ----
void SamplerApp::transportPlay() { s_.play(!st_.playing); }
void SamplerApp::loopRecord() {
    if (st_.freeArmed || st_.freeRecording) { s_.freeRecord(track_, false); say("LOOP SET"); return; }
    if (st_.bounce) { say("RECORDING THE PATTERN"); return; }
    if (ts_[track_].seqState == 1 && st_.seqCount[track_] > 0) { seqBounce(); return; }   // the pattern plays: REC puts one pass of it on the tape
    if (!loopAny_ && !st_.track_clip_rec[track_]) {   // a clean looper: arm the first take, it starts with the first note (or sound) and sets the loop length
        s_.freeRecord(track_, true);
        say(countIn_ ? "COUNT-IN" : loopSrc_ ? "PLAY A KEY TO START" : "WAITING FOR SOUND");
        return;
    }
    if (!st_.playing) s_.play(true);
    s_.clipRecord(track_, !st_.track_clip_rec[track_]);
    say(st_.track_clip_rec[track_] ? "OVERDUB OFF" : "OVERDUB");
}
void SamplerApp::seqBounce() {
    if (!st_.seqCount[track_]) { say("PATTERN IS EMPTY"); return; }
    if (st_.bounce) { say("ALREADY RECORDING"); return; }
    ts_[track_].seqState = 1;
    s_.seqBounce(track_);
    say(loopAny_ ? "TO TAPE AT THE LOOP START" : "PATTERN TO TAPE");
}
void SamplerApp::seqToggleAt(int tick, int note) { s_.seqToggle(track_, tick, stepTicks(), note, 100); }
void SamplerApp::lenAction(bool dbl) {   // loop x2 (the content repeats) or /2; guarded so a dragged slider does not double it again and again
    if (st_.loopSamples <= 0 || nowMs_ - lenAt_ < 300) return;
    lenAt_ = nowMs_ ? nowMs_ : 1;
    if (dbl) { s_.loopDouble(); say("LOOP DOUBLED"); } else { s_.loopHalve(); say("LOOP HALVED"); }
}
void SamplerApp::loopSave() {
    if (!s_.saveClip(track_)) { say("NO LOOP TO SAVE"); return; }
    say("LOOP SAVED TO LIBRARY");
    previewAfterLoad_ = false;
    auditionAt_ = 0;
    commitAt_ = nowMs_ + 1;   // refresh the library shortly, once the file is written
    commitTrack_ = -2;
}
void SamplerApp::loopClear() {   // SAVE held / CLEAR: every loop of every track, so the looper is clean again
    s_.clipClearAll();
    say("ALL LOOPS CLEARED");
}
void SamplerApp::micSample() {
    if (mic_ == 0) {
        s_.recorderWait(true); s_.recorderStart();
        mic_ = 1; micTrack_ = track_; say("SAMPLING: MAKE A SOUND");
    } else if (mic_ == 1) { s_.recorderStop(); mic_ = 0; say("SAMPLING CANCELLED"); }
    else if (mic_ == 2) { s_.recorderStop(); mic_ = 3; say("SAVING"); }
}
std::string SamplerApp::loopText() const {
    if (st_.countBeats > 0) return "COUNT-IN " + std::to_string(st_.countBeats);
    if (st_.bounce) return st_.bounce == 1 ? "PATTERN TO TAPE: WAITING FOR THE LOOP START" : "PATTERN TO TAPE: RECORDING";
    if (st_.seqState[track_] == 2) return seqHeld_ ? "PATTERN REC: LET GO TO LOOP" : "PATTERN REC";
    if (st_.freeArmed) return loopSrc_ ? "PLAY A KEY TO START" : "WAITING FOR SOUND";
    if (st_.freeRecording) return fmt("RECORDING %.1fS", st_.freeSeconds);
    if (st_.track_clip_rec[track_]) return "OVERDUB";
    if (!loopAny_) return "LOOP EMPTY";
    return fmt("LOOP %.0fBPM ", st_.bpm) + std::to_string(st_.loopBars) + (st_.loopBars == 1 ? " BAR" : " BARS");
}
void SamplerApp::keyEvent(int raw, bool down) {   // a key of the FM-1 (chromatic note 53+key): octave shift, chords in CHORD mode
    if (raw < 0 || raw > 127) return;
    if (down) {
        if (keyCount_[raw]) keyEvent(raw, false);
        const bool kit = ts_[track_].algo == 3;   // KIT: every key is a pad (note 60 + key), whatever the tonality or octave
        const int base = kit ? 60 + (raw - 53) : raw + 12 * scale_.octave;
        int cnt = 0; keyNotes_[raw][cnt++] = kit || scale_.mode != 2 ? base : snapToScale(base, scale_.scale, scale_.root);
        if (!kit && hasScale(scale_.scale) && scale_.mode == 1) {
            const int rel = base - scale_.root, o = rel >= 0 ? rel / 12 : -((-rel + 11) / 12), i = scaleIndex(rel - 12 * o, scale_.scale);
            if (i >= 0) { const int d = o * kScaleN[scale_.scale] + i; keyNotes_[raw][cnt++] = scale_.root + degreeNote(d + 2, scale_.scale, 0); keyNotes_[raw][cnt++] = scale_.root + degreeNote(d + 4, scale_.scale, 0); }
        }
        keyCount_[raw] = cnt;
        for (int j = 0; j < cnt; j++) playNote(keyNotes_[raw][j], true);
    } else {
        for (int j = 0; j < keyCount_[raw]; j++) playNote(keyNotes_[raw][j], false);
        keyCount_[raw] = 0;
    }
}
void SamplerApp::playNote(int note, bool down) {
    if (note < 0 || note > 127) return;
    if (down) { heldNote_[note] = track_; s_.noteOn(track_, note, 100); }
    else if (heldNote_[note] >= 0) { s_.noteOff(heldNote_[note], note); heldNote_[note] = -1; }
}
std::vector<float> SamplerApp::peaks(int t, int bins) {
    const std::string lab = s_.trackLabel(t);
    if (peaksLabel_[t] != lab || nowMs_ - peaksAt_[t] > 700 || (int)peaks_[t].size() != bins) {
        peaksLabel_[t] = lab; peaksAt_[t] = nowMs_; peaks_[t] = s_.samplePeaksOf(t, bins);
    }
    return peaks_[t];
}

// ---- input ----
void SamplerApp::onMidi(const uint8_t* m, size_t n) {
    if (n < 3) return;
    linkSeen_ = true;
    const int type = m[0] & 0xf0, ch = m[0] & 15, a = m[1], b = m[2];
    if (ch == 0 && (type == 0x90 || type == 0x80)) keyEvent(a, type == 0x90 && b > 0);
    else if (ch == 1 && (type == 0x90 || type == 0x80)) button(a, type == 0x90 && b > 0);
    else if (ch == 0 && type == 0xb0) {
        if (a >= 16 && a < 23) encoder(a - 16, b - 64);
        else if (a == 7) knobMsb_ = b;
        else if (a == 39) s_.setMasterVolume(((knobMsb_ << 3) | (b >> 4)) / 1023.f * 1.2f);
    }
}
void SamplerApp::onSysex(const uint8_t* m, size_t n) {
    if (n < 9 || m[1] != 0x7d || m[2] != 'D' || m[3] != 'A' || m[4] != 'W') return;
    linkSeen_ = true;
    if (m[6] == 0x41 && n >= 12) {   // the firmware's own SEL screen / OCT buttons: its scale index is the tonality unless it is the OFF we keep it on
        if ((m[7] & 7) != 7) { scale_.scale = m[7] & 7; fwDirty_ = true; }
        scale_.root = m[8] % 12; scale_.octave = (int)m[9] - 3; if ((m[10] & 1) != (scale_.mode == 1)) scale_.mode = m[10] & 1;   // (SNAP is app only: the firmware sees it as KEY)
    }
    else if (m[6] == 0x40 && n >= 30) {   // PONG: frames the FM-1 had to drop
        uint32_t d = 0; for (int i = 0; i < 5; i++) d = d << 7 | (m[24 + i] & 127);
        if (haveDropBase_ && d != dropped_) { gap_ = std::min<uint32_t>(250, gap_ * 16 / 10 + 1); lastLoss_ = nowMs_; needResend_ = true; pending_.clear(); pendingBytes_ = 0; }
        if (!haveDropBase_) { needClear_ = true; repaintAt_ = nowMs_ + 1500; }   // first contact: whatever the firmware drew (its splash) goes
        if (!haveDropBase_) fwDirty_ = true;
        dropped_ = d; haveDropBase_ = true;
    }
    else if (m[6] == 0x42) needClear_ = true;     // the firmware had its own screen up: repaint everything
}
void SamplerApp::button(int b, bool down) {
    if (down) held_ |= 1u << b; else held_ &= ~(1u << b);
    if (b == SAVE) {   // tap = save the loop, hold = clear it
        if (down) { saveDownAt_ = nowMs_ ? nowMs_ : 1; saveHeld_ = true; saveHoldDone_ = false; gotoPage(P_SAVE); }
        else { saveHeld_ = false; if (!saveHoldDone_) loopSave(); }
        return;
    }
    if (b == SEQ) {   // tap = the SEQ mode (again: its next page); hold and play keys = record the pattern, let go and it loops
        if (down) { seqDownAt_ = nowMs_ ? nowMs_ : 1; seqHeld_ = true; seqRecStarted_ = false; }
        else {
            seqHeld_ = false;
            if (seqRecStarted_) { ts_[track_].seqState = 1; applyPattern(track_); say("PATTERN PLAYS"); }
            else modeButton(2);
            seqRecStarted_ = false;
        }
        return;
    }
    if (!down) return;
    switch (b) {
    case HOME: modeButton(0); break;
    case FX: modeButton(1); break;
    case EDIT: modeButton(3); break;
    case ENV: gotoPage(P_ENV); break;      // shortcuts to a page of a mode
    case LFO: gotoPage(P_LFO); break;
    case ARP: gotoPage(P_ARP); break;
    case GLO: gotoPage(P_KEY); break;
    case SEL: gotoPage(P_LEN); break;
    case PLAY: transportPlay(); break;
    case REC: loopRecord(); break;
    default: break;
    }
}
void SamplerApp::encoder(int i, int delta) {
    if (!delta) return;
    if (i == 0) { selectTrack(track_ + (delta > 0 ? 1 : -1)); return; }
    if (i == 6) { browse(delta); return; }
    if (i == 1) { setAlgo(ts_[track_].algo + (delta > 0 ? 1 : -1)); return; }
    if (i == 2 && mic_ == 1 && page_ != P_SAVE) { setMicThreshold01(micThr_ + (delta > 0 ? 0.03f : -0.03f)); touchedKnob_ = 0; touchedAt_ = nowMs_; return; }   // K1 = trigger level while the microphone listens
    Knobs k = knobs();
    const int idx = i - 2;
    if (idx < 4 && k[(size_t)idx].valid && k[(size_t)idx].step) { k[(size_t)idx].step(delta); touchedKnob_ = idx; touchedAt_ = nowMs_; }
}

// ---- tick ----
void SamplerApp::tick(uint32_t now) {
    nowMs_ = now;
    if (fwDirty_) { fwDirty_ = false; sysex(6, {7, (uint8_t)scale_.root, (uint8_t)(scale_.octave + 3), (uint8_t)(scale_.mode == 1)}, true); }
    if (repaintAt_ && now >= repaintAt_) { repaintAt_ = repaintAt_ + 2500 < now + 2500 && repaintCount_++ < 1 ? now + 2500 : 0; needClear_ = true; }
    st_ = s_.status();
    loopAny_ = false;
    for (int t = 0; t < kTracks; t++) loopAny_ |= st_.track_has_clip[t];
    for (int t = 0; t < kTracks; t++) if (st_.seqState[t] != seqSeen_[t]) { seqSeen_[t] = st_.seqState[t]; ts_[t].seqState = st_.seqState[t]; }   // the engine changed it (a bounce mutes the pattern)
    if (st_.bounceDone != lastBounceDone_) { lastBounceDone_ = st_.bounceDone; say("PATTERN IS ON THE TAPE (MUTED)"); }
    if (st_.loopSamples != lastLoopSamples_) { lastLoopSamples_ = st_.loopSamples; tempo_ = st_.bpm; }   // a take or a clear changed the tempo
    if (now - lastLoss_ > 4000 && now - lastRaise_ > 2000 && gap_ > 16) { gap_ = std::max<uint32_t>(16, gap_ * 9 / 10); lastRaise_ = now; }
    if (now - lastPing_ >= 500) { lastPing_ = now; pingId_ = (pingId_ + 1) & 0x3fff; sysex(5, n14(pingId_)); }
    if (saveHeld_ && !saveHoldDone_ && now - saveDownAt_ > 700) { saveHoldDone_ = true; loopClear(); }
    if (seqHeld_ && !seqRecStarted_ && now - seqDownAt_ > 300) {   // SEQ held: the pattern records what is played
        seqRecStarted_ = true; gotoPage(P_SQ); ts_[track_].seqState = 2; applyPattern(track_); say("RECORDING: PLAY KEYS");
    }
    // audition: the cursor rested, load the entry on the selected track and play it
    if (auditionAt_ && now >= auditionAt_) loadEntry(libCursor_);
    if (st_.sampleVersion != lastSampleVersion_) {   // a sample landed on a track: give it this track's settings
        lastSampleVersion_ = st_.sampleVersion;
        applyAll(st_.sampleTrack);
        if (previewAfterLoad_ && st_.sampleTrack == track_) { previewAfterLoad_ = false; previewNote_ = 60; previewTrack_ = track_; s_.noteOn(track_, 60, 100); previewOffAt_ = now + 900; }
    }
    if (st_.sampleFails != lastSampleFails_) { lastSampleFails_ = st_.sampleFails; say("COULD NOT LOAD THE FILE"); }
    if (previewNote_ >= 0 && now >= previewOffAt_) { s_.noteOff(previewTrack_, previewNote_); previewNote_ = -1; }
    if (commitAt_ && now >= commitAt_) {
        commitAt_ = 0;
        if (commitTrack_ == -2) refreshLibrary();
        else if (commitTrack_ >= 0) applyHome(commitTrack_, true);   // the knob rested: build the stretched / pitched copy
    }
    // microphone sample: wait for sound -> recording -> saved -> load into the track it was started on
    if (mic_ == 1 && st_.sampleState == 1) mic_ = 2;
    if (mic_ == 3 && st_.samplePending) {
        const std::string path = s_.lastSamplePath();
        s_.recorderDismiss();
        refreshLibrary();
        for (size_t i = 0; i < lib_.size(); i++) if (lib_[i].path == path) { const int keep = track_; track_ = micTrack_; loadEntry((int)i); track_ = keep; break; }
        mic_ = 0; say("SAMPLE LOADED");
    }
    if (mic_ == 3 && st_.sampleState == 0 && !st_.samplePending && now - lastDraw_ > 3000) mic_ = 0;   // too short: nothing was kept
    if (now - lastDraw_ >= 33) {
        lastDraw_ = now;
        if (now - healAt_ >= 150 && pending_.size() < 4) { healAt_ = now; if (!needClear_) heal(); }
        draw(now);
        leds();
    }
    flush(now);
}

// ---- FM-1 link ----
void SamplerApp::sysex(int cmd, const std::vector<uint8_t>& p, bool urgent, int key) {
    std::vector<uint8_t> m = {0xf0, 0x7d, 'D', 'A', 'W', 1, (uint8_t)cmd};
    cat(m, p); m.push_back(0xf7);
    if (cmd == 2) { pending_.clear(); pendingBytes_ = 0; }
    if (key >= 0) for (auto& f : pending_) if (f.key == key) { pendingBytes_ += m.size() - f.bytes.size(); f.bytes = std::move(m); return; }
    pendingBytes_ += m.size();
    if (urgent) pending_.push_front({key, std::move(m)}); else pending_.push_back({key, std::move(m)});
}
void SamplerApp::clear(uint16_t c) { sysex(2, col(c)); }
bool SamplerApp::rect(int id, int x, int y, int w, int h, uint16_t c) {
    uint64_t sig = (uint64_t)c | (uint64_t)(w & 0xfff) << 16 | (uint64_t)(h & 0xfff) << 28 | (uint64_t)(x & 0xfff) << 40 | (uint64_t)(y & 0xfff) << 52;
    if (!needClear_ && rects_[id] == sig) return false;
    rects_[id] = sig;
    std::vector<uint8_t> p; cat(p, n14(x)); cat(p, n14(y)); cat(p, n14(std::max(1, w))); cat(p, n14(std::max(1, h))); cat(p, col(c));
    sysex(3, p, false, id);
    return true;
}
void SamplerApp::drawText(int id, int x, int y, int scale, uint16_t fg, uint16_t bg, const std::string& text, int width) {
    std::string t = pad(text, (size_t)width);
    SurfaceCell& c = cells_[id];
    if (!needClear_ && c.known && c.text == t && c.fg == fg && c.bg == bg) return;
    c.text = t; c.fg = fg; c.bg = bg; c.known = true;
    std::vector<uint8_t> p; cat(p, n14(x)); cat(p, n14(y)); p.push_back((uint8_t)scale); cat(p, col(fg)); cat(p, col(bg));
    for (char ch : t) p.push_back((uint8_t)(ch & 127));
    sysex(4, p, false, 1000 + id);
}
void SamplerApp::flush(uint32_t now) {
    // ONE frame per call, at least gap_ ms apart: the FM-1 has a 4-frame queue. Small commands are packed into one batch frame.
    if (pending_.empty() || now - lastSend_ < gap_) return;
    lastSend_ = now;
    std::vector<uint8_t> batch = {0xf0, 0x7d, 'D', 'A', 'W', 1, 10};
    int n = 0;
    while (!pending_.empty()) {
        const auto& f = pending_.front().bytes;
        const size_t sub = f.size() - 7;
        if (n > 0 && (batch.size() + 2 + sub + 1 > 290 || sub > 127)) break;
        if (n == 0 && sub > 127) break;
        batch.push_back((uint8_t)(sub >> 7 & 127)); batch.push_back((uint8_t)(sub & 127));
        batch.insert(batch.end(), f.begin() + 6, f.end() - 1);
        pendingBytes_ -= f.size(); pending_.pop_front(); n++;
    }
    if (n == 0) { auto& f = pending_.front().bytes; pendingBytes_ -= f.size(); send_(f.data(), f.size()); pending_.pop_front(); return; }
    batch.push_back(0xf7);
    send_(batch.data(), batch.size());
}
void SamplerApp::heal() {   // a lost frame leaves a hole nothing would redraw: one element is re-sent in the background on every pass
    healCursor_ = (healCursor_ + 1) % (int)cells_.size();
    cells_[(size_t)healCursor_].known = false; rects_[(size_t)healCursor_] = ~0ull;
    const int c2 = 100 + healCursor_ % 160; rects_[(size_t)c2] = ~0ull;
}

void SamplerApp::leds() {
    uint64_t m = 0;
    const bool blinkFast = (nowMs_ / 250) & 1, blinkSlow = (nowMs_ / 500) & 1;
    static const int modeBtn[] = {HOME, FX, SEQ, EDIT};
    m |= 1ull << modeBtn[modeOf(page_)];
    switch (page_) { case P_ENV: m |= 1ull << ENV; break; case P_LFO: m |= 1ull << LFO; break; case P_ARP: m |= 1ull << ARP; break; case P_KEY: m |= 1ull << GLO; break; case P_SAVE: m |= 1ull << SAVE; break; case P_LEN: m |= 1ull << SEL; break; default: break; }
    if (st_.playing) m |= 1ull << PLAY;
    if (st_.freeRecording || st_.track_clip_rec[track_] || mic_ == 2 || st_.seqState[track_] == 2 || st_.bounce == 2) { if (blinkFast) m |= 1ull << REC; }
    else if (st_.freeArmed || mic_ == 1) { if (blinkSlow) m |= 1ull << REC; }
    else if (loopAny_) m |= 1ull << REC;
    // keys follow the tonality: the keyboard stays chromatic, only the keys that belong to the scale light, the root of each octave blinks slowly;
    // with the scale OFF only the root keys light. A sounding key goes dark.
    for (int k = 0; k < 27; k++) {
        const int raw = 53 + k, note = raw + 12 * scale_.octave, pc = ((note - scale_.root) % 12 + 12) % 12;
        bool on = ts_[track_].algo == 3 ? true : hasScale(scale_.scale) ? scaleIndex(pc, scale_.scale) >= 0 : pc == 0;   // KIT: every key is a pad
        if (on && pc == 0 && hasScale(scale_.scale) && ts_[track_].algo != 3) on = blinkSlow;
        if (keyCount_[raw]) on = false;
        if (note < 0 || note > 127) on = false;
        if (on) m |= 1ull << (14 + k);
    }
    if (m == lastLed_) return;
    lastLed_ = m;
    std::vector<uint8_t> p(6);
    for (int i = 0; i < 6; i++) p[i] = (uint8_t)(m >> (7 * i) & 127);
    sysex(1, p);
}

// ---- screen ----
// Layout (240x240): track tags, page and state, sample name, a 60 px picture that depends on the page, the loop strip, four knobs, a hint.
void SamplerApp::draw(uint32_t now) {
    static int lastHero = -1;
    const int heroKind = mic_ == 1 || mic_ == 2 ? 6 : page_ == P_ENV ? 1 : page_ == P_LFO ? 2 : page_ == P_MIX ? 3 : page_ == P_KEY ? 4 : page_ == P_LEN ? 5 : page_ == P_SQ ? 7 : page_ == P_TEMPO ? 8 : 0;
    if (heroKind != lastHero) { lastHero = heroKind; needClear_ = true; }
    if (needClear_) { fwDirty_ = true; clear(BLACK); std::fill(cells_.begin(), cells_.end(), SurfaceCell()); std::fill(rects_.begin(), rects_.end(), ~0ull); needResend_ = false; }
    else if (needResend_) { needResend_ = false; std::fill(cells_.begin(), cells_.end(), SurfaceCell()); std::fill(rects_.begin(), rects_.end(), ~0ull); }
    char b[96];
    const Knobs k = knobs();
    // track tags: the selected one is amber, a track with a sample is white, an empty one grey; the dot under it is the loop
    for (int i = 0; i < kTracks; i++) {
        const bool sel = i == track_, used = hasSample(i);
        snprintf(b, sizeof b, "%d", i + 1);
        drawText(i, i * 30 + 9, 2, 2, sel ? BLACK : used ? PAPER : DIM, sel ? ACCENT : BLACK, b, 1);
        rect(8 + i, i * 30 + 9, 20, 12, 2, st_.track_clip_rec[i] ? ACCENT : st_.track_has_clip[i] ? PAPER : BLACK);
    }
    drawText(20, 6, 26, 2, ACCENT, BLACK, kPage[page_], 9);
    for (int i = 0; i < 7; i++) rect(270 + i, 122 + i * 7, 30, 5, 5, i >= modePageCount(modeOf(page_)) ? BLACK : i == pageSlot(page_) ? ACCENT : BAR);   // where you are inside the mode (press its button again for the next page)
    const char* state = st_.freeRecording || st_.track_clip_rec[track_] ? "REC" : st_.playing ? "PLAY" : "STOP";
    drawText(21, 176, 26, 2, st_.freeRecording || st_.track_clip_rec[track_] ? ACCENT : PAPER, BLACK, state, 5);
    // sample line: name and algorithm (or the toast / microphone state)
    std::string line;
    if (mic_ == 1) line = "MIC: WAITING FOR SOUND";
    else if (mic_ == 2) line = fmt("MIC: RECORDING %.1fS", st_.sampleSeconds);
    else if (!toast().empty()) line = toast();
    else {
        const std::string lab = s_.trackLabel(track_);
        line = (lab.empty() ? "EMPTY" : lab) + "  " + kAlgo[ts_[track_].algo];
        if (auditionAt_) { const Entry& e = lib_[(size_t)libCursor_]; line = ">" + e.label + "  " + e.group; }
    }
    drawText(22, 6, 46, 1, !toast().empty() || mic_ ? ACCENT : auditionAt_ ? ACCENT : DIM, BLACK, line, 38);
    hero(now, k);
    // loop strip
    const int px = (int)(st_.track_has_clip[track_] ? st_.clipPos[track_] * 224 : (loopAny_ && st_.loopSamples > 0 ? st_.pos / st_.loopSamples * 224 : 0));
    static int lastPx = -1; if (px != lastPx) { lastPx = px; rects_[24] = ~0ull; }   // the bar is redrawn under the moving playhead
    if (rect(24, 6, 122, 228, 6, BAR)) rects_[25] = ~0ull;                             // ...and the head on top of it again
    rect(25, 6 + px, 122, 4, 6, st_.freeRecording || st_.track_clip_rec[track_] ? ACCENT : st_.playing ? PAPER : DIM);
    drawText(23, 6, 132, 1, st_.freeRecording || st_.track_clip_rec[track_] || st_.freeArmed ? ACCENT : DIM, BLACK, loopText(), 38);
    // knobs: 2 x 2
    for (int i = 0; i < 4; i++) {
        const int cx = 6 + (i % 2) * 118, cy = 146 + (i / 2) * 42;
        const Knob& p = k[(size_t)i];
        if (!p.valid) { drawText(40 + i * 2, cx, cy, 1, DIM, BLACK, "", 18); drawText(41 + i * 2, cx, cy + 9, 2, PAPER, BLACK, "", 9); if (rect(60 + i, cx, cy + 29, 110, 4, BLACK)) rects_[70 + i] = ~0ull; rect(70 + i, cx, cy + 29, 1, 4, BLACK); continue; }
        snprintf(b, sizeof b, "K%d %s", i + 1, p.name.c_str());
        drawText(40 + i * 2, cx, cy, 1, i == touchedKnob_ && now - touchedAt_ < 1000 ? ACCENT : DIM, BLACK, b, 18);
        drawText(41 + i * 2, cx, cy + 9, 2, p.isChoice ? ACCENT : PAPER, BLACK, p.value, 9);
        const int bw = std::max(1, (int)(cl(p.norm) * 110));
        static int lastBw[4] = {-1, -1, -1, -1};
        if (bw < lastBw[i] || needClear_) rects_[60 + i] = ~0ull;   // the bar shrank: paint the track again under it
        lastBw[i] = bw;
        if (rect(60 + i, cx, cy + 29, 110, 4, BAR)) rects_[70 + i] = ~0ull;   // the fill goes on top of every repaint of the track
        rect(70 + i, cx, cy + 29, bw, 4, p.isChoice ? ACCENT : PAPER);
    }
    const char* hint = mic_ == 1 ? (page_ == P_SAVE ? "K4 = TRIGGER LEVEL  LINE = THRESHOLD" : "K1 = TRIGGER LEVEL  LINE = THRESHOLD") : mic_ == 2 ? "RECORDING  K1 ON LOOP PAGE STOPS"  : page_ == P_LEN ? "TURN K1: X2 OR /2  THE LOOP REPEATS" : page_ == P_SAVE ? "TAP SAVE = SAVE LOOP  HOLD = CLEAR ALL" : page_ == P_SQ ? "HOLD SEQ AND PLAY KEYS TO RECORD" : page_ == P_KEY ? "KEYS FOLLOW THE KEY  CHANGE ANY TIME"
                     : "SEL TRACK  PRE BROWSE  ALG ALGORITHM";
    drawText(90, 6, 230, 1, DIM, BLACK, hint, 38);
    needClear_ = false;
}

void SamplerApp::hero(uint32_t now, const Knobs& k) {
    const int x0 = 6, y0 = 58, W = 228, H = 60;
    // one column: background (cleared only when the height changed) + bar
    static uint32_t sig[100]; if (needClear_ || needResend_) for (auto& s : sig) s = ~0u;
    auto column = [&](int idx, int x, int w, int h, uint16_t fg, bool fromBottom) {
        h = std::min(H, std::max(1, h));
        const uint32_t s = (uint32_t)h | (uint32_t)fg << 8 | (uint32_t)w << 24;
        if (sig[idx] == s && !needClear_) return;
        sig[idx] = s;
        rects_[100 + idx] = ~0ull; rects_[200 + idx] = ~0ull;
        rect(100 + idx, x, y0, w, H, BLACK);
        rect(200 + idx, x, fromBottom ? y0 + H - h : y0 + (H - h) / 2, w, h, fg);
    };
    const TrackState& x = ts_[track_];
    if (mic_ == 1 || mic_ == 2) {   // microphone: input level against the trigger line (K1 sets it), then the growing take
        const int n = 57; const float thr = std::min(1.f, micThreshold() * 2.f), lv = std::min(1.f, st_.inputPeak * 2.f);
        if (mic_ == 1) {
            for (int i = 0; i < n; i++) column(i, x0 + i * 4, 3, (int)(lv * H * (i < (int)(lv * n) ? 1.f : 0.f)) + 1, lv > thr ? ACCENT : PAPER, false);
            column(57, x0 + (int)(thr * (n - 1)) * 4, 3, H, ACCENT, true);
        } else {
            float live[2048]; const int ln = s_.livePeaks(live, 2048), per = std::max(1, (ln + n - 1) / n);
            for (int i = 0; i < n; i++) { float v = 0; for (int j = i * per; j < std::min(ln, (i + 1) * per); j++) v = std::max(v, live[j]); column(i, x0 + i * 4, 3, (int)(std::min(1.f, v * 1.1f) * H) + 1, i * per < ln ? PAPER : BAR, false); }
        }
    } else if (page_ == P_LEN) {   // the loop length in big letters
        char b[32];
        if (st_.loopSamples > 0) snprintf(b, sizeof b, "%.0f BARS", (double)st_.loopBars); else snprintf(b, sizeof b, "NO LOOP");
        drawText(100, x0, y0 + 4, 4, ACCENT, BLACK, b, 9);
        snprintf(b, sizeof b, "%.1fS  %.0f BPM", st_.loopSamples / (double)SAMPLERATE, st_.bpm);
        drawText(101, x0, y0 + 42, 2, PAPER, BLACK, st_.loopSamples > 0 ? b : "REC TO MAKE ONE", 18);
    } else if (page_ == P_ENV) {   // the ADSR curve
        const float sa = 0.08f + 0.5f * x.a, sd = 0.08f + 0.5f * x.d, ss = 0.3f, sr = 0.08f + 0.5f * x.r, tot = sa + sd + ss + sr;
        for (int i = 0; i < 57; i++) {
            const float u = (i + 0.5f) / 57.f * tot; float v;
            if (u < sa) v = u / sa; else if (u < sa + sd) v = 1.f - (1.f - x.s) * (u - sa) / sd; else if (u < sa + sd + ss) v = x.s; else v = x.s * (1.f - (u - sa - sd - ss) / sr);
            column(i, x0 + i * 4, 3, (int)(v * H), PAPER, true);
        }
    } else if (page_ == P_LFO) {   // one and a half cycles of the shape, amplitude = depth; the bar on the right is the live value
        for (int i = 0; i < 56; i++) {
            const float ph = std::fmod(i / 56.f * 1.5f * 2.f, 1.f); float v;
            v = x.lfoShape == 0 ? std::sin(6.2831853f * ph) : x.lfoShape == 1 ? (ph < 0.5f ? 4 * ph - 1 : 3 - 4 * ph) : x.lfoShape == 2 ? (ph < 0.5f ? 1.f : -1.f) : std::sin(ph * 71.f + (int)(i / 9) * 5.1f);
            column(i, x0 + i * 4, 3, (int)((0.08f + 0.92f * x.lfoDepth * std::fabs(v)) * H), PAPER, false);
        }
        column(56, x0 + 224, 4, (int)((0.5f + 0.5f * st_.lfoValue[track_]) * H), ACCENT, true);
    } else if (page_ == P_MIX) {   // eight strips: level (bar) and the track
        for (int i = 0; i < kTracks; i++) column(i, x0 + i * 29, 24, (int)(std::min(1.f, st_.track_peak[i] * 1.5f) * H), i == track_ ? ACCENT : PAPER, true);
    } else if (page_ == P_KEY) {   // the tonality in big letters
        char b[32]; snprintf(b, sizeof b, "%s %s", kNote[scale_.root], kScaleName[scale_.scale]);
        drawText(100, x0, y0 + 6, 4, ACCENT, BLACK, b, 9);
        snprintf(b, sizeof b, "OCT %+d  %s", scale_.octave, kModeName[scale_.mode]);
        drawText(101, x0, y0 + 42, 2, PAPER, BLACK, b, 18);
    } else if (page_ == P_SQ) {   // the pattern: one column per slice, height = pitch, the playhead in amber
        Session::SeqView v[kSeqMax]; const int cnt = s_.seqNotes(track_, v, kSeqMax);
        const int pt = patternTicks(1 << x.seqBars), cols = 57;
        const double ph = std::fmod(st_.beatPos * kPpq, (double)pt);
        const int pc = st_.playing || x.seqState ? (int)(ph / pt * cols) : -1;
        for (int i = 0; i < cols; i++) {
            int top = 0;
            for (int j = 0; j < cnt; j++) { const int c = v[j].tick * cols / pt; if (c == i) top = std::max(top, 12 + (v[j].note % 36) * (H - 12) / 36); }
            const int cc = (int)((double)st_.seqCursor[track_] / pt * cols), cursor = x.seqState == 3 && i == cc;
            column(i, x0 + i * 4, 3, cursor ? std::max(8, top) : std::max(2, top), cursor || i == pc ? ACCENT : top ? PAPER : BAR, true);
        }
    } else if (page_ == P_TEMPO) {   // the tempo in big letters and four beat lamps
        char b[32]; snprintf(b, sizeof b, st_.loopSamples > 0 ? "%.0f LOCK" : "%.0f BPM", st_.loopSamples > 0 ? st_.bpm : tempo_);
        drawText(100, x0, y0 + 2, 4, ACCENT, BLACK, b, 9);
        const int beat = ((int)st_.beatPos % 4 + 4) % 4;
        for (int i = 0; i < 4; i++) rect(100 + i, x0 + i * 30, y0 + 46, 24, 12, st_.metro && i == beat ? ACCENT : BAR);
        drawText(101, x0 + 130, y0 + 42, 2, PAPER, BLACK, st_.metro ? "CLICK ON" : "CLICK OFF", 9);
    } else {   // the waveform with the playback window
        auto pk = peaks(track_, 57);
        for (int i = 0; i < 57; i++) {
            const float pos = (i + 0.5f) / 57.f; const bool in = pos >= x.start && pos <= x.end;
            const float v = i < (int)pk.size() ? pk[(size_t)i] : 0.f;
            column(i, x0 + i * 4, 3, (int)(std::min(1.f, v * 1.1f) * H) + 1, in ? PAPER : BAR, false);
        }
    }
    (void)k; (void)now;
}
