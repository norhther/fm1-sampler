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
    { MasterFx fx(sr); FxParams p; p.eqMid = 0.52f; fx.set(p); CHECK(!fx.active(), "EQ within 0.02 of flat is bypassed"); p.eqMid = 0.8f; fx.set(p); CHECK(fx.active(), "EQ moved: active"); }
    { auto lows = sine(N, 80, sr); FxParams p; p.eqLow = 1.f; auto o = run(p, lows); CHECK(rms(o, 24000, 48000) > rms(lows, 24000, 48000) * 1.6, "EQ low boost lifts 80 Hz (%.3f -> %.3f)", rms(lows, 24000, 48000), rms(o, 24000, 48000));
      auto highs = sine(N, 10000, sr); FxParams q; q.eqHigh = 0.f; auto h = run(q, highs); CHECK(rms(h, 24000, 48000) < rms(highs, 24000, 48000) * 0.5, "EQ high cut lowers 10 kHz (%.3f -> %.3f)", rms(highs, 24000, 48000), rms(h, 24000, 48000)); }
    { FxParams p; p.tremolo = 1.f; auto o = run(p, sine(N, 220, sr)); double lo = 1e9, hi = 0; for (int w = 0; w < 9; w++) { const double r = rms(o, 2 * 2400 * (size_t)(w + 1) * 2, 2 * 2400 * (size_t)(w + 2) * 2); lo = std::min(lo, r); hi = std::max(hi, r); }
      CHECK(lo < hi * 0.6, "tremolo moves the level up and down (%.3f .. %.3f)", lo, hi); }
    { FxParams p; p.tapeStop = 1.f; MasterFx fx(sr); auto in = sine(N * 2, 220, sr); fx.set(FxParams()); fx.process(in.data(), 12000, 0, 1.0 / 24000); fx.set(p);
      for (int i = 12000; i < N * 2; i += 256) fx.process(in.data() + 2 * i, std::min(256, N * 2 - i), 0, 1.0 / 24000);
      CHECK(finite(in) && rms(in, 2 * 80000, 2 * 96000) < 0.02, "tape stop: the sound runs down to nothing (%.4f at the end)", rms(in, 2 * 80000, 2 * 96000));
      fx.set(FxParams()); auto live = sine(4800, 220, sr); fx.process(live.data(), 4800, 0, 1.0 / 24000); CHECK(rms(live, 2 * 2400, 2 * 4800) > 0.3, "...and it is back when the knob is released (%.3f)", rms(live, 2 * 2400, 2 * 4800)); }
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
    for (const char* kit : {"808 Kit", "Electro Kit", "Lo-Fi Kit"}) {
        std::vector<float> d; int sr = 0; readWav((root / "Drums" / (std::string(kit) + ".wav")).string(), d, sr);
        const auto sl = findSlices(d, (int)(d.size() / 2), sr, 16);
        int good = 0; double want = 0.01; for (size_t i = 0; i < sl.size() && i < 16; i++) { good += std::fabs(sl[i] / (double)sr - want) < 0.03; want += slot[i]; }
        CHECK(sl.size() == 16 && good == 16, "%s: the slicer finds all 16 hits (%d of 16 on time)", kit, good);
    }
}
// the piano roll: pinch with two fingers zooms (horizontal = time, vertical = pitch), the buttons zoom too, a pinch never writes a note
static void testRollZoom() {
    const int sr = SAMPLERATE; Session s(sr); s.setDataDir("/tmp/fm1sampler_t4"); s.setSampleDir(std::string(getenv("HOME")) + "/fm1-sampler/samples");
    SamplerApp app(s, [](const uint8_t*, size_t) {}); SamplerUI ui(app); ui.setSize(860, 400, 0, 0);
    std::vector<float> buf(512), in(512, 0.f); uint32_t now = 0;
    auto run = [&](double sec) { for (long i = 0; i < (long)(sec * sr) / 256; i++) { std::fill(buf.begin(), buf.end(), 0.f); s.process(buf.data(), in.data(), 256); if (i % 5 == 0) { now += 33; app.tick(now); } } };
    run(0.5); app.gotoPage(SamplerApp::P_SQ); for (int i = 0; i < 4; i++) app.seqToggleAt(i * 96, 60 + i); run(0.2);
    ui.frame(now);
    CHECK(ui.rollZoom() == 1.f && ui.rollRows() == 24.f, "the roll starts unzoomed (%.1fx, %.0f rows)", ui.rollZoom(), ui.rollRows());
    // two fingers 100 px apart horizontally, then 300 px: three times as much time zoom
    ui.pinch(0, 350, 250, 450, 250); ui.pinch(1, 250, 250, 550, 250); ui.frame(now);
    CHECK(std::fabs(ui.rollZoom() - 3.f) < 0.05f && ui.rollRows() == 24.f, "pinching apart sideways zooms the time 3x (%.2fx, %.0f rows)", ui.rollZoom(), ui.rollRows());
    ui.pinch(1, 330, 250, 470, 250); ui.frame(now);
    CHECK(std::fabs(ui.rollZoom() - 1.4f) < 0.05f, "pinching together zooms out again (%.2fx)", ui.rollZoom());
    ui.pinch(1, 400, 250, 400, 250); ui.pinch(2, 0, 0, 0, 0); ui.frame(now);
    CHECK(ui.rollZoom() == 1.f, "it stops at the whole pattern (%.2fx)", ui.rollZoom());
    // vertical: fingers 100 px apart, then 200 px: half as many pitch rows
    ui.pinch(0, 400, 200, 400, 300); ui.pinch(1, 400, 150, 400, 350); ui.frame(now);
    CHECK(std::fabs(ui.rollRows() - 12.f) < 0.5f && ui.rollZoom() == 1.f, "pinching apart up and down shows half the pitch rows (%.1f rows)", ui.rollRows());
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
}
int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);   // a crash must not swallow the lines before it
    Denormals::enableOnThisThread();
    {   // a session that is destroyed while its samples are still loading used to crash (the worker job read Session::d after unique_ptr had cleared it)
        for (int i = 0; i < 4; i++) { Session sess(SAMPLERATE); sess.setDataDir("/tmp/fm1sampler_t5"); sess.setSampleDir(std::string(getenv("HOME")) + "/fm1-sampler/samples"); SamplerApp a(sess, [](const uint8_t*, size_t) {}); }
        CHECK(true, "a session can be destroyed right after it started (4 times)");
    }
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
    SamplerApp app(s, [](const uint8_t*, size_t) {});
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
    btn(SamplerApp::FX); CHECK(app.page() == SamplerApp::P_TFX, "FX again: track fx");
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
        btn(SamplerApp::HOME); CHECK(app.page() == SamplerApp::P_HOME, "HOME again: back to the sample");
        btn(SamplerApp::ENV); CHECK(app.page() == SamplerApp::P_ENV, "ENV: shortcut to the envelope");
        btn(SamplerApp::LFO); CHECK(app.page() == SamplerApp::P_LFO, "LFO: shortcut");
        btn(SamplerApp::ARP); CHECK(app.page() == SamplerApp::P_ARP, "ARP: shortcut");
        btn(SamplerApp::EDIT); CHECK(app.page() == SamplerApp::P_MIX, "EDIT: mix mode, mixer");
        btn(SamplerApp::EDIT); CHECK(app.page() == SamplerApp::P_KEY, "EDIT again: key");
        btn(SamplerApp::HOME); btn(SamplerApp::EDIT); CHECK(app.page() == SamplerApp::P_KEY, "mix mode returns to the page it was left on");
        btn(SamplerApp::GLO); CHECK(app.page() == SamplerApp::P_KEY, "GLO: shortcut to the key");
        btn(SamplerApp::SEL); CHECK(app.page() == SamplerApp::P_LEN, "SEL: shortcut to the length");
        btn(SamplerApp::SEQ); CHECK(app.page() == SamplerApp::P_SQ, "SEQ tap: sequencer mode, pattern page");
        btn(SamplerApp::SEQ); CHECK(app.page() == SamplerApp::P_SEQ, "SEQ again: tape");
        btn(SamplerApp::SEQ); CHECK(app.page() == SamplerApp::P_TEMPO, "SEQ again: tempo");
        btn(SamplerApp::SEQ); CHECK(app.page() == SamplerApp::P_TIMING, "SEQ again: timing");
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
        auto k = app.knobs(); for (int i = 0; i < 30; i++) k = app.knobs(), k[0].step(1);
        CHECK(app.scale().scale == 7 && std::string(app.knobs()[0].value) == "OFF", "the SCALE knob walks all 22 and OFF is last (%s)", app.knobs()[0].value.c_str());
        k = app.knobs(); for (int i = 0; i < 40; i++) k = app.knobs(), k[0].step(-1);
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
        { auto k = app.knobs(); k[0].set(0.f); k[1].set(0.f); k[2].set(0.f); }
        run(0.5);
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
        CHECK(std::fabs(app.status().bpm - 120.f) < 0.6f, "tempo is locked while a loop exists (%.0f)", app.status().bpm);
        btn(SamplerApp::SAVE); app.onMidi((uint8_t[]){0x91, (uint8_t)SamplerApp::SAVE, 100}, 3); run(0.9); app.onMidi((uint8_t[]){0x81, (uint8_t)SamplerApp::SAVE, 100}, 3); run(0.3);
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
        { auto k = app.knobs(); k[1].set(1.f / 3.f); } run(0.3);   // 2 bars: content repeats
        CHECK(app.status().seqBars[0] == 2 && app.status().seqCount[0] == 4, "2 bars: content repeated (%d notes)", app.status().seqCount[0]);
        { auto k = app.knobs(); k[1].set(0.f); } run(0.3);
        CHECK(app.status().seqBars[0] == 1 && app.status().seqCount[0] == 2, "back to 1 bar: second half dropped (%d notes)", app.status().seqCount[0]);
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
        app.gotoPage(SamplerApp::P_TIMING); { auto k = app.knobs(); k[0].set(1.f / 7.f); k[1].set(0.f); k[2].set(0.f); }   // 1/4, playback quantize OFF
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
        app.gotoPage(SamplerApp::P_TIMING); { auto k = app.knobs(); k[0].set(0.f); }
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
        { const uint8_t d[3] = {0x91, (uint8_t)SamplerApp::SAVE, 100}, u[3] = {0x81, (uint8_t)SamplerApp::SAVE, 100}; app.onMidi(d, 3); run(0.9); app.onMidi(u, 3); run(0.3); }
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
        app.gotoPage(SamplerApp::P_FX); for (int i = 0; i < 6; i++) btn(SamplerApp::FX); CHECK(app.page() == SamplerApp::P_TFX, "FX x6 from the sends: track fx");
        app.gotoPage(SamplerApp::P_SQ); btn(SamplerApp::FX); CHECK(app.page() == SamplerApp::P_TFX, "FX returns to the page it was left on");
    }
    // ---- sequencer v2: overdub with undo, step entry, erase, touch, bounce to tape ----
    {
        auto quiet = [&] { run(4.0); };
        auto stop = [&] { if (app.status().playing) { btn(SamplerApp::PLAY); run(0.2); } };
        stop(); quiet(); app.selectTrack(0); app.seqClear(); run(0.3);
        app.gotoPage(SamplerApp::P_TIMING); { auto k = app.knobs(); k[0].set(0.f); k[1].set(0.f); k[2].set(0.f); }
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
    printf("%s (%d failures)\n", fails ? "FAILED" : "ALL OK", fails);
    return fails ? 1 : 0;
}
