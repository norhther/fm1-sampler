#include "PatternGen.h"
#include "SynthCategory.h"
#include "Harmony.h"
#include "SamplerApp.h"
#include "../ui/Theme.h"
#include <Common.h>
#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>
#include <filesystem>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
constexpr uint16_t rgb(int r, int g, int b) { return (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3)); }
// The palette of the FM-1 screen: the colours of the phone's theme (SamplerApp::applyFmPalette sets them whenever the theme changes). The names are the dark theme's:
// BLACK is the background of whatever theme it is, PAPER the text, ONACC the text on an ACCENT fill, BAR the empty track of a bar.
uint16_t BLACK = 0, PAPER = rgb(237, 235, 228), DIM = rgb(120, 126, 140), ACCENT = rgb(232, 163, 61), BAR = rgb(46, 54, 72), PANEL = rgb(22, 27, 38), ONACC = 0;
const char* kPage[] = {"SAMPLE", "FX", "ENVELOPE", "LFO", "ARP", "TAPE FX", "MIXER", "KEY", "LOOP", "LENGTH", "FX 2", "LIVE FX", "SEQUENCER", "TEMPO", "TRACK FX", "TIMING", "TONE", "MODULATE", "LIVE FX 2", "TOMBOLA", "OP-1 FX", "SOUND SHAPE", "PADS", "SAMPLE EDIT", "TRACK EQ", "DUCK AND LIMIT", "SYNTH OSC", "SYNTH FILTER", "TAPE VIEW", "TRACK FX 2", "TRACK FX 3", "TRACK FX 4", "TAPE STOP", "TRIGGER", "OP-1 FX 2", "TRACK FX 5", "CHOP", "PRINT FX", "FX 5", "TRACK FX 6", "FX 6", "TRACK FX 7"};
const char* kAlgo[] = {"MELODIC", "ONE-SHOT", "LOOPED", "KIT/SLICE", "GRANULAR", "PAULSTRETCH"};
const char* kScaleName[] = {"CHROM", "MIX", "MAJ", "MIN", "HMIN", "PHRYG", "MIN+", "OFF", "DORIAN", "LYDIAN", "LOCR", "PENT+", "PENT-", "BLUES", "WHOLE", "DIM", "PHRDOM", "HMAJ", "MELMIN", "HUNG", "HIRA", "DHARM", "NEAPM", "NEAPJ", "PERSN", "HUNGJ", "LYDDM", "ALTRD", "LYDAG", "DOR#4", "SPAN8", "INSEN", "IWATO", "YO", "EGYPT", "MAJBL", "PROMT", "ENIGM", "OCTWH", "LOCR2", "MAJ6P", "MIN6P"};
// 0 KEY, 1 CHORD (the triad of the scale on the key), 2 SNAP, 3..8 the keys snap to the chord of that progression playing now (QNT CHRD),
// 9 CHORD7, 10 OPEN (the triad spread 1-5-3), 11..17 a chord shape on the key whatever the scale, 18..23 a key plays the whole chord of that progression's bar
const char* kModeName[] = {"KEY", "CHORD", "SNAP", "POP", "50S", "2-5-1", "CIRCLE", "TWO", "WALK", "CHORD7", "OPEN", "MAJ", "MIN", "DOM7", "MAJ7", "MIN7", "SUS4", "POW",
                           "POP CH", "50S CH", "2-5-1 CH", "CIRC CH", "TWO CH", "WALK CH"};
constexpr int kModeCount = 24;
const char* kRepeat[] = {"OFF", "1/4", "1/8", "1/16", "1/32"};
const char* kOffOn[] = {"OFF", "ON"};
const char* kGate[] = {"OFF", "1/8", "1/16", "1/32"};
const char* kTakeQ[] = {"OFF", "BEAT", "BAR"};
const char* kCountIn[] = {"OFF", "1 BAR", "2 BARS"};
const char* kNote[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
const char* kArpRate[] = {"OFF", "1", "2", "2T", "4", "4T", "8", "8T", "16", "16T", "32", "32T", "64"};
const char* kArpPatt[] = {"UP", "DOWN", "UPDN", "CONV", "RAND", "PLAYED", "TOMBOLA"};
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
constexpr int kScaleCount = 42, kScaleOff = 7;   // ids 0..6 are the firmware's tables, 7 = OFF (the firmware's value), 8.. only exist in the app
const int kScaleN[kScaleCount] = {12, 7, 7, 7, 7, 7, 8, 0, 7, 7, 7, 5, 5, 6, 6, 8, 7, 7, 7, 7, 5, 7,
                                 7, 7, 7, 7, 7, 7, 7, 7, 8, 5, 5, 5, 5, 6, 6, 7, 8, 7, 5, 5};
const int kScaleStep[kScaleCount][12] = {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}, {0, 2, 4, 5, 7, 9, 10}, {0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 8, 10},
                               {0, 2, 3, 5, 7, 8, 11}, {0, 1, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 8, 10, 11}, {0},
                               {0, 2, 3, 5, 7, 9, 10}, {0, 2, 4, 6, 7, 9, 11}, {0, 1, 3, 5, 6, 8, 10}, {0, 2, 4, 7, 9}, {0, 3, 5, 7, 10}, {0, 3, 5, 6, 7, 10},
                               {0, 2, 4, 6, 8, 10}, {0, 1, 3, 4, 6, 7, 9, 10}, {0, 1, 4, 5, 7, 8, 10}, {0, 2, 4, 5, 7, 8, 11}, {0, 2, 3, 5, 7, 9, 11},
                               {0, 2, 3, 6, 7, 8, 11}, {0, 2, 3, 7, 8}, {0, 1, 4, 5, 7, 8, 11},
                               {0, 1, 3, 5, 7, 8, 11}, {0, 1, 3, 5, 7, 9, 11}, {0, 1, 4, 5, 6, 8, 11}, {0, 3, 4, 6, 7, 9, 10}, {0, 2, 4, 6, 7, 9, 10}, {0, 1, 3, 4, 6, 8, 10},
                               {0, 2, 4, 6, 8, 9, 11}, {0, 2, 3, 6, 7, 9, 10}, {0, 1, 3, 4, 5, 6, 8, 10}, {0, 1, 5, 7, 10}, {0, 1, 5, 6, 10}, {0, 2, 5, 7, 9}, {0, 2, 5, 7, 10},
                               {0, 2, 3, 4, 7, 9}, {0, 2, 4, 6, 9, 10}, {0, 1, 4, 6, 8, 10, 11}, {0, 2, 3, 5, 6, 8, 9, 11}, {0, 2, 3, 5, 6, 8, 10}, {0, 2, 4, 7, 9}, {0, 3, 5, 7, 9}};
// the order of the SCALE knob: OFF last
const int kScaleOrder[kScaleCount] = {0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 7};
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

const char* kHelp[] = {   // by Page: what the page is for, in plain words
    "Where the sound starts and ends, how fast and how high it plays.",                         // HOME
    "Reverb and delay: how much of each track goes into them.",                                  // FX
    "How the note rises, falls and fades: attack, decay, sustain, release.",                     // ENV
    "A slow wobble on pitch, filter, volume or pan.",                                            // LFO
    "Arpeggiator: holds a chord and plays its notes one after another.",                         // ARP
    "The tape's own sound: speed, shimmer, lo-fi crackle, wobble.",                              // SEQ (tape fx)
    "Volume, pan and sends of the selected track.",                                              // MIX
    "Scale and key: keeps the keys on notes that sound good together.",                          // KEY
    "Where the tape records from (mic or the sound) and how loud it must be to start.",          // SAVE
    "Make the loop longer, shorter or duplicate it.",                                            // LEN
    "Chorus, flanger, phaser and compressor on the whole mix.",                                  // FX2
    "Effects to play with live: filter sweep, beat repeat, gate, crush.",                        // LIVE
    "The pattern of the selected track: tap the grid to add notes.",                             // SQ
    "Speed of the music, metronome and how a take snaps to the beat.",                           // TEMPO
    "Effects for the selected track only.",                                                      // TFX
    "Quantize (snap to the beat), count-in and swing.",                                          // TIMING
    "Drive and three-band EQ on the whole mix.",                                                 // TONE
    "Tremolo, auto-wah, ring mod and stereo width.",                                             // MOD
    "More live effects: reverse, tape stop, freeze.",                                            // LIVE2
    "Balls bounce in a drum and make notes: a random pattern maker.",                            // TOMB
    "OP-1 style effects: spring reverb, phone filter, punch.",                                   // FX3
    "Shape a sound with four macros, set its root key and slice a loop.",                        // SHAPE
    "Pads to play with the fingers: the drums of a kit, or the notes of the scale (pad 1 is the root). Volume, pitch, pan and decay of each pad.",                             // PADS
    "Cut, reverse, normalize and fade the sample. Undo is at the top.",                          // SMPED
    "EQ and compressor of the selected track.",                                                  // TEQ
    "Sidechain: a track (like the kick) pushes another down. Master limiter.",                   // DUCK
    "Synth oscillators: waves, detune and unison.",                                              // SYN1
    "Synth filter and voices: cutoff, resonance, mono or poly.",                                 // SYN2
    "Everything recorded on the tape, one lane per track.",                                      // TAPEV
    "Track effects, part 2: drive, flanger, phaser, tremolo. Only this track.",                  // TFX2
    "Track effects, part 3: ring mod, auto-wah, stereo width, freeze. Only this track.",         // TFX3
    "Track effects, part 4: spring reverb, phone, punch, beat repeat. Only this track.",         // TFX4
    "TAPE STOP of the top bar: run-down time, spin-up time, depth, HOLD or LATCH.",   // TSTOP
    "PATTERN on/off. KEYS: a key starts it, other keys transpose. LAUNCH: beat or bar.",   // TRIG
    "Wave fold, auto pan, vinyl crackle and a vowel filter on the whole mix.",                 // FX4
    "Track effects, part 5: wave fold, auto pan, vinyl, vowel. Only this track.",              // TFX5
    "Edit the chops of a KIT sample: pick a chop, move its start and end. Set the algorithm to KIT/SLICE first.",   // CHOP
    "Put this track's effects on its tape. ON: what its effects do while you record or overdub is recorded, and the tape plays back past them.",   // PRINT
    "Comb filter (rings at a note), octaver and frequency shifter on the whole mix. The two on the right are off in the middle.",   // FX5
    "Track effects, part 6: comb filter, octaver, frequency shifter. Only this track.",   // TFX6
    "A turntable scratch on the whole mix: the sound is pushed and pulled like a record under a DJ's hand, in time with the beat.",   // FX6
    "A turntable scratch on this track only: pick a style and a speed, turn SCRATCH up.",   // TFX7
};
const char* SamplerApp::pageName(Page p) { return kPage[p]; }
const char* SamplerApp::pageHelp(Page p) { return kHelp[p]; }
const char* SamplerApp::algoName(int i) { return kAlgo[std::min(5, std::max(0, i))]; }
const char* SamplerApp::scaleName(int i) { return kScaleName[std::min(kScaleCount - 1, std::max(0, i))]; }
const char* SamplerApp::modeName(int m) { static const char* n[] = {"SOUND", "FX", "SEQ", "MIX", "TAPE"}; return n[m % kModes]; }
const SamplerApp::Page* SamplerApp::modePages(int m) {
    static const Page p[kModes][11] = {{P_HOME, P_ENV, P_LFO, P_ARP, P_SHAPE, P_SMPED, P_SYN1, P_SYN2, P_CHOP}, {P_FX, P_FX2, P_TONE, P_MOD, P_LIVE, P_LIVE2, P_FX3, P_FX4, P_FX5, P_FX6, P_TSTOP}, {P_SQ, P_TEMPO, P_TIMING, P_TOMB, P_PADS, P_TRIG}, {P_MIX, P_TEQ, P_TFX, P_TFX2, P_TFX3, P_TFX4, P_TFX5, P_TFX6, P_TFX7, P_DUCK, P_KEY}, {P_TAPEV, P_SEQ, P_SAVE, P_LEN, P_PRINT}};
    return p[m % kModes];
}
static_assert(sizeof(kPage) / sizeof(kPage[0]) == (size_t)SamplerApp::kPages && sizeof(kHelp) / sizeof(kHelp[0]) == (size_t)SamplerApp::kPages, "one name and one help line per page, in the order of the Page enum");
int SamplerApp::modePageCount(int m) { static const int n[kModes] = {9, 11, 6, 11, 5}; return n[m % kModes]; }
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
    loadSettings();
    cells_.resize(320); rects_.assign(320, ~0ull);
    applyFmPalette();
    for (int& h : heldNote_) h = -1;
    for (auto& r : heldCnt_) for (int& h : r) h = 0;
    for (int i = 0; i < kTracks; i++) { ts_[i].vol = 0.8f; }
    refreshLibrary();
    applyFx();
    s_.setRetLevel(1, 0.7f);
    s_.setLoopSource(loopSrc_);
    // a project starts empty: no sound on any track (pick one from the library, a synth, the mic or a file)
    for (int t = 0; t < kTracks; t++) { applyMix(t); applyPattern(t); }
    applyMasterFx(); applyTempo(); applyTomb(); applyMaster();
    defaultState_ = stateText();   // what NEW PROJECT goes back to
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
    for (int c = 0; c < kSynthCats; c++)   // the synths by kind: bass, leads, pads ... (a chip each), in the order of the list inside a kind
        for (int i = 0; i < kSynthCount; i++) if (synthCategory(kSynths[i]) == c) lib_.push_back({kSynths[i].name, synthCatName(c), "synth:" + std::to_string(i), kSynths[i].kit > 0});
    { std::error_code ec; std::vector<std::string> names; for (auto& e : std::filesystem::directory_iterator(s_.dataDir() + "/patches", ec)) if (e.path().extension() == ".txt") names.push_back(e.path().stem().string());
      std::sort(names.begin(), names.end());
      for (auto& nm : names) {
          std::ifstream f(s_.dataDir() + "/patches/" + nm + ".txt"); std::string line, syn; while (std::getline(f, line)) if (line.rfind("synth=", 0) == 0) syn = line.substr(6);
          bool kit = false; for (int i = 0; i < kSynthCount; i++) if (syn == kSynths[i].name) kit = kSynths[i].kit > 0;
          lib_.push_back({nm, "My Patches", "patch:" + nm, kit});
      } }
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
void SamplerApp::loadEntry(int i, bool preview) {
    chTrack_ = -1;
    if (i < 0 || i >= (int)lib_.size()) return;
    libCursor_ = i; auditionAt_ = 0;
    const Entry& e = lib_[(size_t)i];
    TrackState& t = ts_[track_];
    t.entry = i; t.start = 0.f; t.end = 1.f; t.rootN = 0.5f; t.sliceCount = 3; t.sliceSens = 0.5f; t.synTone = t.synBody = t.synMotion = t.synGrit = 0.5f; t.edW1 = t.edW2 = t.edType = t.edUni = t.edVoice = 0; t.edDet2 = t.edCut = 0.5f; t.edRes = 0.f;
    if (e.kit) t.algo = 3; else if (t.algo == 3) t.algo = 0;
    if (e.path.rfind("patch:", 0) == 0) {   // a saved patch: the synth, then every setting that was saved with it
        std::ifstream f(s_.dataDir() + "/patches/" + e.path.substr(6) + ".txt"); std::string txt((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        std::istringstream is(txt); std::string line; std::map<std::string, std::string> kv; while (std::getline(is, line)) { const size_t eq = line.find('='); if (eq != std::string::npos) kv[line.substr(0, eq)] = line.substr(eq + 1); }
        auto fv = [&](const char* k, float& v) { auto it = kv.find(k); if (it != kv.end()) v = std::min(1.f, std::max(0.f, (float)std::atof(it->second.c_str()))); };
        auto iv = [&](const char* k, int& v, int hi) { auto it = kv.find(k); if (it != kv.end()) v = std::min(hi, std::max(0, std::atoi(it->second.c_str()))); };
        fv("a", t.a); fv("d", t.d); fv("s", t.s); fv("r", t.r); fv("tone", t.synTone); fv("body", t.synBody); fv("motion", t.synMotion); fv("grit", t.synGrit);
        iv("w1", t.edW1, 4); iv("w2", t.edW2, 4); iv("type", t.edType, 2); iv("uni", t.edUni, 3); iv("voice", t.edVoice, 3); fv("det2", t.edDet2); fv("cut", t.edCut); fv("res", t.edRes);
        int base = -1; for (size_t j = 0; j < lib_.size(); j++) if (lib_[j].path.rfind("synth:", 0) == 0 && lib_[j].label == kv["synth"]) base = (int)j;
        previewAfterLoad_ = preview;
        if (base >= 0) { if (lib_[(size_t)base].kit) t.algo = 3; s_.loadSamplePath(lib_[(size_t)base].path, lib_[(size_t)base].kit, track_); }
        return;
    }
    if (e.path.rfind("synth:", 0) == 0) {   // a synth patch brings its own envelope
        const SynthPatch& p = kSynths[std::atoi(e.path.c_str() + 6) % kSynthCount];
        t.a = clampf(std::sqrt(p.a / 3.f), 0.f, 1.f); t.d = clampf(p.d, 0.f, 1.f); t.s = p.s; t.r = clampf(std::sqrt(p.r / 5.f), 0.f, 1.f);
    }
    previewAfterLoad_ = preview;
    s_.loadSamplePath(e.path, e.kit, track_);
}
std::string SamplerApp::resolvePath(int entry) const {
    if (entry < 0 || entry >= (int)lib_.size()) return "";
    const Entry& e = lib_[(size_t)entry];
    if (e.path.rfind("patch:", 0) != 0) return e.path;
    std::ifstream f(s_.dataDir() + "/patches/" + e.path.substr(6) + ".txt"); std::string line; while (std::getline(f, line)) if (line.rfind("synth=", 0) == 0) { for (auto& l : lib_) if (l.path.rfind("synth:", 0) == 0 && l.label == line.substr(6)) return l.path; }
    return "";
}
std::string SamplerApp::savePatch() {
    const int e = ts_[track_].entry; if (!isSynth(track_)) return "";
    std::string base = lib_[(size_t)e].label;
    if (lib_[(size_t)e].group == "My Patches") { std::ifstream f(s_.dataDir() + "/patches/" + base + ".txt"); std::string line; while (std::getline(f, line)) if (line.rfind("synth=", 0) == 0) base = line.substr(6); }
    std::error_code ec; std::filesystem::create_directories(s_.dataDir() + "/patches", ec);
    std::string name; for (int i = 2; i < 1000; i++) { name = base + " " + std::to_string(i); if (!std::filesystem::exists(s_.dataDir() + "/patches/" + name + ".txt")) break; }
    const TrackState& x = ts_[track_]; std::ofstream f(s_.dataDir() + "/patches/" + name + ".txt");
    f << "synth=" << base << "\na=" << x.a << "\nd=" << x.d << "\ns=" << x.s << "\nr=" << x.r << "\ntone=" << x.synTone << "\nbody=" << x.synBody << "\nmotion=" << x.synMotion << "\ngrit=" << x.synGrit
      << "\nw1=" << x.edW1 << "\nw2=" << x.edW2 << "\ntype=" << x.edType << "\nuni=" << x.edUni << "\nvoice=" << x.edVoice << "\ndet2=" << x.edDet2 << "\ncut=" << x.edCut << "\nres=" << x.edRes << "\n";
    refreshLibrary(); say("PATCH SAVED: " + name); return name;
}
void SamplerApp::importFile(const std::string& wav) {
    refreshLibrary();
    for (size_t i = 0; i < lib_.size(); i++) if (lib_[i].path == wav) { loadEntry((int)i); say("LOADED FROM DISK"); return; }
    say("FILE NOT READABLE");
}

// ---- parameters ----
void SamplerApp::applyAll(int t) {
    applyAlgo(t); applyEnv(t); applyLfo(t); applyHome(t); applyArp(t); applySeq(t); applyMix(t); applyShape(t); applyPads(t); applyEdit(t); applySynthEdit(t);
    s_.setPrintFx(t, ts_[t].printFx != 0, false);   // the switch only: a saved tape is already printed (or not)
}
void SamplerApp::applyShape(int t, bool reslice) {
    const TrackState& x = ts_[t];
    s_.setSynthMacros(t, (x.synTone - 0.5f) * 2.f, (x.synBody - 0.5f) * 2.f, (x.synMotion - 0.5f) * 2.f, (x.synGrit - 0.5f) * 2.f);
    s_.setRoot(t, 24 + (int)std::lround(x.rootN * 72.f));
    static const int counts[6] = {4, 8, 12, 16, 24, 32};
    if (reslice || x.sliceCount != 3 || std::fabs(x.sliceSens - 0.5f) > 0.01f) s_.setSlices(t, counts[std::min(5, std::max(0, x.sliceCount))], std::pow(2.f, (x.sliceSens - 0.5f) * 2.f));
}
// AUTO TUNE: the pitch of the sample becomes the root key, so every key sounds as the note it says
void SamplerApp::autoTune() {
    const float g = s_.rootGuessOf(track_);
    if (g < 24.f || g > 96.f) { say("NOT A PITCHED SOUND"); return; }
    const int root = (int)std::lround(g); ts_[track_].rootN = (root - 24) / 72.f; applyShape(track_);
    say(std::string("ROOT KEY ") + noteName(root) + std::to_string(root / 12 - 1));
}
void SamplerApp::applyPad(int t, int pad) {
    const TrackState& x = ts_[t];
    s_.setPad(t, pad, x.padVol[pad] * 1.5f, (x.padPitch[pad] - 0.5f) * 24.f, 0.f, std::pow(4.f, (x.padDecay[pad] - 0.5f) * 2.f), x.padGroup[pad]);
}
void SamplerApp::applyPads(int t) { for (int i = 0; i < 16; i++) applyPad(t, i); }
bool SamplerApp::padIsKit() const {
    return ts_[track_].algo == 3 || synthKit();
}
bool SamplerApp::synthKit() const {
    const int e = ts_[track_].entry;
    return e >= 0 && e < (int)lib_.size() && lib_[(size_t)e].path.rfind("synth:", 0) == 0 && lib_[(size_t)e].kit;
}
int SamplerApp::padNote(int pad) const {
    pad = std::min(15, std::max(0, pad));
    int sc[12]; const int n = padIsKit() ? 0 : scaleSteps(scale_.scale, sc);
    if (n == 0) return 60 + pad;
    return std::min(127, std::max(0, 60 + scale_.root + 12 * scale_.octave + 12 * (pad / n) + sc[pad % n]));
}
std::string SamplerApp::padName(int pad) const {
    static const char* n[16] = {"KICK", "KICK 2", "SNARE", "SNARE 2", "CLAP", "RIM", "HAT", "OPEN HAT", "TOM LO", "TOM MID", "TOM HI", "COWBELL", "CLAVE", "SHAKER", "CRASH", "ZAP"};
    if (synthKit()) return n[pad & 15];
    if (padIsKit()) return std::to_string(pad + 1);
    const int note = padNote(pad);
    return std::string(kNote[note % 12]) + std::to_string(note / 12 - 1);
}
void SamplerApp::applyMaster() {
    static const float ceil[7] = {0.f, -0.3f, -1.f, -2.f, -3.f, -4.5f, -6.f};
    s_.setLimiter(limiter_ > 0, ceil[std::min(6, std::max(0, limiter_))]);
    s_.setDuck(duckSrc_ - 1, 0.05f + duckRel_ * 0.95f);
    for (int t = 0; t < kTracks; t++) s_.setDuckAmount(t, ts_[t].duckAmt);
}
void SamplerApp::applySynthEdit(int t) {
    const TrackState& x = ts_[t]; static const int uni[4] = {-1, 1, 3, 5}; static const float gl[4] = {0.f, 0.f, 0.06f, 0.25f};
    s_.setSynthEdit(t, x.edW1 - 1, x.edW2 - 1, (x.edDet2 - 0.5f) * 100.f, (x.edCut - 0.5f) * 6.f, x.edRes * 6.f, x.edType, uni[std::min(3, std::max(0, x.edUni))], x.edVoice > 0, gl[std::min(3, std::max(0, x.edVoice))]);
}
void SamplerApp::applySong() { std::vector<uint8_t> c; for (int x : songChain_) c.push_back((uint8_t)x); s_.setSong(c.data(), (int)c.size(), songOn_); }
void SamplerApp::applyEdit(int t) { const TrackState& x = ts_[t]; s_.setSampleLevel(t, x.levelN * 3.f, (x.fineN - 0.5f) * 100.f); }
void SamplerApp::resampleNow() {
    if (mic_ != 0) return;
    const int bars = st_.loopSamples > 0 ? st_.loopBars : seqBarsOf(track_);
    mic_ = 3; micTrack_ = track_; s_.resample(bars); say("RESAMPLING " + std::to_string(bars) + (bars == 1 ? " BAR..." : " BARS..."));
}
void SamplerApp::applyAlgo(int t) { s_.setMode(t, ts_[t].algo); s_.setChopFull(t, ts_[t].chopFull != 0); }
int SamplerApp::chopCount(int idx) { static const int c[6] = {4, 8, 12, 16, 24, 32}; return c[std::min(5, std::max(0, idx))]; }
void SamplerApp::autoChop() { chTrack_ = -1; TrackState& x = ts_[track_]; if (x.algo != 3) { x.algo = 3; applyAlgo(track_); } applyShape(track_, true); say("AUTO CHOP: THE HITS OF THE SOUND, UP TO " + std::to_string(chopCount(x.sliceCount))); }
void SamplerApp::evenChop() { chTrack_ = -1; TrackState& x = ts_[track_]; if (x.algo != 3) { x.algo = 3; applyAlgo(track_); } const int n = chopCount(x.sliceCount); std::vector<float> p; for (int i = 0; i < n; i++) p.push_back(i / (float)n); s_.setSliceStarts(track_, p); say(std::to_string(n) + " EVEN CHOPS"); }
void SamplerApp::setChopFull(bool full) { ts_[track_].chopFull = full; applyAlgo(track_); say(full ? "A KEY PLAYS THE WHOLE CHOP" : "THE CHOP SOUNDS WHILE THE KEY IS HELD"); }
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
void SamplerApp::stopAll() {
    s_.silence();
    for (int& h : heldNote_) h = -1;
    for (auto& r : heldCnt_) for (int& h : r) h = 0;
    say("ALL SOUNDS STOPPED");
}
void SamplerApp::applyMix(int t) {
    const TrackState& x = ts_[t];
    const bool audible = !x.mute && (!anySolo() || x.solo);
    s_.setVolume(t, audible ? x.vol * 1.2f : 0.f); s_.setPan(t, x.pan * 2.f - 1.f); s_.setSend(t, 0, x.sendR); s_.setSend(t, 1, x.sendD);
}
void SamplerApp::applyFx() {
    s_.setReverb(reverbSize_);
    s_.setRetLevel(0, reverbDw_);
    s_.setDelay(0.05f + delayTime_ * 1.45f, delayFb_ * 0.95f);
}
FxParams SamplerApp::masterParams() const {
    FxParams p; p.chorus = chorus_; p.flanger = flanger_; p.phaser = phaser_; p.comp = comp_; p.width = width_; p.filter = djFilter_; p.crush = crush_; p.repeat = repeat_; p.gate = gate_;
    p.drive = drive_; p.eqLow = eqLow_; p.eqMid = eqMid_; p.eqHigh = eqHigh_; p.tremolo = tremolo_; p.wah = wah_; p.ringmod = ringmod_; p.tapeStop = tapeStopHeld_ ? tsDepth_ : tapeStop_; p.tsDown = 0.3f * std::pow(20.f, tsDown_); p.tsUp = tsUp_ < 0.02f ? 0.f : 0.1f * std::pow(40.f, tsUp_); p.spring = spring_; p.springDamp = springDamp_; p.phone = phone_; p.punch = punch_; p.fold = fold_; p.autopan = autopan_; p.vinyl = vinyl_; p.vowel = vowel_; p.comb = comb_; p.combTune = combTune_; p.octave = octave_; p.fshift = fshift_; p.scratch = scratch_; p.scratchNoise = scratchNoise_; p.scratchRate = scratchRate_; p.scratchStyle = scratchStyle_; p.reverse = reverse_; p.freeze = freeze_;
    return p;
}
void SamplerApp::applyMasterFx() { s_.setMasterFx(masterParams()); }
void SamplerApp::applyTempo() {
    s_.setMetronome(metro_ != 0, metroVol_);
    s_.setTakeQuant(takeQuant_);
    s_.setQuantize(seqGrid_);
    s_.setInputQuant(liveGrid_);
    s_.setCountIn(countIn_);
    s_.setTakeBars(takeBars());
    s_.setSwing(0.5f + 0.25f * swing_);
}
void SamplerApp::applyTomb() { s_.tombolaSet((tSpin_ - 0.5f) * 14.f, tGrav_, tMass_, tShape_); }
FxParams SamplerApp::trackParams(int t) const {
    const TrackState& x = ts_[t];
    FxParams p; p.filter = x.tfDj; p.chorus = x.tfChorus; p.crush = x.tfCrush; p.gate = x.tfGate; p.eqLow = x.tfEqLow; p.eqMid = x.tfEqMid; p.eqHigh = x.tfEqHigh; p.comp = x.tfComp;
    p.drive = x.tfDrive; p.flanger = x.tfFlanger; p.phaser = x.tfPhaser; p.tremolo = x.tfTremolo; p.wah = x.tfWah; p.ringmod = x.tfRing; p.width = x.tfWidth;
    p.spring = x.tfSpring; p.phone = x.tfPhone; p.punch = x.tfPunch; p.fold = x.tfFold; p.autopan = x.tfAutopan; p.vinyl = x.tfVinyl; p.vowel = x.tfVowel; p.comb = x.tfComb; p.combTune = x.tfCombTune; p.octave = x.tfOctave; p.fshift = x.tfFshift; p.scratch = x.tfScratch; p.scratchNoise = x.tfScratchNoise; p.scratchRate = x.tfScratchRate; p.scratchStyle = x.tfScratchStyle; p.repeat = x.tfRepeat; p.freeze = x.tfFreeze;
    return p;
}
void SamplerApp::applyTrackFx(int t) { s_.setTrackFx(t, trackParams(t)); }
void SamplerApp::applyPattern(int t) { s_.seqSetBars(t, ts_[t].seqBars + 1); s_.seqSetState(t, ts_[t].seqState); }
void SamplerApp::queueCommit(int t) { commitAt_ = nowMs_ + 350; commitTrack_ = t; }   // the offline stretch / pitch runs once the knob rests
void SamplerApp::setAlgo(int a) { chTrack_ = -1; ts_[track_].algo = ((a % 6) + 6) % 6; applyAlgo(track_); }
void SamplerApp::setVolume(int t, float v) { ts_[t].vol = cl(v); applyMix(t); }
void SamplerApp::setPan01(int t, float v) { ts_[t].pan = cl(v); applyMix(t); }
void SamplerApp::setSend(int t, int w, float v) { (w ? ts_[t].sendD : ts_[t].sendR) = cl(v); applyMix(t); }
void SamplerApp::setScale(int scale, int root, int octave, int mode) {
    scale_ = {std::min(kScaleCount - 1, std::max(0, scale)), ((root % 12) + 12) % 12, std::min(3, std::max(-3, octave)), std::min(kModeCount - 1, std::max(0, mode))};
    sysex(6, {7, (uint8_t)scale_.root, (uint8_t)(scale_.octave + 3), (uint8_t)(scale_.mode == 1)}, true);
}
void SamplerApp::selectTrack(int t) {
    t = std::min(trackCount_ - 1, std::max(0, t));
    if (t == track_) return;
    if (st_.track_clip_rec[track_]) s_.clipRecord(track_, false);   // an overdub belongs to the track it was started on
    track_ = t; if (knobTarget_ != KT_MASTER) knobPicked_ = false;   // the knob's value is not this track's value
    if (ts_[t].entry >= 0) libCursor_ = ts_[t].entry;   // PRESETS starts from the sample this track plays
}
void SamplerApp::gotoPage(Page p) { page_ = p; lastPage_[modeOf(p)] = p; }
void SamplerApp::modeButton(int m) {   // a mode button: enter the mode on the page it was left on, press again for the next page
    const int cur = modeOf(page_);
    if (m == 2 && (cur == 2 || cur == 4)) {   // the FM-1 has no fifth mode button: SEQ steps on through the TAPE pages and back
        const int n = modePageCount(cur), s = pageSlot(page_);
        gotoPage(s + 1 < n ? modePages(cur)[s + 1] : modePages(cur == 2 ? 4 : 2)[0]);
        return;
    }
    gotoPage(cur == m ? modePages(m)[(pageSlot(page_) + 1) % modePageCount(m)] : lastPage_[m]);
}

SamplerApp::Knob SamplerApp::quantKnob() {   // QUANTIZE: OFF 1/4 1/8 1/16 1/32, for the keys and the pattern together
    Knob q; q.name = "QUANTIZE"; q.valid = true; q.isChoice = true; q.norm = quantDiv() / 4.f; q.value = quantText();
    q.step = [this](int d) { setQuant((quantDiv() + (d > 0 ? 1 : 4)) % 5, quantTrip()); };
    q.set = [this](float v) { setQuant((int)std::lround(v * 4.f), quantTrip()); };
    return q;
}
SamplerApp::Knob SamplerApp::tripletKnob() {
    Knob q; q.name = "TRIPLETS"; q.valid = true; q.isChoice = true; q.norm = quantTrip() ? 1.f : 0.f; q.value = quantDiv() < 2 ? "NOT FOR 1/4" : quantTrip() ? "ON" : "OFF";
    q.step = [this](int) { setQuant(quantDiv(), !quantTrip()); }; q.set = [this](float v) { setQuant(quantDiv(), v > 0.5f); };
    return q;
}
SamplerApp::Knob SamplerApp::cont(const char* name, float* v, std::function<std::string(float)> f, std::function<void()> apply, float stepSize, bool commit) {
    Knob k; k.name = name; k.valid = true; k.norm = *v; k.value = f(*v);
    const int t = track_;
    k.step = [=](int d) { *v = cl(*v + d * stepSize); apply(); if (commit) queueCommit(t); };
    k.set = [=](float n) { *v = cl(n); apply(); if (commit) queueCommit(t); };
    return k;
}
// STOP / PLAY / REC of the pattern as three buttons: one state out of the two the engine keeps (running or not, and off / play / rec)
SamplerApp::Knob SamplerApp::patternTransport(int t) {
    Knob k; k.name = "PATTERN"; k.valid = true; k.isChoice = true;
    const int cur = st_.seqState[t] == 2 ? 2 : seqRunning(t) ? 1 : 0;
    k.segs = {"STOP", "PLAY", "REC"}; k.segCur = cur;
    k.value = cur == 2 ? "RECORDING" : cur == 1 ? (st_.seqRun[t] ? "PLAYING" : "WAITING FOR THE BEAT") : "STOPPED"; k.norm = cur / 2.f;
    k.pick = [this, t](int i) {   // the app keeps its own seqState (REC bounces a pattern that plays, the FM-1 screen reads it): it moves with the engine's
        auto state = [this, t](int v) { ts_[t].seqState = v; applyPattern(t); };
        if (i == 0) { if (ts_[t].seqState == 2) state(1); s_.seqRun(t, false); }
        else if (i == 2) state(2);   // REC: the engine starts a stopped pattern, after the count-in when the transport is stopped
        else { state(1); s_.seqRun(t, true); }
    };
    k.step = [k](int d) { k.pick(std::min(2, std::max(0, k.segCur + (d > 0 ? 1 : -1)))); }; k.set = [k](float v) { k.pick((int)std::lround(v * 2.f)); };
    return k;
}

SamplerApp::Knob SamplerApp::choice(const char* name, int* v, std::vector<std::string> opts, std::function<void()> apply, bool buttons) {
    Knob k; k.name = name; k.valid = true; k.isChoice = true;
    const int n = (int)opts.size();
    *v = std::min(n - 1, std::max(0, *v));
    k.value = opts[(size_t)*v]; k.norm = n > 1 ? *v / (float)(n - 1) : 0.f;
    k.step = [=](int d) { *v = std::min(n - 1, std::max(0, *v + (d > 0 ? 1 : -1))); apply(); };
    k.set = [=](float x) { *v = std::min(n - 1, std::max(0, (int)std::lround(x * (n - 1)))); apply(); };
    if (buttons) { k.segs = opts; k.segCur = *v; k.pick = [=](int i) { *v = std::min(n - 1, std::max(0, i)); apply(); }; }   // buttons instead of a slider
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
        std::vector<std::string> rates(kArpRate, kArpRate + 13), patt(kArpPatt, kArpPatt + 7);
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
        k[3] = mk("MODE", std::vector<std::string>(kModeName, kModeName + kModeCount), sc.mode, [=](int v) { setScale(sc.scale, sc.root, sc.octave, v); });
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
        const float cur = st_.loopSamples > 0 ? cl((float)(std::log2(std::max(1.0, (double)st_.loopBars)) / 6.0)) : 0.f;
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
        k[0] = patternTransport(t);
        k[1] = choice("BARS", &x.seqBars, {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16"}, [=] { applyPattern(t); });
        k[2] = quantKnob();
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
    case P_TFX2:
        k[0] = cont("DRIVE", &x.tfDrive, [](float v) { return pct(v); }, [=] { applyTrackFx(t); });
        k[1] = cont("FLANGER", &x.tfFlanger, [](float v) { return pct(v); }, [=] { applyTrackFx(t); });
        k[2] = cont("PHASER", &x.tfPhaser, [](float v) { return pct(v); }, [=] { applyTrackFx(t); });
        k[3] = cont("TREMOLO", &x.tfTremolo, [](float v) { return pct(v); }, [=] { applyTrackFx(t); });
        break;
    case P_TFX3:
        k[0] = cont("RING MOD", &x.tfRing, [](float v) { return v < 0.005f ? std::string("OFF") : fmt("%.0f HZ", 20.0 * std::pow(150.0, v)); }, [=] { applyTrackFx(t); });
        k[1] = cont("AUTO WAH", &x.tfWah, [](float v) { return pct(v); }, [=] { applyTrackFx(t); });
        k[2] = cont("WIDTH", &x.tfWidth, [](float v) { return v < 0.01f ? std::string("MONO") : pct(v * 2.f); }, [=] { applyTrackFx(t); });
        k[3] = choice("FREEZE", &x.tfFreeze, {kOffOn[0], kOffOn[1]}, [=] { applyTrackFx(t); });
        break;
    case P_TSTOP:
        k[0] = cont("STOP TIME", &tsDown_, [](float v) { return fmt("%.1f S", 0.3 * std::pow(20.0, v)); }, [=] { applyMasterFx(); });
        k[1] = cont("SPIN-UP TIME", &tsUp_, [](float v) { return v < 0.02f ? std::string("0 S") : fmt("%.1f S", 0.1 * std::pow(40.0, v)); }, [=] { applyMasterFx(); });   // knob all the way down: no spin-up, the tape is back at full speed at once
        k[2] = cont("DEPTH", &tsDepth_, [](float v) { return pct(v); }, [=] { applyMasterFx(); });
        k[3] = choice("BUTTON", &tsLatch_, {"HOLD", "LATCH"}, [=] { tapeStopHeld_ = false; applyMasterFx(); });
        break;
    case P_TFX4:
        k[0] = cont("SPRING", &x.tfSpring, [](float v) { return v < 0.02f ? std::string("OFF") : pct(v); }, [=] { applyTrackFx(t); });
        k[1] = cont("PHONE", &x.tfPhone, [](float v) { return v < 0.02f ? std::string("OFF") : pct(v); }, [=] { applyTrackFx(t); });
        k[2] = cont("PUNCH", &x.tfPunch, [](float v) { return v < 0.02f ? std::string("OFF") : pct(v); }, [=] { applyTrackFx(t); });
        k[3] = choice("BEAT REPEAT", &x.tfRepeat, {kRepeat[0], kRepeat[1], kRepeat[2], kRepeat[3], kRepeat[4]}, [=] { applyTrackFx(t); });
        break;
    case P_TFX5:
        k[0] = cont("WAVE FOLD", &x.tfFold, [](float v) { return v < 0.02f ? std::string("OFF") : pct(v); }, [=] { applyTrackFx(t); });
        k[1] = cont("AUTO PAN", &x.tfAutopan, [](float v) { return v < 0.02f ? std::string("OFF") : pct(v); }, [=] { applyTrackFx(t); });
        k[2] = cont("VINYL", &x.tfVinyl, [](float v) { return v < 0.02f ? std::string("OFF") : pct(v); }, [=] { applyTrackFx(t); });
        k[3] = cont("VOWEL", &x.tfVowel, [](float v) { return v < 0.02f ? std::string("OFF") : pct(v); }, [=] { applyTrackFx(t); });
        break;
    case P_TFX6:
        k[0] = cont("COMB", &x.tfComb, [](float v) { return v < 0.005f ? std::string("OFF") : pct(v); }, [=] { applyTrackFx(t); });
        k[1] = cont("COMB NOTE", &x.tfCombTune, [](float v) { const int n = (int)std::lround(69.0 + 12.0 * std::log2(60.0 * std::pow(2.0, v * 5.0) / 440.0)); return std::string(noteName(n)) + std::to_string(n / 12 - 1); }, [=] { applyTrackFx(t); }, 1.f / 60.f);
        k[2] = cont("OCTAVE", &x.tfOctave, [](float v) { return std::fabs(v - 0.5f) < 0.02f ? std::string("OFF") : fmt(v < 0.5f ? "-1 OCT %.0f%%" : "+1 OCT %.0f%%", std::fabs(v - 0.5f) * 200); }, [=] { applyTrackFx(t); });
        k[3] = cont("FREQ SHIFT", &x.tfFshift, [](float v) { const float u = (v - 0.5f) * 2.f; return std::fabs(u) < 0.04f ? std::string("OFF") : fmt("%+.0f HZ", std::copysign(u * u * 800.f, u)); }, [=] { applyTrackFx(t); });
        break;
    case P_TFX7:
        k[0] = cont("SCRATCH", &x.tfScratch, [](float v) { return v < 0.005f ? std::string("OFF") : pct(v); }, [=] { applyTrackFx(t); });
        k[1] = choice("STYLE", &x.tfScratchStyle, {"BABY", "SCRIBBLE", "RUN BACK", "TRANSFORM"}, [=] { applyTrackFx(t); }, true);
        k[2] = choice("CYCLE (BEATS)", &x.tfScratchRate, {"2", "1", "1/2", "1/4", "1/8"}, [=] { applyTrackFx(t); }, true);
        k[3] = cont("NEEDLE", &x.tfScratchNoise, [](float v) { return pct(v); }, [=] { applyTrackFx(t); });
        break;
    case P_TRIG: {
        k[0] = patternTransport(t);
        k[1] = choice("KEYS", &x.keyTrig, {"PLAY SOUND", "PLAY PATTERN"}, [=] { setKeyTrigger(t, ts_[t].keyTrig != 0); }, true);
        k[2] = choice("START ON", &x.trigQ, {"AT ONCE", "THE BEAT", "THE BAR"}, [=] { setTrigQuant(t, ts_[t].trigQ); }, true);
        Knob nu; nu.name = "FIRST BEAT"; nu.valid = true; nu.isChoice = true; nu.value = "< EARLIER  LATER >"; nu.norm = 0.5f;
        nu.step = [this](int d) { seqNudge(d > 0 ? stepTicks() : -stepTicks()); }; nu.set = [this](float v) { if (v < 0.4f) seqNudge(-stepTicks()); else if (v > 0.6f) seqNudge(stepTicks()); };
        k[3] = nu;
        break;
    }
    case P_TIMING: {
        std::vector<std::string> grids; for (int g = 0; g < kGrids; g++) grids.push_back(gridName(g));
        k[0] = quantKnob();
        k[1] = tripletKnob();
        k[2] = choice("COUNT-IN", &countIn_, {kCountIn[0], kCountIn[1], kCountIn[2]}, [=] { applyTempo(); });
        k[3] = cont("SWING", &swing_, [](float v) { return v < 0.02f ? std::string("OFF") : fmt("%.0f PCT", 50.f + 25.f * v); }, [=] { applyTempo(); }, 0.02f);
        break;
    }
    case P_CHOP: {   // the chops of a KIT sample: pick one, move its start and its end (what the phone's chop editor does with the finger)
        std::vector<float> cs, ce;
        if (!chopsRead(cs, ce)) break;
        const int n = (int)cs.size(), sel = std::min(std::max(0, chopSel_), n - 1); chopSel_ = sel;
        auto mk = [](const char* name, float norm, const std::string& val, bool choice, std::function<void(int)> step, std::function<void(float)> set) {
            Knob q; q.name = name; q.valid = true; q.isChoice = choice; q.norm = norm; q.value = val; q.step = std::move(step); q.set = std::move(set); return q; };
        static const char* stepName[3] = {"FINE", "NORMAL", "COARSE"};
        k[0] = mk("CHOP", n > 1 ? sel / (float)(n - 1) : 0.f, std::to_string(sel + 1) + " OF " + std::to_string(n), true,
                  [this, n](int d) { chopSel_ = std::min(n - 1, std::max(0, chopSel_ + (d > 0 ? 1 : -1))); },
                  [this, n](float v) { chopSel_ = (int)std::lround(cl(v) * (n - 1)); });
        k[1] = mk("START", cs[(size_t)sel], fmt("%.1f%%", cs[(size_t)sel] * 100), false, [this](int d) { chopNudge(false, d); }, [this](float v) { chopSetPos(false, v); });
        k[2] = mk("END", ce[(size_t)sel], fmt("%.1f%%", ce[(size_t)sel] * 100), false, [this](int d) { chopNudge(true, d); }, [this](float v) { chopSetPos(true, v); });
        k[3] = mk("NUDGE STEP", chopStep_ / 2.f, stepName[std::min(2, std::max(0, chopStep_))], true,
                  [this](int d) { chopStep_ = std::min(2, std::max(0, chopStep_ + (d > 0 ? 1 : -1))); },
                  [this](float v) { chopStep_ = (int)std::lround(cl(v) * 2); });
        break;
    }
    case P_PRINT: {   // the effects of this track on its tape
        k[0] = choice("PRINT FX", &x.printFx, {"OFF", "ON"}, [=] { setPrintFx(t, ts_[t].printFx != 0); }, true);
        Knob q; q.name = "TAPE OF THIS TRACK"; q.valid = true; q.isChoice = true; q.norm = x.printFx ? 1.f : 0.f;
        const bool tape = st_.track_has_clip[t];
        q.value = (printPendMask_ >> t & 1) ? "PRINTING..." : !tape ? (x.printFx ? "RECORDS WITH THE FX" : "NO TAPE YET") : x.printFx ? "HAS THE FX" : "DRY, FX AT PLAYBACK";
        q.step = [](int) {}; q.set = [](float) {};
        k[1] = q;
        break;
    }
    case P_SHAPE:
        if (isSynth(t)) {   // the four macros of a built-in synth
            k[0] = cont("TONE", &x.synTone, [](float v) { return fmt("%+.0f", (v - 0.5f) * 200.f); }, [=] { applyShape(t); }, 0.02f);
            k[1] = cont("BODY", &x.synBody, [](float v) { return fmt("%+.0f", (v - 0.5f) * 200.f); }, [=] { applyShape(t); }, 0.02f);
            k[2] = cont("MOTION", &x.synMotion, [](float v) { return fmt("%+.0f", (v - 0.5f) * 200.f); }, [=] { applyShape(t); }, 0.02f);
            k[3] = cont("GRIT", &x.synGrit, [](float v) { return fmt("%+.0f", (v - 0.5f) * 200.f); }, [=] { applyShape(t); }, 0.02f);
        } else {   // a sample: the key it sounds at its own pitch, auto tune, the slices of KIT mode
            k[0] = cont("ROOT KEY", &x.rootN, [](float v) { const int n = 24 + (int)std::lround(v * 72.f); return std::string(noteName(n)) + std::to_string(n / 12 - 1); }, [=] { applyShape(t); }, 1.f / 72.f);
            Knob a; a.name = "AUTO TUNE"; a.valid = true; a.isChoice = true;
            const float g = s_.rootGuessOf(t); a.value = g >= 24.f && g <= 96.f ? std::string("SET ") + noteName((int)std::lround(g)) + std::to_string((int)std::lround(g) / 12 - 1) : std::string("NO PITCH");
            a.norm = 0.f; a.step = [this](int) { autoTune(); }; a.set = [this](float) { autoTune(); };
            k[1] = a;
            k[2] = choice("SLICES", &x.sliceCount, {"4", "8", "12", "16", "24", "32"}, [=] { applyShape(t, true); });
            k[3] = cont("SLICE SENS", &x.sliceSens, [](float v) { return fmt("%.1fX", std::pow(2.f, (v - 0.5f) * 2.f)); }, [=] { applyShape(t, true); }, 0.05f);
        }
        break;
    case P_SYN1:   // a built-in synth: its oscillators
        if (!isSynth(t)) break;
        k[0] = choice("WAVE", &x.edW1, {"PATCH", "SINE", "SAW", "SQUARE", "TRIANGLE"}, [=] { applySynthEdit(t); });
        k[1] = choice("WAVE 2", &x.edW2, {"PATCH", "SINE", "SAW", "SQUARE", "TRIANGLE"}, [=] { applySynthEdit(t); });
        k[2] = cont("DETUNE 2", &x.edDet2, [](float v) { return fmt("%+.0f CENTS", (v - 0.5f) * 100.f); }, [=] { applySynthEdit(t); }, 0.01f);
        k[3] = choice("UNISON", &x.edUni, {"PATCH", "OFF", "3 VOICES", "5 VOICES"}, [=] { applySynthEdit(t); });
        break;
    case P_SYN2:   // its filter and voice mode
        if (!isSynth(t)) break;
        k[0] = cont("CUTOFF", &x.edCut, [](float v) { return fmt("%+.1f OCT", (v - 0.5f) * 6.f); }, [=] { applySynthEdit(t); }, 0.01f);
        k[1] = cont("RESONANCE", &x.edRes, [](float v) { return pct(v); }, [=] { applySynthEdit(t); });
        k[2] = choice("FILTER", &x.edType, {"LOW-PASS", "HIGH-PASS", "BAND-PASS"}, [=] { applySynthEdit(t); });
        k[3] = choice("VOICES", &x.edVoice, {"POLY", "MONO", "MONO GLIDE", "MONO SLOW GLIDE"}, [=] { applySynthEdit(t); });
        break;
    case P_TEQ:   // EQ and compressor of this track
        k[0] = cont("EQ LOW", &x.tfEqLow, [](float v) { return std::fabs(v - 0.5f) < 0.02f ? std::string("FLAT") : fmt("%+.0f DB", (v - 0.5f) * 24.f); }, [=] { applyTrackFx(t); }, 0.02f);
        k[1] = cont("EQ MID", &x.tfEqMid, [](float v) { return std::fabs(v - 0.5f) < 0.02f ? std::string("FLAT") : fmt("%+.0f DB", (v - 0.5f) * 24.f); }, [=] { applyTrackFx(t); }, 0.02f);
        k[2] = cont("EQ HIGH", &x.tfEqHigh, [](float v) { return std::fabs(v - 0.5f) < 0.02f ? std::string("FLAT") : fmt("%+.0f DB", (v - 0.5f) * 24.f); }, [=] { applyTrackFx(t); }, 0.02f);
        k[3] = cont("COMPRESSOR", &x.tfComp, [](float v) { return v < 0.02f ? std::string("OFF") : pct(v); }, [=] { applyTrackFx(t); });
        break;
    case P_DUCK:   // sidechain and the master limiter
        k[0] = choice("DUCK FROM", &duckSrc_, {"OFF", "TRACK 1", "TRACK 2", "TRACK 3", "TRACK 4", "TRACK 5", "TRACK 6", "TRACK 7", "TRACK 8"}, [=] { applyMaster(); });
        k[1] = cont("DUCK THIS TRACK", &x.duckAmt, [](float v) { return v < 0.02f ? std::string("OFF") : pct(v); }, [=] { applyMaster(); });
        k[2] = cont("DUCK RELEASE", &duckRel_, [](float v) { return fmt("%.2fS", 0.05f + v * 0.95f); }, [=] { applyMaster(); });
        k[3] = choice("LIMITER", &limiter_, {"OFF", "-0.3 DB", "-1 DB", "-2 DB", "-3 DB", "-4.5 DB", "-6 DB"}, [=] { applyMaster(); });
        break;
    case P_SMPED:
        if (isSynth(t)) break;   // nothing to edit in a synth
        {
            static const char* ops[6] = {"REVERSE", "NORMALIZE", "TRIM TO WINDOW", "FADE IN", "FADE OUT", "UNDO LAST EDIT"};
            k[0] = choice("EDIT", &x.editOp, {ops[0], ops[1], ops[2], ops[3], ops[4], ops[5]}, [] {});
            Knob a; a.name = "APPLY"; a.valid = true; a.isChoice = true; a.value = "TURN TO RUN"; a.norm = 0.f;
            a.step = [=](int) { const int op = ts_[t].editOp; if (op == 5) s_.undoEdit(t); else s_.editSample(t, op); if (op == 2) { ts_[t].start = 0.f; ts_[t].end = 1.f; } say(op == 5 ? "EDIT TAKEN BACK" : std::string(ops[op]) + " DONE"); };
            a.set = [=](float) { a.step(1); };
            k[1] = a;
            k[2] = cont("FINE TUNE", &x.fineN, [](float v) { return fmt("%+.0f CENTS", (v - 0.5f) * 100.f); }, [=] { applyEdit(t); }, 0.01f);
            k[3] = cont("SAMPLE LEVEL", &x.levelN, [](float v) { return fmt("%+.1f DB", 20.f * std::log10(std::max(0.01f, v * 3.f))); }, [=] { applyEdit(t); });
        }
        break;
    case P_PADS: {   // the selected pad of a kit
        const int pd = x.selPad;
        const std::string head = "PAD " + std::to_string(pd + 1) + " ";
        k[0] = cont((head + "LEVEL").c_str(), &x.padVol[pd], [](float v) { return pct(v * 1.5f); }, [=] { applyPad(t, pd); });
        k[1] = cont((head + "TUNE").c_str(), &x.padPitch[pd], [](float v) { return fmt("%+.1f ST", (v - 0.5f) * 24.f); }, [=] { applyPad(t, pd); }, 1.f / 48.f);
        k[2] = cont((head + "DECAY").c_str(), &x.padDecay[pd], [](float v) { return fmt("%.2fX", std::pow(4.f, (v - 0.5f) * 2.f)); }, [=] { applyPad(t, pd); });
        k[3] = choice((head + "CHOKE").c_str(), &x.padGroup[pd], {"OFF", "GROUP 1", "GROUP 2", "GROUP 3", "GROUP 4"}, [=] { applyPad(t, pd); });
        break;
    }
    case P_FX3:   // the OP-1 effects
        k[0] = cont("SPRING", &spring_, [](float v) { return v < 0.02f ? std::string("OFF") : pct(v); }, [=] { applyMasterFx(); });
        k[1] = cont("SPRING DAMP", &springDamp_, [](float v) { return pct(v); }, [=] { applyMasterFx(); });
        k[2] = cont("PHONE", &phone_, [](float v) { return v < 0.02f ? std::string("OFF") : pct(v); }, [=] { applyMasterFx(); });
        k[3] = cont("PUNCH", &punch_, [](float v) { return v < 0.02f ? std::string("OFF") : pct(v); }, [=] { applyMasterFx(); });
        break;
    case P_FX4:
        k[0] = cont("WAVE FOLD", &fold_, [](float v) { return v < 0.02f ? std::string("OFF") : pct(v); }, [=] { applyMasterFx(); });
        k[1] = cont("AUTO PAN", &autopan_, [](float v) { return v < 0.02f ? std::string("OFF") : pct(v); }, [=] { applyMasterFx(); });
        k[2] = cont("VINYL", &vinyl_, [](float v) { return v < 0.02f ? std::string("OFF") : pct(v); }, [=] { applyMasterFx(); });
        k[3] = cont("VOWEL", &vowel_, [](float v) { return v < 0.02f ? std::string("OFF") : pct(v); }, [=] { applyMasterFx(); });
        break;
    case P_FX5:
        k[0] = cont("COMB", &comb_, [](float v) { return v < 0.005f ? std::string("OFF") : pct(v); }, [=] { applyMasterFx(); });
        k[1] = cont("COMB NOTE", &combTune_, [](float v) { const int n = (int)std::lround(69.0 + 12.0 * std::log2(60.0 * std::pow(2.0, v * 5.0) / 440.0)); return std::string(noteName(n)) + std::to_string(n / 12 - 1); }, [=] { applyMasterFx(); }, 1.f / 60.f);
        k[2] = cont("OCTAVE", &octave_, [](float v) { return std::fabs(v - 0.5f) < 0.02f ? std::string("OFF") : fmt(v < 0.5f ? "-1 OCT %.0f%%" : "+1 OCT %.0f%%", std::fabs(v - 0.5f) * 200); }, [=] { applyMasterFx(); });
        k[3] = cont("FREQ SHIFT", &fshift_, [](float v) { const float u = (v - 0.5f) * 2.f; return std::fabs(u) < 0.04f ? std::string("OFF") : fmt("%+.0f HZ", std::copysign(u * u * 800.f, u)); }, [=] { applyMasterFx(); });
        break;
    case P_FX6:
        k[0] = cont("SCRATCH", &scratch_, [](float v) { return v < 0.005f ? std::string("OFF") : pct(v); }, [=] { applyMasterFx(); });
        k[1] = choice("STYLE", &scratchStyle_, {"BABY", "SCRIBBLE", "RUN BACK", "TRANSFORM"}, [=] { applyMasterFx(); }, true);
        k[2] = choice("CYCLE (BEATS)", &scratchRate_, {"2", "1", "1/2", "1/4", "1/8"}, [=] { applyMasterFx(); }, true);
        k[3] = cont("NEEDLE", &scratchNoise_, [](float v) { return pct(v); }, [=] { applyMasterFx(); });
        break;
    case P_TOMB: {   // balls bounce in a turning drum and play their notes when they hit the wall: play keys to drop balls
        k[0] = cont("SPIN", &tSpin_, [](float v) { return fmt("%+.1f", (v - 0.5f) * 14.f); }, [=] { applyTomb(); }, 0.01f);
        k[1] = cont("GRAVITY", &tGrav_, [](float v) { return pct(v); }, [=] { applyTomb(); });
        k[2] = cont("SHAPE", &tShape_, [](float v) { return fmt("%.0f SIDES", 3.0 + std::round(v * 5.0)); }, [=] { applyTomb(); }, 0.2f);
        k[3] = cont("MASS", &tMass_, [](float v) { return pct(v); }, [=] { applyTomb(); });
        break;
    }
    case P_TEMPO: {
        const bool locked = st_.loopSamples > 0;
        Knob b; b.name = "TEMPO"; b.valid = true; b.norm = cl((tempoNow() - 30.f) / 270.f);
        b.value = fmt("%.0f BPM", tempoNow());
        b.step = [this](int d) { tempoStep(d); }; b.set = [this](float v) { setTempo(30.f + v * 270.f); };
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
void SamplerApp::tempoStep(int d) {
    if (st_.loopSamples <= 0) { setTempo(tempo_ + d); return; }
    setTempo((tempoPending_ > 0 ? tempoPending_ : st_.bpm) + d);
}
void SamplerApp::setTempo(float bpm) {
    if (st_.loopSamples > 0) {   // the loops follow: they are stretched once the finger rests, the pitch stays
        tempoPending_ = cl(std::round(bpm), 30.f, 300.f); tempoApplyAt_ = nowMs_ + 600; tempoSent_ = false;
        say("TEMPO " + std::to_string((int)tempoPending_) + ": THE TAPE IS STRETCHED"); return;
    }
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
void SamplerApp::newLoop(int bars, bool sequencer) {
    if (sequencer) {
        const int b = std::min(16, std::max(1, bars));
        seqSetBarCount(b); s_.seqClear(track_);
        ts_[track_].seqState = 2; applyPattern(track_);
        if (!st_.playing) s_.play(true);
        gotoPage(P_SQ);
        say(bars > 16 ? "A PATTERN HOLDS 16 BARS: PLAY KEYS" : "SEQUENCER LOOP, " + std::to_string(b) + (b == 1 ? " BAR: PLAY KEYS" : " BARS: PLAY KEYS"));
        return;
    }
    if (loopAny_) { say("THE TAPE HAS A LOOP: CLEAR IT FIRST"); return; }
    for (int i = 0; i < kTakeBarsN; i++) { takeBars_ = i; if (takeBars() == bars) break; }
    applyTempo(); gotoPage(P_TAPEV); loopRecord();
}
void SamplerApp::seqBounce() {
    if (st_.bounce) { s_.seqBounceCancel(); say("PATTERN TO TAPE CANCELLED"); return; }   // a second tap cancels
    if (!st_.seqCount[track_]) { say("PATTERN IS EMPTY: HOLD SEQ AND PLAY KEYS"); return; }
    if (st_.freeRecording || st_.freeArmed) { say("FINISH THE TAPE TAKE FIRST"); return; }
    if (st_.countBeats > 0) { say("WAIT FOR THE COUNT-IN"); return; }
    ts_[track_].seqState = 1;
    const int tb = std::max(1, st_.loopBars), pb = std::min(tb, std::max(1, st_.seqBars[track_]));
    const int bar = loopAny_ ? std::min(bounceBar_, tb - pb + 1) : 0;   // no tape yet: the pattern sets the loop
    s_.seqBounce(track_, bar);
    say(!loopAny_ ? "PATTERN TO TAPE" : bar ? "PATTERN TO TAPE FROM BAR " + std::to_string(bar) : "TO TAPE AT THE LOOP START");
}
void SamplerApp::bounceBarStep(int d) {   // ALL, 1 ... last bar the pattern fits from
    const int tb = std::max(1, st_.loopBars), pb = std::min(tb, std::max(1, st_.seqBars[track_]));
    const int n = tb - pb + 2;
    bounceBar_ = ((std::min(bounceBar_, n - 1) + d) % n + n) % n;
}
void SamplerApp::seqToggleAt(int tick, int note, int len) { s_.seqToggle(track_, tick, len > 0 ? len : stepTicks(), note, 100); }
// the dice: a new random pattern in the current scale (one undo step); denser on the beats, sometimes a long note
void SamplerApp::tombolaDrop() {
    int sc[12]; int n = scaleSteps(scale_.scale, sc);
    if (n == 0) { static const int pent[] = {0, 3, 5, 7, 10}; std::copy(pent, pent + 5, sc); n = 5; }
    uint32_t r = nowMs_ * 2654435761u + 777u; r = r * 1664525u + 1013904223u;
    const int deg = (int)((r >> 8) % (uint32_t)(n * 2));
    s_.tombolaAdd(track_, std::min(127, 48 + scale_.root + 12 * (deg / n) + sc[deg % n] + 12 * scale_.octave));
}
void SamplerApp::seqDice() {
    int sc[12]; int n = scaleSteps(scale_.scale, sc);
    if (n == 0) { static const int pent[] = {0, 3, 5, 7, 10}; std::copy(pent, pent + 5, sc); n = 5; }
    uint32_t r = nowMs_ * 2654435761u + 12345u; auto rnd = [&r] { r = r * 1664525u + 1013904223u; return r >> 8; };
    const int bars = seqBarsOf(track_), st = stepTicks(), steps = patternTicks(bars) / st;
    std::vector<Session::SeqView> out; int deg = (int)(rnd() % (uint32_t)n);
    for (int i = 0; i < steps && (int)out.size() < kSeqMax; i++) {
        const bool beat = (i * st) % kPpq == 0;
        if (rnd() % 100 >= (beat ? 60u : 28u)) continue;
        deg = std::min(n * 2 - 1, std::max(0, deg + (int)(rnd() % 5) - 2));   // a random walk over two octaves of the scale
        const int note = std::min(127, 48 + scale_.root + 12 * (deg / n) + sc[deg % n]);
        out.push_back({i * st, st * (rnd() % 100 < 22 ? 2 : 1) * 3 / 4, note, 70 + (int)(rnd() % 50)});
    }
    s_.seqSet(track_, out.data(), (int)out.size());
    if (ts_[track_].seqState == 0) { ts_[track_].seqState = 1; applyPattern(track_); }
}
void SamplerApp::seqGen() {
    int sc[12]; int n = scaleSteps(scale_.scale, sc);
    if (n == 0) { static const int pent[] = {0, 3, 5, 7, 10}; std::copy(pent, pent + 5, sc); n = 5; }
    const int bars = seqBarsOf(track_), st = stepTicks(), steps = patternTicks(bars) / st;
    genSeed_ = genSeed_ * 1664525u + (uint32_t)nowMs_ + 1013904223u;
    const auto notes = patgen::generate(genKind_, steps, st, sc, n, scale_.root, genSeed_);
    std::vector<Session::SeqView> out; out.reserve(notes.size());
    for (const auto& g : notes) out.push_back({g.tick, g.len, g.note, g.vel});
    s_.seqSet(track_, out.data(), (int)out.size());
    say(std::string(patgen::name(genKind_)) + ": " + std::to_string(out.size()) + " NOTES");
    genKind_ = (genKind_ + 1) % patgen::COUNT;   // the next press is the next generator
    if (ts_[track_].seqState == 0) { ts_[track_].seqState = 1; applyPattern(track_); }
}
void SamplerApp::seqResizeAt(int tick, int note, int len) { s_.seqResize(track_, tick, note, len); }
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
int SamplerApp::progOf() const { return scale_.mode >= 3 && scale_.mode <= 8 ? scale_.mode - 3 : scale_.mode >= 18 ? scale_.mode - 18 : -1; }
int SamplerApp::chordDegree() const { return harmony::degreeAt(std::max(0, progOf()), (int)std::floor(st_.beatPos / 4.0), progSeed_); }
int SamplerApp::chordSnap(int note) const {   // QNT CHRD: the nearest tone of the chord of this bar (a bar is four beats)
    int sc[12]; const int n = scaleSteps(scale_.scale, sc); if (n < 1) return note;
    int pcs[4]; const int c = harmony::tones(sc, n, chordDegree(), false, pcs);
    return harmony::snapToChord(note, pcs, c, scale_.root);
}
void SamplerApp::keyEvent(int raw, bool down) {   // a key of the FM-1 (chromatic note 53+key): octave shift, chords in CHORD mode
    if (raw < 0 || raw > 127) return;
    if (down && keyGesture(raw)) return;
    if (down) {
        if (keyCount_[raw]) keyEvent(raw, false);
        const bool kit = ts_[track_].algo == 3;   // KIT: every key is a pad (note 60 + key), whatever the tonality or octave
        const int base = kit ? 60 + (raw - 53) : raw + 12 * scale_.octave;
        int cnt = 0; const int md = kit ? 0 : scale_.mode, sc = hasScale(scale_.scale) ? scale_.scale : -1;
        keyNotes_[raw][cnt++] = md == 0 || md == 1 || md == 9 || md == 10 || (md >= 11 && md <= 17) ? base : (md == 2 || sc < 0) ? snapToScale(base, scale_.scale, scale_.root) : chordSnap(base);
        auto add = [&](int n) { if (cnt < 8 && n >= 0 && n <= 127) keyNotes_[raw][cnt++] = n; };
        if (md == 1 || md == 9 || md == 10) {   // the scale's triad (CHORD), its seventh chord (CHORD7) or the triad spread 1-5-3 (OPEN) on the key; a key outside the scale takes the scale note below it
            const int sb = sc >= 0 ? snapToScale(base, scale_.scale, scale_.root) : base; keyNotes_[raw][0] = sb;
            const int rel = sb - scale_.root, o = rel >= 0 ? rel / 12 : -((-rel + 11) / 12), i = sc >= 0 ? scaleIndex(rel - 12 * o, scale_.scale) : -1;
            if (i >= 0) {
                const int d = o * kScaleN[scale_.scale] + i, third = scale_.root + degreeNote(d + 2, scale_.scale, 0), fifth = scale_.root + degreeNote(d + 4, scale_.scale, 0);
                if (md == 10) { keyNotes_[raw][0] = sb; add(fifth); add(third + 12); } else { add(third); add(fifth); if (md == 9) add(scale_.root + degreeNote(d + 6, scale_.scale, 0)); }
            }
        } else if (md >= 11 && md <= 17) {   // a shape on the key, whatever the scale
            static const int shape[7][4] = {{0, 4, 7, -1}, {0, 3, 7, -1}, {0, 4, 7, 10}, {0, 4, 7, 11}, {0, 3, 7, 10}, {0, 5, 7, -1}, {0, 7, 12, -1}};
            for (int k = 1; k < 4 && shape[md - 11][k] >= 0; k++) add(base + shape[md - 11][k]);
        } else if (md >= 18 && sc >= 0) {   // the whole chord of this bar, the key's note its lowest: neighbouring keys play inversions of the same chord, and some chords repeat
            int ss[12]; const int n = scaleSteps(scale_.scale, ss); int pcs[4]; const int c = harmony::tones(ss, n, chordDegree(), false, pcs);
            int got = 1; for (int step = 1; step <= 12 && got < c; step++) { const int pc = (((keyNotes_[raw][0] + step - scale_.root) % 12) + 12) % 12; for (int k = 0; k < c; k++) if (pcs[k] == pc) { add(keyNotes_[raw][0] + step); got++; break; } }
        }
        keyCount_[raw] = cnt; keyTrack_[raw] = track_;
        for (int j = 0; j < cnt; j++) playNote(keyNotes_[raw][j], true);
    } else {
        for (int j = 0; j < keyCount_[raw]; j++) playNote(keyNotes_[raw][j], false, 100, keyTrack_[raw]);
        keyCount_[raw] = 0;
    }
}
void SamplerApp::playNote(int note, bool down, int vel, int track) {
    if (note < 0 || note > 127) return;
    // chords on neighbouring keys share notes, and the same note can be held on two tracks: count the holders of (track, note) and send the
    // note off only when the last one lets go, to the track that took it
    if (down) { const int t = track_; heldCnt_[t][note]++; heldNote_[note] = t; s_.noteOn(t, note, vel); if (page_ == P_TOMB) s_.tombolaAdd(t, note); return; }
    const int t = track >= 0 ? track : heldNote_[note];
    if (t < 0 || t >= kTracks || heldCnt_[t][note] <= 0) return;
    if (--heldCnt_[t][note] > 0) return;
    s_.noteOff(t, note); if (heldNote_[note] == t) heldNote_[note] = -1;
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
        else if (a == 39) knobMove(((knobMsb_ << 3) | (b >> 4)) / 1023.f);
        else if (a == 24) battery_ = b;
    }
}
void SamplerApp::onSysex(const uint8_t* m, size_t n) {
    if (n < 9 || m[1] != 0x7d || m[2] != 'D' || m[3] != 'A' || m[4] != 'W') return;
    linkSeen_ = true;
    if (m[6] == 0x40) lastPongAt_ = nowMs_ ? nowMs_ : 1;
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
    if (b < 0 || b > REC) return;   // a stray note number must not reach the bit masks
    const uint32_t bit = 1u << b;
    const bool wasHeld = (held_ & bit) != 0;
    if (down) held_ |= bit; else held_ &= ~bit;
    if (page_ == P_CHOP && down && b == REC && chopsAvailable()) { chopSplit(); return; }   // the CHOP page: REC cuts the chop in two (GLO deletes it, on release)
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
    if (b == HOME || b == FX || b == EDIT || b == GLO || b == SEL) {   // these also work as modifiers (hold one, play a key or turn an encoder): the tap acts on release unless it was used
        if (down) { modUsed_ &= ~bit; return; }
        if (!wasHeld) return;   // a release whose press never came (a lost message): nothing to do
        const bool used = (modUsed_ & bit) != 0; modUsed_ &= ~bit;
        if (b == FX && (fxBrowsed_ || used)) {   // FX + PRESETS browsed the effects: let go and the page of the one shown opens
            const auto& cat = fxCatalog();
            if (fxBrowsed_ && fxCur_ >= 0 && fxCur_ < (int)cat.size()) { const Page pg = fxScopeTrack_ ? cat[(size_t)fxCur_].track : cat[(size_t)fxCur_].master; if (pg != kPages) focusFx(cat[(size_t)fxCur_].name, pg); }
            fxBrowsed_ = false; fxCur_ = -1; fxScopeAuto_ = true;   // the next browse starts fresh
            return;
        }
        if (used) return;
        if (b == GLO && page_ == P_CHOP && chopsAvailable()) { chopRemove(); return; }   // the CHOP page: a tap on GLO deletes the chop, holding it for a gesture does not
        switch (b) { case HOME: modeButton(0); break; case FX: modeButton(1); break; case EDIT: modeButton(3); break; case GLO: gotoPage(P_KEY); break; default: gotoPage(P_LEN); break; }
        return;
    }
    if (!down) return;
    switch (b) {
    case ENV: gotoPage(P_ENV); break;      // shortcuts to a page of a mode
    case LFO: gotoPage(P_LFO); break;
    case ARP: gotoPage(P_ARP); break;
    case PLAY: transportPlay(); break;
    case REC: loopRecord(); break;
    default: break;
    }
}
void SamplerApp::encoder(int i, int delta) {
    if (!delta) return;
    const uint32_t fxBit = 1u << FX, gloBit = 1u << GLO;
    if (i == 6 && (held_ & fxBit)) { modUsed_ |= fxBit; fxCursorStep(delta); return; }          // FX + PRESETS: browse the effects on the FM-1 screen
    if (i == 1 && (held_ & fxBit)) { modUsed_ |= fxBit; fxScopeTrack_ = !fxScopeTrack_; fxScopeAuto_ = false; fxCur_ = -1; say(fxScopeTrack_ ? "FX BROWSER: THIS TRACK" : "FX BROWSER: WHOLE MIX"); return; }   // FX + ALGORITHM: whole mix / this track
    if (i == 6 && (held_ & gloBit)) { modUsed_ |= gloBit; setKnobTarget(knobTarget_ + (delta > 0 ? 1 : kKnobTargets - 1)); return; }   // GLO + PRESETS: what the analog knob moves
    if (i == 0) { selectTrack(track_ + (delta > 0 ? 1 : -1)); return; }
    if (i == 6) { browse(delta); return; }
    if (i == 1) { setAlgo(ts_[track_].algo + (delta > 0 ? 1 : -1)); return; }
    if (i == 2 && mic_ == 1 && page_ != P_SAVE) { setMicThreshold01(micThr_ + (delta > 0 ? 0.03f : -0.03f)); touchedKnob_ = 0; touchedAt_ = nowMs_; return; }   // K1 = trigger level while the microphone listens
    Knobs k = knobs();
    const int idx = i - 2;
    if (idx < 4 && k[(size_t)idx].valid && k[(size_t)idx].step) { k[(size_t)idx].step(delta); touchedKnob_ = idx; touchedAt_ = nowMs_; if (lastTouchPage_ != (int)page_ || lastTouchKnob_ != idx) { lastTouchPage_ = (int)page_; lastTouchKnob_ = idx; if (knobTarget_ == KT_LAST) knobPicked_ = false; } }
}

// ---- FM-1 gestures: hold a button, then play a key, turn an encoder or move the analog knob ----
const char* SamplerApp::knobTargetName(int t) {
    static const char* n[kKnobTargets] = {"MASTER VOLUME", "TRACK VOLUME", "DJ FILTER", "LAST TURNED KNOB"};
    return n[((t % kKnobTargets) + kKnobTargets) % kKnobTargets];
}
void SamplerApp::setKnobTarget(int t) {
    knobTarget_ = ((t % kKnobTargets) + kKnobTargets) % kKnobTargets;
    knobPicked_ = knobTarget_ == KT_MASTER;   // the others wait until the knob is where the value is
    say(std::string("KNOB: ") + knobTargetName(knobTarget_));
}
void SamplerApp::knobMove(float v) {
    v = cl(v);
    if (knobTarget_ == KT_MASTER) { s_.setMasterVolume(v * 1.2f); return; }
    Knob kn;
    if (knobTarget_ == KT_TRACK) kn = knobs(P_MIX)[0];
    else if (knobTarget_ == KT_DJ) kn = knobs(P_LIVE)[0];
    else if (lastTouchKnob_ >= 0) kn = knobs((Page)lastTouchPage_)[(size_t)lastTouchKnob_];
    if (!kn.valid || !kn.set) return;
    if (!knobPicked_) {
        if (std::fabs(v - kn.norm) > 0.05f) return;   // not there yet: the value stays where it is
        knobPicked_ = true;
    }
    kn.set(v);
}
bool SamplerApp::keyGesture(int raw) {   // EDIT + key n = mute track n (+ SEL: solo it), HOME + key n = go to track n
    const int k = raw - 53;
    const uint32_t editBit = 1u << EDIT, selBit = 1u << SEL, homeBit = 1u << HOME;
    if (k < 0 || k >= trackCount_ || k >= kFmTracks) return false;
    const std::string n = std::to_string(k + 1);
    if (held_ & editBit) {
        modUsed_ |= editBit | selBit;
        if (held_ & selBit) { toggleSolo(k); say("TRACK " + n + (ts_[k].solo ? " SOLO" : " SOLO OFF")); }
        else { toggleMute(k); say("TRACK " + n + (ts_[k].mute ? " MUTED" : " UNMUTED")); }
        return true;
    }
    if (held_ & homeBit) { modUsed_ |= homeBit; selectTrack(k); return true; }
    return false;
}
void SamplerApp::focusFx(const std::string& name, Page p) {
    gotoPage(p); fxFocusPage_ = (int)p; fxFocusKnob_ = -1;
    const Knobs kk = knobs(p);
    for (int i = 0; i < 4; i++) if (kk[(size_t)i].valid && kk[(size_t)i].name.rfind(name, 0) == 0) { fxFocusKnob_ = i; break; }   // EQ -> EQ LOW
}
void SamplerApp::fxCursorStep(int d) {   // FX + PRESETS: the catalog of the phone's FX browser on the FM-1 screen
    if (fxCur_ < 0 && fxScopeAuto_) fxScopeTrack_ = modeOf(page_) == 3;   // a browse starts on the side of the tab you are in, unless ALGORITHM chose
    const auto& cat = fxCatalog();
    const int n = (int)cat.size(); int i = fxCur_ < 0 ? (d > 0 ? -1 : 0) : fxCur_;
    for (int tries = 0; tries < n; tries++) {
        i = ((i + (d > 0 ? 1 : -1)) % n + n) % n;
        if ((fxScopeTrack_ ? cat[(size_t)i].track : cat[(size_t)i].master) != kPages) break;
    }
    fxCur_ = i; fxBrowsed_ = true;
    say(std::string(cat[(size_t)i].name) + (fxScopeTrack_ ? "  (TRACK)" : "  (MIX)"));
}
const char* SamplerApp::fmHint(Page p) {
    switch (p) {
    case P_CHOP: return "K1 chop  K2 start  K3 end  K4 step.  REC splits the chop, GLO deletes it, the keys play the pads.";
    case P_SQ: return "Keys 1-16 light the steps of the bar that plays; the step under the playhead flips.";
    case P_MIX: case P_TEQ: case P_DUCK: case P_TFX: case P_TFX2: case P_TFX3: case P_TFX4: case P_TFX5: case P_TFX6: case P_TFX7:
        return "Hold EDIT + key 1-8: mute that track (add SEL: solo it).  Hold HOME + key: go to that track.  Hold FX + PRESETS: browse effects.";
    case P_FX: case P_FX2: case P_FX3: case P_FX4: case P_FX5: case P_FX6: case P_TONE: case P_MOD: case P_LIVE: case P_LIVE2: case P_TSTOP:
        return "Hold FX + turn PRESETS to browse the effects, let go of FX to open one.  Hold FX + turn ALGORITHM: whole mix / this track.";
    case P_PRINT: return "PRINT FX ON: the effects you play while you record or overdub this track go onto its tape (K1 turns it on).";
    case P_KEY: return "Hold GLO + turn PRESETS: what the analog knob moves (master volume, track volume, DJ filter, last turned knob).";
    case P_HOME: case P_SHAPE: case P_PADS: return "Hold HOME + key 1-8: go to that track.  The CHOP page (press HOME again) edits chops with K1-K4.";
    default: return "";
    }
}
const char* SamplerApp::fmGesture(int i) {
    static const char* g[] = {
        "HOLD EDIT + KEY 1-8: MUTE THAT TRACK",
        "HOLD EDIT + SEL + KEY 1-8: SOLO THAT TRACK",
        "HOLD HOME + KEY 1-8: GO TO THAT TRACK",
        "HOLD FX + TURN PRESETS: BROWSE EFFECTS, LET GO TO OPEN",
        "HOLD FX + TURN ALGORITHM: WHOLE MIX / THIS TRACK",
        "HOLD GLO + TURN PRESETS: WHAT THE ANALOG KNOB MOVES",
        "CHOP PAGE (SOUND MODE): K1 CHOP, K2 START, K3 END, K4 STEP",
        "CHOP PAGE: REC SPLITS THE CHOP, GLO DELETES IT",
        "KEY LEDS: PAD KEYS LIGHT FOR EACH CHOP, STEP KEYS ON THE PATTERN PAGE",
        nullptr};
    return g[i];
}
// ---- chops (KIT sample): the same list the phone's editor draws; every change goes back to the sampler at once ----
bool SamplerApp::chopsAvailable() const { return ts_[track_].algo == 3 && !isSynth(track_) && hasSample(track_); }
bool SamplerApp::chopsRead(std::vector<float>& s, std::vector<float>& e) {
    if (!chopsAvailable()) return false;
    if (chTrack_ != track_ || chS_.empty() || nowMs_ - chAt_ > 250) {
        chS_ = s_.slicesOf(track_); chE_ = s_.sliceEndsOf(track_); chTrack_ = track_; chAt_ = nowMs_;
        if (chE_.size() != chS_.size()) { chE_.assign(chS_.size(), 1.f); for (size_t i = 0; i + 1 < chS_.size(); i++) chE_[i] = chS_[i + 1]; }   // a chop with no end stops where the next one starts
    }
    s = chS_; e = chE_;
    return !s.empty();
}
void SamplerApp::chopsWrite(const std::vector<float>& s0, const std::vector<float>& e0) {
    std::vector<size_t> order(s0.size()); for (size_t i = 0; i < order.size(); i++) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return s0[a] < s0[b]; });   // the sampler keeps the chops in order of their start: so do we, and the selection follows its chop
    std::vector<float> s, e; for (size_t i = 0; i < order.size(); i++) { s.push_back(s0[order[i]]); e.push_back(e0[order[i]]); if ((int)order[i] == chopSel_) chopSel_ = (int)i; }
    s_.setSliceStarts(track_, s, e);
    chS_ = s; chE_ = e; chTrack_ = track_; chAt_ = nowMs_;   // what the sampler will say in a moment
}
void SamplerApp::chopNudge(bool end, int steps) {
    static const float size[3] = {0.001f, 0.005f, 0.02f};   // of the sample length: fine, normal, coarse
    std::vector<float> s, e; if (!chopsRead(s, e)) return;
    const size_t i = (size_t)std::min((int)s.size() - 1, std::max(0, chopSel_));
    const float d = (float)std::min(8, std::max(-8, steps)) * size[std::min(2, std::max(0, chopStep_))];
    if (end) e[i] = cl(e[i] + d, s[i] + 0.002f, 1.f); else s[i] = cl(s[i] + d, 0.f, e[i] - 0.002f);
    chopsWrite(s, e);
}
void SamplerApp::chopSetPos(bool end, float v) {
    std::vector<float> s, e; if (!chopsRead(s, e)) return;
    const size_t i = (size_t)std::min((int)s.size() - 1, std::max(0, chopSel_));
    if (end) e[i] = cl(v, s[i] + 0.002f, 1.f); else s[i] = cl(v, 0.f, e[i] - 0.002f);
    chopsWrite(s, e);
}
void SamplerApp::chopSplit() {   // the selected chop becomes two halves
    std::vector<float> s, e; if (!chopsRead(s, e)) return;
    if (s.size() >= 32) { say("32 CHOPS IS THE MOST"); return; }
    const size_t i = (size_t)std::min((int)s.size() - 1, std::max(0, chopSel_));
    if (e[i] - s[i] < 0.006f) { say("THAT CHOP IS TOO SHORT"); return; }
    const float m = 0.5f * (s[i] + e[i]);
    s.insert(s.begin() + (long)i + 1, m); e.insert(e.begin() + (long)i + 1, e[i]); e[i] = m;
    chopsWrite(s, e); say("CHOP " + std::to_string(i + 1) + " SPLIT");
}
void SamplerApp::chopRemove() {
    std::vector<float> s, e; if (!chopsRead(s, e)) return;
    if (s.size() <= 2) { say("A KIT KEEPS TWO CHOPS"); return; }
    const size_t i = (size_t)std::min((int)s.size() - 1, std::max(0, chopSel_));
    s.erase(s.begin() + (long)i); e.erase(e.begin() + (long)i);
    chopSel_ = std::min((int)s.size() - 1, (int)i);
    chopsWrite(s, e); say("CHOP " + std::to_string(i + 1) + " DELETED");
}

void SamplerApp::setPrintFx(int t, bool on) {
    if (t < 0 || t >= kTracks) return;
    ts_[t].printFx = on ? 1 : 0;
    const bool tape = st_.track_has_clip[t];
    s_.setPrintFx(t, on);
    printPendMask_ &= ~(1u << t);
    if (on && tape) { printPendMask_ |= 1u << t; printReqAt_[t] = nowMs_ ? nowMs_ : 1; say("PRINTING THE TAPE THROUGH THE FX..."); }
    else say(on ? "THE TAPE RECORDS WITH THE FX" : "PRINT OFF: A PRINTED TAPE GETS THE FX AGAIN");
}

// the FM-1 screen wears the theme of the phone
void SamplerApp::applyFmPalette() {
    auto c = [](uint32_t a) { return rgb((int)(a >> 16 & 255), (int)(a >> 8 & 255), (int)(a & 255)); };
    auto mix = [](uint32_t a, uint32_t b) { return (uint32_t)(((a >> 16 & 255) + (b >> 16 & 255)) / 2) << 16 | (uint32_t)(((a >> 8 & 255) + (b >> 8 & 255)) / 2) << 8 | (uint32_t)(((a & 255) + (b & 255)) / 2); };
    BLACK = c(theme::BG); PAPER = c(theme::TEXT); DIM = c(theme::DIM); ACCENT = c(theme::ACCENT); ONACC = c(theme::ON_ACCENT);
    BAR = c(mix(theme::BG, theme::OUTLINE)); PANEL = c(theme::SURFACE);   // the empty track of a bar: halfway between the background and the line colour, so it shows in every theme
    needClear_ = true;   // every pixel is repainted
}

// ---- tick ----
void SamplerApp::tick(uint32_t now) {
    nowMs_ = now;
    if (fwDirty_) { fwDirty_ = false; sysex(6, {7, (uint8_t)scale_.root, (uint8_t)(scale_.octave + 3), (uint8_t)(scale_.mode == 1)}, true); }
    if (repaintAt_ && now >= repaintAt_) { repaintAt_ = repaintAt_ + 2500 < now + 2500 && repaintCount_++ < 1 ? now + 2500 : 0; needClear_ = true; }
    st_ = s_.status();
    if (lastPongAt_ && nowMs_ - lastPongAt_ > 2500) for (int k = 0; k < 128; k++) if (keyCount_[k]) keyEvent(k, false);   // cable out: a key that never came up must not ring on
    if (held_ && lastPongAt_ && nowMs_ - lastPongAt_ > 2500) { held_ = 0; modUsed_ = 0; fxBrowsed_ = false; }   // the FM-1 went quiet (cable out): a button that never came up must not stay held
    for (int t = 0; t < kTracks; t++) if (st_.seqBars[t] > 0 && nowMs_ - barsLocalAt_[t] > 250) ts_[t].seqBars = std::min(15, st_.seqBars[t] - 1);   // the engine is the truth for the length (a bank can have another one)
    projectTick();
    loopAny_ = false;
    for (int t = 0; t < kTracks; t++) loopAny_ |= st_.track_has_clip[t];
    for (int t = 0; t < kTracks; t++) if (st_.seqState[t] != seqSeen_[t]) { seqSeen_[t] = st_.seqState[t]; ts_[t].seqState = st_.seqState[t]; }   // the engine changed it (a bounce mutes the pattern)
    for (int t = 0; t < kTracks; t++) {   // the engine is the truth for PRINT FX (an undo of a print, a loaded tape and a bake that was dropped all change it behind our back)
        const int eng = (int)(st_.printMask >> t & 1), pend = (int)(printPendMask_ >> t & 1);
        if (pend && eng) { printPendMask_ &= ~(1u << t); say("TAPE PRINTED WITH THE FX"); }
        else if (pend && st_.printBusy == 0 && nowMs_ - printReqAt_[t] > 2500) { printPendMask_ &= ~(1u << t); ts_[t].printFx = 0; say("THE TAPE CHANGED WHILE PRINTING. TURN PRINT FX ON AGAIN"); }
        else if (!pend && ts_[t].printFx != eng) ts_[t].printFx = eng;
    }
    if (st_.bounceDone != lastBounceDone_) { lastBounceDone_ = st_.bounceDone; say("PATTERN IS ON THE TAPE (MUTED)"); }
    if (st_.loopSamples <= 0 && tempoPending_ > 0) { tempoPending_ = 0; tempoSent_ = false; }   // the tape was cleared: nothing to stretch
    if (tempoPending_ > 0 && !tempoSent_ && now >= tempoApplyAt_) { s_.setLoopTempo(tempoPending_); tempoSent_ = true; tempoSentAt_ = now; }
    if (tempoSent_ && (std::fabs(st_.bpm - tempoPending_) < 1.6f || now - tempoSentAt_ > 8000)) { tempoPending_ = 0; tempoSent_ = false; }   // the stretch is done
    if (st_.loopSamples != lastLoopSamples_) { lastLoopSamples_ = st_.loopSamples; tempo_ = st_.bpm; }   // a take or a clear changed the tempo
    if (now - lastLoss_ > 4000 && now - lastRaise_ > 2000 && gap_ > 16) { gap_ = std::max<uint32_t>(16, gap_ * 9 / 10); lastRaise_ = now; }
    if (now - lastPing_ >= 500) { lastPing_ = now; pingId_ = (pingId_ + 1) & 0x3fff; sysex(5, n14(pingId_)); }
    if (saveHeld_ && !saveHoldDone_ && now - saveDownAt_ > 700) { saveHoldDone_ = true; clearAsk_ = true; say("CLEAR THE TAPE? ANSWER ON THE PHONE"); }
    if (seqHeld_ && !seqRecStarted_ && now - seqDownAt_ > 300) {   // SEQ held: the pattern records what is played
        seqRecStarted_ = true; gotoPage(P_SQ); ts_[track_].seqState = 2; applyPattern(track_); say("RECORDING: PLAY KEYS");
    }
    // audition: the cursor rested, load the entry on the selected track and play it
    if (auditionAt_ && now >= auditionAt_) loadEntry(libCursor_, audition_);
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
    const bool trackKeys = (held_ & ((1u << EDIT) | (1u << HOME))) != 0;   // EDIT / HOME held: the keys 1-8 are the tracks
    const bool kitKeys = ts_[track_].algo == 3;
    std::vector<float> cs, ce; const bool haveChops = chopsRead(cs, ce);
    const int pads = kitKeys && !isSynth(track_) && haveChops ? (int)cs.size() : 16;   // KIT sample: a key per chop
    bool stepOn[16] = {}; int stepHead = -1;
    if (page_ == P_SQ) {   // the pattern page: keys 1-16 are the steps of the bar that plays (the step under the playhead flips)
        Session::SeqView v[kSeqMax]; const int cnt = s_.seqNotes(track_, v, kSeqMax);
        const int pt = patternTicks(seqBarsOf(track_)), barTicks = kPpq * 4, st = kPpq / 4;
        const double ph = std::fmod(st_.beatPos * kPpq, (double)pt);
        const int bar = st_.seqState[track_] == 3 ? st_.seqCursor[track_] / barTicks : (int)(ph / barTicks);
        for (int j = 0; j < cnt; j++) { const int rel = v[j].tick - bar * barTicks; if (rel >= 0 && rel < barTicks) stepOn[std::min(15, rel / st)] = true; }
        if (st_.playing || st_.seqState[track_]) { const int rel = (int)ph - bar * barTicks; if (rel >= 0 && rel < barTicks) stepHead = std::min(15, rel / st); }
    }
    for (int k = 0; k < 27; k++) {
        const int raw = 53 + k, note = raw + 12 * scale_.octave, pc = ((note - scale_.root) % 12 + 12) % 12;
        bool on;
        if (trackKeys) {   // lit = a sound, dark = empty or muted, slow blink = the selected track, fast blink = solo
            if (k >= trackCount_ || k >= kFmTracks) on = false;
            else if (ts_[k].solo) on = blinkFast;
            else if (ts_[k].mute) on = false;
            else if (k == track_) on = blinkSlow;
            else on = hasSample(k);
            if (on) m |= 1ull << (14 + k);
            continue;
        }
        if (page_ == P_SQ && k < 16) { on = stepOn[k] != (k == stepHead); if (on) m |= 1ull << (14 + k); continue; }
        on = kitKeys ? k < pads : hasScale(scale_.scale) ? scaleIndex(pc, scale_.scale) >= 0 : pc == 0;   // KIT: a key per pad
        if (kitKeys && page_ == P_CHOP && on && k == chopSel_) on = blinkFast;   // the chop that K2 / K3 move
        else if (on && pc == 0 && hasScale(scale_.scale) && !kitKeys) on = blinkSlow;
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
    const int heroKind = mic_ == 1 || mic_ == 2 ? 6 : page_ == P_ENV ? 1 : page_ == P_LFO ? 2 : page_ == P_MIX ? 3 : page_ == P_KEY ? 4 : page_ == P_LEN ? 5 : page_ == P_SQ ? 7 : page_ == P_TEMPO ? 8 : page_ == P_CHOP && chopsAvailable() ? 9 : 0;
    if (heroKind != lastHero) { lastHero = heroKind; needClear_ = true; }
    if (needClear_) { fwDirty_ = true; clear(BLACK); std::fill(cells_.begin(), cells_.end(), SurfaceCell()); std::fill(rects_.begin(), rects_.end(), ~0ull); needResend_ = false; }
    else if (needResend_) { needResend_ = false; std::fill(cells_.begin(), cells_.end(), SurfaceCell()); std::fill(rects_.begin(), rects_.end(), ~0ull); }
    char b[96];
    const Knobs k = knobs();
    // track tags: the selected one is amber, a track with a sample is white, an empty one grey; the dot under it is the loop
    for (int i = 0; i < kFmTracks; i++) {
        const bool sel = i == track_, used = hasSample(i);
        snprintf(b, sizeof b, "%d", i + 1);
        drawText(i, i * 30 + 9, 2, 2, sel ? ONACC : used ? PAPER : DIM, sel ? ACCENT : BLACK, b, 1);
        rect(8 + i, i * 30 + 9, 20, 12, 2, st_.track_clip_rec[i] ? ACCENT : st_.track_has_clip[i] ? PAPER : BLACK);
    }
    drawText(20, 6, 26, 2, ACCENT, BLACK, kPage[page_], 9);
    for (int i = 0; i < 11; i++) rect(270 + i, 122 + i * 4, 30, 3, 5, i >= modePageCount(modeOf(page_)) ? BLACK : i == pageSlot(page_) ? ACCENT : BAR);   // where you are inside the mode (press its button again for the next page)
    const char* state = st_.freeRecording || st_.track_clip_rec[track_] ? "REC" : st_.playing ? "PLAY" : "STOP";
    drawText(21, 176, 26, 2, st_.freeRecording || st_.track_clip_rec[track_] ? ACCENT : PAPER, BLACK, state, 5);
    // sample line: name and algorithm (or the toast / microphone state)
    std::string line;
    if (mic_ == 1) line = "MIC: WAITING FOR SOUND";
    else if (mic_ == 2) line = fmt("MIC: RECORDING %.1fS", st_.sampleSeconds);
    else if (!toast().empty()) line = toast();
    else {
        const std::string lab = s_.trackLabel(track_);
        line = (lab.empty() ? "EMPTY" : lab) + "  " + (isSynth(track_) ? "SYNTH" : kAlgo[ts_[track_].algo]);
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
    const char* hint = mic_ == 1 ? (page_ == P_SAVE ? "K4 = TRIGGER LEVEL  LINE = THRESHOLD" : "K1 = TRIGGER LEVEL  LINE = THRESHOLD") : mic_ == 2 ? "RECORDING  K1 ON LOOP PAGE STOPS"  : page_ == P_CHOP ? (chopsAvailable() ? "REC SPLIT  GLO DELETE  KEYS PLAY PADS" : "SET ALGORITHM TO KIT/SLICE") : page_ == P_LEN ? "TURN K1: X2 OR /2  THE LOOP REPEATS" : page_ == P_SAVE ? "TAP SAVE = SAVE LOOP  HOLD = CLEAR ALL" : page_ == P_SQ ? "HOLD SEQ AND PLAY KEYS TO RECORD" : page_ == P_KEY ? "KEYS FOLLOW THE KEY  CHANGE ANY TIME"
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
        for (int i = 0; i < kFmTracks; i++) column(i, x0 + i * 29, 24, (int)(std::min(1.f, st_.track_peak[i] * 1.5f) * H), i == track_ ? ACCENT : PAPER, true);
    } else if (page_ == P_KEY) {   // the tonality in big letters
        char b[32]; snprintf(b, sizeof b, "%s %s", kNote[scale_.root], kScaleName[scale_.scale]);
        drawText(100, x0, y0 + 6, 4, ACCENT, BLACK, b, 9);
        if (scale_.mode >= 3) snprintf(b, sizeof b, "%s %s", kModeName[scale_.mode], harmony::numeral(chordDegree())); else snprintf(b, sizeof b, "OCT %+d  %s", scale_.octave, kModeName[scale_.mode]);
        drawText(101, x0, y0 + 42, 2, PAPER, BLACK, b, 18);
    } else if (page_ == P_SQ) {   // the pattern: one column per slice, height = pitch, the playhead in amber
        Session::SeqView v[kSeqMax]; const int cnt = s_.seqNotes(track_, v, kSeqMax);
        const int pt = patternTicks(seqBarsOf(track_)), cols = 57;
        const double ph = std::fmod(st_.beatPos * kPpq, (double)pt);
        const int pc = st_.playing || x.seqState ? (int)(ph / pt * cols) : -1;
        for (int i = 0; i < cols; i++) {
            int top = 0;
            for (int j = 0; j < cnt; j++) { const int c = v[j].tick * cols / pt; if (c == i) top = std::max(top, 12 + (v[j].note % 36) * (H - 12) / 36); }
            const int cc = (int)((double)st_.seqCursor[track_] / pt * cols), cursor = x.seqState == 3 && i == cc;
            column(i, x0 + i * 4, 3, cursor ? std::max(8, top) : std::max(2, top), cursor || i == pc ? ACCENT : top ? PAPER : BAR, true);
        }
    } else if (page_ == P_CHOP && chopsAvailable()) {   // the sample with its chops: the selected one amber, the others white, the gaps dark
        std::vector<float> cs, ce; chopsRead(cs, ce);
        const auto pk = peaks(track_, 57); const int sel = std::min(std::max(0, chopSel_), (int)cs.size() - 1);
        for (int i = 0; i < 57; i++) {
            const float pos = (i + 0.5f) / 57.f; bool any = false, in = false;
            for (size_t j = 0; j < cs.size(); j++) if (pos >= cs[j] && pos <= ce[j]) { any = true; if ((int)j == sel) in = true; }
            const float v = i < (int)pk.size() ? pk[(size_t)i] : 0.f;
            column(i, x0 + i * 4, 3, (int)(std::min(1.f, v * 1.1f) * H) + 1, in ? ACCENT : any ? PAPER : BAR, false);
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

// ---- the FX browser: every effect, by kind, with the page of its knob (master or track) and whether it is on ----
const std::vector<SamplerApp::FxItem>& SamplerApp::fxCatalog() {
    constexpr Page N = kPages;   // no page: the effect does not exist on that side
    auto d = [](float v, float rest) { return std::fabs(v - rest) >= 0.02f; };
    static const std::vector<FxItem> c = {
        {"DJ FILTER", "FILTER", P_LIVE, P_TFX, [d](const FxParams& p) { return d(p.filter, 0.5f); }},
        {"EQ", "FILTER", P_TONE, P_TEQ, [d](const FxParams& p) { return d(p.eqLow, 0.5f) || d(p.eqMid, 0.5f) || d(p.eqHigh, 0.5f); }},
        {"AUTO WAH", "FILTER", P_MOD, P_TFX3, [](const FxParams& p) { return p.wah > 0.01f; }},
        {"VOWEL", "FILTER", P_FX4, P_TFX5, [](const FxParams& p) { return p.vowel > 0.01f; }},
        {"COMB", "FILTER", P_FX5, P_TFX6, [](const FxParams& p) { return p.comb > 0.01f; }},
        {"OCTAVE", "PITCH", P_FX5, P_TFX6, [d](const FxParams& p) { return d(p.octave, 0.5f); }},
        {"FREQ SHIFT", "PITCH", P_FX5, P_TFX6, [d](const FxParams& p) { return d(p.fshift, 0.5f); }},
        {"DRIVE", "DRIVE", P_TONE, P_TFX2, [](const FxParams& p) { return p.drive > 0.01f; }},
        {"WAVE FOLD", "DRIVE", P_FX4, P_TFX5, [](const FxParams& p) { return p.fold > 0.01f; }},
        {"CRUSH", "DRIVE", P_LIVE, P_TFX, [](const FxParams& p) { return p.crush > 0.01f; }},
        {"PUNCH", "DRIVE", P_FX3, P_TFX4, [](const FxParams& p) { return p.punch > 0.01f; }},
        {"PHONE", "DRIVE", P_FX3, P_TFX4, [](const FxParams& p) { return p.phone > 0.01f; }},
        {"COMPRESSOR", "DRIVE", P_FX2, P_TEQ, [](const FxParams& p) { return p.comp > 0.01f; }},
        {"CHORUS", "MOTION", P_FX2, P_TFX, [](const FxParams& p) { return p.chorus > 0.01f; }},
        {"FLANGER", "MOTION", P_FX2, P_TFX2, [](const FxParams& p) { return p.flanger > 0.01f; }},
        {"PHASER", "MOTION", P_FX2, P_TFX2, [](const FxParams& p) { return p.phaser > 0.01f; }},
        {"TREMOLO", "MOTION", P_MOD, P_TFX2, [](const FxParams& p) { return p.tremolo > 0.01f; }},
        {"RING MOD", "MOTION", P_MOD, P_TFX3, [](const FxParams& p) { return p.ringmod > 0.01f; }},
        {"AUTO PAN", "MOTION", P_FX4, P_TFX5, [](const FxParams& p) { return p.autopan > 0.01f; }},
        {"WIDTH", "MOTION", P_MOD, P_TFX3, [d](const FxParams& p) { return d(p.width, 0.5f); }},
        {"REVERB / DELAY", "SPACE", P_FX, P_MIX, nullptr},
        {"SPRING", "SPACE", P_FX3, P_TFX4, [](const FxParams& p) { return p.spring > 0.01f; }},
        {"VINYL", "SPACE", P_FX4, P_TFX5, [](const FxParams& p) { return p.vinyl > 0.01f; }},
        {"SCRATCH", "LIVE", P_FX6, P_TFX7, [](const FxParams& p) { return p.scratch > 0.01f; }},
        {"BEAT REPEAT", "LIVE", P_LIVE, P_TFX4, [](const FxParams& p) { return p.repeat > 0; }},
        {"TRANCE GATE", "LIVE", P_LIVE, P_TFX, [](const FxParams& p) { return p.gate > 0; }},
        {"REVERSE", "LIVE", P_LIVE2, N, [](const FxParams& p) { return p.reverse > 0; }},
        {"FREEZE", "LIVE", P_LIVE2, P_TFX3, [](const FxParams& p) { return p.freeze > 0; }},
        {"TAPE STOP", "LIVE", P_TSTOP, N, nullptr},
    };
    return c;
}
