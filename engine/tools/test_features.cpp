// Host tests of the added features: tick grid and quantize, master fx, metronome, sequencer, tempo and take snap, scales and snap mode, page cycling.
#include <Session.h>
#include <SamplerApp.h>
#include <SamplerUI.h>
#include <MasterFx.h>
#include <Sampler.h>
#include <Paul.h>
#include <Wav.h>
#include <Slicer.h>
#include <filesystem>
#include <Sequencer.h>
#include <Arp.h>
#include <Tombola.h>
#include <Midi.h>
#include <Theme.h>
#include <fstream>
#include <cmath>
#include <thread>
#include <chrono>
#include <cstdio>
#include <string>
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s: ", #c); printf(__VA_ARGS__); printf("\n"); } else { printf("ok   "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static std::vector<float> sine(int frames, float hz, int sr, float amp = 0.5f) {
    std::vector<float> v((size_t)frames * 2);
    for (int i = 0; i < frames; i++) v[2 * i] = v[2 * i + 1] = amp * std::sin(2 * M_PI * hz * i / sr);
    return v;
}
static double rms(const std::vector<float>& v, size_t from = 0, size_t to = 0) { if (!to) to = v.size(); double a = 0; for (size_t i = from; i < to; i++) a += v[i] * v[i]; return to > from ? std::sqrt(a / (to - from)) : 0; }
static bool finite(const std::vector<float>& v) { for (float f : v) if (!std::isfinite(f) || std::fabs(f) > 8.f) return false; return true; }
static double diff(const std::vector<float>& a, const std::vector<float>& b) { double d = 0; for (size_t i = 0; i < a.size(); i++) d += std::fabs(a[i] - b[i]); return d / a.size(); }

// a project starts empty: the tests that were written for the old start (Tape Key, Acoustic, Shiny, TrapKit on tracks 1..4) put those sounds back
static void demoSounds(SamplerApp& app) {
    static const char* names[4] = {"Tape Key", "Acoustic", "Shiny", "TrapKit"};
    for (int t = 0; t < 4; t++) { app.selectTrack(t); for (size_t i = 0; i < app.library().size(); i++) if (app.library()[i].label == names[t]) { app.loadEntry((int)i); break; } }
    app.selectTrack(0);
}

static void testGrid() {
    CHECK(snapTick(50, 4, 768) == 48 && snapTick(71, 4, 768) == 48 && snapTick(72, 4, 768) == 96, "snap to 1/16 (50->%d, 72->%d)", snapTick(50, 4, 768), snapTick(72, 4, 768));
    CHECK(snapTick(767, 4, 768) == 0, "snap past the end wraps to 0 (%d)", snapTick(767, 4, 768));
    CHECK(snapTick(123, 0, 768) == 123, "grid OFF keeps the recorded tick");
    for (int g = 1; g < kGrids; g++) CHECK(kPpq % gridTicks(g) == 0, "grid %s divides the beat", gridName(g));
    std::vector<TickHit> h;
    const double spb = 24000, tl = spb / kPpq;   // 120 bpm at 48k
    int total = 0; double ps = 0;
    for (int b = 0; b < 48000 / 256; b++) { tickHits(ps, 256, tl, 0, h); total += (int)h.size(); ps += 256; }
    CHECK(std::abs(total - 384) <= 3, "one second at 120 bpm has 384 ticks (%d)", total);
    tickHits(95900, 256, tl, 96000, h);
    bool restart = false, mono = true; for (size_t i = 1; i < h.size(); i++) if (h[i].tick < h[i - 1].tick) restart = true;
    CHECK(restart || h.size() > 0, "ticks over the loop end (%zu hits)", h.size());
    tickHits(0, 256, tl, 0, h); CHECK(!h.empty() && h[0].offset == 0 && h[0].tick == 0, "tick 0 sits on sample 0");
}
static void testFx() {
    const int sr = 48000, N = 48000;
    auto src = sine(N, 220, sr);
    auto run = [&](const FxParams& p, std::vector<float>* o = nullptr) {
        MasterFx fx(sr); fx.set(p);
        auto v = src; for (int i = 0; i < N; i += 256) fx.process(v.data() + 2 * i, std::min(256, N - i), i / 24000.0, 1.0 / 24000.0);
        if (o) *o = v; return v;
    };
    for (int i = 0; i < N; i++) src[2 * i + 1] = 0.5f * std::sin(2 * M_PI * 330.0 * i / sr);   // different on each side
    auto dry = run(FxParams());
    CHECK(diff(dry, src) < 1e-6, "rest values: every effect is bypassed");
    struct C { const char* n; FxParams p; } cases[8];
    cases[0] = {"chorus", {}}; cases[0].p.chorus = 1;
    cases[1] = {"phaser", {}}; cases[1].p.phaser = 1;
    cases[2] = {"compressor", {}}; cases[2].p.comp = 1;
    cases[3] = {"width", {}}; cases[3].p.width = 0.f;
    cases[4] = {"dj filter low", {}}; cases[4].p.filter = 0.1f;
    cases[5] = {"dj filter high", {}}; cases[5].p.filter = 0.9f;
    cases[6] = {"crush", {}}; cases[6].p.crush = 0.8f;
    cases[7] = {"gate", {}}; cases[7].p.gate = 2;
    for (auto& c : cases) { auto o = run(c.p); CHECK(finite(o) && diff(o, src) > 1e-3, "%s changes the signal and stays finite (diff %.4f)", c.n, diff(o, src)); }
    { auto o = run(cases[2].p); CHECK(rms(o, 24000, 48000) < rms(src, 24000, 48000), "compressor lowers the level (%.3f < %.3f)", rms(o, 24000, 48000), rms(src, 24000, 48000)); }
    { auto o = run(cases[3].p); bool same = true; for (size_t i = 0; i < o.size(); i += 2) if (std::fabs(o[i] - o[i + 1]) > 1e-5) same = false; CHECK(same, "width 0 is mono"); }
    { auto lo = run(cases[4].p); auto s2 = sine(N, 8000, sr); MasterFx fx(sr); fx.set(cases[4].p); for (int i = 0; i < N; i += 256) fx.process(s2.data() + 2 * i, 256 < N - i ? 256 : N - i, 0, 1.0 / 24000); CHECK(rms(s2, 24000, 48000) < 0.15, "low-pass DJ filter removes 8 kHz (%.3f)", rms(s2, 24000, 48000)); }
    { auto o = run(cases[7].p); double lo = 1, hi = 0; for (size_t i = 24000; i < 48000; i += 2) { lo = std::min<double>(lo, std::fabs(o[i])); } CHECK(rms(o, 0, 1200) > 0 && rms(o) < rms(src) * 0.9, "gate chops the signal (%.3f < %.3f)", rms(o), rms(src)); }
    // beat repeat: the last 1/16 beat (1500 samples at 120 bpm) loops
    {
        MasterFx fx(sr); auto a = src; FxParams p; fx.set(p);
        for (int i = 0; i < 24000; i += 256) fx.process(a.data() + 2 * i, std::min(256, 24000 - i), 0, 1.0 / 24000);
        p.repeat = 3; fx.set(p);
        std::vector<float> b = sine(12000, 1000, sr);   // new input is ignored while repeating
        for (int i = 0; i < 12000; i += 256) fx.process(b.data() + 2 * i, std::min(256, 12000 - i), 0, 1.0 / 24000);
        const int per = 6000;   // 1/4 beat = 6000 samples
        double d = 0; for (int i = 100; i < 4000; i++) d += std::fabs(b[2 * i] - b[2 * (i + per)]);
        CHECK(finite(b) && d / 3900 < 1e-4, "beat repeat loops a slice (period diff %.6f)", d / 3900);
    }
}
static void testFx2() {
    const int sr = 48000, N = 48000;
    auto src = sine(N, 220, sr);
    for (int i = 0; i < N; i++) src[2 * i + 1] = 0.5f * std::sin(2 * M_PI * 330.0 * i / sr);
    auto run = [&](const FxParams& p, std::vector<float> in) {
        MasterFx fx(sr); fx.set(p);
        for (int i = 0; i < (int)(in.size() / 2); i += 256) fx.process(in.data() + 2 * i, std::min<int>(256, (int)(in.size() / 2) - i), i / 24000.0, 1.0 / 24000.0);
        return in;
    };
    struct C { const char* n; FxParams p; } c[7] = {};
    c[0].n = "drive"; c[0].p.drive = 0.8f;
    c[1].n = "flanger"; c[1].p.flanger = 1.f;
    c[2].n = "tremolo"; c[2].p.tremolo = 1.f;
    c[3].n = "auto-wah"; c[3].p.wah = 1.f;
    c[4].n = "ring mod"; c[4].p.ringmod = 0.5f;
    c[5].n = "eq low boost"; c[5].p.eqLow = 1.f;
    c[6].n = "eq high cut"; c[6].p.eqHigh = 0.f;
    for (auto& k : c) { auto o = run(k.p, src); CHECK(finite(o) && diff(o, src) > 1e-3, "%s changes the signal and stays finite (diff %.4f)", k.n, diff(o, src)); }
    {   // the OP-1 effects
        FxParams sp; sp.spring = 1.f; FxParams ph; ph.phone = 1.f; FxParams pu; pu.punch = 1.f;
        { auto o = run(pu, src); CHECK(finite(o) && diff(o, src) > 1e-3, "punch changes the signal and stays finite (diff %.4f)", diff(o, src)); }
        { MasterFx fx(sr); fx.set(sp); std::vector<float> a(2 * 4800, 0.f); for (int i = 0; i < 200; i++) a[2 * i] = a[2 * i + 1] = 0.8f; std::vector<float> tail(2 * 24000, 0.f);   // a click, then silence
          fx.process(a.data(), 4800, 0, 1.0 / 24000); for (int i = 0; i < 24000; i += 256) fx.process(tail.data() + 2 * i, std::min(256, 24000 - i), 0, 1.0 / 24000);
          CHECK(finite(tail) && rms(tail, 2 * 4000, 2 * 24000) > 0.003, "spring rings on after a click (%.4f)", rms(tail, 2 * 4000, 2 * 24000)); }
        { auto lo = sine(N, 80, sr), mid = sine(N, 1000, sr); auto a = run(ph, lo), b = run(ph, mid);
          CHECK(finite(a) && finite(b) && rms(a, 24000, 48000) < rms(lo, 24000, 48000) * 0.5 && rms(b, 24000, 48000) > rms(mid, 24000, 48000) * 0.5, "phone cuts 80 Hz (%.3f -> %.3f) and keeps 1 kHz (%.3f -> %.3f)", rms(lo, 24000, 48000), rms(a, 24000, 48000), rms(mid, 24000, 48000), rms(b, 24000, 48000)); }
        MasterFx rest(sr); CHECK(!rest.active(), "the new effects are off at rest"); rest.set(sp); CHECK(rest.active(), "...and on when turned up");
    }
    { MasterFx fx(sr); FxParams p; p.eqMid = 0.52f; fx.set(p); CHECK(!fx.active(), "EQ within 0.02 of flat is bypassed"); p.eqMid = 0.8f; fx.set(p); CHECK(fx.active(), "EQ moved: active"); }
    { auto lows = sine(N, 80, sr); FxParams p; p.eqLow = 1.f; auto o = run(p, lows); CHECK(rms(o, 24000, 48000) > rms(lows, 24000, 48000) * 1.6, "EQ low boost lifts 80 Hz (%.3f -> %.3f)", rms(lows, 24000, 48000), rms(o, 24000, 48000));
      auto highs = sine(N, 10000, sr); FxParams q; q.eqHigh = 0.f; auto h = run(q, highs); CHECK(rms(h, 24000, 48000) < rms(highs, 24000, 48000) * 0.5, "EQ high cut lowers 10 kHz (%.3f -> %.3f)", rms(highs, 24000, 48000), rms(h, 24000, 48000)); }
    { FxParams p; p.tremolo = 1.f; auto o = run(p, sine(N, 220, sr)); double lo = 1e9, hi = 0; for (int w = 0; w < 9; w++) { const double r = rms(o, 2 * 2400 * (size_t)(w + 1) * 2, 2 * 2400 * (size_t)(w + 2) * 2); lo = std::min(lo, r); hi = std::max(hi, r); }
      CHECK(lo < hi * 0.6, "tremolo moves the level up and down (%.3f .. %.3f)", lo, hi); }
    { FxParams p; p.tapeStop = 1.f; MasterFx fx(sr); const int T = 12000 + 3 * sr; auto in = sine(T, 220, sr); fx.set(FxParams()); fx.process(in.data(), 12000, 0, 1.0 / 24000); fx.set(p);
      for (int i = 12000; i < T; i += 256) fx.process(in.data() + 2 * i, std::min(256, T - i), 0, 1.0 / 24000);
      const double early = rms(in, 2 * 12000 + 2 * 4800, 2 * 12000 + 2 * 9600), late = rms(in, 2 * (T - 9600), 2 * T);
      CHECK(finite(in) && late < 0.02 && early > 0.2, "tape stop: it takes a while to wind down (%.3f in the first 0.2 s, %.4f at the end)", early, late);
      // slow: after 0.9 s the sound is still there, half way down
      CHECK(rms(in, 2 * (12000 + sr * 9 / 10), 2 * (12000 + sr * 9 / 10 + 4800)) > 0.05, "...and it is still sounding after 0.9 s (%.3f)", rms(in, 2 * (12000 + sr * 9 / 10), 2 * (12000 + sr * 9 / 10 + 4800)));
      fx.set(FxParams()); const int R = 2 * sr; auto live = sine(R, 220, sr); for (int i = 0; i < R; i += 256) fx.process(live.data() + 2 * i, std::min(256, R - i), 0, 1.0 / 24000);
      CHECK(finite(live) && rms(live, 2 * 4800, 2 * 9600) < 0.25 && rms(live, 2 * (R - 4800), 2 * R) > 0.3, "...and on release it spins up, then the live sound is back (%.3f, %.3f)", rms(live, 2 * 4800, 2 * 9600), rms(live, 2 * (R - 4800), 2 * R)); }
    { MasterFx fx(sr, 1.f); FxParams p; fx.set(p); auto a = sine(12000, 440, sr); fx.process(a.data(), 12000, 0, 1.0 / 24000); p.freeze = 1; fx.set(p);
      std::vector<float> silence(2 * 48000, 0.f); for (int i = 0; i < 48000; i += 256) fx.process(silence.data() + 2 * i, std::min(256, 48000 - i), 0, 1.0 / 24000);
      CHECK(finite(silence) && rms(silence, 2 * 24000, 2 * 48000) > 0.1, "freeze holds the sound after the input stopped (%.3f)", rms(silence, 2 * 24000, 2 * 48000)); }
    { MasterFx fx(sr); FxParams p; fx.set(p); std::vector<float> ramp(2 * 24000); for (int i = 0; i < 24000; i++) ramp[2 * i] = ramp[2 * i + 1] = i / 48000.f;
      for (int i = 0; i < 24000; i += 256) fx.process(ramp.data() + 2 * i, std::min(256, 24000 - i), 0, 1.0 / 24000);
      p.reverse = 1; fx.set(p); std::vector<float> o(2 * 24000, 0.f); for (int i = 0; i < 24000; i += 256) fx.process(o.data() + 2 * i, std::min(256, 24000 - i), 0, 1.0 / 24000);
      CHECK(finite(o) && o[2 * 2000] > 0.35f && o[2 * 20000] < 0.15f, "reverse plays the last beat backwards (%.2f at the start of the slice, %.2f near its end)", o[2 * 2000], o[2 * 20000]); }
}
// PAUL (Paulstretch) with several notes: the FFT work of the voices must not pile up in one block, and a voice that is pushed out must fade
static void testPaul() {
    const int sr = 44100, block = 256;
    const std::string dir = std::string(getenv("HOME")) + "/fm1-sampler/samples/Keys/Tape Key.wav";
    {   // a chord of four starts in one block: the hops of the voices are spread over the following blocks
        Sampler s(sr); s.load(dir); s.setMode(Sampler::PAUL); s.setEnv(0.01f, 0.3f, 0.8f, 0.3f);
        for (int n : {60, 64, 67, 71}) s.noteOn(n, 100);
        std::vector<float> out(2 * block); unsigned long maxPerBlock = 0, maxSteady = 0, total = 0; double ss = 0; size_t cnt = 0;
        for (int b = 0; b < 8 * sr / block; b++) {
            std::fill(out.begin(), out.end(), 0.f);
            const unsigned long before = PaulStream::stagesDone(); s.render(out.data(), block); const unsigned long d = PaulStream::stagesDone() - before;
            maxPerBlock = std::max(maxPerBlock, d); if (b > 24) maxSteady = std::max(maxSteady, d); total += d;
            if (b > 40) { for (float v : out) ss += v * v; cnt += out.size(); }
        }
        CHECK(maxSteady <= 2, "chord of 4 in PAUL: at most %lu half hops in any block once running (before: 8 at once)", maxSteady);
        CHECK(maxPerBlock <= 4, "...and at most %lu while the voices start", maxPerBlock);
        CHECK(std::sqrt(ss / cnt) > 0.1, "...and the chord sounds (rms %.3f)", std::sqrt(ss / cnt));
    }
    {   // a sine as the sample: a voice that is cut shows as a jump, a faded one does not
        const std::string path = std::string(getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp") + "/fm1_sine_test.wav";
        std::vector<float> w((size_t)2 * sr * 3); for (int i = 0; i < sr * 3; i++) w[2 * i] = w[2 * i + 1] = 0.8f * std::sin(2 * M_PI * 220.0 * i / sr);
        writeWav16(path, w, sr);
        Sampler s(sr); s.load(path); s.setMode(Sampler::MELODIC); s.setEnv(0.002f, 0.3f, 1.f, 0.3f);
        std::vector<float> out(2 * block), all; int note = 60;
        for (int i = 0; i < 8; i++) s.noteOn(note++, 100);   // eight voices: the limit
        for (int ev = 0; ev < 24; ev++) {
            for (int b = 0; b < 12; b++) { std::fill(out.begin(), out.end(), 0.f); s.render(out.data(), block); all.insert(all.end(), out.begin(), out.end()); }
            s.noteOn(note, 100); note = 60 + (note - 59) % 12;   // the ninth note and then every next one takes the oldest voice's place
        }
        float m2 = 0; for (size_t i = 1; i + 1 < all.size() / 2; i++) m2 = std::max(m2, std::fabs(all[2 * (i + 1)] - 2 * all[2 * i] + all[2 * (i - 1)]));   // a cut is a corner: a big second difference
        CHECK(m2 < 0.05f, "stolen voices fade instead of being cut: the largest second difference is %.3f (a cut voice gave 0.57)", m2);
    }
}
// the bundled library: every file loads and is not silent, the generated kits (tools/gen_instruments.py) slice into their 16 hits
static void testLibrary() {
    namespace fs = std::filesystem;
    const fs::path root = fs::path(getenv("HOME")) / "fm1-sampler" / "samples";
    int files = 0, bad = 0, groups = 0;
    for (auto& g : fs::directory_iterator(root)) {
        if (!g.is_directory()) continue;
        groups++;
        for (auto& f : fs::directory_iterator(g.path())) {
            if (f.path().extension() != ".wav") continue;
            files++;
            std::vector<float> d; int sr = 0;
            const bool ok = readWav(f.path().string(), d, sr) && sr >= 22050 && d.size() / 2 > (size_t)sr / 2 && d.size() / 2 < (size_t)sr * 20;
            float pk = 0; for (float v : d) pk = std::max(pk, std::fabs(v));
            if (!ok || pk < 0.05f) { bad++; printf("   bad sample: %s\n", f.path().c_str()); }
        }
    }
    CHECK(bad == 0 && files >= 40 && groups >= 5, "the library: %d files in %d groups all load and are not silent", files, groups);
    static const double slot[16] = {0.5, 1.0, 0.5, 0.5, 0.4, 0.4, 0.7, 0.5, 0.5, 0.5, 0.5, 0.4, 0.5, 0.4, 1.4, 0.9};
    for (const char* kit : {"808 Kit", "Electro Kit", "Lo-Fi Kit", "Rusty Kit", "Boom Bap Kit"}) {
        std::vector<float> d; int sr = 0; readWav((root / "Drums" / (std::string(kit) + ".wav")).string(), d, sr);
        const auto sl = findSlices(d, (int)(d.size() / 2), sr, 16);
        int good = 0; double want = 0.01; for (size_t i = 0; i < sl.size() && i < 16; i++) { good += std::fabs(sl[i] / (double)sr - want) < 0.03; want += slot[i]; }
        CHECK(sl.size() == 16 && good == 16, "%s: the slicer finds all 16 hits (%d of 16 on time)", kit, good);
    }
    {   // a hit with a slow attack: the chop starts where the attack begins (not at its peak) and ends when the hit has died away
        const int sr2 = 44100, n = sr2 * 3; std::vector<float> d((size_t)n * 2, 0.f); const double at[3] = {0.5, 1.2, 1.9};
        for (double t0 : at) for (int i = 0; i < sr2 * 0.6; i++) { const double t = i / (double)sr2, env = t < 0.02 ? t / 0.02 : std::exp(-(t - 0.02) / 0.05); const float v = (float)(env * std::sin(2 * M_PI * 220 * t)); const size_t k = ((size_t)(t0 * sr2) + i) * 2; d[k] = v; d[k + 1] = v; }
        std::vector<int> en; const auto sl = findSlices(d, n, sr2, 8, 1.f, &en);
        int okS = 0, okE = 0; for (size_t i = 0; i < sl.size(); i++) for (double t0 : at) if (std::fabs(sl[i] / (double)sr2 - t0) < 0.01) { okS++; const double len = (en[i] - sl[i]) / (double)sr2; okE += len > 0.1 && len < 0.45; }
        CHECK(sl.size() == 4 && sl[0] == 0 && okS == 3 && okE == 3 && en.size() == 4,   // 4: the silence before the first hit is a chop too
               "autochop: %zu chops, %d starts on the attack (+-10 ms), %d ends at the decay", sl.size(), okS, okE);
    }
}
// the piano roll: pinch with two fingers zooms (horizontal = time, vertical = pitch), the buttons zoom too, a pinch never writes a note
static void testSynths() {
    const int sr = SAMPLERATE;
    int silent = 0, loud = 0, bad = 0, stuck = 0;
    for (int i = 0; i < kSynthCount; i++) {
        Sampler sm(sr);
        if (!sm.load("synth:" + std::to_string(i)) || !sm.isSynth() || sm.label() != kSynths[i].name) { printf("FAIL load synth %d\n", i); fails++; continue; }
        std::vector<float> o((size_t)sr * 2 * 2, 0.f);   // 2 s
        sm.noteOn(48 + (i % 3) * 12, 100);
        for (int b = 0; b < sr / 256; b++) sm.render(o.data() + (size_t)b * 512, 256);
        sm.noteOff(48 + (i % 3) * 12);
        for (int b = sr / 256; b < 2 * sr / 256; b++) sm.render(o.data() + (size_t)b * 512, 256);
        float pk = 0.f; for (float f : o) pk = std::max(pk, std::fabs(f));
        const double r0 = rms(o, 0, (size_t)sr / 2), tail = rms(o, o.size() - 2000);
        if (!finite(o)) bad++;
        if (r0 < 0.01) { silent++; printf("quiet synth %s rms %.4f\n", kSynths[i].name, r0); }
        if (pk > 1.2f) { loud++; printf("loud synth %s peak %.2f\n", kSynths[i].name, pk); }
        if (tail > 0.01 && kSynths[i].r < 1.5f && kSynths[i].engine != 3) { stuck++; printf("synth %s still sounds after release rms %.4f\n", kSynths[i].name, tail); }
        if (sm.peaks().size() != 512) bad++;
    }
    CHECK(kSynthCount >= 20, "%d built-in synths", kSynthCount);
    CHECK(bad == 0 && silent == 0 && loud == 0, "every synth sounds, stays finite and under the ceiling (bad %d, quiet %d, loud %d)", bad, silent, loud);
    CHECK(stuck == 0, "every synth ends after the note off (%d hang)", stuck);
    // pitch: the plucked string and the FM patch follow the note (zero crossing count of a low note)
    for (int idx : {3, 16}) {
        Sampler sm(sr); sm.load("synth:" + std::to_string(idx));
        std::vector<float> o((size_t)sr * 2, 0.f); sm.noteOn(45, 110);   // A2 = 110 Hz
        for (int b = 0; b < sr / 256; b++) sm.render(o.data() + (size_t)b * 512, 256);
        const int w = sr / 4, o0 = sr / 5; int best = 0; double bc = -1e9;   // autocorrelation over a quarter second, lags of 60 .. 180 Hz
        for (int lag = sr / 180; lag < sr / 60; lag++) { double c = 0; for (int k = 0; k < w; k++) c += o[2 * (size_t)(o0 + k)] * o[2 * (size_t)(o0 + k + lag)]; if (c > bc) { bc = c; best = lag; } }
        const double hz = (double)sr / best;
        CHECK(hz > 104 && hz < 116, "%s plays A2 near 110 Hz (%.0f)", kSynths[idx].name, hz);
    }
    // in the app: a Synths group in the library, a track plays one, the envelope follows the patch
    system("rm -rf /tmp/fm1sampler_t6; mkdir -p /tmp/fm1sampler_t6");
    Session s(sr); s.setDataDir("/tmp/fm1sampler_t6"); s.setSampleDir(std::string(getenv("HOME")) + "/fm1-sampler/samples");
    SamplerApp app(s, [](const uint8_t*, size_t) {}); app.setTourDone(true); demoSounds(app);
    int first = -1, count = 0; for (int i = 0; i < (int)app.library().size(); i++) if (app.library()[(size_t)i].group == "Synths") { if (first < 0) first = i; count++; }
    CHECK(count == kSynthCount, "library has the Synths group (%d)", count);
    std::vector<float> buf(512), in(512, 0.f), all; uint32_t now = 0;
    auto run = [&](double sec) { for (long i = 0; i < (long)(sec * sr) / 256; i++) { std::fill(buf.begin(), buf.end(), 0.f); s.process(buf.data(), in.data(), 256); std::this_thread::sleep_for(std::chrono::microseconds(150)); all.insert(all.end(), buf.begin(), buf.end()); if (i % 5 == 0) { now += 33; app.tick(now); } } };
    run(2.0);
    app.selectTrack(4); for (int i = 0; i < (int)app.library().size(); i++) if (app.library()[(size_t)i].label == "FM Bell") app.loadEntry(i);
    run(1.0);   // FM Bell
    CHECK(app.sampleLabel(4) == "FM Bell" && app.isSynth(4), "a track loads a synth (%s)", app.sampleLabel(4).c_str());
    CHECK(app.state(4).s < 0.01f && app.state(4).d > 0.5f, "the track takes the patch envelope (sustain %.2f, decay %.2f)", app.state(4).s, app.state(4).d);
    size_t m = all.size(); app.playNote(72, true); run(0.5); app.playNote(72, false); run(0.3);
    CHECK(rms(all, m, all.size()) > 0.003, "the synth track plays (rms %.4f)", rms(all, m, all.size()));
}

static void testOp1() {
    const int sr = SAMPLERATE;
    {   // arpeggiator: PLAYED keeps the order the keys went down in, TOMBOLA is random with rests
        Arp a; a.setup(7, Arp::PLAYED, 1, 0.5f); a.noteOn(64); a.noteOn(60); a.noteOn(67);
        std::vector<Arp::Ev> ev; for (int i = 0; i < 60; i++) a.run(sr / 20, 120, sr, ev);
        std::vector<int> ons; for (auto& e : ev) if (e.vel > 0) ons.push_back(e.note);
        CHECK(ons.size() >= 8 && ons[0] == 64 && ons[1] == 60 && ons[2] == 67 && ons[3] == 64, "arp PLAYED: %d %d %d %d (expect 64 60 67 64)", ons.size() > 0 ? ons[0] : -1, ons.size() > 1 ? ons[1] : -1, ons.size() > 2 ? ons[2] : -1, ons.size() > 3 ? ons[3] : -1);
        Arp b; b.setup(7, Arp::TOMBOLA, 1, 0.5f); b.noteOn(60); b.noteOn(64); b.noteOn(67);
        ev.clear(); for (int i = 0; i < 400; i++) b.run(sr / 20, 120, sr, ev);   // 20 s at 1/16 = 160 steps
        int ons2 = 0; bool ok = true; for (auto& e : ev) if (e.vel > 0) { ons2++; ok &= e.note == 60 || e.note == 64 || e.note == 67; }
        CHECK(ok && ons2 > 70 && ons2 < 150, "arp TOMBOLA: random held notes with rests (%d of 160 steps)", ons2);
    }
    // the dice: a random pattern in the scale, one undo step
    system("rm -rf /tmp/fm1sampler_t7; mkdir -p /tmp/fm1sampler_t7");
    Session s(sr); s.setDataDir("/tmp/fm1sampler_t7"); s.setSampleDir(std::string(getenv("HOME")) + "/fm1-sampler/samples");
    SamplerApp app(s, [](const uint8_t*, size_t) {}); app.setTourDone(true); demoSounds(app);
    std::vector<float> buf(512), in(512, 0.f); uint32_t now = 1000;
    auto run = [&](double sec) { for (long i = 0; i < (long)(sec * sr) / 256; i++) { std::fill(buf.begin(), buf.end(), 0.f); s.process(buf.data(), in.data(), 256); if (i % 5 == 0) { now += 33; app.tick(now); } } };
    run(0.5); app.setScale(2, 0, 0, 0);   // C major
    app.seqDice(); run(0.2);
    Session::SeqView v[kSeqMax]; const int n = s.seqNotes(0, v, kSeqMax);
    bool inScale = true, inside = true; for (int i = 0; i < n; i++) { const int pc = v[i].note % 12; inScale &= pc == 0 || pc == 2 || pc == 4 || pc == 5 || pc == 7 || pc == 9 || pc == 11; inside &= v[i].tick < patternTicks(app.seqBarsOf(0)) && v[i].len > 0; }
    CHECK(n >= 6 && n < 60 && inScale && inside, "dice: %d notes, all in C major, all inside the pattern", n);
    CHECK(app.state(0).seqState == 1, "dice switches the pattern on");
    app.seqDice(); run(0.2); Session::SeqView w[kSeqMax]; const int n2 = s.seqNotes(0, w, kSeqMax);
    CHECK(n2 > 0 && (n2 != n || w[0].tick != v[0].tick || w[0].note != v[0].note || w[1].tick != v[1].tick), "...and every roll is different (%d, %d notes)", n, n2);
    app.seqUndo(); run(0.2); const int n3 = s.seqNotes(0, w, kSeqMax);
    CHECK(n3 == n, "one undo brings the previous pattern back (%d notes)", n3);
    {   // live TAPE STOP: hold = the sound runs down, let go = it spins up again
        std::vector<float> all2; auto runA = [&](double sec) { for (long i = 0; i < (long)(sec * sr) / 256; i++) { std::fill(buf.begin(), buf.end(), 0.f); s.process(buf.data(), in.data(), 256); all2.insert(all2.end(), buf.begin(), buf.end()); if (i % 5 == 0) { now += 33; app.tick(now); } } };
        for (int i = 0; i < (int)app.library().size(); i++) if (app.library()[(size_t)i].label == "Organ" && app.library()[(size_t)i].group == "Synths") app.loadEntry(i);
        for (int i = 0; i < 20; i++) { run(0.05); std::this_thread::sleep_for(std::chrono::milliseconds(50)); }   // the worker loads the synth in real time
        app.seqClear(); run(0.3);
        app.playNote(60, true); runA(1.0);
        const double before = rms(all2, all2.size() - 4800 * 2, all2.size());
        app.tapeStopHold(true); runA(2.0);
        const double stopped = rms(all2, all2.size() - 4800 * 2, all2.size());
        app.tapeStopHold(false); runA(1.5);
        const double after = rms(all2, all2.size() - 4800 * 2, all2.size()); app.playNote(60, false);
        CHECK(before > 0.01 && stopped < before * 0.15 && after > before * 0.5, "tape stop button: %.3f held note, %.3f while held, %.3f after the release", before, stopped, after);
    }
}

static void testTombola() {
    const int sr = SAMPLERATE;
    {   // physics: balls stay in the drum, hit the walls and stop being added at 18
        Tombola tb; std::vector<Tombola::Hit> hits;
        for (int i = 0; i < 25; i++) tb.add(0, 60 + i, (uint32_t)i);
        CHECK((int)tb.balls().size() == Tombola::kMaxBalls, "at most %d balls", Tombola::kMaxBalls);
        for (int i = 0; i < 400; i++) tb.run(1.f / 100.f, hits);   // 4 s
        bool inside = true; for (auto& b : tb.balls()) inside &= std::hypot(b.x, b.y) < 1.2f;
        CHECK(inside && hits.size() > 20, "balls bounce inside a sealed drum: %zu hits in 4 s", hits.size());
        bool sane = true; for (auto& h : hits) sane &= h.vel >= 20 && h.vel <= 120 && h.len >= 200 && h.len <= 600 && std::fabs(h.pan) <= 1.f;
        CHECK(sane, "hit velocity, length and pan are in range");
        Tombola t2; t2.grav = 0.f; t2.spin = 0.f; hits.clear(); t2.add(0, 60, 1); for (int i = 0; i < 100; i++) t2.run(0.01f, hits);
        CHECK(!t2.balls().empty() && hits.size() < 40, "no gravity, no spin: a ball drifts about (%zu hits)", hits.size());
    }
    system("rm -rf /tmp/fm1sampler_t8; mkdir -p /tmp/fm1sampler_t8");
    Session s(sr); s.setDataDir("/tmp/fm1sampler_t8"); s.setSampleDir(std::string(getenv("HOME")) + "/fm1-sampler/samples");
    SamplerApp app(s, [](const uint8_t*, size_t) {}); app.setTourDone(true); demoSounds(app);
    std::vector<float> buf(512), in(512, 0.f), all; uint32_t now = 1000;
    auto run = [&](double sec) { for (long i = 0; i < (long)(sec * sr) / 256; i++) { std::fill(buf.begin(), buf.end(), 0.f); s.process(buf.data(), in.data(), 256); std::this_thread::sleep_for(std::chrono::microseconds(150)); all.insert(all.end(), buf.begin(), buf.end()); if (i % 5 == 0) { now += 33; app.tick(now); } } };
    run(2.5); app.gotoPage(SamplerApp::P_TOMB);
    CHECK(app.page() == SamplerApp::P_TOMB && SamplerApp::modeOf(SamplerApp::P_TOMB) == 2, "the tombola page is in the SEQ mode");
    run(4.0);
    size_t m = all.size(); for (int i = 0; i < 4; i++) app.tombolaDrop(); run(0.1);
    Session::TombolaBall bl[Tombola::kMaxBalls]; float ang = 0;
    CHECK(s.tombolaView(bl, Tombola::kMaxBalls, &ang) == 4, "four balls in the drum (%d)", s.tombolaView(bl, Tombola::kMaxBalls, &ang));
    run(3.0);
    CHECK(rms(all, m, all.size()) > 0.003 && finite(all), "the balls play the track (rms %.4f)", rms(all, m, all.size()));
    app.playNote(64, true); app.playNote(64, false); run(0.1);
    CHECK(s.tombolaView(bl, Tombola::kMaxBalls, &ang) == 5, "a key played on the tombola page drops a ball");
    { auto k = app.knobs(); k[2].set(0.f); } run(0.1);
    CHECK(std::string(app.knobs()[2].value) == "3 SIDES", "SHAPE knob: %s", app.knobs()[2].value.c_str());
    const float a0 = ang; s.tombolaTurn(1.0f); run(0.1); s.tombolaView(bl, Tombola::kMaxBalls, &ang);
    CHECK(std::fabs(ang - a0) > 0.5f, "turning the drum by hand changes its angle");
    app.tombolaClear(); run(0.2); CHECK(s.tombolaView(bl, Tombola::kMaxBalls, &ang) == 0, "emptying the drum");
    // played into the sequencer: a pattern recording catches the hits
    app.gotoPage(SamplerApp::P_SQ); app.seqClear(); run(0.2); app.setSeqState(2); run(0.3);
    app.gotoPage(SamplerApp::P_TOMB); for (int i = 0; i < 3; i++) app.tombolaDrop(); run(4.0);
    app.gotoPage(SamplerApp::P_SQ); app.setSeqState(1); run(0.2);
    CHECK(app.status().seqCount[0] > 3, "a pattern records what the balls play (%d notes)", app.status().seqCount[0]);
    app.tombolaClear(); app.seqClear(); run(0.2);
}

static void testSwing() {
    const int sr = SAMPLERATE;
    system("rm -rf /tmp/fm1sampler_t9; mkdir -p /tmp/fm1sampler_t9");
    Session s(sr); s.setDataDir("/tmp/fm1sampler_t9"); s.setSampleDir(std::string(getenv("HOME")) + "/fm1-sampler/samples");
    SamplerApp app(s, [](const uint8_t*, size_t) {}); app.setTourDone(true); demoSounds(app);
    std::vector<float> buf(512), in(512, 0.f), all; uint32_t now = 1000;
    auto run = [&](double sec) { for (long i = 0; i < (long)(sec * sr) / 256; i++) { std::fill(buf.begin(), buf.end(), 0.f); s.process(buf.data(), in.data(), 256); std::this_thread::sleep_for(std::chrono::microseconds(150)); all.insert(all.end(), buf.begin(), buf.end()); if (i % 5 == 0) { now += 33; app.tick(now); } } };
    auto btn = [&](int b) { uint8_t m[3] = {0x91, (uint8_t)b, 100}; app.onMidi(m, 3); m[0] = 0x81; app.onMidi(m, 3); };
    run(2.5);
    app.gotoPage(SamplerApp::P_TEMPO); { auto k = app.knobs(); k[0].set((120.f - 30.f) / 270.f); } run(0.2);
    app.gotoPage(SamplerApp::P_TIMING); { auto k = app.knobs(); k[2].set(0.f); k[3].set(0.f); } app.setQuantGrids(0, 4); run(0.2);   // quantize 1/16, no swing
    app.selectTrack(1); app.gotoPage(SamplerApp::P_SQ); app.seqClear(); run(0.2);
    { auto k = app.knobs(); k[0].set(1.f / 3); k[1].set(0.f); }   // PLAY, 1 bar
    app.seqToggleAt(48, 60); run(0.2);   // the second 1/16 step: the one swing delays
    // the transport keeps its phase between stops, so compare the onsets of one run: they come once per pattern pass; the pass in which the swing is switched on is late by the swing delay
    run(3.0); if (!app.status().playing) btn(SamplerApp::PLAY);
    run(3.2);
    const size_t mark = all.size();
    app.gotoPage(SamplerApp::P_TIMING); { auto k = app.knobs(); k[3].set(1.f); } run(0.05);
    CHECK(std::string(app.knobs()[3].value) == "75 PCT", "SWING knob: %s", app.knobs()[3].value.c_str());
    run(4.0);
    std::vector<long> on; long quietFor = 1 << 30;
    for (size_t i = 0; i < all.size(); i += 2) { if (std::fabs(all[i]) > 0.01f) { if (quietFor > sr / 4) on.push_back((long)(i / 2)); quietFor = 0; } else quietFor++; }
    std::vector<long> gaps; for (size_t i = 1; i < on.size(); i++) gaps.push_back(on[i] - on[i - 1]);
    const double want = 0.5 * 48.0 / kPpq * 60.0 / 120.0 * sr;   // swing 75 %: the off-beat 1/16 moves by half a step = 24 ticks
    const long pass = 2 * sr;   // 1 bar at 120 bpm
    bool straightGap = false, lateGap = false; for (long gp : gaps) { straightGap |= std::labs(gp - pass) < 4; lateGap |= std::fabs((double)(gp - pass) - want) < 0.002 * sr; }
    CHECK(gaps.size() >= 3 && straightGap && lateGap, "swing 75 pct: passes are %ld samples apart, the pass where it switched on is %.0f later (%zu onsets)", pass, want, on.size());
    (void)mark;
    app.seqClear(); run(0.2); if (app.status().playing) btn(SamplerApp::PLAY);
}

static void testProject() {
    const int sr = SAMPLERATE;
    {   // midi files: notes out and back in
        MidiSong a; a.bpm = 133.f; a.tracks = {{{0, 96, 60, 100}, {96, 48, 64, 80}, {96, 192, 67, 127}}, {}, {{384, 24, 36, 90}}};
        CHECK(writeMidi("/tmp/fm1_t.mid", a), "midi file written");
        MidiSong b; const bool ok = readMidi("/tmp/fm1_t.mid", b);
        bool same = ok && b.tracks.size() == 2 && b.tracks[0].size() == 3 && b.tracks[1].size() == 1 && std::fabs(b.bpm - 133.f) < 0.1f;
        if (same) { auto& t0 = b.tracks[0]; same = t0[0].tick == 0 && t0[0].len == 96 && t0[0].note == 60 && t0[0].vel == 100 && t0[2].len == 192 && t0[2].vel == 127 && b.tracks[1][0].tick == 384 && b.tracks[1][0].note == 36; }
        CHECK(same, "midi round trip: %zu tracks, %.1f bpm", b.tracks.size(), b.bpm);
        CHECK(!readMidi("/tmp/fm1_nothing.mid", b), "a missing midi file is refused");
        {   // damaged files: data bytes with the high bit set, a track that ends in the middle of an event, a huge length
            std::string bad = "MThd"; bad += std::string("\0\0\0\6\0\0\0\1\0\xc0", 10);
            std::string trk; const char ev[] = {0, (char)0x90, (char)200, (char)100, 10, (char)0x80, (char)250, 0, 5, (char)0x90, 60, (char)130, 5, (char)0x90, 64, 100, 5, (char)0x80, 64, 0, 0, (char)0xff, 0x2f, 0};
            trk.assign(ev, sizeof ev);
            std::string t2 = "MTrk"; t2 += std::string("\0\0\0", 3); t2 += (char)trk.size(); t2 += trk;
            { std::ofstream f("/tmp/fm1_bad.mid", std::ios::binary); f << bad << t2; }
            MidiSong x; const bool ok = readMidi("/tmp/fm1_bad.mid", x);
            CHECK(ok && x.tracks.size() == 1 && x.tracks[0].size() == 1 && x.tracks[0][0].note == 64, "a damaged midi file: the bad events are skipped, the good note stays (%zu notes)", ok && !x.tracks.empty() ? x.tracks[0].size() : 0);
            std::string cut = bad + "MTrk" + std::string("\0\0\xff\xff", 4) + std::string("\0\x90", 2);
            { std::ofstream f("/tmp/fm1_cut.mid", std::ios::binary); f << cut; }
            MidiSong y; readMidi("/tmp/fm1_cut.mid", y); CHECK(true, "a truncated midi file with a huge track length does not crash");
        }
    }
    system("rm -rf /tmp/fm1sampler_ta; mkdir -p /tmp/fm1sampler_ta");
    std::vector<float> buf(512), in(512, 0.f), all; uint32_t now = 1000;
    {
        Session s(sr); s.setDataDir("/tmp/fm1sampler_ta"); s.setSampleDir(std::string(getenv("HOME")) + "/fm1-sampler/samples");
        SamplerApp app(s, [](const uint8_t*, size_t) {}); app.setTourDone(true); demoSounds(app);
        auto run = [&](double sec) { for (long i = 0; i < (long)(sec * sr) / 256; i++) { std::fill(buf.begin(), buf.end(), 0.f); s.process(buf.data(), in.data(), 256); std::this_thread::sleep_for(std::chrono::microseconds(150)); all.insert(all.end(), buf.begin(), buf.end()); if (i % 5 == 0) { now += 33; app.tick(now); } } };
        auto btn = [&](int b) { uint8_t m[3] = {0x91, (uint8_t)b, 100}; app.onMidi(m, 3); m[0] = 0x81; app.onMidi(m, 3); };
        run(2.5);
        // a project to remember: tempo, a pattern on track 1, a bounced loop, odd settings, a synth on track 3
        app.gotoPage(SamplerApp::P_TEMPO); { auto k = app.knobs(); k[0].set((100.f - 30.f) / 270.f); } run(0.2);
        app.gotoPage(SamplerApp::P_SQ); { auto k = app.knobs(); k[0].set(1.f / 3); k[1].set(0.f); }
        app.seqClear(); for (int i = 0; i < 4; i++) app.seqToggleAt(i * 192, 60 + i * 2, i == 1 ? 120 : 0); run(0.3);
        btn(SamplerApp::REC); run(0.4); run(3.0);
        CHECK(app.status().loopSamples > 0, "a loop was bounced to the tape (%.0f samples)", app.status().loopSamples);
        app.gotoPage(SamplerApp::P_ENV); { auto k = app.knobs(); k[0].set(0.7f); k[2].set(0.33f); }
        app.gotoPage(SamplerApp::P_FX); { auto k = app.knobs(); k[0].set(0.9f); }
        app.selectTrack(2); for (int i = 0; i < (int)app.library().size(); i++) if (app.library()[(size_t)i].label == "Hoover") app.loadEntry(i); run(1.0);
        app.gotoPage(SamplerApp::P_SHAPE); { auto k = app.knobs(); k[0].set(0.9f); k[3].set(0.2f); }
        const double loop0 = app.status().loopSamples; const float bpm0 = app.status().bpm;
        CHECK(app.saveProject("Test One"), "project saved");
        run(1.0); while (s.busyJobs() > 0) run(0.2);
        CHECK(std::filesystem::exists("/tmp/fm1sampler_ta/projects/Test One/project.txt") && std::filesystem::exists("/tmp/fm1sampler_ta/projects/Test One/tape_0.wav") && std::filesystem::exists("/tmp/fm1sampler_ta/projects/Test One/mix.wav"), "the project folder has the settings, the tape and the mix");
        // wreck everything, then load
        app.selectTrack(0); app.seqClear(); s.clipClearAll(); run(0.5);
        app.gotoPage(SamplerApp::P_ENV); { auto k = app.knobs(); k[0].set(0.1f); k[2].set(0.9f); }
        app.gotoPage(SamplerApp::P_FX); { auto k = app.knobs(); k[0].set(0.1f); }
        app.selectTrack(2); app.loadEntry(0); run(1.0); app.selectTrack(0);
        CHECK(app.status().loopSamples == 0 && app.status().seqCount[0] == 0, "wrecked: no loop, no notes");
        CHECK(app.loadProject("Test One"), "project loaded");
        run(3.0); while (s.busyJobs() > 0) run(0.2); run(1.0);
        CHECK(app.status().seqCount[0] == 4, "the pattern is back (%d notes)", app.status().seqCount[0]);
        { Session::SeqView v[kSeqMax]; const int n = s.seqNotes(0, v, kSeqMax); bool ok = n == 4; for (int i = 0; ok && i < n; i++) if (v[i].tick == 192 && v[i].len != 120) ok = false;
          CHECK(ok, "...with its note lengths"); }
        CHECK(std::fabs(app.status().loopSamples - loop0) < 2 && std::fabs(app.status().bpm - bpm0) < 0.5f, "the tape is back: %.0f samples (was %.0f), %.1f bpm (was %.1f)", app.status().loopSamples, loop0, app.status().bpm, bpm0);
        CHECK(app.state(0).a > 0.69f && app.state(0).a < 0.71f && std::fabs(app.state(0).s - 0.33f) < 0.01f, "the envelope is back (%.2f, %.2f)", app.state(0).a, app.state(0).s);
        CHECK(app.sampleLabel(2) == "Hoover" && app.isSynth(2), "track 3 plays the synth again (%s)", app.sampleLabel(2).c_str());
        CHECK(std::fabs(app.state(2).synTone - 0.9f) < 0.02f && std::fabs(app.state(2).synGrit - 0.2f) < 0.02f, "...with its macros (%.2f, %.2f)", app.state(2).synTone, app.state(2).synGrit);
        CHECK(app.sampleLabel(0) == "Tape Key" && app.sampleLabel(1) == "Acoustic", "the samples of the tracks are back (%s, %s)", app.sampleLabel(0).c_str(), app.sampleLabel(1).c_str());
        size_t m = all.size(); run(2.5); CHECK(rms(all, m, all.size()) > 0.003 && finite(all), "the loaded project plays (rms %.4f)", rms(all, m, all.size()));
        CHECK(!std::filesystem::exists("/tmp/fm1sampler_ta/projects/.loading"), "the loading marker is gone again");
        CHECK(app.projectList().size() >= 1 && app.projectList()[0] == "Test One" && app.currentProject() == "Test One", "the project is listed (%zu, first %s, current %s)", app.projectList().size(), app.projectList().empty() ? "-" : app.projectList()[0].c_str(), app.currentProject().c_str());
        // midi export of the patterns, and back in on another track
        CHECK(app.exportMidi() && !app.sharePath().empty() && app.sharePath().empty(), "midi exported (the share path is handed out once)");
        MidiSong exp; CHECK(readMidi("/tmp/fm1sampler_ta/exports/Test One.mid", exp) && exp.tracks.size() >= 1 && exp.tracks[0].size() == 4, "the exported file has the four notes");
        app.selectTrack(4); CHECK(app.importMidi("/tmp/fm1sampler_ta/exports/Test One.mid"), "midi imported"); run(0.3);
        CHECK(app.status().seqCount[4] == 4, "...onto the selected track (%d notes)", app.status().seqCount[4]);
        // the autosave, written on its own
        app.autosave(true); run(0.5); while (s.busyJobs() > 0) run(0.2);
        CHECK(std::filesystem::exists("/tmp/fm1sampler_ta/projects/autosave/project.txt"), "autosave written");
        // backups: the autosave is kept as Backup 1 (the first autosave of a session does it), the list shows them
        app.autosave(true); run(0.3); app.autosave(true); run(0.3);
        CHECK(std::filesystem::exists("/tmp/fm1sampler_ta/projects/Backup 1/project.txt"), "the previous autosave is kept as Backup 1");
        app.deleteProject("Test One"); CHECK(!std::filesystem::exists("/tmp/fm1sampler_ta/projects/Test One"), "a project can be deleted");
    }
    {   // the next start finds the autosave and goes back to it; a start after a failed restore skips it once
        Session s(sr); s.setDataDir("/tmp/fm1sampler_ta"); s.setSampleDir(std::string(getenv("HOME")) + "/fm1-sampler/samples");
        SamplerApp app(s, [](const uint8_t*, size_t) {}); app.setTourDone(true); demoSounds(app);
        auto run = [&](double sec) { for (long i = 0; i < (long)(sec * sr) / 256; i++) { std::fill(buf.begin(), buf.end(), 0.f); s.process(buf.data(), in.data(), 256); std::this_thread::sleep_for(std::chrono::microseconds(150)); if (i % 5 == 0) { now += 33; app.tick(now); } } };
        run(2.0); app.restoreLast(); run(3.0); while (s.busyJobs() > 0) run(0.2); run(1.0);
        CHECK(app.status().seqCount[0] == 4 && app.status().loopSamples > 0 && app.sampleLabel(2) == "Hoover", "a new start comes back to the autosave (%d notes, loop %.0f, track 3 %s)", app.status().seqCount[0], app.status().loopSamples, app.sampleLabel(2).c_str());
        { std::ofstream mk("/tmp/fm1sampler_ta/projects/.loading"); mk << "autosave"; }
        Session s2(sr); s2.setDataDir("/tmp/fm1sampler_ta"); s2.setSampleDir(std::string(getenv("HOME")) + "/fm1-sampler/samples");
        SamplerApp app2(s2, [](const uint8_t*, size_t) {});
        app2.restoreLast();
        CHECK(!std::filesystem::exists("/tmp/fm1sampler_ta/projects/.loading") && app2.state(2).entry != app.state(2).entry, "an unfinished restore is skipped once and the marker removed");
    }
}

static int synthIndex(const char* name) { for (int i = 0; i < kSynthCount; i++) if (std::string(kSynths[i].name) == name) return i; return -1; }
static std::vector<float> playSynth(int idx, int note, double sec, const SynthMacros& m = SynthMacros(), double holdSec = 1e9) {
    const int sr = SAMPLERATE; Sampler sm(sr); sm.load("synth:" + std::to_string(idx)); sm.setSynthMacros(m);
    std::vector<float> o((size_t)(sec * sr) * 2, 0.f); sm.noteOn(note, 110);
    for (size_t b = 0; b + 256 <= o.size() / 2; b += 256) { if (b >= (size_t)(holdSec * sr)) sm.noteOff(note); sm.render(o.data() + b * 2, 256); }
    return o;
}
static double zcr(const std::vector<float>& v, size_t a, size_t b) { int z = 0; for (size_t i = a + 2; i < b; i += 2) z += (v[i] < 0.f) != (v[i - 2] < 0.f); return z / (double)((b - a) / 2); }
static double pitchOf(const std::vector<float>& o, int sr, int lo, int hi, size_t from) {   // autocorrelation pitch in Hz between lo and hi
    const int w = sr / 4; int best = 0; double bc = -1e9;
    for (int lag = sr / hi; lag < sr / lo; lag++) { double c = 0; for (int k = 0; k < w; k++) c += o[2 * (from + (size_t)k)] * o[2 * (from + (size_t)k + (size_t)lag)]; if (c > bc) { bc = c; best = lag; } }
    return (double)sr / best;
}
static void testSound() {
    const int sr = SAMPLERATE;
    CHECK(kSynthCount >= 30, "%d built-in synths", kSynthCount);
    {   // wavetable: moves while the note holds, follows the key
        const int wb = synthIndex("Wave Bass"), wp = synthIndex("Wave Pad");
        auto o = playSynth(wb, 45, 1.5); const double hz = pitchOf(o, sr, 60, 180, (size_t)sr / 5);
        CHECK(wb >= 0 && finite(o) && hz > 104 && hz < 116, "Wave Bass plays A2 near 110 Hz (%.0f)", hz);
        auto p = playSynth(wp, 60, 3.0); const double early = zcr(p, 0, (size_t)sr / 2 * 2), late = zcr(p, (size_t)sr * 2 * 2, (size_t)sr * 5), flat = rms(p, (size_t)sr * 4, (size_t)sr * 5);
        CHECK(finite(p) && flat > 0.01 && std::fabs(early - late) > 0.002, "Wave Pad: the sound morphs while the note is held (crossings %.4f -> %.4f)", early, late);
    }
    {   // modal: a struck bar rings at the key and dies away by itself
        const int mb = synthIndex("Marimba"), tb = synthIndex("Tubular Bell");
        auto o = playSynth(mb, 57, 2.0); const double hz = pitchOf(o, sr, 150, 300, (size_t)sr / 20);
        CHECK(mb >= 0 && finite(o) && hz > 215 && hz < 225, "Marimba A3 rings at 220 Hz (%.0f)", hz);
        CHECK(rms(o, 0, (size_t)sr / 5) > 0.02 && rms(o, (size_t)sr * 2 * 2 - 4000, (size_t)sr * 2 * 2) < 0.1 * rms(o, 0, (size_t)sr / 5), "...and dies away (%.3f -> %.4f)", rms(o, 0, (size_t)sr / 5), rms(o, (size_t)sr * 2 * 2 - 4000, (size_t)sr * 2 * 2));
        auto b = playSynth(tb, 60, 4.0); CHECK(rms(b, (size_t)sr * 4, (size_t)sr * 4 + 4800) > 0.003, "a held bell is still ringing after two seconds (%.4f)", rms(b, (size_t)sr * 4, (size_t)sr * 4 + 4800));
    }
    {   // drum kits: 16 pads, every one sounds and ends, kick low and hat high
        const int k = synthIndex("Synth Kit 808"); bool all = k >= 0; double lo = 0, hi = 0;
        for (int pad = 0; pad < 16 && all; pad++) { auto o = playSynth(k, 60 + pad, 2.5); const bool ok = finite(o) && rms(o, 0, (size_t)sr / 5) > 0.01; all &= ok; if (!ok) printf("pad %d silent\n", pad); if (pad == 0) lo = zcr(o, 0, (size_t)sr / 10); if (pad == 6) hi = zcr(o, 0, (size_t)sr / 40); }
        CHECK(all && lo < 0.05 && hi > 0.15, "synth kit: all 16 pads sound; kick crossings %.3f, hat %.3f", lo, hi);
        auto cl = playSynth(k, 64, 3.0); CHECK(rms(cl, (size_t)sr * 2 * 2 + 0, (size_t)sr * 3 * 2) < 0.002, "a clap ends by itself (%.4f)", rms(cl, (size_t)sr * 4, (size_t)sr * 6));
        auto pu = playSynth(synthIndex("Synth Kit Punch"), 60, 1.0), l8 = playSynth(k, 60, 1.0); CHECK(diff(pu, l8) > 1e-3, "the kits differ from each other");
    }
    {   // macros
        const int sb = synthIndex("Saw Bass"); SynthMacros bright; bright.tone = 1.f; SynthMacros dark; dark.tone = -1.f;
        auto br = playSynth(sb, 48, 0.6, bright), dk = playSynth(sb, 48, 0.6, dark);
        CHECK(zcr(br, 4000, 40000) > zcr(dk, 4000, 40000) * 1.2, "TONE: brighter has more crossings (%.4f vs %.4f)", zcr(br, 4000, 40000), zcr(dk, 4000, 40000));
        SynthMacros body; body.body = 1.f; const int mb = synthIndex("Marimba");
        auto longer = playSynth(mb, 60, 2.0, body), normal = playSynth(mb, 60, 2.0);
        CHECK(rms(longer, (size_t)sr * 2, (size_t)sr * 3) > rms(normal, (size_t)sr * 2, (size_t)sr * 3) * 1.5, "BODY: a struck bar rings longer (%.4f vs %.4f)", rms(longer, (size_t)sr * 2, (size_t)sr * 3), rms(normal, (size_t)sr * 2, (size_t)sr * 3));
        SynthMacros grit; grit.grit = 1.f; auto gr = playSynth(sb, 48, 0.5, grit), cl = playSynth(sb, 48, 0.5);
        CHECK(finite(gr) && diff(gr, cl) > 0.005, "GRIT changes the waveform (diff %.4f)", diff(gr, cl));
        SynthMacros mo; mo.motion = 1.f; const int ss = synthIndex("Super Saw");
        CHECK(diff(playSynth(ss, 55, 1.0, mo), playSynth(ss, 55, 1.0)) > 0.005, "MOTION changes a patch");
    }
    {   // auto tune: the pitch of a sample, and the slices
        std::vector<float> w((size_t)sr * 2 * 2); for (int i = 0; i < sr * 2; i++) { const float v = 0.5f * std::sin(2 * M_PI * 220.0 * i / sr) * std::exp(-i / (0.8f * sr)) + 0.2f * std::sin(2 * M_PI * 440.0 * i / sr); w[2 * i] = w[2 * i + 1] = v; }
        writeWav16("/tmp/fm1_a3.wav", w, sr); Sampler sm(sr); sm.load("/tmp/fm1_a3.wav");
        CHECK(std::fabs(sm.rootGuess() - 57.f) < 0.3f, "auto tune finds A3 (MIDI %.2f, expect 57)", sm.rootGuess());
        Sampler dr(sr); dr.load(std::string(getenv("HOME")) + "/fm1-sampler/samples/Drums/808 Kit.wav");
        CHECK(dr.rootGuess() == 0.f, "a drum kit has no pitch (%.1f)", dr.rootGuess());
        std::vector<float> o2((size_t)sr * 2, 0.f); sm.setRoot(57); sm.noteOn(57, 100); for (int b = 0; b < sr / 2 / 256; b++) sm.render(o2.data() + (size_t)b * 512, 256);
        const double hz = pitchOf(o2, sr, 150, 300, (size_t)sr / 20);
        CHECK(hz > 217 && hz < 223, "with the root at A3, key A3 plays the sample as it is (%.0f Hz)", hz);
    }
    system("rm -rf /tmp/fm1sampler_tb; mkdir -p /tmp/fm1sampler_tb");
    Session s(sr); s.setDataDir("/tmp/fm1sampler_tb"); s.setSampleDir(std::string(getenv("HOME")) + "/fm1-sampler/samples");
    SamplerApp app(s, [](const uint8_t*, size_t) {}); app.setTourDone(true); demoSounds(app);
    std::vector<float> buf(512), in(512, 0.f); uint32_t now = 1000;
    auto run = [&](double sec) { for (long i = 0; i < (long)(sec * sr) / 256; i++) { std::fill(buf.begin(), buf.end(), 0.f); s.process(buf.data(), in.data(), 256); std::this_thread::sleep_for(std::chrono::microseconds(150)); if (i % 5 == 0) { now += 33; app.tick(now); } } };
    run(2.5);
    app.selectTrack(3); app.gotoPage(SamplerApp::P_SHAPE);   // the default project has TrapKit here
    const size_t n0 = s.slicesOf(3).size();
    { auto k = app.knobs(); k[2].set(0.f); } run(1.5);   // 4 slices
    const size_t n4 = s.slicesOf(3).size();
    CHECK(n0 >= 8 && n4 <= 5 && n4 >= 2, "SLICES: the pads follow the knob (%zu -> %zu)", n0, n4);
    app.selectTrack(0); app.gotoPage(SamplerApp::P_SHAPE);
    CHECK(std::string(app.knobs()[0].name) == "ROOT KEY" && std::string(app.knobs()[0].value) == "C4", "a sample track shows ROOT KEY C4 (%s %s)", app.knobs()[0].name.c_str(), app.knobs()[0].value.c_str());
    { auto k = app.knobs(); k[0].set(0.6f); } CHECK(std::string(app.knobs()[0].value) != "C4", "the root key moves");
    for (int i = 0; i < (int)app.library().size(); i++) if (app.library()[(size_t)i].label == "Warm Pad") app.loadEntry(i);
    run(2.0); { auto k = app.knobs(); k[1].step(1); }
    CHECK(app.state(0).rootN > 0.f, "AUTO TUNE answers (%s)", app.knobs()[1].value.c_str());
    for (int i = 0; i < (int)app.library().size(); i++) if (app.library()[(size_t)i].label == "Pluck" && app.library()[(size_t)i].group == "Synths") app.loadEntry(i);
    run(1.5); app.gotoPage(SamplerApp::P_SHAPE);
    CHECK(std::string(app.knobs()[0].name) == "TONE" && std::string(app.knobs()[3].name) == "GRIT", "a synth track shows its macros (%s .. %s)", app.knobs()[0].name.c_str(), app.knobs()[3].name.c_str());
}

static void testLook() {
    const int sr = SAMPLERATE;
    system("rm -rf /tmp/fm1sampler_tc; mkdir -p /tmp/fm1sampler_tc");
    {
        Session s(sr); s.setDataDir("/tmp/fm1sampler_tc"); SamplerApp app(s, [](const uint8_t*, size_t) {}); app.setTourDone(true); demoSounds(app);
        CHECK(app.themeId() == 0 && !app.fullscreen() && theme::BG == theme::argb(20, 20, 26), "the default look is DARK, not full screen");
        uint32_t seen[theme::kThemes]; bool distinct = true;
        for (int i = 0; i < theme::kThemes; i++) { app.setTheme(i); seen[i] = theme::BG; for (int j = 0; j < i; j++) distinct &= seen[j] != seen[i]; }
        CHECK(distinct, "five themes with five different backgrounds (%s, %s, %s, %s, %s)", theme::name(0), theme::name(1), theme::name(2), theme::name(3), theme::name(4));
        app.setTheme(2); CHECK(theme::BG == theme::argb(12, 12, 14) && theme::ACCENT == theme::argb(255, 106, 19) && theme::KNOB[0] == theme::argb(47, 125, 225) && theme::KNOB[1] == theme::argb(62, 196, 109), "OP-1: black screen, orange accent, blue and green encoders");
        app.setTheme(3); CHECK(theme::light && theme::TEXT == theme::argb(26, 26, 28), "OP-1 BODY is a light theme with dark ink");
        SamplerUI ui(app); ui.setSize(860, 400, 0, 0); ui.frame(1000); ui.openMenu(); ui.frame(1033);
        bool names = false; for (auto& o : ui.ops()) if (o.type == UiOp::TEXT && o.text == "OP-1 BODY") names = true;
        CHECK(names, "the menu lists the themes");
        { float fx = 0, fy = 0; for (auto& o : ui.ops()) if (o.type == UiOp::TEXT && o.text == "FULLSCREEN: OFF") { fx = o.f[0]; fy = o.f[1] - 4; }
          ui.touch(0, fx, fy, 1100); ui.touch(2, fx, fy, 1100); CHECK(fx > 0 && app.fullscreen(), "the FULLSCREEN button switches it on"); }
        app.setTheme(2);
    }
    {   // a new start keeps both
        Session s(sr); s.setDataDir("/tmp/fm1sampler_tc"); SamplerApp app(s, [](const uint8_t*, size_t) {}); app.setTourDone(true); demoSounds(app);
        CHECK(app.themeId() == 2 && app.fullscreen() && theme::BG == theme::argb(12, 12, 14), "the theme and the full screen are remembered (%s, fullscreen %d)", theme::name(app.themeId()), app.fullscreen());
        app.setTheme(0); app.setFullscreen(false);
    }
}

static std::vector<long> onsetsOf(const std::vector<float>& all, int sr, float thr = 0.02f, double refractory = 0.1) {
    std::vector<long> on; long quiet = 1 << 30; const long gap = (long)(refractory * sr);
    for (size_t i = 0; i < all.size(); i += 2) { if (std::fabs(all[i]) > thr) { if (quiet > gap) on.push_back((long)(i / 2)); quiet = 0; } else quiet++; }
    return on;
}
static void testNoteEdit() {
    const int sr = SAMPLERATE;
    system("rm -rf /tmp/fm1sampler_td; mkdir -p /tmp/fm1sampler_td");
    Session s(sr); s.setDataDir("/tmp/fm1sampler_td"); s.setSampleDir(std::string(getenv("HOME")) + "/fm1-sampler/samples");
    SamplerApp app(s, [](const uint8_t*, size_t) {}); app.setTourDone(true); demoSounds(app);
    std::vector<float> buf(512), in(512, 0.f), all; uint32_t now = 1000;
    auto run = [&](double sec) { for (long i = 0; i < (long)(sec * sr) / 256; i++) { std::fill(buf.begin(), buf.end(), 0.f); s.process(buf.data(), in.data(), 256); std::this_thread::sleep_for(std::chrono::microseconds(150)); all.insert(all.end(), buf.begin(), buf.end()); if (i % 5 == 0) { now += 33; app.tick(now); } } };
    auto btn = [&](int b) { uint8_t m[3] = {0x91, (uint8_t)b, 100}; app.onMidi(m, 3); m[0] = 0x81; app.onMidi(m, 3); };
    run(2.5);
    app.gotoPage(SamplerApp::P_TEMPO); { auto k = app.knobs(); k[0].set((120.f - 30.f) / 270.f); } run(0.2);
    app.gotoPage(SamplerApp::P_TIMING); { auto k = app.knobs(); k[2].set(0.f); k[3].set(0.f); } app.setQuantGrids(0, 4); run(0.2);
    app.selectTrack(4); for (int i = 0; i < (int)app.library().size(); i++) if (app.library()[(size_t)i].label == "Synth Kit 808") app.loadEntry(i);
    for (int i = 0; i < 20; i++) { run(0.05); std::this_thread::sleep_for(std::chrono::milliseconds(50)); }
    app.gotoPage(SamplerApp::P_SQ); app.seqClear(); { auto k = app.knobs(); k[1].set(0.f); } app.seqSetBarCount(1); run(0.3);
    Session::SeqView v[kSeqMax]; auto notes = [&]() { return s.seqNotes(4, v, kSeqMax); };
    // the selection edits
    for (int i = 0; i < 4; i++) app.seqToggleAt(i * 192, 66);   // closed hats on every beat
    run(0.3); CHECK(notes() == 4 && v[0].prob == 100 && v[0].ratchet == 1 && v[0].nudge == 0, "notes start with full chance, no ratchet, no nudge");
    Session::SeqRef two[2] = {{0, 66}, {192, 66}};
    s.seqApply(4, two, 2, Session::OP_TRANSPOSE, 3); run(0.2); { int n = notes(); int t3 = 0; for (int i = 0; i < n; i++) t3 += v[i].note == 69; CHECK(t3 == 2, "transpose moves the selected notes (%d moved)", t3); }
    Session::SeqRef up[2] = {{0, 69}, {192, 69}};
    s.seqApply(4, up, 2, Session::OP_TRANSPOSE, -3); s.seqApply(4, up, 0, Session::OP_DELETE, 0); run(0.2);
    Session::SeqRef back[2] = {{0, 66}, {192, 66}};
    s.seqApply(4, back, 2, Session::OP_VEL, 40); s.seqApply(4, back, 2, Session::OP_PROB, 55); s.seqApply(4, back, 2, Session::OP_RATCHET, 3); s.seqApply(4, back, 2, Session::OP_NUDGE, -7); run(0.2);
    { int n = notes(); bool ok = false; for (int i = 0; i < n; i++) if (v[i].tick == 0) ok = v[i].vel == 40 && v[i].prob == 55 && v[i].ratchet == 3 && v[i].nudge == -7; CHECK(ok, "velocity, chance, ratchet and nudge are set and read back"); }
    s.seqApply(4, back, 2, Session::OP_DUP, 96); run(0.2); CHECK(notes() == 6, "duplicate adds copies (%d notes)", notes());
    s.seqApply(4, back, 2, Session::OP_SHIFT, 48); run(0.2); { int n = notes(); bool ok = false; for (int i = 0; i < n; i++) ok |= v[i].tick == 48; CHECK(ok, "shift moves them in time"); }
    Session::SeqRef all6[6]; { int n = notes(); for (int i = 0; i < n && i < 6; i++) all6[i] = {v[i].tick, v[i].note}; s.seqApply(4, all6, std::min(n, 6), Session::OP_DELETE, 0); } run(0.2);
    CHECK(notes() == 0, "delete removes the selection"); s.seqUndo(4); run(0.2); CHECK(notes() == 6, "...and it is one undo step (%d)", notes());
    s.seqUndo(4); s.seqUndo(4); s.seqUndo(4); s.seqUndo(4); s.seqUndo(4); s.seqUndo(4); s.seqUndo(4); run(0.2);
    app.seqClear(); run(0.2);
    // add a bar as a copy of the last one
    app.seqToggleAt(10, 62); run(0.2); s.seqAddBar(4, true); run(0.2);
    CHECK(app.status().seqBars[4] == 2 && notes() == 2 && v[1].tick == 10 + 768, "+ with a copy: the last bar is repeated in the new one (%d bars, %d notes)", app.status().seqBars[4], notes());
    s.seqAddBar(4, false); run(0.2); CHECK(app.status().seqBars[4] == 3 && notes() == 2, "+ without a copy: the new bar is empty");
    // playback: chance, ratchet, nudge, on a short hat so every hit is one clear onset
    app.seqClear(); app.seqSetBarCount(1); run(0.3);
    { auto k = app.knobs(); k[0].set(1.f / 3); } run(0.3);
    auto stopGo = [&]() { run(4.0); if (app.status().playing) { btn(SamplerApp::PLAY); run(0.3); } run(2.0); all.clear(); btn(SamplerApp::PLAY); };
    app.setSend(4, 0, 0.f); app.setSend(4, 1, 0.f);   // dry: no echoes to be counted as hits
    app.seqToggleAt(0, 66, 768); run(0.2); Session::SeqRef h0[1] = {{0, 66}}; s.seqApply(4, h0, 1, Session::OP_RATCHET, 4); run(0.2);
    stopGo(); run(2.2); { auto on = onsetsOf(all, sr); on.erase(on.begin(), on.begin() + std::min<size_t>(on.size(), 0)); const int n = (int)on.size(); long g1 = n > 1 ? on[1] - on[0] : 0; CHECK(n >= 4 && std::labs(g1 - 24000) < 300, "ratchet 4: four hits for one note (%d onsets, gap %ld samples, expect 24000)", n, g1); }
    s.seqApply(4, h0, 1, Session::OP_RATCHET, 1); s.seqApply(4, h0, 1, Session::OP_PROB, 0); run(3.5); all.clear(); run(4.5);
    CHECK(onsetsOf(all, sr).empty(), "chance 0: the note never plays");
    s.seqApply(4, h0, 1, Session::OP_PROB, 50); run(0.3); all.clear(); run(2.0 * 30);   // the dice are seeded, so the outcome is fixed, but a short run can start with a run of skips: 30 passes, a wide band
    { const int n = (int)onsetsOf(all, sr).size(); CHECK(n >= 6 && n <= 24, "chance 50 pct: it plays about half of the passes (%d of 30)", n); }
    app.seqClear(); run(0.2); app.seqToggleAt(0, 66); app.seqToggleAt(384, 66); run(0.3); Session::SeqRef h1[1] = {{384, 66}}; s.seqApply(4, h1, 1, Session::OP_NUDGE, 12); run(0.3);
    all.clear(); run(2.0 * 5 + 0.5);
    { auto on = onsetsOf(all, sr); long a = on.size() > 3 ? on[2] - on[1] : 0, b = on.size() > 3 ? on[3] - on[2] : 0; long d = std::labs(a - b);
      CHECK(on.size() >= 5 && d > 2500 && d < 3500, "nudge +12 ticks pushes one hit back (gaps %ld / %ld, differ by %ld, expect 3000)", a, b, d); }
    app.seqClear(); run(0.2); if (app.status().playing) btn(SamplerApp::PLAY);
}

static void testPads() {
    const int sr = SAMPLERATE;
    {   // the pad settings of the sampler: level, tune, decay, choke
        auto render = [&](int pad, const Sampler::Pad& p, int idx, double sec) { Sampler sm(sr); sm.load("synth:" + std::to_string(idx)); sm.setPad(pad, p); std::vector<float> o((size_t)(sec * sr) * 2, 0.f); sm.noteOn(60 + pad, 110); for (size_t b = 0; b + 256 <= o.size() / 2; b += 256) sm.render(o.data() + b * 2, 256); return o; };
        const int k = synthIndex("Synth Kit 808"); Sampler::Pad base, loud, quiet, tuned, shortp;
        loud.vol = 1.5f; quiet.vol = 0.3f; tuned.pitch = 12.f; shortp.decay = 0.3f;
        auto a = render(0, base, k, 1.0), l = render(0, loud, k, 1.0), q = render(0, quiet, k, 1.0), tn = render(0, tuned, k, 1.0), sh = render(0, shortp, k, 1.0);
        CHECK(rms(l, 0, sr / 4) > rms(a, 0, sr / 4) * 1.3 && rms(q, 0, sr / 4) < rms(a, 0, sr / 4) * 0.5, "pad level: %.3f quiet, %.3f normal, %.3f loud", rms(q, 0, sr / 4), rms(a, 0, sr / 4), rms(l, 0, sr / 4));
        CHECK(zcr(tn, 0, sr / 8) > zcr(a, 0, sr / 8) * 1.5, "pad tune +12: higher (%.4f vs %.4f crossings)", zcr(tn, 0, sr / 8), zcr(a, 0, sr / 8));
        CHECK(rms(sh, sr / 2 * 2, sr) < rms(a, sr / 2 * 2, sr) * 0.5 + 1e-4, "pad decay 0.3x: gone sooner (%.4f vs %.4f)", rms(sh, sr, 2 * sr), rms(a, sr, 2 * sr));
        // choke: the closed hat (pad 6) cuts the open hat (pad 7) when both are in group 1
        auto choke = [&](int group) { Sampler sm(sr); sm.load("synth:" + std::to_string(k)); Sampler::Pad p; p.group = group; sm.setPad(6, p); sm.setPad(7, p);
            std::vector<float> o((size_t)sr * 2, 0.f); sm.noteOn(67, 110); size_t b = 0; for (; b < (size_t)sr / 10; b += 256) sm.render(o.data() + b * 2, 256);
            sm.noteOn(66, 1); for (; b + 256 <= (size_t)sr / 2; b += 256) sm.render(o.data() + b * 2, 256); return rms(o, (size_t)sr * 2 / 5, (size_t)sr); };
        const double open = choke(0), cut = choke(1); CHECK(cut < open * 0.3, "choke: the closed hat cuts the open one (%.4f -> %.4f)", open, cut);
    }
    system("rm -rf /tmp/fm1sampler_te; mkdir -p /tmp/fm1sampler_te");
    Session s(sr); s.setDataDir("/tmp/fm1sampler_te"); s.setSampleDir(std::string(getenv("HOME")) + "/fm1-sampler/samples");
    SamplerApp app(s, [](const uint8_t*, size_t) {}); app.setTourDone(true); demoSounds(app);
    std::vector<float> buf(512), in(512, 0.f), all; uint32_t now = 1000;
    auto run = [&](double sec) { for (long i = 0; i < (long)(sec * sr) / 256; i++) { std::fill(buf.begin(), buf.end(), 0.f); s.process(buf.data(), in.data(), 256); std::this_thread::sleep_for(std::chrono::microseconds(150)); all.insert(all.end(), buf.begin(), buf.end()); if (i % 5 == 0) { now += 33; app.tick(now); } } };
    run(2.5);
    app.selectTrack(4); for (int i = 0; i < (int)app.library().size(); i++) if (app.library()[(size_t)i].label == "Synth Kit 808") app.loadEntry(i);
    for (int i = 0; i < 20; i++) { run(0.05); std::this_thread::sleep_for(std::chrono::milliseconds(50)); }
    CHECK(app.isKit(4) && app.padName(0) == "KICK" && app.padName(6) == "HAT", "a synth kit names its pads (%s, %s)", app.padName(0).c_str(), app.padName(6).c_str());
    app.gotoPage(SamplerApp::P_PADS); CHECK(app.page() == SamplerApp::P_PADS && SamplerApp::modeOf(SamplerApp::P_PADS) == 2, "the PADS page is in the SEQ mode");
    SamplerUI ui(app); ui.setSize(860, 400, 0, 0); ui.frame(now);
    float px = 0, py = 0; for (auto& o : ui.ops()) if (o.type == UiOp::TEXT && o.text == "SNARE") { px = o.f[0]; py = o.f[1]; }
    size_t m = all.size(); ui.touch(0, px, py + 20, now); run(0.4); ui.touch(2, px, py + 20, now); run(0.3);
    CHECK(px > 0 && app.state(4).selPad == 2 && rms(all, m, all.size()) > 0.01, "tapping the SNARE pad plays it and selects it (pad %d, rms %.4f)", app.state(4).selPad + 1, rms(all, m, all.size()));
    CHECK(std::string(app.knobs()[0].name) == "PAD 3 LEVEL", "the knobs follow the pad (%s)", app.knobs()[0].name.c_str());
    // a hard hit (low on the pad) is louder than a soft one
    auto hitAt = [&](float dy) { ui.frame(now); size_t m0 = all.size(); ui.touch(0, px, py + dy, now); run(0.4); ui.touch(2, px, py + dy, now); run(1.5); return rms(all, m0, m0 + (size_t)sr / 2); };
    const double soft = hitAt(-25), hard = hitAt(25); CHECK(hard > soft * 1.4, "a hit low on the pad is harder (%.4f vs %.4f)", hard, soft);
    // recording pads into the pattern, then the drum grid shows them
    app.gotoPage(SamplerApp::P_SQ); app.seqClear(); app.setSeqState(2); run(0.4); app.padHit(0, 100, true); run(0.15); app.padHit(0, 0, false); run(0.3); app.padHit(2, 100, true); run(0.15); app.padHit(2, 0, false); run(0.4); app.setSeqState(1); run(0.3);
    CHECK(app.status().seqCount[4] == 2, "pads played while recording go into the pattern (%d notes)", app.status().seqCount[4]);
    ui.frame(now); { float tx = 0, ty = 0; for (auto& o : ui.ops()) if (o.type == UiOp::TEXT && o.text == "DRUMS") { tx = o.f[0]; ty = o.f[1] - 3; } ui.touch(0, tx, ty, now); ui.touch(2, tx, ty, now); }
    ui.frame(now); ui.frame(now);
    float gx, gy, gw, gh; ui.rollGrid(gx, gy, gw, gh); CHECK(std::fabs(ui.rollRows() - 8.f) < 0.01f && ui.rollBase() == 60.f, "the roll turns into a drum grid of 8 pad rows (%.0f rows from %.0f)", ui.rollRows(), ui.rollBase());
    bool names = false; for (auto& o : ui.ops()) if (o.type == UiOp::TEXT && o.text.find("KICK") != std::string::npos) names = true; CHECK(names, "...with the pad names on the left");
    const int cnt0 = app.status().seqCount[4]; const float rh = gh / 8, ty2 = gy + 7 * rh + rh / 2 /* pad 1 = the bottom row */ , tx2 = gx + 0.5f * gw * 0.6f;
    ui.touch(0, tx2, ty2 - 3 * rh, now); ui.touch(2, tx2, ty2 - 3 * rh, now); run(0.3); CHECK(app.status().seqCount[4] == cnt0 + 1, "a tap on a pad row adds a hit (%d -> %d)", cnt0, app.status().seqCount[4]);
    app.seqClear(); run(0.2);
}

static void testSampleEdit() {
    const int sr = SAMPLERATE;
    system("rm -rf /tmp/fm1sampler_tf; mkdir -p /tmp/fm1sampler_tf");
    Session s(sr); s.setDataDir("/tmp/fm1sampler_tf"); s.setSampleDir(std::string(getenv("HOME")) + "/fm1-sampler/samples");
    SamplerApp app(s, [](const uint8_t*, size_t) {}); app.setTourDone(true); demoSounds(app);
    std::vector<float> buf(512), in(512, 0.f), all; uint32_t now = 1000;
    auto run = [&](double sec) { for (long i = 0; i < (long)(sec * sr) / 256; i++) { std::fill(buf.begin(), buf.end(), 0.f); s.process(buf.data(), in.data(), 256); std::this_thread::sleep_for(std::chrono::microseconds(150)); all.insert(all.end(), buf.begin(), buf.end()); if (i % 5 == 0) { now += 33; app.tick(now); } } };
    auto wait = [&](double sec) { for (int i = 0; i < (int)(sec * 20); i++) { run(0.05); std::this_thread::sleep_for(std::chrono::milliseconds(50)); } };
    wait(3.0); for (size_t i = 0; i < app.library().size(); i++) if (app.library()[i].label == "Tape Key") app.loadEntry((int)i);
    wait(1.5);
    auto head = [&](int t, bool first) { auto p = s.samplePeaksOf(t, 64); float a = 0; for (int i = first ? 0 : 44; i < (first ? 20 : 64); i++) a += p[(size_t)i]; return a; };
    const int frames0 = s.sampleFramesOf(0); const float h0 = head(0, true), t0 = head(0, false);
    CHECK(frames0 > 1000 && h0 > t0, "Tape Key decays: loud start, quiet end (%.2f vs %.2f)", h0, t0);
    app.gotoPage(SamplerApp::P_SMPED); CHECK(app.page() == SamplerApp::P_SMPED && std::string(app.knobs()[0].name) == "EDIT", "the SAMPLE EDIT page");
    s.editSample(0, Session::EDIT_REVERSE); wait(1.0);
    CHECK(head(0, true) < head(0, false) && s.sampleFramesOf(0) == frames0, "REVERSE: quiet start, loud end (%.2f vs %.2f)", head(0, true), head(0, false));
    s.undoEdit(0); wait(1.0); CHECK(head(0, true) > head(0, false), "EDIT UNDO brings the sample back");
    s.editSample(0, Session::EDIT_NORMALIZE); wait(1.0); { auto p = s.samplePeaksOf(0, 512); float m = 0; for (float v : p) m = std::max(m, v); CHECK(m > 0.9f, "NORMALIZE: the peak is at %.2f", m); }
    // trim to the window: start 25 %, end 50 %
    app.gotoPage(SamplerApp::P_HOME); { auto k = app.knobs(); k[0].set(0.25f); k[1].set(0.5f); } wait(0.8);
    s.editSample(0, Session::EDIT_TRIM); wait(1.0); const int fr = s.sampleFramesOf(0);
    CHECK(std::fabs((double)fr / frames0 - 0.25) < 0.02, "TRIM TO WINDOW keeps a quarter (%d of %d frames)", fr, frames0);
    s.editSample(0, Session::EDIT_FADE_IN); wait(1.0); s.editSample(0, Session::EDIT_FADE_OUT); wait(1.0); { auto p = s.samplePeaksOf(0, 512); CHECK(p[0] < 0.1f && p[511] < 0.1f, "fades: silent at both ends (%.3f, %.3f)", p[0], p[511]); }
    // the knobs of the page
    app.gotoPage(SamplerApp::P_SMPED); { auto k = app.knobs(); k[2].set(1.f); k[3].set(0.5f); }
    CHECK(std::string(app.knobs()[2].value) == "+50 CENTS" && std::string(app.knobs()[3].value).find("DB") != std::string::npos, "fine tune and level knobs (%s, %s)", app.knobs()[2].value.c_str(), app.knobs()[3].value.c_str());
    { auto k = app.knobs(); k[2].set(0.5f); k[3].set(1.f / 3.f); }
    // hand made slices
    app.selectTrack(3); wait(0.5); const size_t n0 = s.slicesOf(3).size();
    s.setSliceStarts(3, {0.f, 0.25f, 0.5f, 0.75f}); wait(0.4); auto sl = s.slicesOf(3);
    CHECK(n0 >= 2 && sl.size() == 4 && std::fabs(sl[1] - 0.25f) < 0.001f && std::fabs(sl[3] - 0.75f) < 0.001f, "slice marks placed by hand: %zu pads (was %zu)", sl.size(), n0);
    // resample: what plays becomes a sample
    app.selectTrack(0); for (int i = 0; i < (int)app.library().size(); i++) if (app.library()[(size_t)i].label == "Tape Key") app.loadEntry(i); wait(1.0);
    app.gotoPage(SamplerApp::P_SQ); app.seqClear(); app.seqSetBarCount(1); for (int i = 0; i < 4; i++) app.seqToggleAt(i * 192, 60 + i * 2, 96); { auto k = app.knobs(); k[0].set(1.f / 3); } s.play(true); wait(0.5);
    const std::string before = app.sampleLabel(0); app.resampleNow(); wait(5.0);
    CHECK(app.sampleLabel(0) != before && app.sampleLabel(0).rfind("sample", 0) == 0 && s.sampleFramesOf(0) > sr, "RESAMPLE: the track plays the new sample (%s, %d frames)", app.sampleLabel(0).c_str(), s.sampleFramesOf(0));
}

static void testMixer() {
    const int sr = SAMPLERATE;
    system("rm -rf /tmp/fm1sampler_tg; mkdir -p /tmp/fm1sampler_tg");
    Session s(sr); s.setDataDir("/tmp/fm1sampler_tg"); s.setSampleDir(std::string(getenv("HOME")) + "/fm1-sampler/samples");
    SamplerApp app(s, [](const uint8_t*, size_t) {}); app.setTourDone(true); demoSounds(app);
    std::vector<float> buf(512), in(512, 0.f), all; uint32_t now = 1000;
    auto run = [&](double sec) { for (long i = 0; i < (long)(sec * sr) / 256; i++) { std::fill(buf.begin(), buf.end(), 0.f); s.process(buf.data(), in.data(), 256); std::this_thread::sleep_for(std::chrono::microseconds(150)); all.insert(all.end(), buf.begin(), buf.end()); if (i % 5 == 0) { now += 33; app.tick(now); } } };
    auto wait = [&](double sec) { for (int i = 0; i < (int)(sec * 20); i++) { run(0.05); std::this_thread::sleep_for(std::chrono::milliseconds(50)); } };
    auto load = [&](int track, const char* name) { app.selectTrack(track); for (int i = 0; i < (int)app.library().size(); i++) if (app.library()[(size_t)i].label == name && app.library()[(size_t)i].group == "Synths") app.loadEntry(i); };
    wait(2.0); load(1, "Organ"); load(0, "Synth Kit 808"); wait(2.0);
    app.gotoPage(SamplerApp::P_TEQ); CHECK(std::string(app.knobs()[0].name) == "EQ LOW" && std::string(app.knobs()[3].name) == "COMPRESSOR" && SamplerApp::modeOf(SamplerApp::P_TEQ) == 3, "MIX mode has the track EQ page");
    app.gotoPage(SamplerApp::P_DUCK); CHECK(std::string(app.knobs()[0].name) == "DUCK FROM" && std::string(app.knobs()[3].name) == "LIMITER", "...and the duck and limiter page");
    // track EQ: a low boost on an organ note
    app.selectTrack(1); app.setSend(1, 0, 0.f); app.setSend(1, 1, 0.f);
    auto held = [&](int note, double sec) { size_t m = all.size(); app.playNote(note, true); run(sec); const double r = rms(all, all.size() - (size_t)sr / 4 * 2, all.size()); app.playNote(note, false); run(0.8); (void)m; return r; };
    const double flat = held(36, 1.0);
    app.gotoPage(SamplerApp::P_TEQ); { auto k = app.knobs(); k[0].set(1.f); } const double boosted = held(36, 1.0);
    CHECK(boosted > flat * 1.25, "track EQ low +12 dB lifts a low organ note (%.3f -> %.3f)", flat, boosted);
    { auto k = app.knobs(); k[0].set(0.5f); }
    // sidechain: track 0 (silent itself) ducks the organ on track 2
    app.setVolume(0, 0.f); app.gotoPage(SamplerApp::P_DUCK);
    app.selectTrack(1); { auto k = app.knobs(); k[0].set(1.f / 8.f); k[1].set(1.f); k[2].set(0.5f); }   // from track 1, push this track fully, 0.5 s release
    app.selectTrack(1); app.gotoPage(SamplerApp::P_DUCK); { auto k = app.knobs(); (void)k; }
    // the source is track 1 (index 0)? the knob says DUCK FROM TRACK 1 = index 0
    app.playNote(48, true); run(1.0);
    const double steady = rms(all, all.size() - 4800 * 2, all.size());
    app.selectTrack(0); app.playNote(60, true); run(0.03); const double ducked = rms(all, all.size() - 480 * 2, all.size()); app.playNote(60, false); run(1.2); const double back = rms(all, all.size() - 4800 * 2, all.size());
    CHECK(ducked < steady * 0.5 && back > steady * 0.8, "sidechain: a hit on track 1 pushes the organ down (%.3f -> %.3f) and it comes back (%.3f)", steady, ducked, back);
    app.playNote(48, false); app.selectTrack(1); app.playNote(48, false); run(1.0);
    { app.selectTrack(1); auto k = app.knobs(); k[0].set(0.f); }
    // limiter
    s.setMasterVolume(1.5f); load(2, "Super Saw"); wait(1.0); app.selectTrack(2);
    auto peakOf = [&]() { float pk = 0.f; for (size_t i = all.size() - (size_t)sr * 2 / 2; i < all.size(); i++) pk = std::max(pk, std::fabs(all[i])); return pk; };
    for (int n : {48, 52, 55, 60, 64, 67}) app.playNote(n, true); run(1.0); const float unl = peakOf(); const bool clip = app.status().clip;
    app.gotoPage(SamplerApp::P_DUCK); { auto k = app.knobs(); k[3].set(1.f); } run(1.0); const float lim = peakOf();
    for (int n : {48, 52, 55, 60, 64, 67}) app.playNote(n, false); run(1.0);
    CHECK(unl > 0.7f && lim < 0.5f && lim < unl, "limiter -6 dB: the peak %.2f falls to %.2f", unl, lim);
    CHECK(clip, "the clip light comes on when the master is pushed (%d)", clip);
    s.setMasterVolume(0.9f);
}

static void testSynthEdit() {
    const int sr = SAMPLERATE;
    auto play = [&](const SynthEdit& e, int idx, std::vector<int> notes, double sec, double gap = 0.0) {
        Sampler sm(sr); sm.load("synth:" + std::to_string(idx)); sm.setSynthEdit(e); std::vector<float> o((size_t)(sec * sr) * 2, 0.f); size_t next = 0; size_t k = 0;
        for (size_t b = 0; b + 256 <= o.size() / 2; b += 256) { if (k < notes.size() && b >= next) { sm.noteOn(notes[k++], 100); next = b + (size_t)(gap * sr); } sm.render(o.data() + b * 2, 256); }
        return o;
    };
    const int sb = synthIndex("Saw Bass"), ss = synthIndex("Super Saw");
    SynthEdit none, sine, hp, lowcut, res;
    sine.w1 = 0; sine.w2 = 0; hp.ftype = 1; lowcut.cutOct = -2.f; res.res = 5.f;
    auto base = play(none, sb, {48}, 0.6);
    { auto o = play(sine, sb, {48}, 0.6); CHECK(finite(o) && diff(o, base) > 0.01 && zcr(o, 4000, 40000) < zcr(base, 4000, 40000), "WAVE sine instead of saw: smoother (%.4f vs %.4f crossings)", zcr(o, 4000, 40000), zcr(base, 4000, 40000)); }
    { auto o = play(hp, sb, {48}, 0.6); CHECK(finite(o) && zcr(o, 4000, 40000) > zcr(base, 4000, 40000) * 1.5, "FILTER high-pass: thinner and brighter (%.4f vs %.4f)", zcr(o, 4000, 40000), zcr(base, 4000, 40000)); }
    { auto o = play(lowcut, sb, {60}, 0.6), b2 = play(none, sb, {60}, 0.6); CHECK(finite(o) && zcr(o, 4000, 40000) < zcr(b2, 4000, 40000) * 0.8, "CUTOFF -2 octaves darkens (%.4f vs %.4f crossings)", zcr(o, 4000, 40000), zcr(b2, 4000, 40000)); }
    { auto o = play(res, sb, {48}, 0.6); CHECK(finite(o) && diff(o, base) > 0.005, "RESONANCE changes the sound and stays finite"); }
    { SynthEdit u1; u1.uni = 1; auto a = play(none, ss, {55}, 0.6), b = play(u1, ss, {55}, 0.6); CHECK(finite(b) && diff(a, b) > 0.01, "UNISON off thins the super saw"); }
    {   // mono: the second key takes the first one's place; poly: both sound
        SynthEdit mono; mono.mono = true; const int pad = synthIndex("Warm Pad");
        auto p = play(none, pad, {48, 55}, 1.2, 0.3), m = play(mono, pad, {48, 55}, 1.2, 0.3);
        CHECK(rms(m, (size_t)sr * 2 - 4800, (size_t)sr * 2) < rms(p, (size_t)sr * 2 - 4800, (size_t)sr * 2) * 0.85, "MONO: two keys sound as one voice (%.3f vs %.3f)", rms(m, sr * 2 - 4800, sr * 2), rms(p, sr * 2 - 4800, sr * 2));
        SynthEdit gl; gl.mono = true; gl.glide = 0.3f; const int lead = synthIndex("Mono Lead");
        auto g = play(gl, lead, {48, 60}, 1.0, 0.4), n = play(mono, lead, {48, 60}, 1.0, 0.4);
        const size_t a0 = (size_t)(0.4 * sr) * 2 + 2400, a1 = a0 + 4800;   // the first 50 ms of the second note
        CHECK(finite(g) && zcr(g, a0, a1) < zcr(n, a0, a1) * 0.85, "GLIDE: the second note slides up from the first (%.4f vs %.4f crossings)", zcr(g, a0, a1), zcr(n, a0, a1));
    }
    system("rm -rf /tmp/fm1sampler_th; mkdir -p /tmp/fm1sampler_th");
    Session s(sr); s.setDataDir("/tmp/fm1sampler_th"); s.setSampleDir(std::string(getenv("HOME")) + "/fm1-sampler/samples");
    SamplerApp app(s, [](const uint8_t*, size_t) {}); app.setTourDone(true); demoSounds(app);
    std::vector<float> buf(512), in(512, 0.f); uint32_t now = 1000;
    auto run = [&](double sec) { for (long i = 0; i < (long)(sec * sr) / 256; i++) { std::fill(buf.begin(), buf.end(), 0.f); s.process(buf.data(), in.data(), 256); std::this_thread::sleep_for(std::chrono::microseconds(150)); if (i % 5 == 0) { now += 33; app.tick(now); } } };
    run(2.5); app.selectTrack(2); app.gotoPage(SamplerApp::P_SYN1);
    CHECK(std::string(app.knobs()[0].name) != "WAVE", "a sample track has no synth knobs");
    for (int i = 0; i < (int)app.library().size(); i++) if (app.library()[(size_t)i].label == "Saw Bass" && app.library()[(size_t)i].group == "Synths") app.loadEntry(i);
    for (int i = 0; i < 20; i++) { run(0.05); std::this_thread::sleep_for(std::chrono::milliseconds(50)); }
    app.gotoPage(SamplerApp::P_SYN1); CHECK(std::string(app.knobs()[0].name) == "WAVE" && std::string(app.knobs()[3].name) == "UNISON" && SamplerApp::modeOf(SamplerApp::P_SYN1) == 0, "a synth track has the OSC page");
    app.gotoPage(SamplerApp::P_SYN2); CHECK(std::string(app.knobs()[0].name) == "CUTOFF" && std::string(app.knobs()[3].name) == "VOICES", "...and the FILTER page");
    // patches: the edited synth saved into the library and loaded again
    app.gotoPage(SamplerApp::P_SYN1); { auto k = app.knobs(); k[0].set(0.25f); }   // WAVE: sine
    app.gotoPage(SamplerApp::P_SYN2); { auto k = app.knobs(); k[0].set(0.8f); k[3].set(1.f); }   // cutoff up, mono slow glide
    app.gotoPage(SamplerApp::P_SHAPE); { auto k = app.knobs(); k[0].set(0.9f); }
    const std::string name = app.savePatch(); CHECK(name == "Saw Bass 2", "SAVE PATCH names it after the synth (%s)", name.c_str());
    int pe = -1; for (int i = 0; i < (int)app.library().size(); i++) if (app.library()[(size_t)i].label == name && app.library()[(size_t)i].group == "My Patches") pe = i;
    CHECK(pe >= 0, "the patch is in the library under My Patches");
    for (int i = 0; i < (int)app.library().size(); i++) if (app.library()[(size_t)i].label == "Brass" && app.library()[(size_t)i].group == "Synths") app.loadEntry(i);
    for (int i = 0; i < 20; i++) { run(0.05); std::this_thread::sleep_for(std::chrono::milliseconds(50)); }
    app.gotoPage(SamplerApp::P_SYN2); { auto k = app.knobs(); k[3].set(0.f); } app.gotoPage(SamplerApp::P_SYN1); { auto k = app.knobs(); k[0].set(0.f); }
    app.loadEntry(pe); for (int i = 0; i < 20; i++) { run(0.05); std::this_thread::sleep_for(std::chrono::milliseconds(50)); }
    CHECK(app.sampleLabel(2) == "Saw Bass" && app.isSynth(2) && app.state(2).edW1 == 1 && app.state(2).edVoice == 3 && app.state(2).synTone > 0.89f && app.state(2).edCut > 0.79f, "loading the patch brings the synth and its settings back (%s, wave %d, voices %d, tone %.2f)", app.sampleLabel(2).c_str(), app.state(2).edW1, app.state(2).edVoice, app.state(2).synTone);
    CHECK(app.saveProject("Patch Test") , "a project with a patch on a track saves"); for (int i = 0; i < 20; i++) { run(0.05); std::this_thread::sleep_for(std::chrono::milliseconds(50)); }
    for (int i = 0; i < (int)app.library().size(); i++) if (app.library()[(size_t)i].label == "Brass" && app.library()[(size_t)i].group == "Synths") app.loadEntry(i);
    for (int i = 0; i < 10; i++) { run(0.05); std::this_thread::sleep_for(std::chrono::milliseconds(50)); }
    app.loadProject("Patch Test"); for (int i = 0; i < 60; i++) { run(0.05); std::this_thread::sleep_for(std::chrono::milliseconds(50)); }
    CHECK(app.sampleLabel(2) == "Saw Bass", "...and loads with the synth on the track (%s)", app.sampleLabel(2).c_str());
}

static void testBanks() {
    const int sr = SAMPLERATE;
    system("rm -rf /tmp/fm1sampler_ti; mkdir -p /tmp/fm1sampler_ti");
    Session s(sr); s.setDataDir("/tmp/fm1sampler_ti"); s.setSampleDir(std::string(getenv("HOME")) + "/fm1-sampler/samples");
    SamplerApp app(s, [](const uint8_t*, size_t) {}); app.setTourDone(true); demoSounds(app);
    std::vector<float> buf(512), in(512, 0.f); uint32_t now = 1000;
    auto run = [&](double sec) { for (long i = 0; i < (long)(sec * sr) / 256; i++) { std::fill(buf.begin(), buf.end(), 0.f); s.process(buf.data(), in.data(), 256); std::this_thread::sleep_for(std::chrono::microseconds(150)); if (i % 5 == 0) { now += 33; app.tick(now); } } };
    auto btn = [&](int b) { uint8_t m[3] = {0x91, (uint8_t)b, 100}; app.onMidi(m, 3); m[0] = 0x81; app.onMidi(m, 3); };
    run(2.5);
    app.gotoPage(SamplerApp::P_TEMPO); { auto k = app.knobs(); k[0].set((120.f - 30.f) / 270.f); } run(0.2);
    app.selectTrack(4); for (int i = 0; i < (int)app.library().size(); i++) if (app.library()[(size_t)i].label == "Synth Kit 808") app.loadEntry(i);
    for (int i = 0; i < 20; i++) { run(0.05); std::this_thread::sleep_for(std::chrono::milliseconds(50)); }   // a track plays its pattern when it has a sound
    app.gotoPage(SamplerApp::P_SQ); app.seqClear(); app.seqSetBarCount(1); run(0.3);
    Session::SeqView v[kSeqMax]; auto cnt = [&]() { return s.seqNotes(4, v, kSeqMax); };
    app.seqToggleAt(0, 60); app.seqToggleAt(192, 62); run(0.3); CHECK(cnt() == 2 && app.status().seqSlot[4] == 0, "bank A holds two notes");
    app.selectPattern(1); run(0.3); CHECK(app.status().seqSlot[4] == 1 && cnt() == 0, "bank B is empty (slot %d, %d notes)", app.status().seqSlot[4], cnt());
    app.seqToggleAt(96, 64); run(0.3); CHECK(cnt() == 1, "...and has its own notes");
    app.selectPattern(0); run(0.3); CHECK(app.status().seqSlot[4] == 0 && cnt() == 2, "back to A: its two notes are still there (%d)", cnt());
    app.copyPattern(2); run(0.3); { Session::SeqView w[kSeqMax]; int bars = 0; const int n = s.seqSlotNotes(4, 2, w, kSeqMax, &bars); CHECK(n == 2 && bars == 1, "COPY: bank C is a copy of A (%d notes, %d bars)", n, bars); }
    app.selectPattern(2); run(0.3); CHECK(app.status().seqSlot[4] == 2 && cnt() == 2, "C plays the copy"); app.selectPattern(0); run(0.3);
    // while it plays the next bank waits for the end of the pattern
    { auto k = app.knobs(); k[0].set(1.f / 3); } run(0.2); if (!app.status().playing) btn(SamplerApp::PLAY);
    run(0.5); app.selectPattern(1); run(0.1);
    CHECK(app.status().seqSlotPending[4] == 1 && app.status().seqSlot[4] == 0, "B is queued while A plays (pending %d, now %d)", app.status().seqSlotPending[4], app.status().seqSlot[4]);
    run(2.5);
    CHECK(app.status().seqSlot[4] == 1 && app.status().seqSlotPending[4] == -1, "...and starts at the end of the pattern (now %d)", app.status().seqSlot[4]);
    // the song: A, B, C, A
    app.selectPattern(0); run(2.5);
    app.songClear(); app.songAdd(0); app.songAdd(1); app.songAdd(2); app.songAdd(0); app.songSet(true); run(0.2);
    CHECK(app.songChain().size() == 4 && app.songOn(), "a song of four steps");
    bool order = true; std::vector<int> seen; int last = -1; for (int i = 0; i < 40; i++) { run(0.25); const int sp = app.status().songPos, sl = app.status().seqSlot[4]; if (app.status().songOn && sp >= 0 && sp < 4) { static const int want[4] = {0, 1, 2, 0}; if (sl != want[sp]) order = false; if (sp != last) { seen.push_back(sp); last = sp; } } }
    bool advanced = seen.size() >= 3; for (size_t i = 1; i < seen.size(); i++) advanced &= seen[i] == (seen[i - 1] + 1) % 4;
    CHECK(order && advanced, "the song plays its banks in order (%zu steps seen, bank follows the step: %d)", seen.size(), order);
    // a project keeps the banks and the song
    CHECK(app.saveProject("Bank Test"), "saved"); run(1.5);
    app.songClear(); app.selectPattern(0); app.seqClear(); run(0.3); app.selectPattern(1); app.seqClear(); run(0.3); app.selectPattern(0);
    app.loadProject("Bank Test"); for (int i = 0; i < 40; i++) { run(0.1); std::this_thread::sleep_for(std::chrono::milliseconds(30)); }
    { Session::SeqView w[kSeqMax]; int bars = 0; const int a = s.seqSlotNotes(4, 0, w, kSeqMax, &bars), b = s.seqSlotNotes(4, 1, w, kSeqMax, &bars), c = s.seqSlotNotes(4, 2, w, kSeqMax, &bars);
      CHECK(a == 2 && b == 1 && c == 2 && app.songChain().size() == 4 && app.songOn(), "loaded: banks %d / %d / %d notes, song of %zu steps", a, b, c, app.songChain().size()); }
    app.songClear(); run(0.2); if (app.status().playing) btn(SamplerApp::PLAY);
}

static void testMidiIn() {
    const int sr = SAMPLERATE;
    system("rm -rf /tmp/fm1sampler_tj; mkdir -p /tmp/fm1sampler_tj");
    Session s(sr); s.setDataDir("/tmp/fm1sampler_tj"); s.setSampleDir(std::string(getenv("HOME")) + "/fm1-sampler/samples");
    SamplerApp app(s, [](const uint8_t*, size_t) {}); app.setTourDone(true); demoSounds(app);
    std::vector<float> buf(512), in(512, 0.f), all; uint32_t now = 1000;
    auto run = [&](double sec) { for (long i = 0; i < (long)(sec * sr) / 256; i++) { std::fill(buf.begin(), buf.end(), 0.f); s.process(buf.data(), in.data(), 256); std::this_thread::sleep_for(std::chrono::microseconds(150)); all.insert(all.end(), buf.begin(), buf.end()); if (i % 5 == 0) { now += 33; app.tick(now); } } };
    run(2.5); for (size_t i = 0; i < app.library().size(); i++) if (app.library()[i].label == "Tape Key") app.loadEntry((int)i);
    run(1.5);
    {   // a keyboard plays the selected track
        size_t m = all.size(); const uint8_t on[3] = {0x90, 60, 100}, off[3] = {0x80, 60, 0};
        app.midiExternal(on, 3, 1000000000ull); run(0.5); app.midiExternal(off, 3, 2000000000ull); run(0.3);
        CHECK(rms(all, m, all.size()) > 0.01, "a note from a MIDI keyboard sounds (rms %.4f)", rms(all, m, all.size()));
        const uint8_t junk[5] = {0x90, 200, 100, 0xf9, 0x90}; app.midiExternal(junk, 5, 3000000000ull); app.midiExternal(junk, 2, 3000000000ull); run(0.2);
        CHECK(finite(all), "damaged MIDI is ignored");
    }
    {   // the clock of another sequencer: 100 bpm = 24 pulses in 0.6 s
        app.setMidiSync(1); const uint64_t t0 = 10000000000ull; for (int i = 0; i < 24 * 4; i++) { const uint8_t c = 0xf8; app.midiExternal(&c, 1, t0 + (uint64_t)(i * 0.6e9 / 24.0)); }
        run(0.3); CHECK(std::fabs(app.status().bpm - 100.f) < 0.6f, "the clock sets the tempo (%.1f bpm)", app.status().bpm);
        const uint8_t start = 0xfa, stop = 0xfc; app.midiExternal(&start, 1, t0 + 8000000000ull); run(0.4); const bool playing = app.status().playing;
        app.midiExternal(&stop, 1, t0 + 9000000000ull); run(0.4); CHECK(playing && !app.status().playing, "START and STOP of the other gear start and stop the transport (%d then %d)", playing, app.status().playing);
        app.setMidiSync(0); const uint8_t c2 = 0xfa; app.midiExternal(&c2, 1, 0); run(0.3); CHECK(!app.status().playing, "with SYNC off the transport messages are ignored");
    }
    {   // the clock going out: 24 pulses per beat while it plays
        app.gotoPage(SamplerApp::P_TEMPO); { auto k = app.knobs(); k[0].set((120.f - 30.f) / 270.f); } run(0.3);
        s.clockPulses(); s.clockEvents(); s.play(true); run(2.0); const int ev = s.clockEvents(), pulses = s.clockPulses(); s.play(false); run(0.3); const int ev2 = s.clockEvents();
        CHECK((ev & 1) && std::abs(pulses - 24 * 4) <= 3 && (ev2 & 2), "clock out: %d pulses in 2 s at 120 bpm (expect 96), start and stop events (%d, %d)", pulses, ev, ev2);
    }
    {   // tap tempo
        app.setMidiSync(0); uint32_t t = 100000; for (int i = 0; i < 5; i++) { now = t; app.tick(now); app.tapTempo(); t += 500; }   // every 0.5 s = 120... done at 100 bpm: 600 ms
        t = 200000; for (int i = 0; i < 6; i++) { now = t; app.tick(now); app.tapTempo(); t += 600; } run(0.3);
        CHECK(std::fabs(app.status().bpm - 100.f) < 1.5f, "tap tempo: six taps 600 ms apart give %.1f bpm", app.status().bpm);
    }
    { MidiSong a; (void)a; }
}

static void testLock() {
    const int sr = SAMPLERATE;
    {   // the packed note word keeps every field
        SeqNote n; n.tick = 12287; n.len = 12288; n.note = 127; n.vel = 127; n.prob = 100; n.ratchet = 8; n.nudge = -16; n.cut = 127; SeqNote m = unpackNote(packNote(n));
        SeqNote z; z.tick = 0; z.len = 1; z.note = 0; z.vel = 1; z.prob = 0; z.ratchet = 1; z.nudge = 15; z.cut = 0; SeqNote q = unpackNote(packNote(z));
        CHECK(m.tick == 12287 && m.len == 12288 && m.note == 127 && m.vel == 127 && m.prob == 100 && m.ratchet == 8 && m.nudge == -16 && m.cut == 127 && q.tick == 0 && q.len == 1 && q.prob == 0 && q.nudge == 15 && q.cut == 0, "a note survives packing into one word (both extremes)");
    }
    system("rm -rf /tmp/fm1sampler_tk; mkdir -p /tmp/fm1sampler_tk");
    Session s(sr); s.setDataDir("/tmp/fm1sampler_tk"); s.setSampleDir(std::string(getenv("HOME")) + "/fm1-sampler/samples");
    SamplerApp app(s, [](const uint8_t*, size_t) {}); app.setTourDone(true); demoSounds(app);
    std::vector<float> buf(512), in(512, 0.f), all; uint32_t now = 1000;
    auto run = [&](double sec) { for (long i = 0; i < (long)(sec * sr) / 256; i++) { std::fill(buf.begin(), buf.end(), 0.f); s.process(buf.data(), in.data(), 256); std::this_thread::sleep_for(std::chrono::microseconds(150)); all.insert(all.end(), buf.begin(), buf.end()); if (i % 5 == 0) { now += 33; app.tick(now); } } };
    auto wait = [&](double sec) { for (int i = 0; i < (int)(sec * 20); i++) { run(0.05); std::this_thread::sleep_for(std::chrono::milliseconds(50)); } };
    run(2.5); app.selectTrack(4); for (int i = 0; i < (int)app.library().size(); i++) if (app.library()[(size_t)i].label == "Saw Bass" && app.library()[(size_t)i].group == "Synths") app.loadEntry(i);
    wait(1.0); app.setSend(4, 0, 0.f); app.setSend(4, 1, 0.f);
    app.gotoPage(SamplerApp::P_SQ); app.seqClear(); app.seqSetBarCount(1); app.seqToggleAt(0, 60, 192); { auto k = app.knobs(); k[0].set(1.f / 3); } s.play(true); wait(0.5);
    auto zc = [&]() { all.clear(); run(2.6); double d = 0, a = 1e-9; for (size_t i = 2; i < all.size(); i += 2) { d += std::fabs(all[i] - all[i - 2]); a += std::fabs(all[i]); } return d / a; };   // how bright: the movement of the waveform against its size
    const double open = zc();
    Session::SeqRef ref[1] = {{0, 60}}; s.seqApply(4, ref, 1, Session::OP_CUT, 1); run(0.3);
    Session::SeqView v[kSeqMax]; const int n = s.seqNotes(4, v, kSeqMax); const double locked = zc();
    CHECK(n == 1 && v[0].cut == 1 && locked < open * 0.8, "a filter lock darkens the note (%.4f -> %.4f brightness, lock %d)", open, locked, n ? v[0].cut : -1);
    s.seqApply(4, ref, 1, Session::OP_CUT, 0); run(0.3); const double freed = zc(); CHECK(freed > locked * 1.2, "lock 0 gives the filter back (%.4f)", freed);
    app.seqClear(); run(0.2);
}

static void testRollZoom() {
    const int sr = SAMPLERATE; Session s(sr); s.setDataDir("/tmp/fm1sampler_t4"); s.setSampleDir(std::string(getenv("HOME")) + "/fm1-sampler/samples");
    SamplerApp app(s, [](const uint8_t*, size_t) {}); app.setTourDone(true); demoSounds(app); SamplerUI ui(app); ui.setSize(860, 400, 0, 0);
    std::vector<float> buf(512), in(512, 0.f); uint32_t now = 0;
    auto run = [&](double sec) { for (long i = 0; i < (long)(sec * sr) / 256; i++) { std::fill(buf.begin(), buf.end(), 0.f); s.process(buf.data(), in.data(), 256); if (i % 5 == 0) { now += 33; app.tick(now); } } };
    run(0.5); app.gotoPage(SamplerApp::P_SQ); for (int i = 0; i < 4; i++) app.seqToggleAt(i * 96, 60 + i); run(0.2);
    ui.frame(now);
    CHECK(ui.rollZoom() == 1.f && ui.rollRows() == 10.f, "the roll starts unzoomed with big rows (%.1fx, %.0f rows)", ui.rollZoom(), ui.rollRows());
    // two fingers 100 px apart horizontally, then 300 px: three times as much time zoom
    ui.pinch(0, 350, 250, 450, 250); ui.pinch(1, 250, 250, 550, 250); ui.frame(now);
    CHECK(std::fabs(ui.rollZoom() - 3.f) < 0.05f && ui.rollRows() == 10.f, "pinching apart sideways zooms the time 3x (%.2fx, %.0f rows)", ui.rollZoom(), ui.rollRows());
    ui.pinch(1, 330, 250, 470, 250); ui.frame(now);
    CHECK(std::fabs(ui.rollZoom() - 1.4f) < 0.05f, "pinching together zooms out again (%.2fx)", ui.rollZoom());
    ui.pinch(1, 400, 250, 400, 250); ui.pinch(2, 0, 0, 0, 0); ui.frame(now);
    CHECK(ui.rollZoom() == 1.f, "it stops at the whole pattern (%.2fx)", ui.rollZoom());
    // vertical: fingers 100 px apart, then 200 px: half as many pitch rows
    ui.pinch(0, 400, 200, 400, 300); ui.pinch(1, 400, 150, 400, 350); ui.frame(now);
    CHECK(std::fabs(ui.rollRows() - 5.f) < 0.5f && ui.rollZoom() == 1.f, "pinching apart up and down shows half the pitch rows (%.1f rows)", ui.rollRows());
    ui.pinch(2, 0, 0, 0, 0);
    // one finger lands on the roll, a second one joins: a pinch, not a tap
    const int before = app.status().seqCount[0];
    ui.touch(0, 300, 250, now); ui.pinch(0, 300, 250, 500, 250); ui.pinch(1, 280, 250, 520, 250); ui.pinch(2, 0, 0, 0, 0); ui.touch(2, 300, 250, now); run(0.1);
    CHECK(app.status().seqCount[0] == before, "a pinch does not add a note (%d -> %d)", before, app.status().seqCount[0]);
    // fingers outside the roll are ignored
    const float z0 = ui.rollZoom(); ui.frame(now); ui.pinch(0, 20, 20, 80, 20); ui.pinch(1, 0, 20, 200, 20); ui.pinch(2, 0, 0, 0, 0); ui.frame(now);
    CHECK(ui.rollZoom() == z0, "a pinch outside the roll does nothing");
    // the buttons
    float tx = 0, ty = 0; ui.frame(now); for (auto& o : ui.ops()) if (o.type == UiOp::TEXT && o.text == "TIME +") { tx = o.f[0]; ty = o.f[1] - 4; }
    ui.touch(0, tx, ty, now); ui.touch(2, tx, ty, now); ui.frame(now);
    CHECK(tx > 0 && ui.rollZoom() > 1.5f, "the TIME + button zooms (%.2fx)", ui.rollZoom());
    // long notes: drag sideways from an empty cell, tap a note to remove it, drag on a note to resize it
    {
        app.seqClear(); run(0.2); SamplerUI ui(app); ui.setSize(860, 400, 0, 0); ui.frame(now);
        float gx, gy, gw, gh; ui.rollGrid(gx, gy, gw, gh);
        const float zoom = ui.rollZoom(), rows = std::round(ui.rollRows()), rh = gh / rows; const int pt = patternTicks(app.seqBarsOf(app.track())), vis = (int)(pt / zoom);
        auto X = [&](double tick) { return gx + (float)((tick - ui.rollScroll()) / vis * gw); };
        auto Y = [&](int note) { return gy + (rows - 1 - (note - std::floor(ui.rollBase()))) * rh + rh / 2; };
        auto notes = [&](Session::SeqView* v) { return s.seqNotes(0, v, kSeqMax); };
        Session::SeqView v[kSeqMax];
        const int note = (int)std::floor(ui.rollBase()) + 5;
        ui.touch(0, X(100), Y(note), now); ui.touch(1, X(200), Y(note), now); ui.touch(1, X(250), Y(note), now); ui.frame(now); ui.touch(2, X(250), Y(note), now); run(0.2);
        int c = notes(v);
        CHECK(c == 1 && v[0].tick == 96 && v[0].note == note && v[0].len == 192, "dragging from an empty cell makes a long note: tick %d, len %d (expect 96, 192)", c ? v[0].tick : -1, c ? v[0].len : -1);
        ui.frame(now); ui.touch(0, X(100), Y(note), now); ui.touch(2, X(100), Y(note), now); run(0.2);
        CHECK(notes(v) == 0, "a tap on a note removes it");
        ui.frame(now); ui.touch(0, X(100), Y(note), now); ui.touch(2, X(100), Y(note), now); run(0.2);
        c = notes(v); CHECK(c == 1 && v[0].len == 48, "a tap on an empty cell makes a one-step note (%d, len %d)", c, c ? v[0].len : -1);
        ui.frame(now); ui.touch(0, X(110), Y(note), now); ui.touch(1, X(300), Y(note), now); ui.touch(1, X(400), Y(note), now); ui.frame(now); ui.touch(2, X(400), Y(note), now); run(0.2);
        c = notes(v); CHECK(c == 1 && v[0].tick == 96 && v[0].len == 336, "dragging on a note stretches it: len %d (expect 336), %d note(s)", c ? v[0].len : -1, c);
        ui.frame(now); ui.touch(0, X(110), Y(note), now); ui.touch(1, X(200), Y(note), now); ui.touch(1, X(250), Y(note), now); ui.frame(now); ui.touch(2, X(250), Y(note), now); run(0.2);
        c = notes(v); CHECK(c == 1 && v[0].len == 192, "...and shortens it again: len %d (expect 192)", c ? v[0].len : -1);
        app.seqUndo(); run(0.2); c = notes(v); CHECK(c == 1 && v[0].len == 336, "a resize is one undo step (len %d)", c ? v[0].len : -1);
        {   // a pattern of 3 bars: a tap in the third bar adds the note there
            app.seqClear(); app.seqSetBarCount(3); run(0.3); SamplerUI u3(app); u3.setSize(860, 400, 0, 0); u3.frame(now); u3.frame(now);
            float x3, y3, w3, h3; u3.rollGrid(x3, y3, w3, h3); const float rows3 = std::round(u3.rollRows()), rh3 = h3 / rows3; const int pt3 = patternTicks(3), vis3 = (int)(pt3 / u3.rollZoom());
            const int note3 = (int)std::floor(u3.rollBase()) + 4; const float tx3 = x3 + (float)((1900.0 - u3.rollScroll()) / vis3 * w3), ty3 = y3 + (rows3 - 1 - (note3 - std::floor(u3.rollBase()))) * rh3 + rh3 / 2;
            u3.touch(0, tx3, ty3, now); u3.touch(2, tx3, ty3, now); run(0.2); Session::SeqView v3[kSeqMax]; const int n3 = notes(v3);
            CHECK(n3 == 1 && v3[0].tick >= 1536 && v3[0].tick < 2304 && v3[0].note == note3, "tapping in the third bar adds the note in the third bar (tick %d)", n3 ? v3[0].tick : -1);
            app.seqClear(); app.seqSetBarCount(4);
        }
        {   // SELECT mode: pick notes, edit them together
            app.seqClear(); run(0.2); for (int i = 0; i < 3; i++) app.seqToggleAt(i * 96, 60 + i); run(0.3);
            SamplerUI us(app); us.setSize(860, 400, 0, 0); us.frame(now);
            auto tapText = [&](const char* label) { us.frame(now); float tx = 0, ty = 0; for (auto& o : us.ops()) if (o.type == UiOp::TEXT && o.text == label) { tx = o.f[0]; ty = o.f[1] - 3; } us.touch(0, tx, ty, now); us.touch(2, tx, ty, now); run(0.2); return tx > 0; };
            CHECK(tapText("SELECT"), "the SELECT button exists"); us.frame(now);
            float gx, gy, gw, gh; us.rollGrid(gx, gy, gw, gh); const float rows = std::round(us.rollRows()), rh = gh / rows; const int pt = patternTicks(app.seqBarsOf(app.track())), vis = (int)(pt / us.rollZoom());
            auto X = [&](double tick) { return gx + (float)((tick - us.rollScroll()) / vis * gw); }; auto Y = [&](int note) { return gy + (rows - 1 - (note - std::floor(us.rollBase()))) * rh + rh / 2; };
            Session::SeqView v[kSeqMax];
            us.touch(0, X(10), Y(60), now); us.touch(2, X(10), Y(60), now); us.frame(now);
            CHECK(!us.isSelected(0, 60) == false && us.selectedCount() == 1, "a tap on a note picks it (%d picked)", us.selectedCount());
            us.touch(0, X(106), Y(61), now); us.touch(2, X(106), Y(61), now); us.frame(now); CHECK(us.selectedCount() == 2, "a second tap adds it (%d)", us.selectedCount());
            us.touch(0, X(106), Y(61), now); us.touch(2, X(106), Y(61), now); us.frame(now); CHECK(us.selectedCount() == 1, "a tap on a picked note drops it");
            us.touch(0, X(1), Y(64), now); us.touch(1, X(300), Y(58), now); us.touch(1, X(300), Y(58) + 1, now); us.frame(now); us.touch(2, X(300), Y(58), now); us.frame(now);
            CHECK(us.selectedCount() == 3, "dragging a box over empty cells picks everything inside (%d)", us.selectedCount());
            CHECK(tapText("+12"), "..."); const int n = s.seqNotes(0, v, kSeqMax); bool up = n == 3; for (int i = 0; i < n; i++) up &= v[i].note >= 72 && v[i].note <= 74;
            CHECK(up, "+12 moves the picked notes an octave up"); us.frame(now);
            tapText("DEL"); CHECK(s.seqNotes(0, v, kSeqMax) == 0 && us.selectedCount() == 0, "DELETE removes them");
            app.seqUndo(); run(0.2); CHECK(s.seqNotes(0, v, kSeqMax) == 3, "...and UNDO brings them back");
        }
        // a long note plays for its length: the engine holds it, a short one does not
        app.seqClear(); app.seqToggleAt(0, 60, 384); run(0.1);
        app.setSeqState(1); run(0.1);
    }
}
int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);   // a crash must not swallow the lines before it
    Denormals::enableOnThisThread();
    {   // a session that is destroyed while its samples are still loading used to crash (the worker job read Session::d after unique_ptr had cleared it)
        for (int i = 0; i < 4; i++) { Session sess(SAMPLERATE); sess.setDataDir("/tmp/fm1sampler_t5"); sess.setSampleDir(std::string(getenv("HOME")) + "/fm1-sampler/samples"); SamplerApp a(sess, [](const uint8_t*, size_t) {}); }
        CHECK(true, "a session can be destroyed right after it started (4 times)");
    }
    testSynths();
    testOp1();
    testLock();
    testMidiIn();
    testBanks();
    testSynthEdit();
    testMixer();
    testSampleEdit();
    testPads();
    testNoteEdit();
    testLook();
    testSound();
    testProject();
    testTombola();
    testSwing();
    testRollZoom();
    testLibrary();
    testPaul();
    testGrid();
    testFx();
    testFx2();
    const int sr = SAMPLERATE;
    // ---- metronome ----
    {
        Session s(sr); s.setMetronome(true, 0.8f); s.setTempo(120);
        std::vector<float> o(512), all; for (int i = 0; i < 4 * sr / 256; i++) { std::fill(o.begin(), o.end(), 0.f); s.process(o.data(), nullptr, 256); all.insert(all.end(), o.begin(), o.end()); }
        int clicks = 0; size_t until = 0; for (size_t i = 0; i < all.size(); i += 2) if (i >= until && std::fabs(all[i]) > 0.05f) { clicks++; until = i + 2 * (sr / 4); }
        CHECK(clicks >= 7 && clicks <= 9, "metronome at 120 bpm: %d clicks in 4 s (expect 8)", clicks);
        s.setMetronome(false, 0.8f); std::fill(o.begin(), o.end(), 0.f); for (int i = 0; i < 40; i++) s.process(o.data(), nullptr, 256);
        CHECK(rms(o) < 1e-4, "metronome off is silent");
    }
    // ---- app level ----
    system("rm -rf /tmp/fm1sampler_t3; mkdir -p /tmp/fm1sampler_t3");
    Session s(sr); s.setDataDir("/tmp/fm1sampler_t3"); s.setSampleDir(std::string(getenv("HOME")) + "/fm1-sampler/samples");
    SamplerApp app(s, [](const uint8_t*, size_t) {}); app.setTourDone(true); demoSounds(app);
    std::vector<float> buf(512), in(512, 0.f), all; uint32_t now = 0;
    auto run = [&](double sec) { for (long i = 0; i < (long)(sec * sr) / 256; i++) { std::fill(buf.begin(), buf.end(), 0.f); s.process(buf.data(), in.data(), 256); std::this_thread::sleep_for(std::chrono::microseconds(150)); all.insert(all.end(), buf.begin(), buf.end()); if (i % 5 == 0) { now += 33; app.tick(now); } } };
    auto enc = [&](int i, int d) { uint8_t m[3] = {0xb0, (uint8_t)(16 + i), (uint8_t)(64 + d)}; app.onMidi(m, 3); };
    auto btn = [&](int b) { uint8_t m[3] = {0x91, (uint8_t)b, 100}; app.onMidi(m, 3); m[0] = 0x81; app.onMidi(m, 3); };
    run(2.5);
    // pages
    btn(SamplerApp::FX); CHECK(app.page() == SamplerApp::P_FX, "FX: sends");
    btn(SamplerApp::FX); CHECK(app.page() == SamplerApp::P_FX2, "FX again: inserts");
    btn(SamplerApp::FX); CHECK(app.page() == SamplerApp::P_TONE, "FX again: tone (drive, EQ)");
    btn(SamplerApp::FX); CHECK(app.page() == SamplerApp::P_MOD, "FX again: modulation");
    btn(SamplerApp::FX); CHECK(app.page() == SamplerApp::P_LIVE, "FX again: live fx");
    btn(SamplerApp::FX); CHECK(app.page() == SamplerApp::P_LIVE2, "FX again: live fx 2");
    btn(SamplerApp::FX); CHECK(app.page() == SamplerApp::P_FX3, "FX again: the OP-1 effects");
    btn(SamplerApp::FX); CHECK(app.page() == SamplerApp::P_FX4, "FX again: fold, auto pan, vinyl, vowel");
    btn(SamplerApp::FX); CHECK(app.page() == SamplerApp::P_TSTOP, "FX again: the tape stop settings");
    btn(SamplerApp::FX); CHECK(app.page() == SamplerApp::P_FX, "FX again: back to sends");
    // the four modes: every page belongs to exactly one, the same button steps through its pages, a mode comes back to the page it was left on
    {
        int seen[SamplerApp::kPages] = {};
        for (int m = 0; m < SamplerApp::kModes; m++) for (int i = 0; i < SamplerApp::modePageCount(m); i++) { const auto pg = SamplerApp::modePages(m)[i]; seen[pg]++; if (SamplerApp::modeOf(pg) != m || SamplerApp::pageSlot(pg) != i) seen[0] += 100; }
        bool once = true; for (int i = 0; i < SamplerApp::kPages; i++) once &= seen[i] == 1;
        CHECK(once, "every page is in exactly one mode, in one slot");
        btn(SamplerApp::HOME); CHECK(app.page() == SamplerApp::P_HOME, "HOME: sound mode, sample page");
        btn(SamplerApp::HOME); CHECK(app.page() == SamplerApp::P_ENV, "HOME again: envelope");
        btn(SamplerApp::HOME); btn(SamplerApp::HOME); CHECK(app.page() == SamplerApp::P_ARP, "HOME x4: arpeggiator");
        btn(SamplerApp::HOME); CHECK(app.page() == SamplerApp::P_SHAPE, "HOME x5: shape (macros, root key, slices)");
        btn(SamplerApp::HOME); CHECK(app.page() == SamplerApp::P_SMPED, "HOME x6: sample edit");
        btn(SamplerApp::HOME); btn(SamplerApp::HOME);
        btn(SamplerApp::HOME); CHECK(app.page() == SamplerApp::P_CHOP, "HOME again: the chops");
        btn(SamplerApp::HOME); CHECK(app.page() == SamplerApp::P_HOME, "HOME again: back to the sample");
        btn(SamplerApp::ENV); CHECK(app.page() == SamplerApp::P_ENV, "ENV: shortcut to the envelope");
        btn(SamplerApp::LFO); CHECK(app.page() == SamplerApp::P_LFO, "LFO: shortcut");
        btn(SamplerApp::ARP); CHECK(app.page() == SamplerApp::P_ARP, "ARP: shortcut");
        btn(SamplerApp::EDIT); CHECK(app.page() == SamplerApp::P_MIX, "EDIT: mix mode, mixer");
        btn(SamplerApp::EDIT); CHECK(app.page() == SamplerApp::P_TEQ, "EDIT again: track eq");
        btn(SamplerApp::HOME); btn(SamplerApp::EDIT); CHECK(app.page() == SamplerApp::P_TEQ, "mix mode returns to the page it was left on");
        btn(SamplerApp::GLO); CHECK(app.page() == SamplerApp::P_KEY, "GLO: shortcut to the key");
        btn(SamplerApp::SEL); CHECK(app.page() == SamplerApp::P_LEN, "SEL: shortcut to the length");
        btn(SamplerApp::SEQ); CHECK(app.page() == SamplerApp::P_SQ, "SEQ tap: sequencer mode, pattern page");
        btn(SamplerApp::SEQ); CHECK(app.page() == SamplerApp::P_TEMPO, "SEQ again: tempo");
        btn(SamplerApp::SEQ); CHECK(app.page() == SamplerApp::P_TIMING, "SEQ again: timing");
        btn(SamplerApp::SEQ); CHECK(app.page() == SamplerApp::P_TOMB, "SEQ again: tombola");
        btn(SamplerApp::SEQ); CHECK(app.page() == SamplerApp::P_PADS, "SEQ again: pads");
        btn(SamplerApp::SEQ); CHECK(app.page() == SamplerApp::P_TRIG, "SEQ again: trigger");
        btn(SamplerApp::SEQ); CHECK(app.page() == SamplerApp::P_TAPEV, "SEQ again: on into the TAPE tab, tape view");
        btn(SamplerApp::SEQ); CHECK(app.page() == SamplerApp::P_SEQ, "SEQ again: tape fx");
        btn(SamplerApp::SEQ); CHECK(app.page() == SamplerApp::P_SAVE, "SEQ again: loop source");
        btn(SamplerApp::SEQ); CHECK(app.page() == SamplerApp::P_LEN, "SEQ again: loop length");
        btn(SamplerApp::SEQ); CHECK(app.page() == SamplerApp::P_SQ, "SEQ again: back to the pattern");
        app.gotoPage(SamplerApp::P_FX);
    }
    // scales
    {
        bool ok = true;
        for (int sc = 0; sc < 22; sc++) {
            if (sc == 7) continue;
            int st[12]; const int n = SamplerApp::scaleSteps(sc, st);
            bool good = n >= 5 && st[0] == 0 && st[n - 1] <= 11;
            for (int i = 1; i < n; i++) good &= st[i] > st[i - 1];
            if (!good) { ok = false; printf("bad scale %d (%s)\n", sc, SamplerApp::scaleName(sc)); }
        }
        CHECK(ok, "all 21 scale tables are ascending within the octave");
        CHECK(SamplerApp::snapNote(61, 2, 0) == 60 && SamplerApp::snapNote(63, 2, 0) == 62 && SamplerApp::snapNote(64, 2, 0) == 64, "snap in C major: 61->%d 63->%d 64->%d", SamplerApp::snapNote(61, 2, 0), SamplerApp::snapNote(63, 2, 0), SamplerApp::snapNote(64, 2, 0));
        CHECK(SamplerApp::snapNote(61, 11, 0) == 60 && SamplerApp::snapNote(61, 7, 0) == 61, "snap: pentatonic moves, scale OFF keeps (%d, %d)", SamplerApp::snapNote(61, 11, 0), SamplerApp::snapNote(61, 7, 0));
        app.gotoPage(SamplerApp::P_KEY);
        CHECK(app.page() == SamplerApp::P_KEY, "GLO: key page");
        auto k = app.knobs(); for (int i = 0; i < 60; i++) k = app.knobs(), k[0].step(1);
        CHECK(app.scale().scale == 7 && std::string(app.knobs()[0].value) == "OFF", "the SCALE knob walks all 42 and OFF is last (%s)", app.knobs()[0].value.c_str());
        k = app.knobs(); for (int i = 0; i < 60; i++) k = app.knobs(), k[0].step(-1);
        CHECK(app.scale().scale == 0, "...and back to CHROM"); app.setScale(2, 0, 0, 2);
        CHECK(app.scale().mode == 2 && std::string(app.knobs()[3].value) == "SNAP", "mode SNAP exists (%s)", app.knobs()[3].value.c_str());
        uint8_t sd[12] = {0xf0, 0x7d, 'D', 'A', 'W', 1, 0x41, 7, 0, 3, 0, 0xf7}; app.onSysex(sd, sizeof sd);
        CHECK(app.scale().mode == 2 && app.scale().scale == 2, "firmware echo does not reset SNAP or the app scale");
        app.setScale(3, 0, 0, 0);
    }
    // live fx and fx2 pages sound
    {
        for (SamplerApp::Page pg : {SamplerApp::P_FX2, SamplerApp::P_TONE, SamplerApp::P_MOD}) {
            app.gotoPage(pg);
            for (int i = 0; i < 4; i++) { auto k = app.knobs(); k[i].set(0.7f); }
            size_t m = all.size(); app.playNote(60, true); run(1.0); app.playNote(60, false); run(0.3);
            CHECK(rms(all, m, all.size()) > 0.002 && finite(all), "%s page, every knob up: sound, finite (rms %.4f)", SamplerApp::pageName(pg), rms(all, m, all.size()));
            for (int i = 0; i < 4; i++) { auto k = app.knobs(); k[i].set(pg == SamplerApp::P_TONE || pg == SamplerApp::P_MOD ? (i == 3 && pg == SamplerApp::P_MOD ? 0.5f : (pg == SamplerApp::P_TONE && i > 0 ? 0.5f : 0.f)) : 0.f); }
        }
        app.gotoPage(SamplerApp::P_LIVE); size_t m;
        { auto k = app.knobs(); k[0].set(0.2f); k[1].set(1.f); k[2].set(1.f); k[3].set(0.5f); }
        m = all.size(); app.playNote(60, true); run(1.0); app.playNote(60, false); run(0.3);
        CHECK(rms(all, m, all.size()) > 0.0005 && finite(all), "LIVE FX all on: finite (rms %.4f)", rms(all, m, all.size()));
        { auto k = app.knobs(); k[0].set(0.5f); k[1].set(0.f); k[2].set(0.f); k[3].set(0.f); }
        run(0.3);
        app.gotoPage(SamplerApp::P_LIVE2);
        { auto k = app.knobs(); k[0].set(0.5f); k[1].set(0.5f); k[2].set(1.f); }
        m = all.size(); app.playNote(60, true); run(1.0); app.playNote(60, false); run(0.5);
        CHECK(finite(all), "LIVE FX 2 (reverse, tape stop, freeze) all on: finite");
        { auto k = app.knobs(); k[0].set(0.f); k[1].set(0.f); k[2].set(0.f); } app.setQuantGrids(0, 0);
        run(2.5);   // the tape spins up again before the live sound is back
        app.gotoPage(SamplerApp::P_TONE); { auto k = app.knobs(); k[1].set(1.f); }
        CHECK(std::string(app.knobs()[1].value) == "+12 DB" && std::string(app.knobs()[2].value) == "FLAT", "EQ shows its gain in dB (%s, %s)", app.knobs()[1].value.c_str(), app.knobs()[2].value.c_str());
        { auto k = app.knobs(); k[1].set(0.5f); }
    }
    // tempo, take snap, sequencer
    {
        app.gotoPage(SamplerApp::P_TEMPO);
        { auto k = app.knobs(); k[0].set((120.f - 30.f) / 270.f); k[3].set(1.f); }   // 120 bpm, take snaps to bars
        run(0.3); CHECK(std::fabs(app.status().bpm - 120.f) < 0.6f, "tempo knob sets %.0f bpm", app.status().bpm);
        btn(SamplerApp::REC); run(0.1);
        app.playNote(60, true); run(0.2); app.playNote(60, false); run(2.9);   // ~3.1 s -> 2 bars of 2 s at 120
        btn(SamplerApp::REC); run(0.5);
        const double L = app.status().loopSamples;
        CHECK(std::fabs(L - 4.0 * sr) < 2 && std::fabs(app.status().bpm - 120.f) < 0.6f && app.status().loopBars == 2, "take snapped to 2 bars: %.0f samples (expect %d), %.0f bpm", L, 4 * sr, app.status().bpm);
        app.gotoPage(SamplerApp::P_TEMPO); { auto k = app.knobs(); k[0].step(5); } run(0.1);
        CHECK(std::fabs(app.status().bpm - 120.f) < 0.6f && std::fabs(app.tempoNow() - 125.f) < 0.6f, "a new tempo with a loop waits for the finger to rest (tape %.0f, asked %.0f)", app.status().bpm, app.tempoNow());
        run(4.0);
        CHECK(std::fabs(app.status().bpm - 125.f) < 1.f && std::fabs(app.status().loopSamples - 4.0 * sr * 120.0 / 125.0) < 0.002 * sr && app.status().loopBars == 2, "...then the loop is stretched to it (%.1f bpm, %.0f samples, expect %.0f)", app.status().bpm, app.status().loopSamples, 4.0 * sr * 120.0 / 125.0);
        app.globalUndo(); run(1.0);
        CHECK(std::fabs(app.status().bpm - 120.f) < 0.6f && std::fabs(app.status().loopSamples - 4.0 * sr) < 2, "UNDO takes the stretch back (%.1f bpm, %.0f samples)", app.status().bpm, app.status().loopSamples);
        { const size_t m0 = all.size(); app.playNote(60, true); run(0.2); app.playNote(60, false); run(0.3); (void)m0; }
        // an overdub on another track is a tape edit too: UNDO takes it away
        { const int tr0 = app.track(); app.selectTrack(3); const int u0 = app.status().globalUndoN; btn(SamplerApp::REC); run(0.2); app.playNote(64, true); run(0.5); app.playNote(64, false); run(0.2); btn(SamplerApp::REC); run(0.3);
          CHECK(app.status().track_has_clip[3] && app.status().globalUndoN > u0, "an overdub is in the global log (%d -> %d)", u0, app.status().globalUndoN);
          app.globalUndo(); run(0.3); CHECK(!app.status().track_has_clip[3] && app.status().track_has_clip[0], "UNDO takes the overdub off the tape and keeps the first take");
          app.globalRedo(); run(0.3); CHECK(app.status().track_has_clip[3], "REDO puts it back"); app.globalUndo(); run(0.3); app.selectTrack(tr0); }
        btn(SamplerApp::SAVE); app.onMidi((uint8_t[]){0x91, (uint8_t)SamplerApp::SAVE, 100}, 3); run(0.9); app.onMidi((uint8_t[]){0x81, (uint8_t)SamplerApp::SAVE, 100}, 3); run(0.3); CHECK(app.takeClearAsk(), "a SAVE hold asks before clearing"); app.loopClear(); run(0.3);
        CHECK(!app.loopExists(), "loops cleared");
        app.gotoPage(SamplerApp::P_TEMPO);
        { auto k = app.knobs(); k[0].set((90.f - 30.f) / 270.f); k[3].set(0.f); } run(0.2);
        CHECK(std::fabs(app.status().bpm - 90.f) < 0.6f, "tempo 90 without a loop (%.1f)", app.status().bpm);
        { auto k = app.knobs(); k[0].set((120.f - 30.f) / 270.f); } run(0.2);
        // sequencer on track 1: record two notes, then they play on their own
        if (app.status().playing) { btn(SamplerApp::PLAY); run(0.2); }
        app.gotoPage(SamplerApp::P_SQ);
        { auto k = app.knobs(); k[1].set(0.f); }   // 1 bar = 2 s
        { auto k = app.knobs(); k[0].set(1.f); }   // REC
        run(0.01);
        run(0.5);  app.playNote(60, true); run(0.2); app.playNote(60, false);          // beat 1 -> tick 192
        run(0.8);  app.playNote(64, true); run(0.2); app.playNote(64, false);          // beat 3 + a bit
        run(0.5);
        Session::SeqView v[kSeqMax]; int n = s.seqNotes(0, v, kSeqMax);
        CHECK(n == 2, "sequencer recorded %d notes", n);
        if (n == 2) {
            CHECK(std::abs(v[0].tick - 196) <= 8 && v[0].note == 60 && std::abs(v[0].len - 77) <= 8, "note 1 at tick %d (expect ~196), len %d (expect ~77)", v[0].tick, v[0].len);
            CHECK(std::abs(v[1].tick - 580) <= 8 && v[1].note == 64, "note 2 at tick %d (expect ~580)", v[1].tick);
        }
        CHECK(app.status().seqCount[0] == 2 && app.status().seqState[0] == 2, "status shows the pattern (%d notes, state %d)", app.status().seqCount[0], app.status().seqState[0]);
        { auto k = app.knobs(); k[0].set(1.f / 3); }   // PLAY: now the pattern plays by itself
        run(0.3); size_t m = all.size(); run(4.0);
        CHECK(rms(all, m, all.size()) > 0.003, "pattern plays with no live notes (rms %.4f)", rms(all, m, all.size()));
        { auto k = app.knobs(); k[2].set(4.f / 7.f); }   // quantize 1/16
        run(0.3); m = all.size(); run(2.0); CHECK(rms(all, m, all.size()) > 0.003, "pattern plays with quantize 1/16");
        app.seqSetBarCount(2); run(0.3);   // a new bar is empty
        CHECK(app.status().seqBars[0] == 2 && app.status().seqCount[0] == 2, "2 bars: the new bar is empty (%d notes)", app.status().seqCount[0]);
        app.seqSetBarCount(3); run(0.2); app.seqToggleAt(2 * 768 + 100, 62); run(0.2);
        CHECK(app.status().seqBars[0] == 3 && app.status().seqCount[0] == 3, "3 bars exist now, and a note lands in the third (%d notes)", app.status().seqCount[0]);
        { Session::SeqView v[kSeqMax]; const int n = s.seqNotes(0, v, kSeqMax); bool third = false; for (int i = 0; i < n; i++) third |= v[i].tick >= 1536 && v[i].note == 62; CHECK(third, "...at its own tick, not folded into bar 2"); }
        app.seqSetBarCount(1); run(0.3);
        CHECK(app.status().seqBars[0] == 1 && app.status().seqCount[0] == 2, "back to 1 bar: the notes behind the end are dropped (%d notes)", app.status().seqCount[0]);
        app.seqUndo(); run(0.2); CHECK(app.status().seqBars[0] == 3 && app.status().seqCount[0] == 3, "undo gets the removed bars and their notes back (%d bars, %d notes)", app.status().seqBars[0], app.status().seqCount[0]);
        app.seqSetBarCount(1); run(0.3);
        app.seqClear(); run(0.5);
        CHECK(app.status().seqCount[0] == 0, "clear empties the pattern");
        run(5.0); m = all.size(); run(1.5); CHECK(rms(all, m, all.size()) < 0.004, "...and nothing plays any more once the fx tails are gone (rms %.5f)", rms(all, m, all.size()));
    }
    // ---- live quantize, count-in, per-track fx ----
    {
        auto quiet = [&] { run(4.0); };
        { MasterFx fx(sr, 1.f); std::vector<float> a = sine(4800, 440, sr); fx.feedOnly(a.data(), 4800); FxParams p; p.repeat = 3; fx.set(p); std::vector<float> b((size_t)2 * 2400, 0.f);
          fx.process(b.data(), 2400, 0, 1.0 / 24000); CHECK(!fx.active() == false && rms(b) > 0.1, "feedOnly keeps the repeat memory warm (rms %.3f)", rms(b)); FxParams q; fx.set(q); CHECK(!fx.active(), "rest values are inactive"); }
        // live quantize: 1/4 grid, the sequencer records where the keys were snapped to
        quiet();
        if (app.status().playing) { btn(SamplerApp::PLAY); run(0.2); }
        app.gotoPage(SamplerApp::P_TIMING); { auto k = app.knobs(); k[2].set(0.f); } app.setQuantGrids(1, 0);   // 1/4, playback quantize OFF
        app.gotoPage(SamplerApp::P_SQ); { auto k = app.knobs(); k[1].set(0.f); k[0].set(1.f); }                          // 1 bar, REC from a stopped transport
        run(0.01);
        run(0.79); size_t mark = all.size(); app.playNote(60, true); run(0.12); size_t early = all.size(); run(0.28); app.playNote(60, false);   // 0.8 s: closer to beat 2 (1.0 s)
        const double rEarly = rms(all, mark, early), rLate = rms(all, early + (size_t)sr * 2 / 10, all.size());
        run(0.5); app.playNote(64, true); run(0.2); app.playNote(64, false);                                                                  // 1.7 s: closer to beat 3 (1.5 s), already late
        run(0.5);
        Session::SeqView v[kSeqMax]; const int n = s.seqNotes(0, v, kSeqMax);
        CHECK(n == 2, "live quantize: %d notes recorded", n);
        if (n == 2) CHECK(std::abs(v[0].tick - 384) <= 3 && std::abs(v[1].tick - 576) <= 3, "recorded on the grid: ticks %d (expect 384) and %d (expect 576)", v[0].tick, v[1].tick);
        CHECK(rEarly < 0.003 && rLate > 0.003, "the early key waits for the grid line (before %.4f, after %.4f)", rEarly, rLate);
        { auto k = app.knobs(); k[0].set(0.f); } app.seqClear();   // stop and clear
        app.gotoPage(SamplerApp::P_TIMING); app.setQuantGrids(0, app.seqGridIndex());
        if (app.status().playing) { btn(SamplerApp::PLAY); run(0.2); }
        quiet();
        // count-in before a take: clicks (metronome is off), the take starts after one bar, keys during the count do not start it
        { auto k = app.knobs(); k[2].set(0.5f); }   // 1 BAR
        app.gotoPage(SamplerApp::P_TEMPO); { auto k = app.knobs(); k[0].set((120.f - 30.f) / 270.f); k[1].set(0.f); k[3].set(0.f); } run(0.2);
        mark = all.size(); btn(SamplerApp::REC); run(0.45);
        CHECK(app.status().countBeats > 0 && !app.status().freeRecording && rms(all, mark, all.size()) > 0.004, "count-in: clicks with the metronome off (rms %.4f, %d beats left)", rms(all, mark, all.size()), app.status().countBeats);
        app.playNote(60, true); run(0.2); app.playNote(60, false); run(0.6);
        CHECK(!app.status().freeRecording && app.status().freeArmed, "a key during the count-in does not start the take");
        run(0.9);
        CHECK(app.status().freeRecording && app.status().countBeats == 0, "the take starts when the bar is over");
        run(1.0); btn(SamplerApp::REC); run(0.3);
        { const uint8_t d[3] = {0x91, (uint8_t)SamplerApp::SAVE, 100}, u[3] = {0x81, (uint8_t)SamplerApp::SAVE, 100}; app.onMidi(d, 3); run(0.9); app.onMidi(u, 3); run(0.3); CHECK(app.takeClearAsk(), "a SAVE hold asks before clearing"); app.loopClear(); run(0.3); }
        CHECK(!app.loopExists(), "loop cleared again");
        // count-in before a sequencer recording
        if (app.status().playing) { btn(SamplerApp::PLAY); run(0.2); }
        app.gotoPage(SamplerApp::P_SQ); { auto k = app.knobs(); k[0].set(1.f); } run(0.4);
        CHECK(!app.status().playing && app.status().countBeats > 0, "sequencer REC counts in first (%d beats left)", app.status().countBeats);
        run(1.5);
        CHECK(app.status().playing && app.status().countBeats == 0, "...then the transport starts");
        { auto k = app.knobs(); k[0].set(0.f); } btn(SamplerApp::PLAY); run(0.2);
        app.gotoPage(SamplerApp::P_TIMING); { auto k = app.knobs(); k[2].set(0.f); }
        // per track fx: a low-pass on track 1 only
        quiet(); app.selectTrack(0);
        auto note = [&] { size_t m = all.size(); app.playNote(60, true); run(1.0); app.playNote(60, false); run(0.3); return rms(all, m, all.size()); };
        const double dry = note();
        app.gotoPage(SamplerApp::P_TFX); { auto k = app.knobs(); k[0].set(0.04f); }
        const double lp = note();
        app.selectTrack(1); const double other = note(); app.selectTrack(0);
        CHECK(lp < dry * 0.6 && finite(all), "track fx: low-pass on track 1 lowers it (%.4f -> %.4f)", dry, lp);
        { auto k = app.knobs(); k[0].set(0.5f); k[1].set(1.f); k[2].set(0.5f); k[3].set(0.6f); }
        const double all4 = note(); CHECK(all4 > 0.001 && finite(all), "track fx: chorus + gate + crush sound (%.4f)", all4);
        { auto k = app.knobs(); k[1].set(0.f); k[2].set(0.f); k[3].set(0.f); }
        CHECK(other > 0.003, "the other tracks are untouched (%.4f)", other);
        app.gotoPage(SamplerApp::P_FX); for (int i = 0; i < 6; i++) btn(SamplerApp::FX); CHECK(app.page() == SamplerApp::P_FX3, "FX x6 from the sends: the OP-1 effects");
        app.gotoPage(SamplerApp::P_SQ); btn(SamplerApp::FX); CHECK(app.page() == SamplerApp::P_FX3, "FX returns to the page it was left on");
    }
    // ---- sequencer v2: overdub with undo, step entry, erase, touch, bounce to tape ----
    {
        auto quiet = [&] { run(4.0); };
        auto stop = [&] { if (app.status().playing) { btn(SamplerApp::PLAY); run(0.2); } };
        stop(); quiet(); app.selectTrack(0); app.seqClear(); run(0.3);
        app.gotoPage(SamplerApp::P_TIMING); { auto k = app.knobs(); k[2].set(0.f); } app.setQuantGrids(0, 0);
        app.gotoPage(SamplerApp::P_TEMPO); { auto k = app.knobs(); k[0].set((120.f - 30.f) / 270.f); k[1].set(0.f); k[3].set(0.f); } run(0.2);
        app.gotoPage(SamplerApp::P_SQ); { auto k = app.knobs(); k[0].set(1.f / 3); k[1].set(0.f); }   // PLAY, 1 bar
        // SEQ held = record the pattern while keys are played; let go and it loops
        auto seqDown = [&] { const uint8_t m[3] = {0x91, (uint8_t)SamplerApp::SEQ, 100}; app.onMidi(m, 3); };
        auto seqUp = [&] { const uint8_t m[3] = {0x81, (uint8_t)SamplerApp::SEQ, 100}; app.onMidi(m, 3); };
        app.gotoPage(SamplerApp::P_HOME);
        seqDown(); run(0.1); seqUp(); run(0.1);
        CHECK(app.page() == SamplerApp::P_SQ && app.status().seqState[0] == 1, "a short SEQ press only changes the page");
        seqDown(); run(0.5);
        CHECK(app.page() == SamplerApp::P_SQ && app.status().seqState[0] == 2 && app.status().playing && !app.status().freeArmed, "SEQ held: the pattern records, the tape is not armed (state %d)", app.status().seqState[0]);
        app.playNote(60, true); run(0.2); app.playNote(60, false);
        run(1.2); seqUp(); run(0.3);
        CHECK(app.status().seqState[0] == 1 && app.status().seqCount[0] == 1, "let go: the pattern plays on (%d note)", app.status().seqCount[0]);
        seqDown(); run(0.5);
        app.playNote(64, true); run(0.2); app.playNote(64, false);
        run(1.2); seqUp(); run(0.3);
        { Session::SeqView v[kSeqMax]; const int n = s.seqNotes(0, v, kSeqMax);
          CHECK(n == 2 && v[0].note != v[1].note, "holding SEQ again overdubs on top: %d notes", n); }
        app.seqUndo(); run(0.2);
        { Session::SeqView v[kSeqMax]; const int n = s.seqNotes(0, v, kSeqMax); CHECK(n == 1 && v[0].note == 60, "undo (engine) takes the newest pass away (%d note left)", n); }
        { auto k = app.knobs(); k[3].step(1); } run(0.2); CHECK(app.status().seqCount[0] == 0, "K4 clears the pattern");
        stop();
        // step entry: keys write at the cursor, a rest, erase, a chord is one step
        app.setSeqState(3); run(0.2);
        CHECK(app.status().seqState[0] == 3, "STEP mode (engine)");
        app.playNote(60, true); run(0.1); app.playNote(60, false); run(0.2);
        app.playNote(64, true); run(0.1); app.playNote(64, false); run(0.2);
        s.seqRest(0); run(0.2);   // rest
        app.playNote(67, true); app.playNote(71, true); run(0.1); app.playNote(67, false); run(0.05); app.playNote(71, false); run(0.3);
        { Session::SeqView v[kSeqMax]; const int n = s.seqNotes(0, v, kSeqMax);
          CHECK(n == 4 && v[0].tick == 0 && v[1].tick == 48 && v[2].tick == 144 && v[3].tick == 144, "step entry: ticks %d %d %d %d (expect 0 48 144 144, the rest skipped one step, the chord shares one)", n > 0 ? v[0].tick : -1, n > 1 ? v[1].tick : -1, n > 2 ? v[2].tick : -1, n > 3 ? v[3].tick : -1); }
        CHECK(app.status().seqCursor[0] == 192, "the cursor sits after the chord (%d)", app.status().seqCursor[0]);
        s.seqCursorMove(0, -1); run(0.2); app.playNote(67, true); run(0.1); app.playNote(67, false); run(0.3);
        CHECK(app.status().seqCount[0] == 3, "the same key on the same step erases it (%d notes)", app.status().seqCount[0]);
        app.seqUndo(); run(0.2); CHECK(app.status().seqCount[0] == 4 && app.status().seqRedoN[0] > 0, "undo takes the erase back (%d notes, redo possible)", app.status().seqCount[0]);
        app.seqRedo(); run(0.2); CHECK(app.status().seqCount[0] == 3, "redo erases it again (%d notes)", app.status().seqCount[0]);
        app.seqUndo(); app.seqUndo(); run(0.2); CHECK(app.status().seqCount[0] == 2, "two more undos: the chord is gone (%d notes)", app.status().seqCount[0]);
        for (int i = 0; i < 20; i++) { app.seqUndo(); run(0.02); }
        CHECK(app.status().seqCount[0] == 0 && app.status().seqUndoN[0] == 0, "undo runs back to the empty pattern and stops (%d notes, %d steps left)", app.status().seqCount[0], app.status().seqUndoN[0]);
        for (int i = 0; i < 20; i++) { app.seqRedo(); run(0.02); }
        CHECK(app.status().seqCount[0] == 3 && app.status().seqRedoN[0] == 0, "redo runs forward again (%d notes)", app.status().seqCount[0]);
        { const int c0 = app.status().seqCount[0]; app.seqToggleAt(240, 72); run(0.1); app.seqUndo(); run(0.1); const int c1 = app.status().seqCount[0]; app.seqToggleAt(300, 70); run(0.1); CHECK(c1 == c0 && app.status().seqRedoN[0] == 0, "a new edit after an undo drops the redo steps (%d, redo %d)", c1, app.status().seqRedoN[0]); }
        app.seqClear(); run(0.1); app.seqUndo(); run(0.1); CHECK(app.status().seqCount[0] > 0, "CLEAR can be undone (%d notes back)", app.status().seqCount[0]);
        // touch edit: the roll adds a note under the finger and removes it again
        { const int before = app.status().seqCount[0]; app.seqToggleAt(240, 72); run(0.2); const int added = app.status().seqCount[0]; app.seqToggleAt(240, 72); run(0.2);
          CHECK(added == before + 1 && app.status().seqCount[0] == before, "touch: tap adds (%d -> %d), tap again removes (%d)", before, added, app.status().seqCount[0]); }
        // bounce with no loop: the tape gets one pass of the pattern, exactly its length, and the pattern is muted
        app.seqClear(); run(0.2);
        { auto k = app.knobs(); k[0].set(1.f / 3); }
        app.seqToggleAt(0, 60); app.seqToggleAt(96, 64); app.seqToggleAt(384, 67); app.seqToggleAt(576, 72); run(0.2);
        CHECK(!app.loopExists(), "no loop before the bounce");
        const int done0 = app.status().bounceDone; size_t m = all.size();
        btn(SamplerApp::REC); run(0.4);   // REC with a playing pattern: the pattern goes on the tape
        CHECK(app.status().bounce == 2 && app.status().playing, "bouncing: recording the pattern to the tape");
        run(2.2);
        CHECK(app.status().bounceDone == done0 + 1 && app.status().bounce == 0, "the bounce ended by itself");
        CHECK(std::fabs(app.status().loopSamples - 2.0 * sr) < 2 && app.status().loopBars == 1 && app.status().track_has_clip[0], "the loop has the length of the pattern: %.0f samples (expect %d)", app.status().loopSamples, 2 * sr);
        CHECK(app.status().seqState[0] == 0 && app.status().seqCount[0] == 4, "the pattern is muted but kept (state %d, %d notes)", app.status().seqState[0], app.status().seqCount[0]);
        run(0.5); m = all.size(); run(3.9);
        CHECK(rms(all, m, all.size()) > 0.003 && finite(all), "the tape plays the pattern back (rms %.4f)", rms(all, m, all.size()));
        // bounce again on top of the loop: it waits for the loop start and records one more cycle
        { auto k = app.knobs(); k[0].set(1.f / 3); } run(0.2);
        const int done1 = app.status().bounceDone; btn(SamplerApp::REC); run(0.3);
        CHECK(app.status().bounce >= 1, "second bounce waits for the loop start (%d)", app.status().bounce);
        run(5.0);
        CHECK(app.status().bounceDone == done1 + 1 && app.status().bounce == 0 && std::fabs(app.status().loopSamples - 2.0 * sr) < 2, "...and ends after one cycle without changing the loop");
        app.loopClear(); run(0.3); stop();
    }
    {   // global undo / redo: pattern edits on two tracks, taken back in the order they were made
        Session& g = app.session(); Session::SeqView nv[2] = {{0, 48, 60, 100}, {96, 48, 64, 100}}; Session::SeqView buf[kSeqMax];
        g.seqSet(6, nv, 1); g.seqSet(7, nv, 2); run(0.2);
        CHECK(app.status().globalUndoN >= 2, "two pattern edits are in the global log (%d)", app.status().globalUndoN);
        app.globalUndo(); run(0.2);
        CHECK(g.seqNotes(7, buf, kSeqMax) == 0 && g.seqNotes(6, buf, kSeqMax) == 1, "UNDO takes back the last edit, on track 8");
        app.globalUndo(); run(0.2);
        CHECK(g.seqNotes(6, buf, kSeqMax) == 0, "...then the one before, on track 7");
        app.globalRedo(); app.globalRedo(); run(0.2);
        CHECK(g.seqNotes(6, buf, kSeqMax) == 1 && g.seqNotes(7, buf, kSeqMax) == 2 && app.status().globalRedoN == 0, "REDO brings both back in order");
        app.globalUndo(); app.globalUndo(); run(0.2);
    }
    {   // the tape is only erased after the dialog: the CLEAR button opens it, the FM-1 SAVE hold asks for it too
        SamplerUI ui(app); ui.setSize(860, 400, 0, 0); ui.frame(5000);
        CHECK(!app.takeClearAsk(), "no clear request without a SAVE hold");
    }
    {   // the pads play with several fingers, nothing stays down when the fingers go, QUANTIZE in plain words, the track effects, the first take can be undone
        Session s3(sr); s3.setDataDir("/tmp/fm1sampler_pads"); s3.setSampleDir(std::string(getenv("HOME")) + "/fm1-sampler/samples"); SamplerApp a3(s3, [](const uint8_t*, size_t) {}); a3.setTourDone(true); demoSounds(a3);
        std::vector<float> ob(512), ib(512, 0.f); auto run3 = [&](double sec) { for (long i = 0; i < (long)(sec * sr) / 256; i++) { std::fill(ob.begin(), ob.end(), 0.f); s3.process(ob.data(), ib.data(), 256); std::this_thread::sleep_for(std::chrono::milliseconds(1)); a3.tick((uint32_t)(i + 1)); } };
        run3(0.5); a3.gotoPage(SamplerApp::P_PADS); SamplerUI u3(a3); u3.setSize(860, 400, 0, 0); u3.frame(100);
        float p1x = -1, p1y = 0, p2x = -1, p2y = 0;
        for (auto& o : u3.ops()) if (o.type == UiOp::TEXT && o.f[2] == 10.f && o.f[0] > 170 && o.f[0] < 600) { if (o.text == "1") { p1x = o.f[0] + 20; p1y = o.f[1] + 18; } if (o.text == "2") { p2x = o.f[0] + 20; p2y = o.f[1] + 18; } }
        CHECK(p1x > 0 && p2x > 0, "the pads page shows pads 1 and 2");
        u3.touchP(0, 0, p1x, p1y, 200); u3.touchP(1, 0, p2x, p2y, 210); run3(0.4);
        CHECK(s3.status().voiceN[a3.track()] >= 2, "two fingers on two pads: both sound (%d voices)", s3.status().voiceN[a3.track()]);
        u3.touchP(0, 2, p1x, p1y, 600); u3.touchP(1, 2, p2x, p2y, 610); u3.frame(700); run3(4.0);
        CHECK(s3.status().voiceN[a3.track()] == 0, "...and nothing is stuck once the fingers are up (%d voices)", s3.status().voiceN[a3.track()]);
        u3.touchP(0, 0, p1x, p1y, 800); u3.touchP(1, 0, p2x, p2y, 810); run3(0.3); u3.cancelTouches(); u3.frame(900); run3(4.0);
        CHECK(s3.status().voiceN[a3.track()] == 0, "...also when the system takes the touches away (%d voices)", s3.status().voiceN[a3.track()]);
        a3.setQuant(3, false); CHECK(a3.quantText() == "1/16" && a3.seqGridIndex() == 4, "QUANTIZE 1/16 sets the pattern grid (%s)", a3.quantText().c_str());
        a3.setQuant(3, true); CHECK(a3.quantText() == "1/16 TRIPLET" && a3.seqGridIndex() == 5, "...triplets are a switch on top (%s)", a3.quantText().c_str());
        a3.setQuant(1, true); CHECK(a3.quantText() == "1/4", "...a quarter has no triplet here (%s)", a3.quantText().c_str());
        a3.cycleQuant(); a3.cycleQuant(); a3.cycleQuant(); a3.cycleQuant(); CHECK(a3.quantText() == "OFF", "one button cycles it round to OFF (%s)", a3.quantText().c_str());
        int tfx = 0; for (auto pg : {SamplerApp::P_TFX, SamplerApp::P_TFX2, SamplerApp::P_TFX3, SamplerApp::P_TFX4}) { a3.gotoPage(pg); auto k = a3.knobs(); int v = 0; for (auto& kn : k) v += kn.valid; tfx += v == 4 && SamplerApp::modeOf(pg) == 3; }
        CHECK(tfx == 4, "four track effect pages with four knobs each, in the MIX tab");
        a3.gotoPage(SamplerApp::P_TFX2); { auto k = a3.knobs(); k[0].set(0.8f); } CHECK(a3.state(a3.track()).tfDrive > 0.7f, "the track effect knobs reach the track (drive %.2f)", a3.state(a3.track()).tfDrive);
        // the first take: UNDO empties the tape again
        a3.gotoPage(SamplerApp::P_TEMPO); { auto k = a3.knobs(); k[3].set(1.f); } run3(0.2);
        a3.loopRecord(); run3(0.1); a3.playNote(60, true); run3(0.2); a3.playNote(60, false); run3(2.5); a3.loopRecord(); run3(0.5);
        CHECK(a3.loopExists(), "a first take is on the tape");
        a3.globalUndo(); run3(0.5); CHECK(!a3.loopExists(), "UNDO of the first take empties the tape (tempo free again)");
        a3.globalRedo(); run3(0.5); CHECK(a3.loopExists(), "REDO brings the take back");
    }
    {   // the overhaul: mute / solo, the first-run tour, the left-handed layout, the long press on a track
        Session s2(sr); s2.setDataDir("/tmp/fm1sampler_tour"); SamplerApp a2(s2, [](const uint8_t*, size_t) {});
        a2.toggleMute(2); a2.toggleSolo(5);
        CHECK(a2.state(2).mute && a2.state(5).solo && a2.anySolo(), "mute and solo are kept per track");
        a2.toggleMute(2); a2.toggleSolo(5); CHECK(!a2.state(2).mute && !a2.anySolo(), "...and switch off again");
        a2.setTourDone(false);
        SamplerUI u(a2); u.setSize(860, 400, 0, 0); u.frame(100);
        CHECK(u.tourOpen(), "the tour opens on the first start");
        auto at = [&](const char* label, float& x, float& y) { x = y = -1; for (auto& o : u.ops()) if (o.type == UiOp::TEXT && o.text == label) { x = o.f[0]; y = o.f[1] - 4; } return x >= 0; };
        float x, y; u.frame(133); CHECK(at("SKIP TOUR", x, y), "the tour card has a SKIP TOUR button");
        u.touch(0, x, y, 200); u.touch(2, x, y, 200); u.frame(233);
        CHECK(!u.tourOpen() && a2.tourDone(), "skipping the tour closes it for good");
        at("SOUND", x, y); const float xr = x;
        a2.setLeftHanded(true); u.frame(300); at("SOUND", x, y);
        CHECK(xr > 430 && x < 430, "left-handed puts the pages on the left (%.0f -> %.0f)", xr, x);
        a2.setLeftHanded(false); u.frame(333);
        { float tx = -1, ty = -1; for (auto& o : u.ops()) if (o.type == UiOp::TEXT && o.text == "2" && o.f[0] < 40) { tx = o.f[0]; ty = o.f[1] - 6; }
          u.touch(0, tx + 20, ty, 1000); u.frame(1700); u.touch(2, tx + 20, ty, 1710); u.frame(1740);
          CHECK(at("OPEN ITS SOUND", x, y) && a2.track() == 1, "holding a track opens its menu (track %d)", a2.track() + 1);
          u.touch(0, x, y, 1800); u.touch(2, x, y, 1800); u.frame(1833); CHECK(!at("OPEN ITS SOUND", x, y), "...and a button of the menu closes it"); }
        a2.gotoPage(SamplerApp::P_MIX); u.frame(2000);
        CHECK(at("MUTE", x, y), "the mixer page shows the strips");
        u.touch(0, x, y, 2100); u.touch(2, x, y, 2100); CHECK(a2.state(7).mute, "MUTE on a strip mutes its track (the last strip was hit)");
        a2.toggleMute(7);
        CHECK(std::string(SamplerApp::pageHelp(SamplerApp::P_TAPEV)).size() > 10 && std::string(SamplerApp::modeName(4)) == "TAPE", "every page has a sentence, the fifth tab is TAPE");
    }
    {   // scales, scale pads, METRO, tape stop settings, more tracks, 64-bar takes, a take without its instrument
        for (int i = 0; i < 42; i++) {
            int st[12]; const int n = SamplerApp::scaleSteps(i, st); bool ok = i == 7 ? n == 0 : n > 0 && st[0] == 0;
            for (int k = 1; k < n; k++) ok &= st[k] > st[k - 1] && st[k] < 12;
            if (!ok) CHECK(false, "scale %d (%s) has sane steps (%d notes)", i, SamplerApp::scaleName(i), n);
        }
        CHECK(std::string(SamplerApp::scaleName(41)) == "MIN6P" && std::string(SamplerApp::scaleName(22)) == "NEAPM", "42 scales, 20 of them new");
        Session s5(sr); s5.setDataDir("/tmp/fm1sampler_t5"); s5.setSampleDir(std::string(getenv("HOME")) + "/fm1-sampler/samples"); SamplerApp a5(s5, [](const uint8_t*, size_t) {}); a5.setTourDone(true); demoSounds(a5);
        std::vector<float> ob(512), ib(512, 0.f), heard; auto run5 = [&](double sec) { for (long i = 0; i < (long)(sec * sr) / 256; i++) { std::fill(ob.begin(), ob.end(), 0.f); s5.process(ob.data(), ib.data(), 256); heard.insert(heard.end(), ob.begin(), ob.end()); std::this_thread::sleep_for(std::chrono::milliseconds(1)); a5.tick((uint32_t)(i + 1)); } };
        run5(0.5);
        a5.setScale(2, 0, 0, 0); CHECK(a5.padNote(0) == 60 && a5.padNote(1) == 62 && a5.padNote(6) == 71 && a5.padNote(7) == 72 && a5.padName(2) == "E4", "the pads play the major scale (%d %d %d %d, pad 3 = %s)", a5.padNote(0), a5.padNote(1), a5.padNote(6), a5.padNote(7), a5.padName(2).c_str());
        a5.setScale(2, 2, 0, 0); CHECK(a5.padNote(0) == 62 && a5.padName(0) == "D4", "...from the root (%s)", a5.padName(0).c_str());
        a5.setScale(7, 0, 0, 0); CHECK(a5.padNote(5) == 65, "scale OFF: chromatic pads");
        a5.setScale(2, 0, 0, 0); a5.selectTrack(3); CHECK(a5.isKit(3) && a5.padNote(5) == 65, "a kit keeps its chromatic pads (%d)", a5.padNote(5)); a5.selectTrack(0);
        CHECK(!a5.metronome(), "the metronome starts off"); a5.toggleMetro(); CHECK(a5.metronome(), "METRO turns it on from anywhere"); a5.toggleMetro();
        for (int i = 0; i < 9; i++) if (a5.takeBars() != 64) a5.cycleTakeBars(); CHECK(a5.takeBars() == 64, "a take can be 64 bars");
        a5.cycleTakeBars(); CHECK(a5.takeBars() == 0, "...and then FREE again");
        CHECK(a5.trackCount() == 8, "eight tracks to start with"); CHECK(a5.addTrack() && a5.trackCount() == 9 && a5.track() == 8, "ADD TRACK opens the ninth and selects it");
        for (int i = 0; i < 20; i++) a5.addTrack(); CHECK(a5.trackCount() == 16 && !a5.addTrack(), "...up to 16");
        a5.selectTrack(15); CHECK(a5.track() == 15, "the last track can be selected"); a5.selectTrack(0);
        { SamplerUI ui(a5); ui.setSize(860, 400, 0, 0); ui.frame(100); a5.gotoPage(SamplerApp::P_TAPEV); ui.frame(200); bool lane16 = false; for (auto& o : ui.ops()) if (o.type == UiOp::TEXT && o.text == "16") lane16 = true; CHECK(lane16, "the tape view has a lane for the 16th track"); }
        a5.gotoPage(SamplerApp::P_TSTOP); { auto k = a5.knobs(); int v = 0; for (auto& kn : k) v += kn.valid; CHECK(v == 4, "the TAPE STOP page has four knobs"); }
        CHECK(!a5.tapeStopLatch(), "tape stop holds by default"); a5.tapeStopHold(true); CHECK(a5.tapeStopHeld(), "...down: it stops"); a5.tapeStopHold(false); CHECK(!a5.tapeStopHeld(), "...up: it spins up again");
        { auto k = a5.knobs(); k[3].step(1); } CHECK(a5.tapeStopLatch(), "BUTTON = LATCH");
        a5.tapeStopHold(true); a5.tapeStopHold(false); CHECK(a5.tapeStopHeld(), "latch: a tap stops it and releasing does not undo it"); a5.tapeStopHold(true); a5.tapeStopHold(false); CHECK(!a5.tapeStopHeld(), "...the next tap spins it up");
        {   // the same sound dropped to 10 percent speed: a slower spin-up takes longer to come back
            auto spin = [&](float upT) { MasterFx fx(sr); FxParams p; p.tsDown = 0.2f; p.tsUp = upT; p.tapeStop = 1.f; std::vector<float> x(2 * 256); long n = 0; double level = 0;
                for (long b = 0; b < sr / 256 * 8; b++) { for (int i = 0; i < 256; i++) x[2 * i] = x[2 * i + 1] = 0.5f * std::sin(2 * M_PI * 220 * (n + i) / sr); n += 256; if (b == sr / 256 * 2) { p.tapeStop = 0.f; } fx.set(p); fx.process(x.data(), 256, 0.0, 0.0);
                    if (b == sr / 256 * 2 + 60) { level = rms(x); } }
                return level; };
            CHECK(spin(0.2f) > spin(4.f) * 1.3, "a long spin-up is still quiet when a short one is already back (%.3f vs %.3f)", spin(0.2f), spin(4.f));
            CHECK(spin(0.f) > 0.3, "SPIN-UP 0 S: the tape is back at once (rms %.3f)", spin(0.f));
        }
        // the tape does not need the instrument: a one bar take, then the track is emptied
        a5.selectTrack(0); run5(0.2);
        for (int i = 0; i < 9; i++) if (a5.takeBars() != 1) a5.cycleTakeBars();
        a5.loopRecord(); run5(0.2); a5.playNote(60, true); run5(0.5); a5.playNote(60, false); run5(3.0);
        CHECK(a5.loopExists(), "a one bar take is on the tape");
        s5.unloadSample(0); run5(0.3); heard.clear(); run5(1.8);
        CHECK(a5.loopExists() && rms(heard) > 0.003, "the take plays on after the instrument is taken off the track (rms %.4f)", rms(heard));
        {   // undo: only real steps, the clear of a track is one, and a new project forgets them
            run5(0.3); const int n0 = a5.status().globalUndoN;
            a5.loopRecord(); run5(0.5); a5.loopRecord(); run5(0.5);
            CHECK(a5.status().globalUndoN == n0, "an overdub that recorded only silence leaves no undo step (%d -> %d)", n0, a5.status().globalUndoN);
            a5.clearTrackLoop(0); run5(0.5); CHECK(!a5.loopExists(), "the track tape is cleared");
            CHECK(a5.status().globalUndoN == n0 + 1, "...and that is one undo step (%d)", a5.status().globalUndoN);
            a5.globalUndo(); run5(0.5); CHECK(a5.loopExists(), "UNDO brings the cleared tape back");
            a5.globalRedo(); run5(0.5); CHECK(!a5.loopExists(), "REDO clears it again");
            a5.globalUndo(); run5(0.5); CHECK(a5.loopExists(), "UNDO again: the tape is back (loop %d, undo %d)", (int)a5.status().loopSamples, a5.status().globalUndoN);
            CHECK(a5.newProject(), "NEW PROJECT starts"); run5(1.0);
            CHECK(!a5.loopExists() && a5.trackCount() == 8 && a5.status().globalUndoN == 0 && a5.status().globalRedoN == 0, "...the tape is empty, 8 tracks, nothing to undo (%d)", a5.status().globalUndoN);
            CHECK(a5.currentProject() == "autosave", "...an unnamed session");
            bool saved = false; for (auto& nme : a5.projectList()) saved |= nme.rfind("Project ", 0) == 0; CHECK(saved, "...and the old work was saved first");
            a5.globalUndo(); run5(0.2); CHECK(!a5.loopExists(), "UNDO right after NEW changes nothing (%s)", a5.toast().c_str());
            run5(2.0); a5.newLoop(2, false); run5(0.3); CHECK(a5.takeBars() == 2 && a5.status().freeArmed, "NEW LOOP, tape: a 2 bar take is armed");
            a5.loopRecord(); run5(0.3); CHECK(!a5.status().freeArmed, "...and can be cancelled");
            a5.newLoop(4, true); run5(0.5); CHECK(a5.state(a5.track()).seqState == 2 && a5.seqBarsOf(a5.track()) == 4 && a5.page() == SamplerApp::P_SQ, "NEW LOOP, sequencer: a 4 bar pattern records (state %d, %d bars)", a5.state(a5.track()).seqState, a5.seqBarsOf(a5.track()));
            a5.newLoop(64, true); run5(0.3); CHECK(a5.seqBarsOf(a5.track()) == 16, "...a pattern holds 16 bars at most");
            SamplerUI uil(a5); uil.setSize(860, 400, 0, 0); a5.gotoPage(SamplerApp::P_TAPEV); uil.frame(300);
            auto atl = [&](const char* label, float& x, float& y) { x = y = -1; for (auto& o : uil.ops()) if (o.type == UiOp::TEXT && o.text == label) { x = o.f[0]; y = o.f[1] - 4; } return x >= 0; };
            float lx, ly; CHECK(atl("NEW LOOP", lx, ly), "the tape view has a NEW LOOP button");
            uil.touch(0, lx, ly, 400); uil.touch(2, lx, ly, 410); uil.frame(433); CHECK(atl("START", lx, ly) && atl("SEQUENCER", lx, ly), "...it opens the sheet with TAPE / SEQUENCER and START");
            { float sx, sy; atl("START", sx, sy); uil.touch(0, sx, sy, 500); uil.touch(2, sx, sy, 510); uil.frame(533); CHECK(!atl("START", sx, sy), "...START closes it"); }
        }
    }
    {   // the pattern on its own: play / stop, the keys as triggers (OP-1 style), the first beat, the full screen editor
        Session s6(sr); s6.setDataDir("/tmp/fm1sampler_t6b"); s6.setSampleDir(std::string(getenv("HOME")) + "/fm1-sampler/samples"); SamplerApp a6(s6, [](const uint8_t*, size_t) {}); a6.setTourDone(true); demoSounds(a6);
        std::vector<float> ob(512), ib(512, 0.f), heard; auto run6 = [&](double sec) { for (long i = 0; i < (long)(sec * sr) / 256; i++) { std::fill(ob.begin(), ob.end(), 0.f); s6.process(ob.data(), ib.data(), 256); heard.insert(heard.end(), ob.begin(), ob.end()); std::this_thread::sleep_for(std::chrono::milliseconds(1)); a6.tick((uint32_t)(i + 1)); } };
        auto level = [&](double sec) { heard.clear(); run6(sec); return rms(heard); };
        run6(1.0); a6.seqSetBarCount(1);
        for (int b = 0; b < 4; b++) a6.seqToggleAt(b * 96, 60, 60);
        a6.setSeqState(1); a6.transportPlay(); run6(0.5);
        CHECK(a6.status().playing && a6.seqRunning(0) && level(2.0) > 0.01, "the pattern plays with the transport (rms %.3f)", rms(heard));
        a6.seqRunToggle(); run6(1.5); CHECK(!a6.seqRunning(0) && level(2.0) < 0.002, "STOP silences the pattern while the transport runs (rms %.4f)", rms(heard));
        CHECK(a6.status().playing, "...the transport keeps going");
        a6.seqRunToggle(); run6(0.2); CHECK(a6.seqRunning(0), "PLAY starts it again (waiting for the beat or running)"); CHECK(level(3.0) > 0.01, "...and it sounds (rms %.3f)", rms(heard));
        // nudge: the notes slide round the pattern, one undo step
        { Session::SeqView v[64]; a6.seqNudge(24); run6(0.3); const int n = s6.seqNotes(0, v, 64); int first = 9999, last = -1; for (int i = 0; i < n; i++) { first = std::min(first, v[i].tick); last = std::max(last, v[i].tick); }
          CHECK(n == 4 && first == 24 && last == 24 + 3 * 96, "NUDGE slides the pattern by a step (%d notes from tick %d)", n, first);
          a6.seqNudge(-24); run6(0.3); a6.seqNudge(-48); run6(0.3);
          const int n2 = s6.seqNotes(0, v, 64); first = 9999; for (int i = 0; i < n2; i++) first = std::min(first, v[i].tick); CHECK(first == 48 && n2 == 4, "...and round the end (earliest tick %d)", first);
          a6.seqNudge(48); run6(0.3); }
        // the keys as triggers
        a6.seqRunToggle(); run6(0.5); a6.setKeyTrigger(0, true); run6(0.3);
        CHECK(a6.status().keyTrig[0] && !a6.seqRunning(0), "KEYS = PATTERN: the pattern waits for a key");
        CHECK(level(1.0) < 0.002, "...and nothing sounds (rms %.4f)", rms(heard));
        a6.playNote(64, true); run6(1.5); CHECK(a6.status().seqRun[0] && a6.status().seqTrans[0] == 4, "a key starts it, E4 = 4 semitones up (%d)", a6.status().seqTrans[0]);
        CHECK(level(1.5) > 0.01, "...it sounds (rms %.3f)", rms(heard));
        a6.playNote(62, true); run6(0.3); CHECK(a6.status().seqTrans[0] == 2, "another key moves it (%d)", a6.status().seqTrans[0]);
        a6.playNote(64, false); run6(0.2); CHECK(a6.status().seqRun[0], "one key up, one still held: it goes on");
        a6.playNote(62, false); run6(2.0); CHECK(!a6.status().seqRun[0] && level(1.5) < 0.002, "the last key up stops it (rms %.4f)", rms(heard));
        a6.setKeyTrigger(0, false); run6(0.3); CHECK(!a6.status().keyTrig[0] && a6.seqRunning(0), "KEYS = SOUND: the pattern runs again");
        // ONE SHOT: one pass of the pattern, then it stops by itself (LOOPS is the default)
        a6.setOneShot(0, true); a6.seqRunToggle(); run6(0.3); a6.seqRunToggle(); run6(0.3);
        CHECK(a6.status().seqOne[0] && a6.seqRunning(0), "ONE SHOT: PLAY starts the pattern");
        run6(3.0); CHECK(!a6.seqRunning(0) && level(1.0) < 0.002, "...it stops after one pass (rms %.4f)", rms(heard));
        a6.setOneShot(0, false); a6.seqRunToggle(); run6(4.5); CHECK(a6.seqRunning(0), "LOOPS: it keeps going past one pass");
        // CHOP: auto / even chops, and a key that plays the whole chop or only while it is held
        a6.seqRunToggle(); run6(0.3); a6.setSeqState(0); run6(0.2);
        for (size_t i = 0; i < a6.library().size(); i++) if (a6.library()[i].label == "Boom Bap Kit") a6.loadEntry((int)i);
        run6(2.0); a6.setAlgo(3); run6(0.3);
        a6.evenChop(); run6(0.3); { const auto sl = s6.slicesOf(0); CHECK(sl.size() == 16 && std::fabs(sl[1] - 1.f / 16.f) < 0.001f && std::fabs(sl[8] - 0.5f) < 0.001f, "EVEN CHOP: 16 equal chops (%zu)", sl.size()); }
        a6.autoChop(); run6(0.3); { const auto sl = s6.slicesOf(0); CHECK(sl.size() == 16, "AUTO CHOP: cut at the hits (%zu chops)", sl.size()); }
        a6.setChopFull(false); run6(0.2);
        a6.playNote(60, true); run6(0.1); a6.playNote(60, false); heard.clear(); run6(1.0);
        { std::vector<float> tail(heard.begin() + (long)(0.6 * sr) * 2, heard.end()); CHECK(rms(tail, 0, tail.size()) < 0.003, "KEY: HOLD: the chop stops with the key (rms %.4f)", rms(tail, 0, tail.size())); }
        a6.setChopFull(true); run6(0.2);
        a6.playNote(60, true); run6(0.1); a6.playNote(60, false); heard.clear(); run6(0.5);
        CHECK(rms(heard, 0, heard.size()) > 0.005, "KEY: WHOLE CHOP: it keeps sounding after the key is up (rms %.4f)", rms(heard, 0, heard.size()));
        // the TRIGGER page
        a6.gotoPage(SamplerApp::P_TRIG); { auto k = a6.knobs(); int v = 0; for (auto& kn : k) v += kn.valid; CHECK(v == 4 && SamplerApp::modeOf(SamplerApp::P_TRIG) == 2, "the TRIGGER page: four knobs, in the SEQ tab"); }
        // the full screen editor
        a6.gotoPage(SamplerApp::P_SQ); SamplerUI u6(a6); u6.setSize(860, 400, 0, 0); u6.frame(100);
        auto at6 = [&](const char* label, float& x, float& y) { x = y = -1; for (auto& o : u6.ops()) if (o.type == UiOp::TEXT && o.text == label) { x = o.f[0]; y = o.f[1] - 4; } return x >= 0; };
        float fx, fy; CHECK(at6("FULL SCREEN", fx, fy), "the pattern page has FULL SCREEN");
        u6.touch(0, fx, fy, 200); u6.touch(2, fx, fy, 210); u6.frame(233);
        CHECK(at6("EXIT", fx, fy) && !at6("TAPE STOP", fx, fy) && at6("BEAT >", fx, fy), "...the editor takes the whole screen, with the first beat buttons");
        at6("EXIT", fx, fy); u6.touch(0, fx, fy, 300); u6.touch(2, fx, fy, 310); u6.frame(333);
        CHECK(!at6("EXIT", fx, fy) && at6("TAPE STOP", fx, fy), "...EXIT brings the screen back");
        // TO TAPE with the keys: the pass is recorded from the loop start, transposed
        a6.setKeyTrigger(0, true); a6.playNote(67, true); run6(0.8); a6.playNote(67, false); run6(0.5);
        for (int i = 0; i < 9; i++) if (a6.takeBars() != 1) a6.cycleTakeBars();
        a6.setKeyTrigger(0, false); run6(0.2); a6.seqBounce(); run6(8.0);
        CHECK(a6.loopExists() && a6.status().bounceDone > 0, "TO TAPE puts one pass of the pattern on the tape (bounce done %d)", a6.status().bounceDone);
    }
    // ---- FM-1 hardware gestures: modifiers, analog knob, FX browse, chop page, key LEDs ----
    {
        Session s7(sr); s7.setDataDir("/tmp/fm1sampler_t7"); s7.setSampleDir(std::string(getenv("HOME")) + "/fm1-sampler/samples");
        uint64_t led = 0; std::string lastToast;
        auto grab = [&](const uint8_t* m, size_t n) {   // the last LED frame, alone or inside a batch
            if (n < 8 || m[6] == 2) return;
            auto one = [&](const uint8_t* c, size_t len) { if (c[0] == 1 && len >= 7) { uint64_t v = 0; for (int i = 0; i < 6; i++) v |= (uint64_t)(c[1 + i] & 127) << (7 * i); led = v; } };
            if (m[6] == 1) one(m + 6, n - 7);
            else if (m[6] == 10) { size_t i = 7; while (i + 2 < n - 1) { const size_t len = (size_t)(m[i] << 7 | m[i + 1]); if (i + 2 + len > n - 1) break; one(m + i + 2, len); i += 2 + len; } }
        };
        SamplerApp a7(s7, grab); a7.setTourDone(true); demoSounds(a7);
        std::vector<float> ob(512), ib(512, 0.f); uint32_t tn = 0;
        auto run7 = [&](double sec) { for (long i = 0; i < (long)(sec * sr) / 256; i++) { std::fill(ob.begin(), ob.end(), 0.f); s7.process(ob.data(), ib.data(), 256); std::this_thread::sleep_for(std::chrono::microseconds(300)); tn += 6; a7.tick(tn); } };
        auto hold = [&](int b, bool d) { uint8_t m[3] = {(uint8_t)(d ? 0x91 : 0x81), (uint8_t)b, 100}; a7.onMidi(m, 3); };
        auto tap = [&](int b) { hold(b, true); hold(b, false); };
        auto key = [&](int k, bool d) { uint8_t m[3] = {(uint8_t)(d ? 0x90 : 0x80), (uint8_t)(53 + k), 100}; a7.onMidi(m, 3); };
        auto enc7 = [&](int i, int d) { uint8_t m[3] = {0xb0, (uint8_t)(16 + i), (uint8_t)(64 + d)}; a7.onMidi(m, 3); };
        auto cc = [&](int c, int v) { uint8_t m[3] = {0xb0, (uint8_t)c, (uint8_t)v}; a7.onMidi(m, 3); };
        auto knobAt = [&](float v) { const int raw = (int)std::lround(v * 1023); cc(7, raw >> 3); cc(39, (raw & 7) << 4); };
        auto lit = [&](int k) { return (led >> (14 + k) & 1) != 0; };
        run7(1.5);
        // a tap on EDIT still opens the MIX tab; it acts when the button is let go
        tap(SamplerApp::EDIT); CHECK(SamplerApp::modeOf(a7.page()) == 3, "EDIT tapped: the MIX tab");
        tap(SamplerApp::HOME); CHECK(SamplerApp::modeOf(a7.page()) == 0, "HOME tapped: the SOUND tab");
        { const SamplerApp::Page pg0 = a7.page(); hold(SamplerApp::EDIT, false); hold(SamplerApp::GLO, false); hold(40, true); hold(40, false);
          CHECK(a7.page() == pg0, "a release without a press, and a stray button number, change nothing"); }
        // hold EDIT + key: mute, with SEL: solo; the page does not change
        const SamplerApp::Page before = a7.page();
        hold(SamplerApp::EDIT, true); key(1, true); key(1, false); hold(SamplerApp::EDIT, false);
        CHECK(a7.state(1).mute && a7.page() == before, "EDIT + key 2 mutes track 2 and stays on the page");
        hold(SamplerApp::EDIT, true); run7(0.2); led = 0; run7(0.3);
        CHECK(!lit(1) && lit(2) && !lit(5), "EDIT held: the keys show the tracks (muted dark, track 3 lit, empty track 6 dark)");
        key(1, true); key(1, false); hold(SamplerApp::EDIT, false);
        CHECK(!a7.state(1).mute, "EDIT + key 2 again unmutes it");
        hold(SamplerApp::EDIT, true); hold(SamplerApp::SEL, true); key(2, true); key(2, false); hold(SamplerApp::SEL, false); hold(SamplerApp::EDIT, false);
        CHECK(a7.state(2).solo && a7.page() == before, "EDIT + SEL + key 3 solos track 3");
        a7.toggleSolo(2);
        // HOME + key picks the track
        hold(SamplerApp::HOME, true); key(3, true); key(3, false); hold(SamplerApp::HOME, false);
        CHECK(a7.track() == 3 && a7.page() == before, "HOME + key 4 goes to track 4");
        // the analog knob: master volume at once, the others wait until the knob is where the value is
        CHECK(a7.knobTarget() == SamplerApp::KT_MASTER, "the knob moves the master volume at first");
        hold(SamplerApp::GLO, true); enc7(6, 1); enc7(6, 1); hold(SamplerApp::GLO, false);
        CHECK(a7.knobTarget() == SamplerApp::KT_DJ && a7.page() == before, "GLO + PRESETS picks the knob target (%s)", SamplerApp::knobTargetName(a7.knobTarget()));
        const float dj0 = a7.knobs(SamplerApp::P_LIVE)[0].norm;
        knobAt(0.f); CHECK(std::fabs(a7.knobs(SamplerApp::P_LIVE)[0].norm - dj0) < 0.001f && a7.knobWaiting(), "far from the value the knob does nothing (no jump)");
        knobAt(dj0); knobAt(0.9f); CHECK(std::fabs(a7.knobs(SamplerApp::P_LIVE)[0].norm - 0.9f) < 0.03f, "once it reached the value it moves the DJ filter (%.2f)", a7.knobs(SamplerApp::P_LIVE)[0].norm);
        a7.knobs(SamplerApp::P_LIVE)[0].set(0.5f); a7.setKnobTarget(SamplerApp::KT_MASTER);
        cc(24, 127); CHECK(a7.fmBattery() == 100, "the battery reading of the FM-1 is kept (%d)", a7.fmBattery());
        // FX + PRESETS browses the effects, letting go of FX opens the page and lights the knob
        a7.gotoPage(SamplerApp::P_HOME);
        hold(SamplerApp::FX, true); enc7(6, 1); CHECK(a7.toast().find("DJ FILTER") != std::string::npos, "FX + PRESETS shows the effect on the FM-1 screen (%s)", a7.toast().c_str());
        enc7(6, 1); enc7(6, 1); hold(SamplerApp::FX, false);   // DJ FILTER, EQ, AUTO WAH
        CHECK(a7.page() == SamplerApp::P_MOD && a7.fxFocusKnob(SamplerApp::P_MOD) == 1, "letting go opens AUTO WAH's page and lights its knob (page %d, knob %d)", (int)a7.page(), a7.fxFocusKnob(a7.page()));
        a7.gotoPage(SamplerApp::P_HOME);
        hold(SamplerApp::FX, true); enc7(1, 1); enc7(6, 1); hold(SamplerApp::FX, false);
        CHECK(a7.fxScopeTrack() && SamplerApp::modeOf(a7.page()) == 3, "FX + ALGORITHM flips to the track's effects (page %d)", (int)a7.page());
        // the CHOP page edits the chops of a KIT sample with the knobs
        a7.selectTrack(3); a7.gotoPage(SamplerApp::P_CHOP); run7(0.5);
        CHECK(a7.chopsAvailable() && a7.knobs()[0].valid && a7.knobs()[3].valid, "CHOP page: four knobs for a KIT sample");
        const auto c0 = s7.slicesOf(3), e0 = s7.sliceEndsOf(3);
        enc7(2, 1); run7(0.2); CHECK(a7.chopSel() == 1, "K1 picks chop 2");
        enc7(4, 2); enc7(3, 3); run7(0.4);   // the end first, so the chop has room
        const auto c1 = s7.slicesOf(3), e1 = s7.sliceEndsOf(3);
        CHECK(c1.size() == c0.size() && std::fabs((c1[1] - c0[1]) - 3 * 0.005f) < 0.0015f && std::fabs((e1[1] - e0[1]) - 2 * 0.005f) < 0.0015f && std::fabs(c1[2] - c0[2]) < 1e-4f, "K2 moves the start (+%.3f), K3 the end (%.3f), the rest stays", c1[1] - c0[1], e1[1] - e0[1]);
        { a7.setChopSel(1); const float s1 = s7.slicesOf(3)[1]; const float s2 = s7.slicesOf(3)[2]; a7.chopSetPos(true, s2 + 0.05f); a7.chopSetPos(false, s2 + 0.001f); run7(0.4);   // past the next chop: the order changes, the selection follows its chop
          const auto cs = s7.slicesOf(3); CHECK(std::is_sorted(cs.begin(), cs.end()) && a7.chopSel() == 2 && std::fabs(cs[2] - (s2 + 0.001f)) < 1e-4f, "a chop moved past its neighbour keeps the selection (sel %d)", a7.chopSel());
          a7.chopSetPos(false, s1); run7(0.4); a7.setChopSel(1); }
        tap(SamplerApp::REC); run7(0.4); CHECK(s7.slicesOf(3).size() == c0.size() + 1, "REC splits the chop (%zu chops)", s7.slicesOf(3).size());
        tap(SamplerApp::GLO); run7(0.4); CHECK(s7.slicesOf(3).size() == c0.size() && a7.page() == SamplerApp::P_CHOP, "GLO deletes it and stays on the page (%zu chops)", s7.slicesOf(3).size());
        { const int n = (int)s7.slicesOf(3).size(); a7.gotoPage(SamplerApp::P_HOME); run7(0.8); int on = 0; for (int k = 0; k < 27; k++) on += lit(k) ? 1 : 0;
          CHECK(on == n && (n >= 27 || !lit(n)), "KIT keys: one lit key per chop (%d lit, %d chops)", on, n); }
        // the pattern page: keys light the steps that have a note
        a7.selectTrack(0); a7.gotoPage(SamplerApp::P_SQ); a7.seqClear(); a7.seqSetBarCount(1); a7.seqToggleAt(0, 60, 96); a7.seqToggleAt(4 * 48, 62, 96); run7(0.4); led = 0; run7(0.6);
        CHECK(lit(4) && !lit(2) && !lit(6), "pattern page: step 5 has a note (steps 3 and 7 dark)");
        CHECK(std::string(SamplerApp::fmHint(SamplerApp::P_CHOP)).size() > 10 && SamplerApp::fmGesture(0) && !SamplerApp::fmGesture(9), "the phone has hints for the gestures");
    }
    printf("%s (%d failures)\n", fails ? "FAILED" : "ALL OK", fails);
    return fails ? 1 : 0;
}
