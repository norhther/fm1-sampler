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
    const float cw = (box.w - 36) / 2, c3 = (box.w - 32) / 3; float y = box.y + 44;
    button({box.x + 12, y, c3, 40}, "TAPE (TAKE)", !nlSeq_ && !nlEmpty_, [this] { nlSeq_ = false; nlEmpty_ = false; }, 10.5f);
    button({box.x + 16 + c3, y, c3, 40}, "EMPTY TAPE", !nlSeq_ && nlEmpty_, [this] { nlSeq_ = false; nlEmpty_ = true; }, 10.5f);
    button({box.x + 20 + 2 * c3, y, c3, 40}, "SEQUENCER", nlSeq_, [this] { nlSeq_ = true; }, 10.5f);
    y += 48;
    text(box.x + 14, y + 8, T_CAPTION, DIM, fit(nlSeq_ ? "A PATTERN THAT RECORDS THE KEYS YOU PLAY AND LOOPS. EDIT IT ON THE SEQ PAGE." : nlEmpty_ ? "A SILENT TAPE OF THE LENGTH YOU PICK. MOVE THE HEAD, THEN REC FROM." : "THE FIRST TAKE ON THE TAPE: PLAY, AND IT ENDS BY ITSELF. THEN OVERDUB WITH REC.", box.w - 28, T_CAPTION), 0, false);
    y += 28;
    static const int bars[7] = {1, 2, 4, 8, 16, 32, 64};
    const float bw = (box.w - 24 - 6 * 4) / 7;
    for (int i = 0; i < 7; i++) {
        const bool off = nlSeq_ && bars[i] > 16;   // a pattern holds 16 bars
        button({box.x + 12 + i * (bw + 4), y, bw, 40}, std::to_string(bars[i]) + (i == 0 ? " BAR" : ""), nlBars_ == i && !off, [this, i, off] { if (!off) nlBars_ = i; }, 10.5f);
    }
    y += 48;
    text(box.x + 14, y + 8, T_CAPTION, DIM, fit(nlSeq_ ? "UP TO 16 BARS. IT STARTS RECORDING AT ONCE." : nlEmpty_ && !app_.loopExists() ? "UP TO 64 BARS, AT THE CURRENT TEMPO. NOTHING RECORDS YET." : app_.loopExists() ? "THE TAPE ALREADY HAS A LOOP: CLEAR IT FIRST." : "UP TO 64 BARS, AT THE CURRENT TEMPO. REC STARTS IT.", box.w - 28, T_CAPTION), 0, false);
    const float by = box.y + box.h - 58;
    button({box.x + 12, by, cw, 46}, "CANCEL", false, [this] { newLoopOpen_ = false; }, 12);
    button({box.x + 24 + cw, by, cw, 46}, "START", false, [this] { newLoopOpen_ = false; app_.newLoop(std::min(nlSeq_ ? 16 : 64, bars[nlBars_]), nlSeq_, nlEmpty_ && !nlSeq_); }, 12, true);
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
    text(box.x + 80, box.y + 21, T_CAPTION, DIM, std::string(linkMidi_ ? "FM-1 CONNECTED" : "NO FM-1") + "   CPU " + std::to_string((int)(st_.cpu_permille / 10)) + "% PEAK " + std::to_string((int)(st_.cpu_peak_permille / 10)) + "%" + (xruns_ >= 0 ? "   XRUNS " + std::to_string(xruns_) : std::string()), 0, false);
    button({box.x + box.w - 70, box.y + 6, 60, 28}, "CLOSE", false, [this] { menuOpen_ = false; }, 10);
    const float lab = 92, bx = box.x + lab + 10, bw = box.w - lab - 22, bh = 27; float y = box.y + 44;
    auto row = [&](const char* name, std::vector<std::pair<std::string, bool>> items, std::function<void(int)> f, bool danger = false) {
        text(box.x + 12, y + bh / 2, T_CAPTION, DIM, name, 0, true);
        const float w = (bw - 4 * (items.size() - 1)) / items.size();
        for (size_t i = 0; i < items.size(); i++) { const Rect c{bx + i * (w + 4), y, w, bh}; if (danger) dangerButton(c, items[i].first, [f, i] { f((int)i); }, 10); else button(c, items[i].first, items[i].second, [f, i] { f((int)i); }, 10); }
        y += bh + 5;
    };
    row("SOUND GOES TO", {{"THE FM-1", !app_.phoneOut()}, {"PHONE SPEAKER", app_.phoneOut()}}, [this](int i) { app_.setPhoneOut(i == 1); });
    row("SOUNDS", {{app_.audition() ? "AUDITION: ON" : "AUDITION: OFF", app_.audition()}, {"SAVE LOOP TO LIBRARY", false}, {"PROJECTS", false}, {"NEW PROJECT", false}},
        [this](int i) { if (i == 0) app_.setAudition(!app_.audition()); else if (i == 1) { app_.loopSave(); } else if (i == 3) { if (app_.newProject()) menuOpen_ = false; } else { menuOpen_ = false; projOpen_ = true; projSel_ = -1; projScroll_ = 0; } });
    row("BROWSE", {{"INSTRUMENTS", false}, {"EFFECTS", false}}, [this](int i) { menuOpen_ = false; if (i == 0) openLibrary(); else { fxOpen_ = true; fxTrack_ = false; } });
    row("TIMING", {{"TAP TEMPO", false}, {"TAKE: " + (app_.takeBars() ? std::to_string(app_.takeBars()) + (app_.takeBars() == 1 ? " BAR" : " BARS") : std::string("FREE")), app_.takeBars() > 0}, {"QUANTIZE: " + app_.quantText(), app_.quantDiv() > 0}},
        [this](int i) { if (i == 0) app_.tapTempo(); else if (i == 1) app_.cycleTakeBars(); else app_.cycleQuant(); });
    row("OTHER GEAR", {{"MIDI SYNC: OFF", app_.midiSync() == 0}, {"FOLLOW CLOCK IN", app_.midiSync() == 1}, {"SEND CLOCK OUT", app_.midiSync() == 2}}, [this](int i) { app_.setMidiSync(i); });
    for (int r0 = 0; r0 < theme::kThemes; r0 += 5) {   // the looks, five to a row
        std::vector<std::pair<std::string, bool>> th; const int e = std::min(theme::kThemes, r0 + 5);
        for (int i = r0; i < e; i++) th.push_back({theme::name(i), app_.themeId() == i});
        row(r0 == 0 ? "LOOK" : "", th, [this, r0](int i) { app_.setTheme(r0 + i); });
    }
    row("SCREEN", {{app_.fullscreen() ? "FULLSCREEN: ON" : "FULLSCREEN: OFF", app_.fullscreen()}, {app_.leftHanded() ? "LEFT-HANDED: ON" : "LEFT-HANDED: OFF", app_.leftHanded()},
                   {app_.access() ? "BIG TEXT, HIGH CONTRAST: ON" : "BIG TEXT, HIGH CONTRAST: OFF", app_.access()}},
        [this](int i) { if (i == 0) app_.setFullscreen(!app_.fullscreen()); else if (i == 1) app_.setLeftHanded(!app_.leftHanded()); else { const bool on = !app_.access(); app_.setAccess(on); app_.setTheme(on ? 4 : 0); } });
    row("HELP", {{"SHOW THE TOUR AGAIN", false}, {"FM-1 HINTS", false}}, [this](int i) { if (i == 0) { app_.setTourDone(false); openTour(); } else { menuOpen_ = false; fmOpen_ = true; } });
    const std::string t = app_.toast();
    if (!t.empty()) text(box.x + 14, box.y + box.h - 12, 11, ACCENT, fit(t, box.w - 28, 11), 0, true);
}


// ---- the FM-1 gestures: the hint of this page, the analog knob, the whole cheat sheet ----
void SamplerUI::fmSheet() {
    rrect({0, 0, W_, H_}, 0, withAlpha(BG, 215));
    tap({0, 0, W_, H_}, [this] { fmOpen_ = false; });
    const Rect box{W_ * 0.12f, 12, W_ * 0.76f, H_ - 24};
    rrect(box, R_CARD, SURFACE); stroke(box, R_CARD, ACCENT, 1);
    tap(box, [] {});
    text(box.x + 14, box.y + 20, T_TITLE, TEXT, "FM-1 HINTS", 0, true);
    const int bat = app_.fmBattery();
    text(box.x + 130, box.y + 21, T_CAPTION, DIM, linkMidi_ ? (bat >= 0 ? "FM-1 CONNECTED   BATTERY " + std::to_string(bat) + "%" : std::string("FM-1 CONNECTED")) : "NO FM-1", 0, false);
    button({box.x + box.w - 70, box.y + 6, 60, 28}, "CLOSE", false, [this] { fmOpen_ = false; }, 10);
    float y = box.y + 44; const float x = box.x + 14, w = box.w - 28;
    auto wrapped = [&](const std::string& s, float size, uint32_t col) {   // wrapped at spaces to the width of the sheet
        const size_t per = (size_t)std::max(12.f, w / (size * textScale_ * 0.6f)); size_t a = 0;
        while (a < s.size()) { size_t b = std::min(s.size(), a + per); if (b < s.size()) { const size_t sp = s.rfind(' ', b); if (sp != std::string::npos && sp > a) b = sp; } text(x, y + 6, size, col, s.substr(a, b - a), 0, false); y += size * textScale_ + 4; a = b; while (a < s.size() && s[a] == ' ') a++; }
    };
    const std::string h = SamplerApp::fmHint(app_.page());
    text(x, y + 6, T_CAPTION, ACCENT, std::string("THIS PAGE: ") + SamplerApp::pageName(app_.page()), 0, true); y += 18;
    if (h.empty()) wrapped("Nothing special on this page. The keys, the encoders and K1-K4 work as always.", 10.f, DIM); else wrapped(h, 10.f, TEXT);
    y += 8;
    text(x, y + 6, T_CAPTION, ACCENT, "ANALOG KNOB MOVES", 0, true);
    button({x + 150, y - 4, w - 150, 26}, std::string(SamplerApp::knobTargetName(app_.knobTarget())) + (app_.knobWaiting() ? "   (MOVE THE KNOB TO THE VALUE)" : ""), true, [this] { app_.setKnobTarget(app_.knobTarget() + 1); }, 10.f);
    y += 32;
    text(x, y + 6, T_CAPTION, ACCENT, "ALL GESTURES", 0, true); y += 18;
    for (int i = 0; SamplerApp::fmGesture(i); i++) wrapped(SamplerApp::fmGesture(i), 9.5f, TEXT);
}

// ---- long-press on a track: what you can do with it ----
void SamplerUI::trackMenuSheet() {
    const int t = trackMenu_;
    rrect({0, 0, W_, H_}, 0, withAlpha(BG, 215));
    tap({0, 0, W_, H_}, [this] { trackMenu_ = -1; });
    const Rect box{W_ * 0.3f, H_ * 0.03f, W_ * 0.4f, H_ * 0.94f};
    rrect(box, R_CARD, SURFACE); stroke(box, R_CARD, ACCENT, 1);
    tap(box, [] {});
    const std::string lab = app_.sampleLabel(t);
    text(box.x + 14, box.y + 20, T_TITLE, TEXT, "TRACK " + std::to_string(t + 1), 0, true);
    text(box.x + 14, box.y + 40, T_CAPTION, DIM, fit(lab.empty() ? "EMPTY" : lab, box.w - 28, T_CAPTION), 0, false);
    float y = box.y + 54; const float bw = box.w - 24, bh = std::min(32.f, (box.h - 54.f - 10.f) / 10.f - 5.f);   // ten rows at most, they always fit the sheet
    auto b = [&](const std::string& s, bool on, std::function<void()> f) { button({box.x + 12, y, bw, bh}, s, on, std::move(f), 10.5f); y += bh + 5; };
    b("OPEN ITS SOUND", false, [this] { trackMenu_ = -1; app_.gotoMode(0); });
    b(app_.state(t).mute ? "UNMUTE" : "MUTE", app_.state(t).mute, [this, t] { app_.toggleMute(t); });
    b("SOLO", app_.state(t).solo, [this, t] { app_.toggleSolo(t); });
    b("RESET SOUND TO DEFAULTS", false, [this, t] { app_.resetSound(t); trackMenu_ = -1; });
    b("RESET ITS EFFECTS", false, [this, t] { app_.resetTrackFx(t); trackMenu_ = -1; });
    b("RESET MASTER EFFECTS", false, [this] { app_.resetMasterFx(); trackMenu_ = -1; });
    b("CLEAR ITS PATTERN", false, [this, t] { app_.seqClear(); showUndo("PATTERN OF TRACK " + std::to_string(t + 1) + " CLEARED"); trackMenu_ = -1; });
    if (st_.track_has_clip[t]) { dangerButton({box.x + 12, y, bw, bh}, "ERASE ITS TAPE", [this, t] { trackMenu_ = -1; askTapeClear(t); }, 10.5f); y += bh + 5; }
    { const bool last = t == app_.trackCount() - 1 && app_.trackCount() > SamplerApp::kFmTracks;
      dangerButton({box.x + 12, y, bw, bh}, last ? "REMOVE TRACK" : "EMPTY THIS TRACK", [this, t] { trackMenu_ = -1; app_.removeTrack(t); }, 10.5f); y += bh + 5; }
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
        if (std::string(blk[i].name) == "OUT") {   // the master out as a scope: highest and lowest sample of every 256 frames, newest on the right
            float hi[Session::kScope], lo[Session::kScope]; app_.session().scope(hi, lo);
            const float cy = c.y + c.h / 2, half = c.h / 2 - 2, bw = (c.w - 4) / Session::kScope;
            for (int k = 0; k < Session::kScope; k++) { const float a = clamp01(hi[k]) * half, b = clamp01(-lo[k]) * half; rrect({c.x + 2 + k * bw, cy - a, std::max(1.f, bw), std::max(1.f, a + b)}, 0, st_.clip ? DANGER : TEXT); }
        } else
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
        if (st_.track_peak[t] > 0.98f) rrect({mt.x, mt.y - 3, mt.w, 3}, 0, DANGER);   // clip LED above the meter
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

// ---- synth tracks: what the patch is doing, drawn from the ENV and SYNTH pages (ponytail: shapes only, no per-voice phase) ----
void SamplerUI::synthViz(Rect w) {
    const int t = app_.track(); const auto ts = app_.state(t);
    const float lv = clamp01(st_.track_peak[t] * 1.5f);
    const float cw = (w.w - 2 * 6) / 3, pad = 10;
    auto cell = [&](int i) { return Rect{w.x + i * (cw + 6), w.y, cw, w.h}; };
    const auto pg = app_.page(); const int hot = pg == SamplerApp::P_ENV ? 0 : pg == SamplerApp::P_SYN1 || pg == SamplerApp::P_LFO ? 1 : pg == SamplerApp::P_SYN2 ? 2 : -1;   // the cell of the page you are editing
    auto frameCell = [&](Rect c, int i) { rrect(c, R_CARD, SURFACE); stroke(c, R_CARD, i == hot ? ACCENT : OUTLINE, i == hot ? 2 : 1); };
    auto glow = [&](Rect c) { rrect({c.x, c.y + c.h - 3, c.w * lv, 3}, 0, ACCENT); };   // level bar under each cell
    auto poly = [&](Rect c, const std::vector<std::pair<float, float>>& pts, bool fill) {   // points in 0..1, y up
        const float x0 = c.x + pad, y0 = c.y + c.h - 22, pw = c.w - 2 * pad, ph = c.h - 40;
        for (size_t i = 1; i < pts.size(); i++) {
            const float ax = x0 + pts[i - 1].first * pw, ay = y0 - pts[i - 1].second * ph, bx = x0 + pts[i].first * pw, by = y0 - pts[i].second * ph;
            if (fill) for (float x = ax; x < bx; x += 2) { const float y = ay + (by - ay) * (x - ax) / std::max(1e-3f, bx - ax); rrect({x, y, 2, y0 - y}, 0, withAlpha(ACCENT, 40 + (int)(lv * 90))); }   // strips under the line
            line(ax, ay, bx, by, lv > 0.05f ? ACCENT : TEXT, 1.6f);
        }
    };
    auto ghost = [&](Rect c, const std::vector<std::pair<float, float>>& pts) {   // a faint line, same box as poly
        const float x0 = c.x + pad, y0 = c.y + c.h - 22, pw = c.w - 2 * pad, ph = c.h - 40;
        for (size_t i = 1; i < pts.size(); i++) line(x0 + pts[i - 1].first * pw, y0 - pts[i - 1].second * ph, x0 + pts[i].first * pw, y0 - pts[i].second * ph, withAlpha(TEXT, 60), 1.f);
    };
    // ENV: attack / decay / sustain / release, each a quarter of the width
    { Rect c = cell(0); frameCell(c, 0); text(c.x + pad, c.y + 12, 9, DIM, "ENVELOPE", 0, true);
      const float a = 0.05f + ts.a * 0.2f, d = 0.05f + ts.d * 0.2f, r = 0.05f + ts.r * 0.2f, sus = 0.3f;
      std::vector<std::pair<float, float>> p{{0, 0}, {a, 1}, {a + d, ts.s}, {a + d + sus, ts.s}, {std::min(1.f, a + d + sus + r), 0}};
      for (auto& q : p) q.first /= p.back().first;
      poly(c, p, true); glow(c); }
    // OSC: one cycle of the wave (PATCH = a blend of saw and sine, since the patch brings its own)
    if (pg == SamplerApp::P_LFO) {   // LFO page: the wave scrolls at its real rate (1 s window), its height is the depth
      Rect c = cell(1); frameCell(c, 1); static const char* sh[] = {"SINE", "TRIANGLE", "SQUARE", "RANDOM"};
      const float hz = 0.1f * std::pow(200.f, ts.lfoRate), ph0 = std::fmod(now_ * 0.001f * hz, 1.f);
      char hzs[24]; std::snprintf(hzs, sizeof hzs, "%.1f HZ", hz);
      text(c.x + pad, c.y + 12, 9, DIM, std::string("LFO  ") + sh[std::min(3, std::max(0, ts.lfoShape))] + "  " + hzs, 0, true);
      std::vector<std::pair<float, float>> p, gh; const float pi = 3.14159265f;
      for (int i = 0; i <= 96; i++) { const float x = (float)i / 96, ph = std::fmod(x * hz + ph0, 1.f); float v;
          switch (ts.lfoShape) { case 1: v = ph < 0.5f ? 4 * ph - 1 : 3 - 4 * ph; break; case 2: v = ph < 0.5f ? 1.f : -1.f; break;
              case 3: v = std::sin((std::floor(x * hz + ph0)) * 12.9898f) ; break; default: v = std::sin(2 * pi * ph); }
          p.push_back({x, clamp01(0.5f + 0.5f * v * ts.lfoDepth)}); gh.push_back({x, clamp01(0.5f + 0.5f * v)}); }
      ghost(c, gh); poly(c, p, false); glow(c);   // the faint line is the shape at full depth
    } else
    { Rect c = cell(1); frameCell(c, 1);
      static const char* nm[] = {"PATCH", "SINE", "SAW", "SQUARE", "TRIANGLE"}; text(c.x + pad, c.y + 12, 9, DIM, std::string("OSC  ") + nm[std::min(4, std::max(0, ts.edW1))], 0, true);
      std::vector<std::pair<float, float>> p; const int N = 48; const float pi = 3.14159265f;
      const int uni = ts.edUni == 2 ? 3 : ts.edUni == 3 ? 5 : 1; const float det = std::fabs(ts.edDet2 - 0.5f) * 0.25f + 0.02f;   // unison: the detuned copies drift apart as faint lines
      auto wave = [&](float x) { x -= std::floor(x); float y;
          switch (ts.edW1) { case 2: y = x; break; case 3: y = x < 0.5f ? 1.f : 0.f; break; case 4: y = x < 0.5f ? 2 * x : 2 - 2 * x; break;
              case 0: y = 0.5f + 0.5f * (0.5f * std::sin(2 * pi * x) - 0.5f * (x - 0.5f) * 2); break; default: y = 0.5f + 0.5f * std::sin(2 * pi * x); }
          return clamp01(y); };
      for (int i = 0; i <= N; i++) p.push_back({(float)i / N, wave((float)i / N)});
      for (int u = 1; u < uni; u++) { std::vector<std::pair<float, float>> g; const float off = det * ((u + 1) / 2) * (u % 2 ? 1 : -1); for (int i = 0; i <= N; i++) g.push_back({(float)i / N, wave((float)i / N + off)}); ghost(c, g); }
      poly(c, p, false); glow(c); }
    // FILTER: response around the cutoff (the edit is +-3 octaves from the patch; the patch's own cutoff is the middle)
    { Rect c = cell(2); frameCell(c, 2);
      static const char* nm[] = {"LOW-PASS", "HIGH-PASS", "BAND-PASS"}; text(c.x + pad, c.y + 12, 9, DIM, std::string("FILTER  ") + nm[std::min(2, std::max(0, ts.edType))], 0, true);
      const float fc = 0.5f + (ts.edCut - 0.5f) * 6.f / 8.f, q = 0.15f + ts.edRes * 0.6f;
      std::vector<std::pair<float, float>> p;
      for (int i = 0; i <= 48; i++) { const float x = (float)i / 48, u = (x - fc) * 8.f;   // octaves from the cutoff
          float y = ts.edType == 1 ? (u > 0 ? 1.f : std::pow(2.f, u * 2)) : ts.edType == 2 ? std::pow(2.f, -std::fabs(u) * 1.5f) : (u < 0 ? 1.f : std::pow(2.f, -u * 2));
          y = std::min(1.f, y * 0.75f + q * std::exp(-u * u * 8.f));
          p.push_back({x, clamp01(y)}); }
      poly(c, p, true); glow(c); }
}
