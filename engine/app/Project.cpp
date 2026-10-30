// Projects: the whole state of the sampler on disk. dataDir/projects/<name>/project.txt holds the settings and the patterns as key=value lines,
// tape_N.wav the loops (see Session::writeTape). "autosave" is written every 20 s and when the app goes to the background and is loaded at the start.
#include "SamplerApp.h"
#include "../dsp/Midi.h"
#include "../ui/Theme.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace fs = std::filesystem;
namespace {
constexpr int kFormat = 2, kScaleCount = 22;   // (the number of scales in SamplerApp.cpp)
using KV = std::map<std::string, std::string>;

// one description of the state serves both directions: saving writes the key, loading reads it (and clamps what it read)
struct Io {
    bool save; const KV* in; std::ostringstream out;
    void f(const std::string& k, float& v, float lo = 0.f, float hi = 1.f) {
        if (save) { char b[40]; snprintf(b, sizeof b, "%.6g", v); out << k << '=' << b << '\n'; return; }
        const auto it = in->find(k); if (it == in->end()) return;
        char* e = nullptr; const float x = std::strtof(it->second.c_str(), &e); if (e != it->second.c_str() && std::isfinite(x)) v = std::min(hi, std::max(lo, x));
    }
    void i(const std::string& k, int& v, int lo, int hi) {
        if (save) { out << k << '=' << v << '\n'; return; }
        const auto it = in->find(k); if (it == in->end()) return;
        char* e = nullptr; const long x = std::strtol(it->second.c_str(), &e, 10); if (e != it->second.c_str()) v = (int)std::min<long>(hi, std::max<long>(lo, x));
    }
};
KV parse(const std::string& text) {
    KV kv; std::istringstream is(text); std::string line;
    while (std::getline(is, line)) { const size_t eq = line.find('='); if (eq != std::string::npos && eq > 0) kv[line.substr(0, eq)] = line.substr(eq + 1); }
    return kv;
}
std::string readFile(const std::string& p) { std::ifstream f(p); return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); }
}  // namespace

std::string SamplerApp::projectsRoot() const { return s_.dataDir() + "/projects"; }
std::string SamplerApp::projectDir(const std::string& name) const { return projectsRoot() + "/" + name; }
std::string SamplerApp::cleanName(const std::string& n) {
    std::string o; for (char c : n) if (std::isalnum((unsigned char)c) || c == ' ' || c == '-' || c == '_') o += c;
    while (!o.empty() && o.back() == ' ') o.pop_back();
    return o.substr(0, 40);
}

std::vector<std::string> SamplerApp::projectList() const {
    std::vector<std::pair<fs::file_time_type, std::string>> v; std::error_code ec;
    for (auto& e : fs::directory_iterator(projectsRoot(), ec)) if (e.is_directory() && e.path().filename() != "autosave" && fs::exists(e.path() / "project.txt")) v.push_back({fs::last_write_time(e.path() / "project.txt", ec), e.path().filename().string()});
    std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.first > b.first; });
    std::vector<std::string> out; for (auto& p : v) out.push_back(p.second); return out;
}
std::string SamplerApp::newProjectName() const {
    for (int i = 1; i < 1000; i++) { const std::string n = "Project " + std::to_string(i); if (!fs::exists(projectDir(n))) return n; }
    return "Project";
}

// ---- the state ----
std::string SamplerApp::stateText() {
    Io io{true, nullptr, {}}; int fmtv = kFormat, sr = s_.sampleRate();
    io.i("format", fmtv, 0, 99); io.i("rate", sr, 8000, 192000);
    float tempo = st_.loopSamples > 0 ? st_.bpm : tempo_; int bars = st_.loopSamples > 0 ? st_.loopBars : 0, track = track_;
    io.f("tempo", tempo, 30, 300); io.i("loopBars", bars, 0, 32); io.i("track", track, 0, kTracks - 1);
    auto common = [&](Io& x) {
        x.f("reverbSize", reverbSize_); x.f("reverbDw", reverbDw_); x.f("delayTime", delayTime_); x.f("delayFb", delayFb_); x.i("loopSrc", loopSrc_, 0, 1);
        x.f("chorus", chorus_); x.f("phaser", phaser_); x.f("comp", comp_); x.f("width", width_); x.f("djFilter", djFilter_); x.f("crush", crush_);
        x.f("spring", spring_); x.f("springDamp", springDamp_); x.f("phone", phone_); x.f("punch", punch_); x.f("flanger", flanger_); x.f("drive", drive_); x.f("eqLow", eqLow_); x.f("eqMid", eqMid_); x.f("eqHigh", eqHigh_); x.f("tremolo", tremolo_); x.f("wah", wah_); x.f("ringmod", ringmod_); x.f("tapeStop", tapeStop_);
        x.i("reverse", reverse_, 0, 1); x.i("freeze", freeze_, 0, 1); x.i("repeat", repeat_, 0, 8); x.i("gate", gate_, 0, 8);
        x.i("metro", metro_, 0, 1); x.i("takeQuant", takeQuant_, 0, 2); x.i("seqGrid", seqGrid_, 0, kGrids - 1); x.i("liveGrid", liveGrid_, 0, kGrids - 1); x.i("countIn", countIn_, 0, 2);
        x.f("swing", swing_); x.f("tSpin", tSpin_); x.f("tGrav", tGrav_); x.f("tMass", tMass_); x.f("tShape", tShape_); x.f("metroVol", metroVol_);
        x.i("duckSrc", duckSrc_, 0, 8); x.f("duckRel", duckRel_); x.i("limiter", limiter_, 0, 6); x.f("exciter", exciter_); x.f("lofi", lofi_); x.f("wobble", wobble_); x.f("loopSpeed", loopSpeed_);
        x.i("scale", scale_.scale, 0, kScaleCount - 1); x.i("scaleRoot", scale_.root, 0, 11); x.i("scaleOct", scale_.octave, -3, 3); x.i("scaleMode", scale_.mode, 0, 2);
    };
    common(io);
    for (int t = 0; t < kTracks; t++) {
        TrackState& x = ts_[t]; const std::string k = "t" + std::to_string(t) + ".";
        io.i(k + "algo", x.algo, 0, 5); io.f(k + "a", x.a); io.f(k + "d", x.d); io.f(k + "s", x.s); io.f(k + "r", x.r);
        io.f(k + "lfoRate", x.lfoRate); io.f(k + "lfoDepth", x.lfoDepth); io.i(k + "lfoTarget", x.lfoTarget, 0, 3); io.i(k + "lfoShape", x.lfoShape, 0, 3);
        io.f(k + "start", x.start); io.f(k + "end", x.end); io.f(k + "speed", x.speed); io.f(k + "pitch", x.pitch);
        io.i(k + "arpRate", x.arpRate, 0, 12); io.i(k + "arpPatt", x.arpPatt, 0, 6); io.i(k + "arpOct", x.arpOct, 0, 3); io.f(k + "arpLen", x.arpLen);
        io.f(k + "vol", x.vol); io.f(k + "pan", x.pan); io.f(k + "sendR", x.sendR); io.f(k + "sendD", x.sendD);
        io.f(k + "rootN", x.rootN); io.f(k + "synTone", x.synTone); io.f(k + "synBody", x.synBody); io.f(k + "synMotion", x.synMotion); io.f(k + "synGrit", x.synGrit); io.f(k + "sliceSens", x.sliceSens); io.i(k + "sliceCount", x.sliceCount, 0, 5);
        for (int q = 0; q < 16; q++) { const std::string pk = k + "pad" + std::to_string(q) + "."; io.f(pk + "v", x.padVol[q]); io.f(pk + "p", x.padPitch[q]); io.f(pk + "d", x.padDecay[q]); io.i(pk + "g", x.padGroup[q], 0, 4); }
        io.f(k + "fineN", x.fineN); io.f(k + "levelN", x.levelN);
        io.f(k + "tfEqLow", x.tfEqLow); io.f(k + "tfEqMid", x.tfEqMid); io.f(k + "tfEqHigh", x.tfEqHigh); io.f(k + "tfComp", x.tfComp); io.f(k + "duckAmt", x.duckAmt);
        io.f(k + "tfDj", x.tfDj); io.f(k + "tfChorus", x.tfChorus); io.f(k + "tfCrush", x.tfCrush); io.i(k + "tfGate", x.tfGate, 0, 8);
        int sq = x.seqState == 0 ? 0 : 1, bars2 = x.seqBars; io.i(k + "seqState", sq, 0, 1); io.i(k + "seqBars", bars2, 0, 15);
        if (x.entry >= 0 && x.entry < (int)lib_.size()) io.out << k << "sample=" << lib_[(size_t)x.entry].group << '|' << lib_[(size_t)x.entry].label << '\n';
        Session::SeqView v[kSeqMax]; const int n = s_.seqNotes(t, v, kSeqMax);
        if (n > 0) { io.out << k << "notes="; for (int i = 0; i < n; i++) io.out << (i ? ";" : "") << v[i].tick << ',' << v[i].len << ',' << v[i].note << ',' << v[i].vel; io.out << '\n'; }
    }
    return io.out.str();
}

void SamplerApp::applyStateText(const std::string& text) {
    const KV kv = parse(text); Io io{false, &kv, {}};
    int fmtv = 0, sr = 0, bars = 0, track = 0; float tempo = tempo_;
    io.i("format", fmtv, 0, 99); io.i("rate", sr, 8000, 192000); io.f("tempo", tempo, 30, 300); io.i("loopBars", bars, 0, 32); io.i("track", track, 0, kTracks - 1);
    auto common = [&](Io& x) {
        x.f("reverbSize", reverbSize_); x.f("reverbDw", reverbDw_); x.f("delayTime", delayTime_); x.f("delayFb", delayFb_); x.i("loopSrc", loopSrc_, 0, 1);
        x.f("chorus", chorus_); x.f("phaser", phaser_); x.f("comp", comp_); x.f("width", width_); x.f("djFilter", djFilter_); x.f("crush", crush_);
        x.f("spring", spring_); x.f("springDamp", springDamp_); x.f("phone", phone_); x.f("punch", punch_); x.f("flanger", flanger_); x.f("drive", drive_); x.f("eqLow", eqLow_); x.f("eqMid", eqMid_); x.f("eqHigh", eqHigh_); x.f("tremolo", tremolo_); x.f("wah", wah_); x.f("ringmod", ringmod_); x.f("tapeStop", tapeStop_);
        x.i("reverse", reverse_, 0, 1); x.i("freeze", freeze_, 0, 1); x.i("repeat", repeat_, 0, 8); x.i("gate", gate_, 0, 8);
        x.i("metro", metro_, 0, 1); x.i("takeQuant", takeQuant_, 0, 2); x.i("seqGrid", seqGrid_, 0, kGrids - 1); x.i("liveGrid", liveGrid_, 0, kGrids - 1); x.i("countIn", countIn_, 0, 2);
        x.f("swing", swing_); x.f("tSpin", tSpin_); x.f("tGrav", tGrav_); x.f("tMass", tMass_); x.f("tShape", tShape_); x.f("metroVol", metroVol_);
        x.i("duckSrc", duckSrc_, 0, 8); x.f("duckRel", duckRel_); x.i("limiter", limiter_, 0, 6); x.f("exciter", exciter_); x.f("lofi", lofi_); x.f("wobble", wobble_); x.f("loopSpeed", loopSpeed_);
        x.i("scale", scale_.scale, 0, kScaleCount - 1); x.i("scaleRoot", scale_.root, 0, 11); x.i("scaleOct", scale_.octave, -3, 3); x.i("scaleMode", scale_.mode, 0, 2);
    };
    common(io);
    tempo_ = tempo;
    for (int t = 0; t < kTracks; t++) {
        TrackState& x = ts_[t]; const std::string k = "t" + std::to_string(t) + ".";
        io.i(k + "algo", x.algo, 0, 5); io.f(k + "a", x.a); io.f(k + "d", x.d); io.f(k + "s", x.s); io.f(k + "r", x.r);
        io.f(k + "lfoRate", x.lfoRate); io.f(k + "lfoDepth", x.lfoDepth); io.i(k + "lfoTarget", x.lfoTarget, 0, 3); io.i(k + "lfoShape", x.lfoShape, 0, 3);
        io.f(k + "start", x.start); io.f(k + "end", x.end); io.f(k + "speed", x.speed); io.f(k + "pitch", x.pitch);
        io.i(k + "arpRate", x.arpRate, 0, 12); io.i(k + "arpPatt", x.arpPatt, 0, 6); io.i(k + "arpOct", x.arpOct, 0, 3); io.f(k + "arpLen", x.arpLen);
        io.f(k + "vol", x.vol); io.f(k + "pan", x.pan); io.f(k + "sendR", x.sendR); io.f(k + "sendD", x.sendD);
        io.f(k + "rootN", x.rootN); io.f(k + "synTone", x.synTone); io.f(k + "synBody", x.synBody); io.f(k + "synMotion", x.synMotion); io.f(k + "synGrit", x.synGrit); io.f(k + "sliceSens", x.sliceSens); io.i(k + "sliceCount", x.sliceCount, 0, 5);
        for (int q = 0; q < 16; q++) { const std::string pk = k + "pad" + std::to_string(q) + "."; io.f(pk + "v", x.padVol[q]); io.f(pk + "p", x.padPitch[q]); io.f(pk + "d", x.padDecay[q]); io.i(pk + "g", x.padGroup[q], 0, 4); }
        io.f(k + "tfDj", x.tfDj); io.f(k + "tfChorus", x.tfChorus); io.f(k + "tfCrush", x.tfCrush); io.i(k + "tfGate", x.tfGate, 0, 8);
        int sq = 0, bars2 = x.seqBars; io.i(k + "seqState", sq, 0, 1); io.i(k + "seqBars", bars2, 0, 15);
        if (fmtv < 2) bars2 = std::min(3, bars2) == 0 ? 0 : (1 << std::min(3, bars2)) - 1;   // format 1 kept 0..3 for 1, 2, 4, 8 bars
        if (x.start > x.end - 0.02f) { x.start = 0.f; x.end = 1.f; }
        x.seqState = sq; x.seqBars = bars2;
        // the sample: found by group and name in the library (a recording has its file name)
        x.entry = -1;
        const auto it = kv.find(k + "sample");
        if (it != kv.end()) {
            const size_t bar = it->second.find('|'); const std::string group = it->second.substr(0, bar), label = bar == std::string::npos ? "" : it->second.substr(bar + 1);
            for (size_t i = 0; i < lib_.size(); i++) if (lib_[i].group == group && lib_[i].label == label) { x.entry = (int)i; break; }
        }
        if (x.entry >= 0) s_.loadSamplePath(lib_[(size_t)x.entry].path, lib_[(size_t)x.entry].kit, t); else s_.unloadSample(t);
        applyAll(t); applyTrackFx(t); applyPattern(t);
        std::vector<Session::SeqView> notes;
        const auto nt = kv.find(k + "notes");
        if (nt != kv.end()) {
            std::istringstream ns(nt->second); std::string item;
            while (std::getline(ns, item, ';') && (int)notes.size() < kSeqMax) {
                int a, b, c, d; if (sscanf(item.c_str(), "%d,%d,%d,%d", &a, &b, &c, &d) == 4 && a >= 0 && a < patternTicks(8) && b > 0 && c >= 0 && c < 128) notes.push_back({a, std::min(b, 65535), c, std::min(127, std::max(1, d))});
            }
        }
        s_.seqSet(t, notes.data(), (int)notes.size());
    }
    applyFx(); applyMasterFx(); applySeq(0); applyTomb(); applyTempo(); applyMaster(); s_.setLoopSource(loopSrc_);
    setScale(scale_.scale, scale_.root, scale_.octave, scale_.mode);
    track_ = track;
    (void)bars; (void)fmtv; (void)sr;
}

// ---- save, load ----
bool SamplerApp::saveProject(const std::string& rawName) {
    const std::string name = cleanName(rawName);
    if (name.empty()) return false;
    std::error_code ec; fs::create_directories(projectDir(name), ec);
    const std::string text = stateText();
    { std::ofstream f(projectDir(name) + "/project.txt.tmp"); if (!f) { say("CANNOT WRITE THE PROJECT"); return false; } f << text; if (!f) return false; }
    fs::rename(projectDir(name) + "/project.txt.tmp", projectDir(name) + "/project.txt", ec);   // a crash in the middle never leaves a half written project
    s_.writeTape(projectDir(name));
    if (name != "autosave") project_ = name;
    return !ec;
}
bool SamplerApp::loadProject(const std::string& rawName) {
    const std::string name = cleanName(rawName);
    const std::string text = readFile(projectDir(name) + "/project.txt");
    if (text.empty()) { say("PROJECT NOT FOUND"); return false; }
    const KV kv = parse(text);
    std::error_code ec; fs::create_directories(projectsRoot(), ec);
    { std::ofstream m(projectsRoot() + "/.loading"); m << name; }   // removed again when the project is up; if the app dies in between, the next start does not try it again
    loading_ = true; loadedAt_ = nowMs_;
    applyStateText(text);
    float tempo = tempo_; int bars = 0;
    { Io io{false, &kv, {}}; io.f("tempo", tempo, 30, 300); io.i("loopBars", bars, 0, 32); }
    s_.loadTape(projectDir(name), bars > 0 ? bars : 4, tempo);
    if (name != "autosave") project_ = name;
    tapeDirty_ = false;
    return true;
}
void SamplerApp::deleteProject(const std::string& rawName) {
    const std::string name = cleanName(rawName);
    if (name.empty() || name == "autosave") return;
    std::error_code ec; fs::remove_all(projectDir(name), ec);
    if (project_ == name) project_ = "autosave";
}

// ---- autosave and the start of the app ----
void SamplerApp::autosave(bool force) {
    if (loading_ || s_.busyJobs() > 0) return;
    const bool rec = st_.freeRecording || st_.freeArmed || st_.bounce > 0; bool clipRec = false; for (int i = 0; i < kTracks; i++) clipRec |= st_.track_clip_rec[i];
    if ((anyRec_ && !rec && !clipRec) || st_.bounceDone != lastBounceSeen_ || std::fabs(st_.loopSamples - lastLoopSeen_) > 1) tapeDirty_ = true;   // a take, an overdub or a bounce just ended
    anyRec_ = rec || clipRec; lastBounceSeen_ = st_.bounceDone; lastLoopSeen_ = st_.loopSamples;
    if (autosaveAt_ == 0) autosaveAt_ = nowMs_ + 20000;   // the first save comes after the start-up restore had its chance
    if (!force && nowMs_ < autosaveAt_) return;
    autosaveAt_ = nowMs_ + 20000;
    if (anyRec_ && !force) return;
    std::error_code ec; fs::create_directories(projectDir("autosave"), ec);
    const std::string text = stateText();
    { std::ofstream f(projectDir("autosave") + "/project.txt.tmp"); if (!f) return; f << text; }
    fs::rename(projectDir("autosave") + "/project.txt.tmp", projectDir("autosave") + "/project.txt", ec);
    if (tapeDirty_ && !anyRec_) { s_.writeTape(projectDir("autosave")); tapeDirty_ = false; }
}
void SamplerApp::restoreLast() {
    const std::string marker = projectsRoot() + "/.loading";
    if (fs::exists(marker)) { std::error_code ec; fs::remove(marker, ec); say("LAST SESSION SKIPPED"); return; }
    if (fs::exists(projectDir("autosave") + "/project.txt")) { loadProject("autosave"); say("BACK WHERE YOU LEFT OFF"); }
}
void SamplerApp::projectTick() {
    if (loading_ && s_.busyJobs() == 0 && st_.sampleState != 2 && nowMs_ - loadedAt_ > 1500) {   // the samples and the tape are in
        loading_ = false; std::error_code ec; fs::remove(projectsRoot() + "/.loading", ec);
        if (project_ == "autosave" || project_.empty()) tapeDirty_ = false;
    }
    autosave(false);
    if (!pendingShare_.empty() && s_.busyJobs() == 0 && nowMs_ >= pendingShareAt_) { sharePath_ = pendingShare_; pendingShare_.clear(); }
}

// ---- exports ----
bool SamplerApp::exportMidi() {
    MidiSong song; song.bpm = st_.loopSamples > 0 ? st_.bpm : tempo_;
    for (int t = 0; t < kTracks; t++) {
        Session::SeqView v[kSeqMax]; const int n = s_.seqNotes(t, v, kSeqMax); std::vector<MidiNote> notes;
        for (int i = 0; i < n; i++) notes.push_back({v[i].tick, v[i].len, v[i].note, v[i].vel});
        song.tracks.push_back(std::move(notes));
    }
    bool any = false; for (auto& t : song.tracks) any |= !t.empty();
    if (!any) { say("NO NOTES TO EXPORT"); return false; }
    std::error_code ec; const std::string dir = s_.dataDir() + "/exports"; fs::create_directories(dir, ec);
    const std::string path = dir + "/" + (project_ == "autosave" ? std::string("patterns") : cleanName(project_)) + ".mid";
    if (!writeMidi(path, song)) { say("CANNOT WRITE THE FILE"); return false; }
    sharePath_ = path; say("MIDI EXPORTED"); return true;
}
bool SamplerApp::exportMix() {
    if (st_.loopSamples <= 0) { say("NO LOOP TO EXPORT"); return false; }
    std::error_code ec; const std::string dir = s_.dataDir() + "/exports/mix"; fs::create_directories(dir, ec);
    s_.writeTape(dir);
    pendingShare_ = dir + "/mix.wav"; pendingShareAt_ = nowMs_ + 300; say("MIXING THE LOOP...");
    return true;
}
bool SamplerApp::importMidi(const std::string& path) {
    MidiSong song;
    if (!readMidi(path, song)) { say("NOT A MIDI FILE"); return false; }
    if (st_.loopSamples <= 0) setTempo(song.bpm);
    int t = track_, used = 0;
    for (auto& notes : song.tracks) {
        if (t >= kTracks) break;
        int end = 0; for (auto& n : notes) end = std::max(end, n.tick + n.len);
        int bars = 1; while (bars < 16 && patternTicks(bars) < end) bars++;
        std::vector<Session::SeqView> v; const int pt = patternTicks(bars);
        for (auto& n : notes) if (n.tick < pt && (int)v.size() < kSeqMax) v.push_back({n.tick, std::min(n.len, pt - n.tick), std::min(127, std::max(0, n.note)), std::min(127, std::max(1, n.vel))});
        ts_[t].seqBars = bars - 1; ts_[t].seqState = 1; applyPattern(t);
        s_.seqSet(t, v.data(), (int)v.size()); t++; used++;
    }
    say(std::to_string(used) + " TRACKS IMPORTED"); return used > 0;
}

// ---- settings: the look of the app, outside of any project ----
void SamplerApp::loadSettings() {
    const KV kv = parse(readFile(s_.dataDir() + "/settings.txt")); Io io{false, &kv, {}};
    int th = 0, fs = 0; io.i("theme", th, 0, theme::kThemes - 1); io.i("fullscreen", fs, 0, 1);
    theme_ = th; fullscreen_ = fs != 0; theme::apply(theme_);
}
void SamplerApp::saveSettings() {
    if (s_.dataDir().empty()) return;
    std::error_code ec; fs::create_directories(s_.dataDir(), ec);
    std::ofstream f(s_.dataDir() + "/settings.txt"); f << "theme=" << theme_ << "\nfullscreen=" << (fullscreen_ ? 1 : 0) << "\n";
}
void SamplerApp::setTheme(int id) { theme_ = std::min(theme::kThemes - 1, std::max(0, id)); theme::apply(theme_); saveSettings(); }
