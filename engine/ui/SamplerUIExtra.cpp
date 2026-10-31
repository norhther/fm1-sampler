// The sheets and views that came with the interface overhaul: the confirm dialog, the menu, the first-run tour, the long-press menu of a track,
// the mixer with its signal flow, and the UNDO toast. (Split from SamplerUI.cpp, which holds the frame, the top bar and the pages.)
#include "SamplerUI.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace theme;

namespace {
float clamp01(float v) { return std::min(1.f, std::max(0.f, v)); }
}

// ---- the dialog for what cannot be taken back ----
void SamplerUI::askTapeClear(int t) {
    if (t < 0) askConfirm("ERASE THE WHOLE TAPE?", "EVERY LOOP OF EVERY TRACK IS DELETED.", "YES, ERASE", [this] { app_.loopClear(); });
    else askConfirm("ERASE THE TAPE OF TRACK " + std::to_string(t + 1) + "?", "THIS TRACK'S LOOP IS DELETED.", "YES, ERASE", [this, t] { app_.clearTrackLoop(t); });
}

void SamplerUI::confirmSheet() {
    rrect({0, 0, W_, H_}, 0, withAlpha(BG, 225));
    tap({0, 0, W_, H_}, [this] { confirmOpen_ = false; });
    const Rect box{W_ * 0.22f, H_ * 0.26f, W_ * 0.56f, H_ * 0.48f};
    rrect(box, R_CARD, SURFACE); stroke(box, R_CARD, DANGER, 2);
    tap(box, [] {});
    text(box.x + box.w / 2, box.y + 34, T_TITLE, TEXT, fit(confirm_.title, box.w - 20, T_TITLE), 1, true);
    text(box.x + box.w / 2, box.y + 64, T_CAPTION, DIM, fit(confirm_.body, box.w - 20, T_CAPTION), 1, false);
    text(box.x + box.w / 2, box.y + 84, T_CAPTION, DANGER, "THIS CANNOT BE UNDONE. ARE YOU ABSOLUTELY SURE?", 1, true);
    const float bw = (box.w - 36) / 2, by = box.y + box.h - 58;
    button({box.x + 12, by, bw, 46}, "NO, KEEP IT", false, [this] { confirmOpen_ = false; }, 12);
    const Rect y{box.x + 24 + bw, by, bw, 46};
    rrect(y, R_CONTROL, DANGER);
    text(y.x + y.w / 2, y.y + y.h / 2, 12, BG, confirm_.yes, 1, true);
    tap(y, [this] { auto go = confirm_.go; confirmOpen_ = false; if (go) go(); });
}

// ---- NEW LOOP: a take on the tape, OP-1 style, or a pattern that records and loops, as a sequencer ----
void SamplerUI::newLoopSheet() {
    rrect({0, 0, W_, H_}, 0, withAlpha(BG, 225));
    tap({0, 0, W_, H_}, [this] { newLoopOpen_ = false; });
    const Rect box{W_ * 0.16f, H_ * 0.12f, W_ * 0.68f, H_ * 0.76f};
    rrect(box, R_CARD, SURFACE); stroke(box, R_CARD, ACCENT, 2);
    tap(box, [] {});
    text(box.x + 14, box.y + 22, T_TITLE, TEXT, "NEW LOOP ON TRACK " + std::to_string(app_.track() + 1), 0, true);
    const float cw = (box.w - 36) / 2; float y = box.y + 44;
    button({box.x + 12, y, cw, 40}, "TAPE (OP-1 STYLE)", !nlSeq_, [this] { nlSeq_ = false; }, 11);
    button({box.x + 24 + cw, y, cw, 40}, "SEQUENCER", nlSeq_, [this] { nlSeq_ = true; }, 11);
    y += 48;
    text(box.x + 14, y + 8, T_CAPTION, DIM, fit(nlSeq_ ? "A PATTERN THAT RECORDS THE KEYS YOU PLAY AND LOOPS. EDIT IT ON THE SEQ PAGE." : "THE FIRST TAKE ON THE TAPE: PLAY, AND IT ENDS BY ITSELF. THEN OVERDUB WITH REC.", box.w - 28, T_CAPTION), 0, false);
    y += 28;
    static const int bars[7] = {1, 2, 4, 8, 16, 32, 64};
    const float bw = (box.w - 24 - 6 * 4) / 7;
    for (int i = 0; i < 7; i++) {
        const bool off = nlSeq_ && bars[i] > 16;   // a pattern holds 16 bars
        button({box.x + 12 + i * (bw + 4), y, bw, 40}, std::to_string(bars[i]) + (i == 0 ? " BAR" : ""), nlBars_ == i && !off, [this, i, off] { if (!off) nlBars_ = i; }, 10.5f);
    }
    y += 48;
    text(box.x + 14, y + 8, T_CAPTION, DIM, fit(nlSeq_ ? "UP TO 16 BARS. IT STARTS RECORDING AT ONCE." : app_.loopExists() ? "THE TAPE ALREADY HAS A LOOP: CLEAR IT FIRST." : "UP TO 64 BARS, AT THE CURRENT TEMPO. REC STARTS IT.", box.w - 28, T_CAPTION), 0, false);
    const float by = box.y + box.h - 58;
    button({box.x + 12, by, cw, 46}, "CANCEL", false, [this] { newLoopOpen_ = false; }, 12);
    button({box.x + 24 + cw, by, cw, 46}, "START", false, [this] { newLoopOpen_ = false; app_.newLoop(std::min(nlSeq_ ? 16 : 64, bars[nlBars_]), nlSeq_); }, 12, true);
}

// ---- a few seconds of UNDO after something that can be undone ----
void SamplerUI::undoToast() {
    if (!undoMsgAt_) return;
    if (now_ - undoMsgAt_ > 5000) { undoMsgAt_ = 0; return; }
    const Rect b{W_ / 2 - 190, H_ - 58, 380, 42};
    rrect(b, R_CARD, RAISED); stroke(b, R_CARD, ACCENT, 1);
    text(b.x + 14, b.y + b.h / 2, 11, TEXT, undoMsg_, 0, true);
    button({b.x + b.w - 96, b.y + 5, 90, b.h - 10}, "UNDO", true, [this] { app_.globalUndo(); undoMsgAt_ = 0; }, 11);
}

// ---- the menu: everything that is not music ----
void SamplerUI::menuSheet() {
    rrect({0, 0, W_, H_}, 0, withAlpha(BG, 215));
    tap({0, 0, W_, H_}, [this] { menuOpen_ = false; });
    const Rect box{W_ * 0.12f, 12, W_ * 0.76f, H_ - 24};
    rrect(box, R_CARD, SURFACE); stroke(box, R_CARD, ACCENT, 1);
    tap(box, [] {});
    text(box.x + 14, box.y + 20, T_TITLE, TEXT, "MENU", 0, true);
    text(box.x + 80, box.y + 21, T_CAPTION, DIM, std::string(linkMidi_ ? "FM-1 CONNECTED" : "NO FM-1") + "   CPU " + std::to_string((int)(st_.cpu_permille / 10)) + "%", 0, false);
    button({box.x + box.w - 70, box.y + 6, 60, 28}, "CLOSE", false, [this] { menuOpen_ = false; }, 10);
    const float lab = 92, bx = box.x + lab + 10, bw = box.w - lab - 22, bh = 32; float y = box.y + 46;
    auto row = [&](const char* name, std::vector<std::pair<std::string, bool>> items, std::function<void(int)> f, bool danger = false) {
        text(box.x + 12, y + bh / 2, T_CAPTION, DIM, name, 0, true);
        const float w = (bw - 4 * (items.size() - 1)) / items.size();
        for (size_t i = 0; i < items.size(); i++) { const Rect c{bx + i * (w + 4), y, w, bh}; if (danger) dangerButton(c, items[i].first, [f, i] { f((int)i); }, 10); else button(c, items[i].first, items[i].second, [f, i] { f((int)i); }, 10); }
        y += bh + 8;
    };
    row("SOUND GOES TO", {{"THE FM-1", !app_.phoneOut()}, {"PHONE SPEAKER", app_.phoneOut()}}, [this](int i) { app_.setPhoneOut(i == 1); });
    row("SOUNDS", {{app_.audition() ? "AUDITION: ON" : "AUDITION: OFF", app_.audition()}, {"SAVE LOOP TO LIBRARY", false}, {"PROJECTS", false}, {"NEW PROJECT", false}},
        [this](int i) { if (i == 0) app_.setAudition(!app_.audition()); else if (i == 1) { app_.loopSave(); } else if (i == 3) { if (app_.newProject()) menuOpen_ = false; } else { menuOpen_ = false; projOpen_ = true; projSel_ = -1; projScroll_ = 0; } });
    row("TIMING", {{"TAP TEMPO", false}, {"TAKE: " + (app_.takeBars() ? std::to_string(app_.takeBars()) + (app_.takeBars() == 1 ? " BAR" : " BARS") : std::string("FREE")), app_.takeBars() > 0}, {"QUANTIZE: " + app_.quantText(), app_.quantDiv() > 0}},
        [this](int i) { if (i == 0) app_.tapTempo(); else if (i == 1) app_.cycleTakeBars(); else app_.cycleQuant(); });
    row("OTHER GEAR", {{"MIDI SYNC: OFF", app_.midiSync() == 0}, {"FOLLOW CLOCK IN", app_.midiSync() == 1}, {"SEND CLOCK OUT", app_.midiSync() == 2}}, [this](int i) { app_.setMidiSync(i); });
    { std::vector<std::pair<std::string, bool>> th; for (int i = 0; i < theme::kThemes; i++) th.push_back({theme::name(i), app_.themeId() == i}); row("LOOK", th, [this](int i) { app_.setTheme(i); }); }
    row("SCREEN", {{app_.fullscreen() ? "FULLSCREEN: ON" : "FULLSCREEN: OFF", app_.fullscreen()}, {app_.leftHanded() ? "LEFT-HANDED: ON" : "LEFT-HANDED: OFF", app_.leftHanded()},
                   {app_.access() ? "BIG TEXT, HIGH CONTRAST: ON" : "BIG TEXT, HIGH CONTRAST: OFF", app_.access()}},
        [this](int i) { if (i == 0) app_.setFullscreen(!app_.fullscreen()); else if (i == 1) app_.setLeftHanded(!app_.leftHanded()); else { const bool on = !app_.access(); app_.setAccess(on); app_.setTheme(on ? 4 : 0); } });
    row("HELP", {{"SHOW THE TOUR AGAIN", false}}, [this](int) { app_.setTourDone(false); openTour(); });
    const std::string t = app_.toast();
    if (!t.empty()) text(box.x + 14, box.y + box.h - 12, 11, ACCENT, fit(t, box.w - 28, 11), 0, true);
}

// ---- long-press on a track: what you can do with it ----
void SamplerUI::trackMenuSheet() {
    const int t = trackMenu_;
    rrect({0, 0, W_, H_}, 0, withAlpha(BG, 215));
    tap({0, 0, W_, H_}, [this] { trackMenu_ = -1; });
    const Rect box{W_ * 0.3f, H_ * 0.12f, W_ * 0.4f, H_ * 0.76f};
    rrect(box, R_CARD, SURFACE); stroke(box, R_CARD, ACCENT, 1);
    tap(box, [] {});
    const std::string lab = app_.sampleLabel(t);
    text(box.x + 14, box.y + 20, T_TITLE, TEXT, "TRACK " + std::to_string(t + 1), 0, true);
    text(box.x + 14, box.y + 40, T_CAPTION, DIM, fit(lab.empty() ? "EMPTY" : lab, box.w - 28, T_CAPTION), 0, false);
    float y = box.y + 54; const float bw = box.w - 24, bh = 36;
    auto b = [&](const std::string& s, bool on, std::function<void()> f) { button({box.x + 12, y, bw, bh}, s, on, std::move(f), 10.5f); y += bh + 6; };
    b("OPEN ITS SOUND", false, [this] { trackMenu_ = -1; app_.gotoMode(0); });
    b(app_.state(t).mute ? "UNMUTE" : "MUTE", app_.state(t).mute, [this, t] { app_.toggleMute(t); });
    b("SOLO", app_.state(t).solo, [this, t] { app_.toggleSolo(t); });
    b("CLEAR ITS PATTERN", false, [this, t] { app_.seqClear(); showUndo("PATTERN OF TRACK " + std::to_string(t + 1) + " CLEARED"); trackMenu_ = -1; });
    if (st_.track_has_clip[t]) { dangerButton({box.x + 12, y, bw, bh}, "ERASE ITS TAPE", [this, t] { trackMenu_ = -1; askTapeClear(t); }, 10.5f); y += bh + 6; }
    b("CLOSE", false, [this] { trackMenu_ = -1; });
}

// ---- the signal flow: where the sound of a track goes (tap a block to open its page) ----
void SamplerUI::flowStrip(Rect r) {
    struct Blk { const char* name; int page; };
    static const Blk blk[] = {{"TRACKS", -1}, {"EQ", SamplerApp::P_TEQ}, {"SENDS", SamplerApp::P_FX}, {"MASTER FX", SamplerApp::P_FX2}, {"LIMITER", SamplerApp::P_DUCK}, {"OUT", -1}, {"TAPE", SamplerApp::P_TAPEV}};
    const int n = 7; const float gap = 12, w = (r.w - gap * (n - 1)) / n;
    for (int i = 0; i < n; i++) {
        const Rect c{r.x + i * (w + gap), r.y, w, r.h};
        const bool link = blk[i].page >= 0;
        rrect(c, R_CONTROL, link ? RAISED : SURFACE); stroke(c, R_CONTROL, link ? ACCENT : OUTLINE, 1);
        text(c.x + c.w / 2, c.y + c.h / 2, 8.f, link ? TEXT : DIM, fit(blk[i].name, c.w - 4, 8.f), 1, true);
        if (link) { const int pg = blk[i].page; tap(c, [this, pg] { app_.gotoPage((SamplerApp::Page)pg); }); }
        if (i + 1 < n) tri(c.x + c.w + 2, c.y + c.h / 2 - 4, c.x + c.w + 2, c.y + c.h / 2 + 4, c.x + c.w + gap - 2, c.y + c.h / 2, DIM);
    }
}

// ---- the mixer: eight strips (the next eight when the selected track is 9..16), tap = this track, drag the fader or the pan ----
void SamplerUI::mixerView(Rect r) {
    flowStrip({r.x, r.y, r.w, 24});
    const float top = r.y + 30, h = r.h - 30, sw = (r.w - 7 * 4) / 8.f;
    const int bank = app_.track() / 8;   // eight strips at a time: the bank of the selected track
    for (int t = bank * 8; t < std::min(app_.trackCount(), bank * 8 + 8); t++) {
        const Rect c{r.x + (t - bank * 8) * (sw + 4), top, sw, h};
        const auto& x = app_.state(t); const bool sel = t == app_.track();
        const bool audible = !x.mute && (!app_.anySolo() || x.solo);
        rrect(c, R_CARD, sel ? RAISED : SURFACE); stroke(c, R_CARD, sel ? ACCENT : OUTLINE, sel ? 2 : 1);
        text(c.x + sw / 2, c.y + 12, 13, sel ? ACCENT : audible ? TEXT : DIM, std::to_string(t + 1), 1, true);
        tap({c.x, c.y, c.w, 24}, [this, t] { app_.selectTrack(t); });
        const float bw = sw - 8; float by = c.y + 26;
        button({c.x + 4, by, bw, 22}, "MUTE", x.mute, [this, t] { app_.toggleMute(t); }, 8.f); by += 25;
        button({c.x + 4, by, bw, 22}, "SOLO", x.solo, [this, t] { app_.toggleSolo(t); }, 8.f); by += 28;
        const float fh = c.y + c.h - 26 - by;
        const Rect f{c.x + 6, by, std::max(14.f, sw * 0.5f - 6), fh}, mt{c.x + sw - 16, by, 8, fh};
        rrect(f, 0, BG); stroke(f, 0, OUTLINE, 1);
        const float vy = f.y + f.h * (1.f - clamp01(x.vol));
        rrect({f.x + 1, vy, f.w - 2, f.y + f.h - vy - 1}, 0, audible ? TEXT : DIM);
        rrect({f.x - 2, vy - 2, f.w + 4, 4}, 0, ACCENT);   // the handle
        const Rect fhit{c.x, f.y - 6, sw * 0.62f, f.h + 12};
        auto setV = [this, t, f](float, float py) { app_.setVolume(t, clamp01(1.f - (py - f.y) / f.h)); };
        hit(fhit, setV, setV);
        rrect(mt, 0, BG); stroke(mt, 0, OUTLINE, 1);
        { const float lv = std::min(1.f, st_.track_peak[t] * 1.5f); rrect({mt.x + 1, mt.y + mt.h - 1 - (mt.h - 2) * lv, mt.w - 2, std::max(1.f, (mt.h - 2) * lv)}, 0, audible ? TEXT : DIM); }
        const Rect p{c.x + 4, c.y + c.h - 20, bw, 14};   // pan: the mark away from the middle
        rrect(p, 0, BG); stroke(p, 0, OUTLINE, 1);
        line(p.x + p.w / 2, p.y, p.x + p.w / 2, p.y + p.h, OUTLINE, 1);
        rrect({p.x + 1 + x.pan * (p.w - 6), p.y + 1, 4, p.h - 2}, 0, ACCENT);
        auto setP = [this, t, p](float px, float) { app_.setPan01(t, clamp01((px - p.x) / p.w)); };
        hit({p.x, p.y - 4, p.w, p.h + 8}, setP, setP);
    }
}

// ---- first-run tour: coach marks over the real screen ----
void SamplerUI::tourSheet() {
    struct Step { const char* title; const char* body; const Rect* box; };
    const Step steps[] = {
        {"TRACKS", "Eight tracks. Tap one to play it. Hold a track for its menu: mute, solo, clear.", &rTracks_},
        {"PICK A SOUND", "Tap the sample name below the waveform to open the library. Turn AUDITION on in the MENU to hear sounds as you browse.", &rArea_},
        {"FIVE TABS", "SOUND, FX, SEQ for patterns, MIX and TAPE. The grey sentence under the pages says what the page is for.", &rPanel_},
        {"RECORD", "REC records what you play onto the tape. TAKE sets how many bars it lasts. Everything recorded is in the TAPE tab.", &rRec_},
        {"UNDO", "UNDO and REDO work on every track. Only erasing the tape cannot be undone, so it always asks first.", &rUndo_},
        {"TEMPO", "Change the tempo here, or drag on the number. It locks once the tape holds a loop.", &rTempo_},
    };
    const int n = 6; tourStep_ = std::min(n - 1, std::max(0, tourStep_));
    const Step& s = steps[tourStep_]; const Rect h = *s.box;
    const uint32_t dim = withAlpha(BG, 205);
    rrect({0, 0, W_, h.y}, 0, dim); rrect({0, h.y + h.h, W_, H_ - h.y - h.h}, 0, dim);
    rrect({0, h.y, h.x, h.h}, 0, dim); rrect({h.x + h.w, h.y, W_ - h.x - h.w, h.h}, 0, dim);
    stroke({h.x - 2, h.y - 2, h.w + 4, h.h + 4}, R_CONTROL, ACCENT, 3);
    tap({0, 0, W_, H_}, [] {});   // nothing underneath reacts while the tour runs
    const bool low = h.y + h.h / 2 < H_ / 2;
    const Rect card{W_ * 0.25f, low ? H_ - 150.f : 56.f, W_ * 0.5f, 120};
    rrect(card, R_CARD, SURFACE); stroke(card, R_CARD, ACCENT, 1);
    text(card.x + 14, card.y + 18, T_TITLE, ACCENT, std::to_string(tourStep_ + 1) + "/" + std::to_string(n) + "  " + s.title, 0, true);
    { const std::string b = s.body; const size_t per = (size_t)std::max(20.f, (card.w - 28) / (11.f * textScale_ * 0.6f)); size_t a = 0; int ln = 0;
      while (a < b.size() && ln < 3) { size_t e = std::min(b.size(), a + per); if (e < b.size()) { const size_t sp = b.rfind(' ', e); if (sp != std::string::npos && sp > a) e = sp; } text(card.x + 14, card.y + 44 + ln * 16 * textScale_, 11, TEXT, b.substr(a, e - a), 0, false); a = e + (e < b.size() && b[e] == ' ' ? 1 : 0); ln++; } }
    const bool last = tourStep_ == n - 1;
    button({card.x + card.w - 110, card.y + card.h - 40, 100, 32}, last ? "DONE" : "NEXT", true, [this, last] { if (last) { tourOpen_ = false; app_.setTourDone(true); } else tourStep_++; }, 11);
    button({card.x + 12, card.y + card.h - 40, 100, 32}, "SKIP TOUR", false, [this] { tourOpen_ = false; app_.setTourDone(true); }, 10);
}
