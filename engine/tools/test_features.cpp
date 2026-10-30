// Host tests of the added features: tick grid and quantize, master fx, metronome, sequencer, tempo and take snap, scales and snap mode, page cycling.
#include <Session.h>
#include <SamplerApp.h>
#include <MasterFx.h>
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
int main() {
    Denormals::enableOnThisThread();
    testGrid();
    testFx();
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
    btn(SamplerApp::FX); CHECK(app.page() == SamplerApp::P_LIVE, "FX again: live fx");
    btn(SamplerApp::FX); CHECK(app.page() == SamplerApp::P_TFX, "FX again: track fx");
    btn(SamplerApp::FX); CHECK(app.page() == SamplerApp::P_FX, "FX again: back to sends");
    btn(SamplerApp::ARP); btn(SamplerApp::ARP); CHECK(app.page() == SamplerApp::P_SQ, "ARP twice: sequencer");
    btn(SamplerApp::GLO); btn(SamplerApp::GLO); CHECK(app.page() == SamplerApp::P_TEMPO, "GLO twice: tempo");
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
        btn(SamplerApp::FX); btn(SamplerApp::FX);
        for (int i = 0; i < 4; i++) { auto k = app.knobs(); k[i].set(0.8f); }
        size_t m = all.size(); app.playNote(60, true); run(1.0); app.playNote(60, false); run(0.3);
        CHECK(rms(all, m, all.size()) > 0.002 && finite(all), "FX 2 inserts all on: sound, finite (rms %.4f)", rms(all, m, all.size()));
        for (int i = 0; i < 4; i++) { auto k = app.knobs(); k[i].set(0.f); }
        btn(SamplerApp::FX);
        { auto k = app.knobs(); k[0].set(0.2f); k[1].set(1.f); k[2].set(1.f); k[3].set(0.5f); }
        m = all.size(); app.playNote(60, true); run(1.0); app.playNote(60, false); run(0.3);
        CHECK(rms(all, m, all.size()) > 0.0005 && finite(all), "LIVE FX all on: finite (rms %.4f)", rms(all, m, all.size()));
        { auto k = app.knobs(); k[0].set(0.5f); k[1].set(0.f); k[2].set(0.f); k[3].set(0.f); }
        run(0.3);
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
        { auto k = app.knobs(); k[0].set(0.5f); }   // PLAY: now the pattern plays by itself
        run(0.3); size_t m = all.size(); run(4.0);
        CHECK(rms(all, m, all.size()) > 0.003, "pattern plays with no live notes (rms %.4f)", rms(all, m, all.size()));
        { auto k = app.knobs(); k[2].set(4.f / 7.f); }   // quantize 1/16
        run(0.3); m = all.size(); run(2.0); CHECK(rms(all, m, all.size()) > 0.003, "pattern plays with quantize 1/16");
        { auto k = app.knobs(); k[1].set(1.f / 3.f); } run(0.3);   // 2 bars: content repeats
        CHECK(app.status().seqBars[0] == 2 && app.status().seqCount[0] == 4, "2 bars: content repeated (%d notes)", app.status().seqCount[0]);
        { auto k = app.knobs(); k[1].set(0.f); } run(0.3);
        CHECK(app.status().seqBars[0] == 1 && app.status().seqCount[0] == 2, "back to 1 bar: second half dropped (%d notes)", app.status().seqCount[0]);
        { auto k = app.knobs(); k[3].step(1); } run(0.5);
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
        app.gotoPage(SamplerApp::P_SQ); { auto k = app.knobs(); k[1].set(0.f); k[0].set(1.f); }                              // 1 bar, REC from a stopped transport
        run(0.01);
        run(0.79); size_t mark = all.size(); app.playNote(60, true); run(0.12); size_t early = all.size(); run(0.28); app.playNote(60, false);   // 0.8 s: closer to beat 2 (1.0 s)
        const double rEarly = rms(all, mark, early), rLate = rms(all, early + (size_t)sr * 2 / 10, all.size());
        run(0.5); app.playNote(64, true); run(0.2); app.playNote(64, false);                                                                  // 1.7 s: closer to beat 3 (1.5 s), already late
        run(0.5);
        Session::SeqView v[kSeqMax]; const int n = s.seqNotes(0, v, kSeqMax);
        CHECK(n == 2, "live quantize: %d notes recorded", n);
        if (n == 2) CHECK(std::abs(v[0].tick - 384) <= 3 && std::abs(v[1].tick - 576) <= 3, "recorded on the grid: ticks %d (expect 384) and %d (expect 576)", v[0].tick, v[1].tick);
        CHECK(rEarly < 0.003 && rLate > 0.003, "the early key waits for the grid line (before %.4f, after %.4f)", rEarly, rLate);
        { auto k = app.knobs(); k[0].set(0.f); k[3].step(1); }   // stop and clear
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
        app.gotoPage(SamplerApp::P_FX); btn(SamplerApp::FX); btn(SamplerApp::FX); btn(SamplerApp::FX); CHECK(app.page() == SamplerApp::P_TFX, "FX x3 from the sends: track fx");
        btn(SamplerApp::GLO); btn(SamplerApp::GLO); btn(SamplerApp::GLO); app.gotoPage(SamplerApp::P_KEY); btn(SamplerApp::GLO); btn(SamplerApp::GLO); CHECK(app.page() == SamplerApp::P_TIMING, "GLO x2 from the key: timing");
    }
    printf("%s (%d failures)\n", fails ? "FAILED" : "ALL OK", fails);
    return fails ? 1 : 0;
}
