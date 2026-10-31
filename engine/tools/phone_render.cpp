// Drives SamplerApp + SamplerUI with touches and dumps the draw ops of several screens as JSON (render_ui.py draws them).
#include <Session.h>
#include <SamplerApp.h>
#include <SamplerUI.h>
#include <Common.h>
#include <cmath>
#include <thread>
#include <chrono>
#include <cstdio>
#include <string>
static std::string esc(const std::string& s) { std::string o; for (char c : s) { if (c == '"' || c == '\\') o += '\\'; o += c; } return o; }
int main(int argc, char** argv) {
    Denormals::enableOnThisThread();
    const int sr = SAMPLERATE;
    Session s(sr); s.setDataDir("/tmp/fm1sampler_test"); s.setSampleDir(std::string(getenv("HOME")) + "/fm1-sampler/samples");
    SamplerApp app(s, [](const uint8_t*, size_t) {}); app.setTourDone(true);
    if (argc > 2) app.setTheme(atoi(argv[2]));
    SamplerUI ui(app);
    const float W = 860, H = 400;
    ui.setSize(W, H, 0, 0); ui.setLink(true, true);
    std::vector<float> buf(512), in(512, 0.f);
    uint32_t now = 0;
    auto run = [&](double sec) { for (long i = 0; i < (long)(sec * sr) / 256; i++) { std::fill(buf.begin(), buf.end(), 0.f); s.process(buf.data(), in.data(), 256); std::this_thread::sleep_for(std::chrono::microseconds(200)); if (i % 5 == 0) { now += 33; app.tick(now); } } };
    auto tapAt = [&](float x, float y) { ui.touch(0, x, y, now); ui.touch(2, x, y, now); run(0.05); };
    run(2.5);
    for (int i = 0; i < 4; i++) { app.playNote(60 + i * 4, true); run(0.3); app.playNote(60 + i * 4, false); run(0.1); }
    // a loop on two tracks: the first take on track 1, an overdub of notes on track 2
    app.loopRecord(); run(0.1); { int sq[] = {60, 64, 67, 72, 67, 64}; for (int n : sq) { app.playNote(n, true); run(0.45); app.playNote(n, false); run(0.15); } } app.loopRecord(); run(0.5);
    app.selectTrack(1); app.setAlgo(1); app.loopRecord(); run(0.2); { int sq[] = {48, 55}; for (int n : sq) { app.playNote(n, true); run(0.9); app.playNote(n, false); run(0.6); } } app.loopRecord(); run(1.0); app.selectTrack(0);
    FILE* f = fopen(argc > 1 ? argv[1] : "ui_ops.json", "w");
    fprintf(f, "{\"W\":%g,\"H\":%g,\"screens\":[", W, H);
    int count = 0;
    auto dump = [&](const char* name) { fprintf(stderr, "dump %s\n", name);
        run(0.2); ui.frame(now);
        fprintf(f, "%s{\"name\":\"%s\",\"ops\":[", count++ ? "," : "", name);
        bool first = true;
        for (auto& o : ui.ops()) { fprintf(f, "%s[%d,%g,%g,%g,%g,%g,%g,%u,\"%s\"]", first ? "" : ",", (int)o.type, o.f[0], o.f[1], o.f[2], o.f[3], o.f[4], o.f[5], o.color, esc(o.text).c_str()); first = false; }
        fprintf(f, "]}");
    };
    auto tab = [&](int i) { app.gotoPage((SamplerApp::Page)i); run(0.05); };   // Page order == the old tab order
    ui.frame(now); dump("sample");
    tab(1); ui.frame(now); dump("fx");
    tab(2); ui.frame(now); dump("env");
    tab(3); ui.frame(now); dump("lfo");
    tab(6); ui.frame(now); dump("mix");
    tab(7); ui.frame(now); dump("key");
    tab(8); ui.frame(now); dump("loop");
    tab(9); ui.frame(now); dump("len");
    tab(10); ui.frame(now); dump("fx2");
    tab(11); ui.frame(now); dump("live");
    tab(12); { int notes[] = {60, 64, 67, 72, 67, 64, 55, 62}; for (int i = 0; i < 8; i++) app.seqToggleAt(i * 96, notes[i], i % 3 == 0 ? 192 : 0); app.setSeqState(1); run(0.5); app.playNote(65, true); } ui.frame(now); dump("seq");
    app.saveProject("Demo One"); app.saveProject("Beat idea"); run(0.5); ui.openProjectSheet(); ui.frame(now); dump("projects"); ui.touch(0, 5, 5, now); ui.touch(2, 5, 5, now); ui.frame(now);
    tab(13); ui.frame(now); dump("tempo");
    tab(19); for (int i = 0; i < 5; i++) app.tombolaDrop(); run(1.2); ui.frame(now); dump("tomb"); app.tombolaClear();
    tab(16); ui.frame(now); dump("tone");
    tab(18); ui.frame(now); dump("live2");
    tab(0);
    app.playNote(60, true); app.playNote(67, true); run(0.5); ui.frame(now); dump("voices"); app.playNote(60, false); app.playNote(67, false); run(0.3);
    tapAt(60, 100); run(1.0); ui.frame(now); dump("track2");
    app.micSample(); in.assign(512, 0.01f); run(0.4); ui.frame(now); dump("mic_wait");
    { double ph = 0; for (long i = 0; i < 3 * sr / 256; i++) { for (int k = 0; k < 256; k++) { double env = 0.15 + 0.6 * std::fabs(std::sin(i * 256.0 / sr * 2.2)); float v = (float)(env * std::sin(ph)); ph += 0.05; in[2 * k] = in[2 * k + 1] = v; } std::fill(buf.begin(), buf.end(), 0.f); s.process(buf.data(), in.data(), 256); std::this_thread::sleep_for(std::chrono::microseconds(200)); if (i % 5 == 0) { now += 33; app.tick(now); } } }
    ui.frame(now); dump("mic_rec");
    app.micSample(); in.assign(512, 0.f); run(2.0); app.tick(now + 100);
    tapAt(176 + 8 + 100, 74 + 8 + 66 + 142 + 6 + 10 + 12 + 17); ui.frame(now); dump("library");
    ui.touch(0, 5, 5, now); ui.touch(2, 5, 5, now); ui.frame(now);
    app.selectTrack(0);
    tab(SamplerApp::P_TAPEV); ui.frame(now);
    app.gotoPage(SamplerApp::P_MIX); app.setVolume(0, 0.9f); app.setVolume(1, 0.5f); app.toggleMute(2); app.toggleSolo(1); ui.frame(now); dump("mixer"); app.toggleMute(2); app.toggleSolo(1);
    ui.openMenu(); ui.frame(now); dump("menu"); ui.touch(0, 790, 22, now); ui.touch(2, 790, 22, now); ui.frame(now);
    app.gotoPage(SamplerApp::P_HOME); app.setAccess(true); app.setTheme(4); ui.frame(now); dump("access");
    app.setAccess(false); app.setTheme(0); app.setLeftHanded(true); ui.frame(now); dump("lefthand"); app.setLeftHanded(false);
    app.gotoPage(SamplerApp::P_TAPEV); ui.frame(now); dump("tape_view");
    app.gotoPage(SamplerApp::P_TFX2); ui.frame(now); dump("trackfx2");
    app.gotoPage(SamplerApp::P_PADS); ui.frame(now); dump("pads");
    app.gotoPage(SamplerApp::P_TAPEV);
    ui.frame(now); dump("tape_view2");
    app.selectTrack(0); app.setScale(2, 0, 0, 0); app.gotoPage(SamplerApp::P_PADS); ui.frame(now); dump("pads_scale");
    app.gotoPage(SamplerApp::P_TSTOP); ui.frame(now); dump("tapestop");
    for (int i = 0; i < 5; i++) app.addTrack();
    app.gotoPage(SamplerApp::P_TAPEV); ui.frame(now); dump("tracks13");
    app.gotoPage(SamplerApp::P_MIX); ui.frame(now); dump("mixer_bank2");
    app.selectTrack(0); app.gotoPage(SamplerApp::P_TAPEV); ui.frame(now); { float nx = 0, ny = 0; for (auto& o : ui.ops()) if (o.type == UiOp::TEXT && o.text == "NEW LOOP") { nx = o.f[0]; ny = o.f[1] - 4; } tapAt(nx, ny); } ui.frame(now); dump("newloop");
    tapAt(5, 5); ui.frame(now);
    app.selectTrack(0); app.gotoPage(SamplerApp::P_TRIG); ui.frame(now); dump("trigger");
    app.gotoPage(SamplerApp::P_SQ); app.setKeyTrigger(0, true); ui.frame(now); { float nx = 0, ny = 0; for (auto& o : ui.ops()) if (o.type == UiOp::TEXT && o.text == "FULL SCREEN") { nx = o.f[0]; ny = o.f[1] - 4; } tapAt(nx, ny); } ui.frame(now); dump("seqfull");
    fprintf(f, "]}\n"); fclose(f);
    return 0;
}
