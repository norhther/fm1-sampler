#include "SamplerUI.h"
#include <algorithm>
#include "../dsp/Tombola.h"
#include <cmath>
#include <cstdio>

using namespace theme;

namespace {
float clamp01(float v) { return std::min(1.f, std::max(0.f, v)); }
std::string num(const char* f, double v) { char b[48]; snprintf(b, sizeof b, f, v); return b; }
const char* kPageTab[] = {"SAMPLE", "SENDS", "ENV", "LFO", "ARP", "TAPE FX", "MIXER", "KEY", "LOOP", "LENGTH", "INSERT", "LIVE", "PATTERN", "TEMPO", "TRK FX", "TIMING", "TONE", "MOD", "LIVE 2", "TOMBOLA", "OP-1", "SHAPE", "PADS", "EDIT", "TRK EQ", "DUCK", "OSC", "FILTER", "TAPE", "DRIVE", "WARP", "OP-1 FX", "STOP", "TRIGGER", "FOLD", "TRK FOLD"};   // by Page
static_assert(sizeof(kPageTab) / sizeof(*kPageTab) == SamplerApp::kPages, "one tab label for every page");
}  // namespace

SamplerUI::SamplerUI(SamplerApp& app) : app_(app) {}

// ---- primitives ----
void SamplerUI::rrect(Rect r, float rad, uint32_t c) { UiOp o; o.type = UiOp::RRECT; o.f[0] = r.x; o.f[1] = r.y; o.f[2] = r.w; o.f[3] = r.h; o.f[4] = rad; o.color = c; ops_.push_back(o); }
void SamplerUI::stroke(Rect r, float rad, uint32_t c, float w) { UiOp o; o.type = UiOp::STROKE; o.f[0] = r.x; o.f[1] = r.y; o.f[2] = r.w; o.f[3] = r.h; o.f[4] = rad; o.f[5] = w; o.color = c; ops_.push_back(o); }
void SamplerUI::text(float x, float cy, float size, uint32_t c, const std::string& s, int align, bool bold) {
    size *= textScale_;
    UiOp o; o.type = UiOp::TEXT; o.f[0] = x; o.f[1] = cy + size * 0.36f; o.f[2] = size; o.f[3] = (float)align; o.f[4] = bold; o.color = c; o.text = s; ops_.push_back(o);
}
void SamplerUI::line(float x1, float y1, float x2, float y2, uint32_t c, float w) { UiOp o; o.type = UiOp::LINE; o.f[0] = x1; o.f[1] = y1; o.f[2] = x2; o.f[3] = y2; o.f[4] = w; o.color = c; ops_.push_back(o); }
void SamplerUI::circle(float cx, float cy, float r, uint32_t c) { UiOp o; o.type = UiOp::CIRCLE; o.f[0] = cx; o.f[1] = cy; o.f[2] = r; o.color = c; ops_.push_back(o); }
void SamplerUI::tri(float x1, float y1, float x2, float y2, float x3, float y3, uint32_t c) { UiOp o; o.type = UiOp::TRI; o.f[0] = x1; o.f[1] = y1; o.f[2] = x2; o.f[3] = y2; o.f[4] = x3; o.f[5] = y3; o.color = c; ops_.push_back(o); }
void SamplerUI::clip(Rect r) { UiOp o; o.type = UiOp::CLIP; o.f[0] = r.x; o.f[1] = r.y; o.f[2] = r.w; o.f[3] = r.h; ops_.push_back(o); }
void SamplerUI::endClip() { UiOp o; o.type = UiOp::ENDCLIP; ops_.push_back(o); }
void SamplerUI::hit(Rect r, std::function<void(float, float)> d, std::function<void(float, float)> m, std::function<void()> u) { hits_.push_back({r, std::move(d), std::move(m), std::move(u)}); }
void SamplerUI::tap(Rect r, std::function<void()> f) { hit(r, [f](float, float) { f(); }); }
std::string SamplerUI::fit(const std::string& s, float width, float size) const {
    const size_t maxc = (size_t)std::max(1.f, width / (size * textScale_ * 0.60f));
    return s.size() <= maxc ? s : s.substr(0, maxc > 1 ? maxc - 1 : 1) + "\xE2\x80\xA6";
}
void SamplerUI::touchP(int id, int action, float x, float y, uint32_t now) {
    now_ = now;
    Slot& sl = slot_[id & 15];
    if (action == 0) {
        if (sl.on && sl.h.up) { cur_ = &sl; sl.h.up(); }   // a finger that never lifted (a lost event): let go of what it held
        sl = Slot{}; sl.downAt = now; sl.dx = x; sl.dy = y;
        for (size_t i = hits_.size(); i-- > 0;) if (hits_[i].r.has(x, y)) { sl.h = hits_[i]; sl.on = true; haptic_ = true; cur_ = &sl; if (sl.h.down) sl.h.down(x, y); break; }
    } else if (action == 1) {
        if (std::fabs(x - sl.dx) + std::fabs(y - sl.dy) > 16) sl.lpMoved = true;
        if (sl.on && sl.h.move) { cur_ = &sl; sl.h.move(x, y); }
    } else if (sl.on) {
        cur_ = &sl; sl.on = false;
        if (sl.h.up) sl.h.up();
    }
}
void SamplerUI::cancelTouches() { for (int i = 0; i < 16; i++) if (slot_[i].on) touchP(i, 2, 0, 0, now_); }
// two fingers on the piano roll: the distance between them in x zooms the time, in y the pitch range; what is under the fingers stays under them
bool SamplerUI::pinch(int phase, float x1, float y1, float x2, float y2) {
    if (phase == 2) { pinchOn_ = false; return false; }
    const float cx = 0.5f * (x1 + x2), cy = 0.5f * (y1 + y2), dx = std::fabs(x1 - x2), dy = std::fabs(y1 - y2), minD = 60.f;   // (tiny distances would make the zoom jump)
    if (phase == 0) {
        pinchOn_ = false;
        if (!rollOn_ || cx < rollX0_ || cx > rollX0_ + rollW_ || cy < rollY0_ || cy > rollY0_ + rollH_) return false;
        rollTouchAt_ = now_; for (auto& sl : slot_) sl.on = false; rollMoved_ = true; drag_ = {};   // the first finger was a touch of the roll: it must not become a tap or a stretched note
        if (auditionNote_ >= 0) { app_.session().noteOff(app_.track(), auditionNote_); auditionNote_ = -1; }
        pinchOn_ = true; pinchDx0_ = std::max(dx, minD); pinchDy0_ = std::max(dy, minD);
        pinchZoom0_ = rollZoom_; pinchRows0_ = rollRowsF_; pinchBase0_ = rollBase_;
        const float vis0 = rollPt_ / rollZoom_;
        pinchTick0_ = rollScroll_ + (cx - rollX0_) / rollW_ * vis0;
        pinchYFrac0_ = (cy - rollY0_) / rollH_;
        pinchNote0_ = rollBase_ + rollRowsF_ * (1.f - pinchYFrac0_);
        return true;
    }
    if (!pinchOn_) return false;
    const float zoomMax = std::min(32.f, std::max(1.f, rollPt_ / (float)rollMinVisible_));
    rollZoom_ = std::min(zoomMax, std::max(1.f, pinchZoom0_ * std::max(dx, minD) / pinchDx0_));
    rollRowsF_ = std::min(60.f, std::max(5.f, pinchRows0_ * pinchDy0_ / std::max(dy, minD)));    // fingers apart = fewer, bigger rows
    const float vis = rollPt_ / rollZoom_, yFrac = (cy - rollY0_) / rollH_;
    rollScroll_ = pinchTick0_ - (cx - rollX0_) / rollW_ * vis;                                      // the tick that was between the fingers still is
    rollBase_ = pinchNote0_ - rollRowsF_ * (1.f - yFrac);
    return true;
}

void SamplerUI::button(Rect r, const std::string& label, bool on, std::function<void()> f, float size, bool accent) {
    rrect(r, R_CONTROL, on ? ACCENT : SURFACE);
    if (!on) stroke(r, R_CONTROL, accent ? ACCENT : OUTLINE, 1);
    else stroke({r.x + 2, r.y + 2, r.w - 4, r.h - 4}, R_CONTROL, ON_ACCENT, 1);   // on is told by a second frame too, not by the colour alone
    text(r.x + r.w / 2, r.y + r.h / 2, size, on ? ON_ACCENT : accent ? ACCENT : TEXT, fit(label, r.w - 6, size), 1, true);
    tap(r, std::move(f));
}
void SamplerUI::dangerButton(Rect r, const std::string& label, std::function<void()> f, float size) {
    rrect(r, R_CONTROL, SURFACE); stroke(r, R_CONTROL, DANGER, 2);
    const float tx = r.x + 12, cy = r.y + r.h / 2;
    tri(tx - 6, cy + 6, tx + 6, cy + 6, tx, cy - 6, DANGER);   // the warning mark
    text(tx, cy + 2, 8, BG, "!", 1, true);
    text(r.x + r.w / 2 + 10, cy, size, DANGER, fit(label, r.w - 34, size), 1, true);
    tap(r, std::move(f));
}
void SamplerUI::longPress(Rect r, std::function<void()> onTap, std::function<void()> onLong) {
    hits_.push_back({r, [](float, float) {}, nullptr, [this, onTap] { if (cur_ && !cur_->lpFired && !cur_->lpMoved) onTap(); }, std::move(onLong)});
}

// ---- frame ----
void SamplerUI::frame(uint32_t now) {
    now_ = now;
    st_ = app_.status();
    textScale_ = app_.access() ? 1.2f : 1.f;
    ops_.clear(); hits_.clear(); rollOn_ = false;
    { const std::string t = app_.toast(); if (t != lastToast_) { lastToast_ = t; if (!t.empty()) announce_ = t; } }   // toasts are read out by the screen reader
    if (!app_.tourDone() && !tourSeen_) { tourSeen_ = true; tourOpen_ = true; tourStep_ = 0; }
    for (auto& sl : slot_) if (sl.on && sl.h.lp && !sl.lpFired && !sl.lpMoved && now - sl.downAt > 500) { sl.lpFired = true; haptic_ = true; cur_ = &sl; sl.h.lp(); }
    if (padMask_ && !anyTouch()) { for (int p = 0; p < 16; p++) if (padMask_ >> p & 1) app_.padHit(p, 0, false); padMask_ = 0; }   // no finger on the screen, so no pad can be down
    if (app_.takeClearAsk() && app_.loopExists()) askTapeClear(-1);   // the FM-1 SAVE hold asks the same question
    rrect({0, 0, W_, H_}, 0, BG);
    const float tb = 44, left = 176, right = 252, y0 = tb + MARGIN, h = H_ - tb - 2 * MARGIN;
    if (seqFull_ && app_.page() == SamplerApp::P_SQ) { seqFullScreen(); }
    else if (chopScreen_) { chopFullScreen(); }
    else {
    topBar({0, 0, W_, tb});
    const bool lh = app_.leftHanded();   // left-handed: the pages on the left, the tracks on the right
    rTracks_ = lh ? Rect{W_ - left, y0, left - MARGIN, h} : Rect{MARGIN, y0, left - MARGIN, h};
    rArea_ = lh ? Rect{right + MARGIN, y0, W_ - left - right - 2 * MARGIN, h} : Rect{left + MARGIN, y0, W_ - left - right - 2 * MARGIN, h};
    rPanel_ = lh ? Rect{MARGIN, y0, right - MARGIN, h} : Rect{W_ - right, y0, right - MARGIN, h};
    trackList(rTracks_); sampleArea(rArea_); pagePanel(rPanel_);
    }
    if (sheetOpen_) librarySheet();
    if (fxOpen_) fxSheet();
    if (projOpen_) projectSheet();
    if (songOpen_) songSheet();
    if (menuOpen_) menuSheet();
    if (trackMenu_ >= 0) trackMenuSheet();
    if (newLoopOpen_) newLoopSheet();
    undoToast();
    if (confirmOpen_) confirmSheet();
    if (tourOpen_) tourSheet();
}

void SamplerUI::topBar(Rect r) {
    rrect(r, 0, SURFACE);
    line(0, r.h, W_, r.h, OUTLINE, 1);
    const float cy = r.h / 2, bh = r.h - 12;
    float x = MARGIN;
    // PLAY / STOP
    Rect pl{x, 6, 44, bh};
    rrect(pl, R_CONTROL, RAISED);
    if (st_.playing) { rrect({pl.x + 15, cy - 8, 5, 16}, 0, ACCENT); rrect({pl.x + 24, cy - 8, 5, 16}, 0, ACCENT); }
    else tri(pl.x + 16, cy - 9, pl.x + 16, cy + 9, pl.x + 30, cy, TEXT);
    tap(pl, [this] { app_.transportPlay(); });
    x += 52;
    // REC: arms the first take on a clean looper, overdub afterwards
    Rect rc{x, 6, 74, bh};
    const bool recOn = st_.freeRecording || st_.track_clip_rec[app_.track()], armed = st_.freeArmed;
    rrect(rc, R_CONTROL, recOn ? ACCENT : RAISED);
    if (armed) stroke(rc, R_CONTROL, ACCENT, 2);
    circle(rc.x + 18, cy, 6, recOn ? ON_ACCENT : ACCENT);
    text(rc.x + 28, cy, 9.5f, recOn ? ON_ACCENT : TEXT, app_.loopExists() ? "OVERDUB" : "REC", 0, true);
    tap(rc, [this] { app_.loopRecord(); });
    rRec_ = rc; x += 82;
    // UNDO / REDO over every track
    rUndo_ = {x, 6, 52, bh};
    button(rUndo_, "UNDO", st_.globalUndoN > 0, [this] { app_.globalUndo(); }, 9.5f); x += 56;
    button({x, 6, 52, bh}, "REDO", st_.globalRedoN > 0, [this] { app_.globalRedo(); }, 9.5f); x += 64;
    // the tempo: - and +, or drag sideways on the number
    const bool locked = app_.tempoLocked();
    button({x, 6, 30, bh}, "-", false, [this] { app_.tempoStep(-1); }, 14); x += 33;
    rTempo_ = {x, 6, 74, bh};
    rrect(rTempo_, R_CONTROL, SURFACE);
    text(rTempo_.x + rTempo_.w / 2, cy - 4, 15, TEXT, num("%.0f", app_.tempoNow()), 1, true);
    text(rTempo_.x + rTempo_.w / 2, cy + 11, 8, locked ? ACCENT : DIM, locked ? "STRETCHES TAPE" : "BPM  DRAG", 1, false);
    hit(rTempo_, [this](float px, float) { tempoDragX_ = px; }, [this](float px, float) { const int d = (int)((px - tempoDragX_) / 5.f); if (d) { app_.tempoStep(d); tempoDragX_ += d * 5.f; } });
    x += 77;
    button({x, 6, 30, bh}, "+", false, [this] { app_.tempoStep(1); }, 14); x += 34;
    button({x, 6, 52, bh}, "METRO", app_.metronome(), [this] { app_.toggleMetro(); }, 9.5f); x += 62;   // the click, from every page
    // right side, from the edge: MENU, the link dot, the master meter, TAPE STOP
    const float menuX = W_ - MARGIN - 62;
    button({menuX, 6, 62, bh}, "MENU", menuOpen_, [this] { menuOpen_ = true; }, 10.5f);
    const uint32_t lc = linkMidi_ && linkAudio_ ? TEXT : linkMidi_ ? ACCENT : DIM;
    circle(menuX - 10, cy, 4, lc);
    const Rect mt{menuX - 34, 6, 14, bh};   // master level: red when it clips
    rrect(mt, 0, BG); stroke(mt, 0, OUTLINE, 1);
    { const float lv = std::min(1.f, st_.master_peak); rrect({mt.x + 1, mt.y + mt.h - 1 - (mt.h - 2) * lv, mt.w - 2, std::max(1.f, (mt.h - 2) * lv)}, 0, st_.clip ? DANGER : TEXT); }
    if (st_.clip) text(mt.x + mt.w / 2, mt.y - 1, 7, DANGER, "CLIP", 1, true);
    { Rect ts{mt.x - 8 - 100, 6, 100, bh}; const bool on = app_.tapeStopHeld();   // live: hold TAPE STOP and the tape runs down, let go and it spins up
      rrect(ts, R_CONTROL, on ? ACCENT : RAISED); if (!on) stroke(ts, R_CONTROL, OUTLINE, 1);
      text(ts.x + ts.w / 2, cy, 10.5f, on ? ON_ACCENT : TEXT, "TAPE STOP", 1, true);
      hit(ts, [this](float, float) { app_.tapeStopHold(true); }, nullptr, [this] { app_.tapeStopHold(false); });
      // in between: how long the first take is, then what the loop is doing
      const float right = ts.x - 8; float mx = x;
      if (!app_.loopExists() && !st_.freeRecording && !armed) {   // it ends by itself after that many bars (FREE: when you press REC again)
          button({mx, 6, 78, bh}, app_.takeBars() ? "TAKE " + std::to_string(app_.takeBars()) + (app_.takeBars() == 1 ? " BAR" : " BARS") : std::string("TAKE FREE"), app_.takeBars() > 0, [this] { app_.cycleTakeBars(); }, 9.5f);
          mx += 86;
      }
      text(mx, cy - 6, 11, st_.freeRecording || recOn || armed ? ACCENT : TEXT, fit(app_.loopText(), right - mx, 11), 0, true);
      text(mx, cy + 11, 9, DIM, fit(std::string("LOOP SOURCE: ") + (app_.knobs(SamplerApp::P_SAVE)[1].value), right - mx, 9), 0, false);
      tap({mx - 4, 0, std::max(60.f, right - mx), r.h}, [this] { auto k = app_.knobs(SamplerApp::P_SAVE); k[1].step(k[1].norm > 0.5f ? -1 : 1); }); }
}

void SamplerUI::trackList(Rect r) {   // eight rows; the tracks from 9 on make a second column. ADD TRACK opens the next one
    const int n = app_.trackCount(); const bool canAdd = n < SamplerApp::kTracks, two = n > 8;
    const float addH = canAdd ? 30.f : 0.f, rh = (r.h - addH - (canAdd ? 4.f : 0.f) - 7 * 4) / 8, cw = two ? (r.w - 4) / 2 : r.w, fs = two ? 9.f : 11.f;
    for (int i = 0; i < n; i++) {
        Rect row{r.x + (i / 8) * (cw + 4), r.y + (i % 8) * (rh + 4), cw, rh};
        const bool sel = i == app_.track();
        rrect(row, R_CARD, sel ? RAISED : SURFACE);
        stroke(row, R_CARD, sel ? ACCENT : OUTLINE, sel ? 2 : 1);
        const float nx = two ? 8.f : 14.f, lx = two ? 16.f : 30.f;
        text(row.x + nx, row.y + rh / 2, two ? 11.f : T_TITLE, sel ? ACCENT : TEXT, std::to_string(i + 1), 1, true);
        const std::string lab = app_.sampleLabel(i);
        text(row.x + lx, row.y + rh / 2 - 6, fs, lab.empty() ? DIM : TEXT, fit(lab.empty() ? "EMPTY" : lab, cw - lx - 8, fs), 0, true);
        // level and loop mark
        const float lv = std::min(1.f, st_.track_peak[i] * 1.5f);
        rrect({row.x + lx, row.y + rh - 12, cw - lx - 14, 3}, 0, OUTLINE);
        rrect({row.x + lx, row.y + rh - 12, (cw - lx - 14) * lv, 3}, 0, theme::trackColor(i));
        if (st_.track_clip_rec[i]) circle(row.x + cw - 10, row.y + 10, 4, ACCENT);
        else if (st_.track_has_clip[i]) circle(row.x + cw - 10, row.y + 10, 3.5f, TEXT);
        longPress(row, [this, i] { app_.selectTrack(i); }, [this, i] { app_.selectTrack(i); trackMenu_ = i; });   // tap = this track, hold = its menu
    }
    if (canAdd) button({r.x, r.y + r.h - addH, r.w, addH}, "+ ADD TRACK", false, [this] { app_.addTrack(); }, 10.f);
}

// the waveform: the peaks (the playback window bright), the KIT pad marks and the voices that are sounding
void SamplerUI::waveDraw(Rect w) {
    const int t = app_.track(); const auto ts = app_.state(t); const int bins = std::max(20, (int)(w.w / 3));
    auto pk = app_.peaks(t, bins);
    const float cy = w.y + w.h / 2, bw = w.w / bins;
    for (int i = 0; i < (int)pk.size(); i++) {
        const float pos = (i + 0.5f) / bins, v = std::min(1.f, pk[(size_t)i] * 1.1f);
        const bool in = pos >= ts.start && pos <= ts.end;
        rrect({w.x + i * bw + 0.5f, cy - std::max(1.f, v * (w.h / 2 - 8)), std::max(1.f, bw - 1), std::max(2.f, v * (w.h - 16))}, 0, in ? TEXT : OUTLINE);
    }
    if (pk.empty()) text(w.x + w.w / 2, cy, T_BODY, DIM, "NO SAMPLE", 1, false);
    if (ts.algo == 3 && !app_.isSynth(t)) {   // KIT: every chop from its start to its end, the selected one shaded
        std::vector<float> cs, ce; chopsNow(cs, ce);
        if (chopIdx_ < 0 && chopHold_ > 0) chopHold_--;
        const int sel = std::min(chopSel_, (int)cs.size() - 1);
        auto X = [&](float v) { return w.x + 2 + v * (w.w - 4); };
        for (int i = 0; i < (int)cs.size(); i++) {
            const bool on = i == sel; const float sx = X(cs[(size_t)i]), ex = X(ce[(size_t)i]);
            if (on) rrect({sx, w.y + 6, std::max(1.f, ex - sx), w.h - 12}, 0, theme::withAlpha(ACCENT, 45));
            line(sx, w.y + 6, sx, w.y + w.h - 6, on ? ACCENT : theme::withAlpha(ACCENT, 170), on ? 2.f : 1.f);
            text(sx + 3, w.y + w.h - 14, 8.f, ACCENT, std::to_string(i + 1), 0, false);
            const uint32_t ec = on ? ACCENT : theme::withAlpha(DIM, 170);
            line(ex, w.y + 6, ex, w.y + w.h - 6, ec, on ? 2.f : 1.f); rrect({ex - 6, w.y + 6, 6, 9}, 1, ec);   // the end has its flag on the left of the line
        }
    }
    for (int j = 0; j < st_.voiceN[t]; j++) {   // where the sounding voices are reading
        const float vx = w.x + 2 + st_.voicePos[t][j] * (w.w - 4);
        line(vx, w.y + 22, vx, w.y + w.h - 10, ACCENT, 1.5f);
        circle(vx, w.y + w.h - 10, 3.5f, ACCENT);
    }
}

void SamplerUI::chopsNow(std::vector<float>& s, std::vector<float>& e) const {
    if (chopIdx_ >= 0 || chopHold_ > 0) { s = chopS_; e = chopE_; return; }
    s = app_.session().slicesOf(app_.track()); e = app_.session().sliceEndsOf(app_.track());
    if (e.size() != s.size()) { e.clear(); for (size_t i = 0; i < s.size(); i++) e.push_back(i + 1 < s.size() ? s[i + 1] : 1.f); }
}
void SamplerUI::chopDelete() {
    std::vector<float> s, e; chopsNow(s, e); if (s.size() <= 2) return;
    const int sel = std::min(chopSel_, (int)s.size() - 1);
    s.erase(s.begin() + sel); e.erase(e.begin() + sel);
    chopS_ = s; chopE_ = e; chopHold_ = 8; chopSel_ = std::min(sel, (int)s.size() - 1);
    app_.setSliceStarts(s, e);
}
// KIT: every chop has its own START and END handle; drag one and nothing else moves. The chop to edit is picked with the numbered buttons, never from the wave. Tap empty wave to cut a new chop
void SamplerUI::chopEdit(Rect w) {
    const float wx = w.x + 2, ww = w.w - 4;
    hit({w.x, w.y + 22, w.w, w.h - 22},
        [this, wx, ww](float x, float) {
            chopsNow(chopS_, chopE_); chopHold_ = 0; chopIdx_ = -1; chopMoved_ = false; chopNew_ = false; chopEndH_ = false;
            const int n = (int)chopS_.size(), sel = std::min(chopSel_, n - 1); float bd = 14.f;
            for (int k = 0; k < n; k++) {   // the selected chop first, so when handles sit on top of each other it wins
                const int i = k == 0 ? sel : (k - 1 < sel ? k - 1 : k);
                for (int h = 0; h < 2; h++) { const float d = std::fabs(wx + (h ? chopE_[(size_t)i] : chopS_[(size_t)i]) * ww - x); if (d < bd) { bd = d; chopIdx_ = i; chopEndH_ = h == 1; } }
            }
            if (chopIdx_ >= 0) return;
            const float v = clamp01((x - wx) / ww); float nx = 1.f; for (float st : chopS_) if (st > v + 0.002f) nx = std::min(nx, st);
            std::vector<std::pair<float, float>> c; for (int i = 0; i < n; i++) c.push_back({chopS_[(size_t)i], chopE_[(size_t)i]});
            c.push_back({v, std::max(nx, v + 0.002f)}); std::stable_sort(c.begin(), c.end(), [](auto& a, auto& b) { return a.first < b.first; });
            chopS_.clear(); chopE_.clear(); for (int i = 0; i < (int)c.size(); i++) { chopS_.push_back(c[(size_t)i].first); chopE_.push_back(c[(size_t)i].second); if (c[(size_t)i].first == v) chopIdx_ = i; }
            chopNew_ = true; },
        [this, wx, ww](float x, float) {
            if (chopIdx_ < 0 || chopIdx_ >= (int)chopS_.size()) return;
            const float v = clamp01((x - wx) / ww); chopMoved_ = true;
            if (chopEndH_) chopE_[(size_t)chopIdx_] = std::max(v, chopS_[(size_t)chopIdx_] + 0.002f); else chopS_[(size_t)chopIdx_] = std::min(v, chopE_[(size_t)chopIdx_] - 0.002f); },
        [this]() {
            if (chopIdx_ < 0) return;
            if (chopMoved_ || chopNew_) { chopHold_ = 8; app_.setSliceStarts(chopS_, chopE_); }
            chopIdx_ = -1; });
}

void SamplerUI::sampleArea(Rect r) {
    const int t = app_.track();
    const auto ts = app_.state(t);
    const std::string lab = app_.sampleLabel(t);
    text(r.x, r.y + 12, T_TITLE, lab.empty() ? DIM : TEXT, fit(lab.empty() ? "EMPTY TRACK: PICK A SAMPLE" : lab, r.w - (app_.page() == SamplerApp::P_TAPEV ? 150 : 120), T_TITLE), 0, true);
    if (app_.page() != SamplerApp::P_TAPEV) text(r.x + r.w, r.y + 12, T_CAPTION, DIM, "TRACK " + std::to_string(t + 1), 2, true);   // (the tape page has its own lanes and a NEW LOOP button in that corner)
    if (app_.page() == SamplerApp::P_SQ) button({r.x + r.w - 214, r.y, 130, 26}, "FULL SCREEN", false, [this] { seqFull_ = true; }, 10.5f);
    if (app_.page() == SamplerApp::P_PADS) button({r.x + r.w - 214, r.y, 214, 26}, "QUANTIZE: " + app_.quantText(), app_.quantDiv() > 0, [this] { app_.cycleQuant(); }, 10.5f);   // the pads snap to the beat too
    if (app_.page() == SamplerApp::P_TEMPO) { button({r.x + r.w - 210, r.y, 110, 26}, "TAP TEMPO", false, [this] { app_.tapTempo(); }, 10.5f, true); }
    if (app_.page() == SamplerApp::P_MIX) { mixerView({r.x, r.y + 28, r.w, r.h - 28}); return; }
    if (app_.page() == SamplerApp::P_TAPEV) { button({r.x + r.w - 130, r.y, 130, 26}, "NEW LOOP", false, [this] { newLoopOpen_ = true; }, 10.5f, true); tapeView({r.x, r.y + 28, r.w, r.h - 28}); return; }
    if (app_.page() == SamplerApp::P_PADS) { padsView({r.x, r.y + 28, r.w, r.h - 28}); return; }
    if (app_.page() == SamplerApp::P_TOMB) { tombolaView({r.x, r.y + 28, r.w, r.h - 28}); return; }
    if (app_.page() == SamplerApp::P_SQ) { sequencerView({r.x, r.y + 28, r.w, r.h - 28}); return; }   // the sequencer page shows the pattern instead of the waveform, with all the room there is
    if (!app_.isSynth(t) && !lab.empty() && app_.micState() == 0) button({r.x + r.w - 214, r.y, 130, 26}, "CHOP EDITOR", false, [this] { chopScreen_ = true; if (app_.state(app_.track()).algo != 3) app_.setAlgo(3); }, 10.5f);   // the whole screen for cutting the sound into pads
    // algorithm (a synth has none)
    const float cw = (r.w - 5 * 4) / 6;
    if (app_.isSynth(t)) { text(r.x, r.y + 44, T_CAPTION, DIM, "BUILT-IN SYNTH: ENV, SHAPE, OSC, FILTER, LFO", 0, false); button({r.x + r.w - 116, r.y + 30, 116, 28}, "SAVE PATCH", false, [this] { app_.savePatch(); }, 10.5f); }
    else for (int a = 0; a < 6; a++) {
        Rect c{r.x + a * (cw + 4), r.y + 30, cw, 28};
        button(c, a == 5 ? "PAUL" : SamplerApp::algoName(a), ts.algo == a, [this, a] { app_.setAlgo(a); }, 9.5f);
    }
    // waveform with the playback window
    Rect w{r.x, r.y + 66, r.w, 118};
    rrect(w, R_CARD, SURFACE); stroke(w, R_CARD, OUTLINE, 1);
    const int bins = std::max(20, (int)(w.w / 3));
    if (app_.micState() == 1 || app_.micState() == 2) {   // microphone: input level + trigger threshold while waiting, the growing take while recording
        const float cy0 = w.y + w.h / 2, half = w.h / 2 - 8;
        if (app_.micState() == 1) {
            const float thr = std::min(1.f, app_.micThreshold() * 2.f), lv = std::min(1.f, st_.inputPeak * 2.f);
            rrect({w.x + 8, cy0 - lv * half, w.w - 16, std::max(2.f, lv * half * 2)}, 0, lv > thr ? ACCENT : OUTLINE);
            line(w.x + 8, cy0 - thr * half, w.x + w.w - 8, cy0 - thr * half, ACCENT, 1);
            line(w.x + 8, cy0 + thr * half, w.x + w.w - 8, cy0 + thr * half, ACCENT, 1);
            text(w.x + w.w / 2, w.y + w.h - 12, T_CAPTION, DIM, "WAITING FOR SOUND ABOVE THE LINES  DRAG TO SET  " + num("%.0f DB", 20 * std::log10(app_.micThreshold())), 1, false);
            auto setThr = [this, half, cy0](float, float y) { app_.setMicThreshold01(std::log10(std::max(0.0025f, std::fabs(y - cy0) / half / 2.f) / 0.005f) / 2.f); };
            hit(w, setThr, setThr);
        } else {
            float live[2048]; const int n = app_.session().livePeaks(live, 2048);
            const int cols = std::max(20, (int)(w.w / 3)), per = std::max(1, (n + cols - 1) / cols);   // longer takes are squeezed to fit
            const float bw0 = (w.w - 16) / std::max(cols, (n + per - 1) / per);
            for (int i = 0; i * per < n; i++) {
                float v = 0; for (int j = i * per; j < std::min(n, (i + 1) * per); j++) v = std::max(v, live[j]);
                v = std::min(1.f, v * 1.1f);
                rrect({w.x + 8 + i * bw0, cy0 - std::max(1.f, v * half), std::max(1.f, bw0 - 1), std::max(2.f, v * half * 2)}, 0, TEXT);
            }
            text(w.x + w.w - 24, w.y + 16, T_CAPTION, ACCENT, "REC " + std::to_string(st_.sampleSeconds).substr(0, 4) + "S", 2, true);
        }
        goto after_wave;
    }
    {
    waveDraw(w);
    }
    after_wave:
    if (ts.algo == 3 && !app_.isSynth(t) && app_.micState() == 0 && app_.page() != SamplerApp::P_SQ) chopEdit(w);   // KIT: tap the waveform to cut a new pad there, tap a mark to remove it, drag a mark to move it
    // start / end handles (not while the microphone take is shown)
    for (int h = 0; h < (app_.micState() == 1 || app_.micState() == 2 ? 0 : 2); h++) {   // (SQ page returned earlier)
        const float v = h ? ts.end : ts.start, hx = w.x + 2 + v * (w.w - 4);
        line(hx, w.y + 4, hx, w.y + w.h - 4, ACCENT, 2);
        rrect({hx - (h ? 10 : 0), w.y + 4, 10, 14}, 1, ACCENT);
        hit({hx - 18, w.y, 36, w.h},
            [](float, float) {},
            [this, h, w](float x, float) { auto k = app_.knobs(SamplerApp::P_HOME); k[(size_t)h].set(clamp01((x - w.x - 2) / (w.w - 4))); });
    }
    // loop strip under the waveform
    Rect ls{r.x, w.y + w.h + 6, r.w, 34};
    rrect(ls, 0, SURFACE); stroke(ls, 0, OUTLINE, 1);
    {   // the whole loop: every track mixed (dim) and the selected track's own loop on top
        float mixp[400], trp[400]; const int ln = app_.session().loopPeaks(-1, mixp, 400); app_.session().loopPeaks(t, trp, 400);
        if (ln > 0) {
            float mx = 0.08f; for (int i = 0; i < ln; i++) mx = std::max(mx, mixp[i]);
            const float lcy = ls.y + ls.h / 2, lh = ls.h / 2 - 3, lbw = (ls.w - 4) / ln;
            for (int i = 0; i < ln; i++) {
                const float v = std::min(1.f, mixp[i] / mx), u = std::min(1.f, trp[i] / mx);
                if (v > 0.01f) rrect({ls.x + 2 + i * lbw, lcy - v * lh, std::max(1.f, lbw), std::max(1.f, 2 * v * lh)}, 0, OUTLINE);
                if (u > 0.01f) rrect({ls.x + 2 + i * lbw, lcy - u * lh, std::max(1.f, lbw), std::max(1.f, 2 * u * lh)}, 0, TEXT);
            }
        } else text(ls.x + ls.w / 2, ls.y + ls.h / 2, T_CAPTION, DIM, "NO LOOP: PRESS REC", 1, false);
    }
    const float lp = st_.track_has_clip[t] ? st_.clipPos[t] : (app_.loopExists() && st_.loopSamples > 0 ? (float)(st_.pos / st_.loopSamples) : 0.f);
    if (app_.loopExists() || st_.freeRecording) rrect({ls.x + lp * (ls.w - 4), ls.y - 2, 3, ls.h + 4}, 0, st_.freeRecording || st_.track_clip_rec[t] ? ACCENT : TEXT);
    // library row
    Rect lr{r.x, ls.y + ls.h + 12, r.w, 34};
    const auto& lib = app_.library();
    const int cur = app_.libraryCursor();
    button({lr.x, lr.y, 34, lr.h}, "<", false, [this] { app_.browse(-1); }, 15);
    button({lr.x + lr.w - 34 - 0, lr.y, 34, lr.h}, ">", false, [this] { app_.browse(1); }, 15);
    Rect nm{lr.x + 40, lr.y, lr.w - 80, lr.h};
    rrect(nm, R_CARD, SURFACE); stroke(nm, R_CARD, OUTLINE, 1);
    if (!lib.empty()) {
        text(nm.x + 10, nm.y + nm.h / 2 - 6, 12, TEXT, fit(lib[(size_t)cur].label, nm.w - 80, 12), 0, true);
        text(nm.x + 10, nm.y + nm.h / 2 + 9, 9, DIM, lib[(size_t)cur].group + "  " + std::to_string(cur + 1) + "/" + std::to_string(lib.size()), 0, false);
    }
    text(nm.x + nm.w - 10, nm.y + nm.h / 2, 9.5f, ACCENT, "LIBRARY", 2, true);
    tap(nm, [this] { openLibrary(); });
    // microphone and disk
    Rect br{r.x, lr.y + lr.h + 8, r.w, 38};
    const int ms = app_.micState();
    static const char* micLab[] = {"RECORD FROM MIC", "WAITING FOR SOUND...", "STOP AND KEEP", "SAVING..."};
    button({br.x, br.y, br.w * 0.4f - 3, br.h}, ms == 0 ? "RECORD FROM MIC" : micLab[ms], ms == 2, [this] { app_.micSample(); }, 11, ms != 0);
    button({br.x + br.w * 0.4f + 3, br.y, br.w * 0.3f - 6, br.h}, "LOAD FILE", false, [this] { app_.requestFile(); }, 11);
    button({br.x + br.w * 0.7f, br.y, br.w * 0.3f, br.h}, "RESAMPLE", false, [this] { app_.resampleNow(); }, 11);
    const std::string ts_ = app_.toast();
    if (!ts_.empty()) text(r.x, br.y + br.h + 14, 11, ACCENT, ts_, 0, true);
}

// ---- tape view: one lane per track with what is recorded there, the bars of the loop and the playhead (like the OP-1 tape) ----
void SamplerUI::tapeView(Rect r) {   // like the OP-1 tape: two reels and the head, a counter, one lane per track with its waveform, a ruler of bars
    const bool has = app_.loopExists(), rec = st_.freeRecording;
    const int bars = std::max(1, st_.loopBars), sr = std::max(1, app_.session().sampleRate());
    const double lenS = has ? st_.loopSamples / sr : 0.0, nowS = rec ? st_.freeSeconds : has ? st_.pos / sr : 0.0;
    const float lp = has && st_.loopSamples > 0 ? std::min(1.f, std::max(0.f, (float)(st_.pos / st_.loopSamples))) : 0.f;
    // the reels: the supply shrinks and the take-up grows as the tape runs by; the spokes turn with the time
    const float cy = r.y + 21, rl = 17.f - 7.f * lp, rr = 10.f + 7.f * lp, lx = r.x + 24, rx = r.x + 82, ph = (float)nowS * 5.f;
    auto reel = [&](float cx, float rad) {
        circle(cx, cy, rad, OUTLINE); circle(cx, cy, rad - 2.5f, SURFACE);
        for (int k = 0; k < 3; k++) { const float a = ph + k * 2.0944f; line(cx, cy, cx + std::cos(a) * (rad - 3), cy + std::sin(a) * (rad - 3), rec ? ACCENT : TEXT, 2); }
        circle(cx, cy, 3.5f, rec ? ACCENT : TEXT);
    };
    reel(lx, rl); reel(rx, rr);
    line(lx - rl * 0.3f, cy + rl, r.x + 53, cy + 20, DIM, 1); line(r.x + 53, cy + 20, rx + rr * 0.3f, cy + rr, DIM, 1);   // the tape to the head
    tri(r.x + 48, cy + 19, r.x + 58, cy + 19, r.x + 53, cy + 25, TEXT);
    const int m = (int)(nowS / 60); const double sec = nowS - m * 60;
    text(r.x + 108, cy - 4, 15, rec ? ACCENT : TEXT, num("%d:", m) + (sec < 10 ? "0" : "") + num("%.1f", sec), 0, true);
    const std::string capS = has ? std::to_string(bars) + (bars == 1 ? " BAR  " : " BARS  ") + num("%.0f BPM  ", st_.bpm) + num("%.1f S", lenS) : rec ? "RECORDING THE FIRST TAKE" : "THE TAPE IS EMPTY: PRESS REC";
    text(r.x + 108, cy + 14, T_CAPTION, DIM, capS, 0, true);
    const float textEnd = r.x + 108 + 7 * 15.f * textScale_ * 0.55f + 4;   // the counter ("10:02.7") is on the same row as the buttons: they give way to it, the take button first
    { float bx = r.x + r.w - 96;   // right to left: CLEAR TAPE, SAVE LOOP, and the take button (UNDO TAKE, or before the first take how many bars it lasts)
      dangerButton({bx, r.y + 2, 96, 26}, "CLEAR TAPE", [this, has] { if (has) askTapeClear(-1); else app_.say("THE TAPE IS ALREADY EMPTY"); }, 9.5f); bx -= 70;
      if (bx >= textEnd) button({bx, r.y + 2, 66, 26}, "SAVE LOOP", false, [this] { app_.loopSave(); }, 9.f); bx -= 74;
      if (bx >= textEnd) {
          if (has) button({bx, r.y + 2, 70, 26}, "UNDO TAKE", st_.globalUndoN > 0, [this] { app_.globalUndo(); }, 9.f);
          else if (!rec && !st_.freeArmed) button({bx, r.y + 2, 70, 26}, app_.takeBars() ? "TAKE " + std::to_string(app_.takeBars()) + (app_.takeBars() == 1 ? " BAR" : " BARS") : std::string("TAKE FREE"), app_.takeBars() > 0, [this] { app_.cycleTakeBars(); }, 9.f); } }
    const int nT = app_.trackCount(), vis = std::min(nT, 8);
    const float labW = 34, ruler = 12, top = r.y + 48 + ruler, xW = 38, sbW = nT > vis ? 14.f : 0.f, laneH = (r.y + r.h - top) / vis, wx = r.x + labW + 4, ww = r.w - labW - 4 - xW - sbW;   // xW: a gutter on the right for each lane's X, so it never covers the waveform
    if (has) for (int b = 0; b < bars; b += std::max(1, bars / 16)) text(wx + ww * b / bars + 3, top - ruler / 2 - 1, 8, DIM, std::to_string(b + 1), 0, false);   // the ruler: where each bar starts
    if (tapeSeen_ != app_.track()) { tapeSeen_ = app_.track(); tapeTop_ = std::min(tapeTop_, app_.track()); tapeTop_ = std::max(tapeTop_, app_.track() - vis + 1); }   // a track picked elsewhere scrolls into view
    tapeTop_ = std::max(0, std::min(tapeTop_, nT - vis));
    if (sbW > 0) {   // a scroll bar: drag it (or tap it) to move through the tracks
        const Rect sb{r.x + r.w - sbW + 2, top, sbW - 2, laneH * vis - 2};
        rrect(sb, 3, SURFACE); const float th = std::max(24.f, sb.h * vis / nT), ty = sb.y + (sb.h - th) * tapeTop_ / (nT - vis);
        rrect({sb.x, ty, sb.w, th}, 3, ACCENT);
        auto go = [this, sb, th, nT, vis](float, float y) { tapeTop_ = std::max(0, std::min(nT - vis, (int)std::lround((y - sb.y - th / 2) / std::max(1.f, sb.h - th) * (nT - vis)))); };
        hit({sb.x - 8, sb.y, sb.w + 10, sb.h}, go, go);
    }
    for (int t = tapeTop_; t < tapeTop_ + vis; t++) {
        const Rect lane{r.x, top + (t - tapeTop_) * laneH, r.w - sbW, laneH - 2};
        const bool sel = t == app_.track(), clip = st_.track_has_clip[t];
        rrect(lane, 3, sel ? RAISED : SURFACE); if (sel) stroke(lane, 3, ACCENT, 1);
        text(r.x + labW / 2, lane.y + lane.h / 2, 11, sel ? ACCENT : DIM, std::to_string(t + 1), 1, true);
        float pk[400]; const int n = clip ? app_.session().loopPeaks(t, pk, 400) : 0;
        if (n > 0) {
            float mx = 0.05f; for (int i = 0; i < n; i++) mx = std::max(mx, pk[i]);
            const float lcy = lane.y + lane.h / 2, half = lane.h / 2 - 2, bw = ww / n;
            const uint32_t col = st_.track_clip_rec[t] ? ACCENT : theme::trackColor(t);   // a colour per track, like the four OP-1 tracks
            for (int i = 0; i < n; i++) { const float v = std::min(1.f, pk[i] / mx); if (v > 0.02f) rrect({wx + i * bw, lcy - v * half, std::max(1.f, bw), std::max(1.f, 2 * v * half)}, 0, col); }
        } else text(wx + 4, lane.y + lane.h / 2, 9, DIM, "EMPTY", 0, false);
        hit(lane, [this, t](float, float) { app_.selectTrack(t); });   // tap a lane: that track
        if (clip) button({lane.x + lane.w - xW + 2, lane.y + 1, xW - 4, std::min(lane.h - 2, 18.f)}, "X", false, [this, t] { askTapeClear(t); }, 9);
    }
    if (has) {   // bar lines and the playhead over all lanes
        for (int b = 0; b < bars; b++) line(wx + ww * b / bars, top, wx + ww * b / bars, top + laneH * vis - 2, OUTLINE, 1);
        rrect({wx + lp * ww - 1, top - 2, 3, laneH * vis}, 0, rec ? ACCENT : TEXT);
    }
}

// ---- sequencer: piano roll with touch editing, zoom and undo / redo ----
// Touch: tap an empty cell = a note of one step; drag sideways from it = a longer note; tap a note = remove it; drag sideways on a note = change its length.
// The strip on the left is a keyboard (tap = hear the note, drag = scroll the pitch), the strip on top a ruler (drag = scroll the time when zoomed in).
void SamplerUI::sequencerView(Rect r) {
    const int t = app_.track();
    const auto ts = app_.state(t);
    const int bars = app_.seqBarsOf(t), pt = patternTicks(bars), st = app_.stepTicks();
    const int state = ts.seqState;
    Session::SeqView v[kSeqMax]; const int n = app_.session().seqNotes(t, v, kSeqMax);
    if (rollFitTrack_ != t) {   // first look at this track: show where its notes are
        rollFitTrack_ = t;
        int lo = 127, hi = 0; for (int i = 0; i < n; i++) { lo = std::min(lo, v[i].note); hi = std::max(hi, v[i].note); }
        if (n) { rollRowsF_ = std::min(14.f, std::max(10.f, (float)(hi - lo + 4))); // never smaller than a finger: wider patterns scroll
        rollBase_ = (lo + hi) / 2.f - rollRowsF_ / 2.f + 0.5f; }
        else { rollRowsF_ = 10.f; rollBase_ = 55.f; }
    }
    // toolbar: undo, redo, zoom in time and pitch, fit, grid, select
    const float tbh = 26, bw9 = (r.w - 7 * 4) / 9.3f;   // the last two buttons (GRID, SELECT) are a bit wider (zooming the rows is the vertical pinch)
    int tx = 0; float tbx = r.x;
    auto tb = [&](const std::string& label, bool on, std::function<void()> f, float wmul = 1.f) { button({tbx, r.y, bw9 * wmul, tbh}, label, on, std::move(f), 9.5f); tbx += bw9 * wmul + 4; tx++; };
    tb("UNDO", st_.seqUndoN[t] > 0, [this] { app_.seqUndo(); });
    tb("REDO", st_.seqRedoN[t] > 0, [this] { app_.seqRedo(); });
    const float zoomMax = std::min(32.f, std::max(1.f, pt / (2.f * st)));
    tb("TIME -", false, [this] { rollZoom_ = std::max(1.f, rollZoom_ / 1.6f); });
    tb("TIME +", false, [this, zoomMax] { rollZoom_ = std::min(zoomMax, rollZoom_ * 1.6f); });
    tb("FIT", false, [this] { rollFitTrack_ = -1; });
    tb("QUANTIZE " + app_.quantText(), app_.quantDiv() > 0, [this] { app_.cycleQuant(); }, 1.9f);   // one tap: off, 1/4, 1/8, 1/16, 1/32
    tb(drumView_ == 0 ? "DRUMS" : drumView_ == 1 ? "PADS 1-8" : "PADS 9-16", drumView_ != 0, [this] { drumView_ = (drumView_ + 1) % 3;  });
    tb("SELECT", selMode_, [this] { selMode_ = !selMode_; sel_.clear(); drag_ = {}; rbOn_ = false; }, 1.4f);
    // the roll
    const float keyW = drumView_ ? 76.f : 34.f, rulerH = 14;
    const float panelH = selMode_ ? 86.f : 0.f, slotH = 26.f;
    const float tapeH = app_.loopExists() ? 26.f : 0.f;   // the row that says where on the tape TAPE puts the pattern
    Rect roll{r.x, r.y + tbh + 4, r.w, r.h - tbh - 4 - 46 - panelH - slotH - 4 - tapeH};
    rrect(roll, R_CARD, SURFACE); stroke(roll, R_CARD, OUTLINE, 1);
    if (drumView_) { rollRowsF_ = 8.f; rollBase_ = 60.f + 8.f * (drumView_ - 1); }   // one row per pad, eight at a time
    rollZoom_ = std::min(zoomMax, std::max(1.f, rollZoom_)); rollRowsF_ = std::min(60.f, std::max(5.f, rollRowsF_));
    const int rows = (int)std::lround(rollRowsF_), visible = std::max(2 * st, (int)(pt / rollZoom_)); const float zoom = (float)pt / visible;
    const float x0 = roll.x + 2 + keyW, y0 = roll.y + 2 + rulerH, W = roll.w - 4 - keyW, H = roll.h - 4 - rulerH, rh = H / rows;
    const int seqPos = st_.seqPos[t];   // where the pattern is, not where the beat clock is: it was launched on a beat of its own, and it can be stopped while the tape runs
    const double playTick = seqPos >= 0 ? (double)seqPos : 0.0;
    if (zoom > 1 && seqPos >= 0 && !anyTouch() && now_ - rollTouchAt_ > 3000 && (playTick < rollScroll_ || playTick >= rollScroll_ + visible)) rollScroll_ = (float)(std::floor(playTick / visible) * visible);   // the view pages along with the playhead
    rollScroll_ = std::min((float)(pt - visible), std::max(0.f, rollScroll_));
    rollBase_ = std::min(127.f - rows, std::max(0.f, rollBase_));
    const int base = (int)std::floor(rollBase_); const float sc0 = rollScroll_;   // what is drawn is what a touch lands on, even if the view moves before the finger arrives
    auto px = [&](double tick) { return x0 + (float)((tick - rollScroll_) / visible * W); };
    clip(roll);
    for (int i = 0; i < rows; i++) {   // rows from the top: the highest note first; black keys are a little darker
        const int nn = base + rows - 1 - i, pc = nn % 12; const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
        if (black && !drumView_) rrect({x0, y0 + i * rh, W, rh}, 0, theme::withAlpha(BG, 110));
        line(x0, y0 + (i + 1) * rh, x0 + W, y0 + (i + 1) * rh, pc == 0 ? OUTLINE : theme::withAlpha(OUTLINE, 70), 1);
        // the keyboard strip
        rrect({roll.x + 2, y0 + i * rh, keyW - 2, rh}, 0, black ? BG : RAISED);
        line(roll.x + 2, y0 + (i + 1) * rh, roll.x + keyW, y0 + (i + 1) * rh, OUTLINE, 1);
        if (drumView_) text(roll.x + 4, y0 + i * rh + rh / 2, 8.5f, TEXT, fit(std::to_string(nn - 59) + " " + app_.padName(nn - 60), keyW - 6, 8.5f), 0, false);
        else if (pc == 0 || rh >= 13) text(roll.x + 5, y0 + i * rh + rh / 2, 8.5f, pc == 0 ? ACCENT : DIM, std::string(SamplerApp::noteName(pc)) + std::to_string(nn / 12 - 1), 0, pc == 0);
    }
    // the ruler: bar numbers, a tick for each beat
    rrect({x0, roll.y + 2, W, rulerH}, 0, RAISED);
    const int lineStep = visible <= kPpq * 2 ? std::max(1, st) : kPpq;   // zoomed in far: a line on every step, else on every beat
    for (int tick = (int)(rollScroll_ / lineStep) * lineStep; tick <= rollScroll_ + visible; tick += lineStep) {
        const bool bar = tick % (4 * kPpq) == 0, beat = tick % kPpq == 0;
        line(px(tick), y0, px(tick), y0 + H, bar ? DIM : beat ? OUTLINE : theme::withAlpha(OUTLINE, 120), bar ? 1.5f : 1);
        if (beat && px(tick) >= x0 && px(tick) < x0 + W - 12) text(px(tick) + 3, roll.y + 2 + rulerH / 2, 8.f, bar ? ACCENT : DIM, std::to_string(tick / (4 * kPpq) + 1) + (bar ? "" : "." + std::to_string(tick % (4 * kPpq) / kPpq + 1)), 0, bar);
    }
    if (state == 3) rrect({px(st_.seqCursor[t]), y0, std::max(2.f, (float)st / visible * W), H}, 0, theme::withAlpha(ACCENT, 90));
    if (selMode_) { std::vector<Session::SeqRef> keep; for (auto& q : sel_) for (int i = 0; i < n; i++) if (v[i].tick == q.tick && v[i].note == q.note) { keep.push_back(q); break; } sel_ = keep; }
    for (int i = 0; i < n; i++) {
        const int row = base + rows - 1 - v[i].note;
        if (row < 0 || row >= rows || v[i].tick + v[i].len < rollScroll_ || v[i].tick > rollScroll_ + visible) continue;
        const bool grabbed = drag_.on && drag_.existing && drag_.tick == v[i].tick && drag_.note == v[i].note;
        const int len = grabbed ? drag_.len : v[i].len;
        const Rect nr{px(v[i].tick), y0 + row * rh + 1.f, std::max(4.f, (float)len / visible * W), std::max(3.f, rh - 2)};
        const bool picked = selMode_ && isSel(v[i].tick, v[i].note);
        rrect(nr, 2, grabbed || picked ? ACCENT : theme::withAlpha(TEXT, 110 + (int)(145.f * v[i].vel / 127.f)));   // louder notes are brighter
        if (v[i].prob < 100) stroke(nr, 2, DIM, 1);
        if (v[i].ratchet > 1 && nr.w > 12) for (int k = 1; k < v[i].ratchet; k++) { const float rx = nr.x + nr.w * k / v[i].ratchet; line(rx, nr.y + 2, rx, nr.y + nr.h - 2, BG, 1); }
        if (nr.w > 14) line(nr.x + nr.w - 4, nr.y + 3, nr.x + nr.w - 4, nr.y + nr.h - 3, grabbed ? ON_ACCENT : BG, 1.5f);   // a grip at the end: drag it to stretch the note
    }
    if (drag_.on && !drag_.existing) {   // the new note under the finger
        const int row = base + rows - 1 - drag_.note;
        if (row >= 0 && row < rows) rrect({px(drag_.tick), y0 + row * rh + 1.f, std::max(4.f, (float)drag_.len / visible * W), std::max(3.f, rh - 2)}, 2, ACCENT);
    }
    if (seqPos >= 0) { const float pxx = px(playTick); if (pxx >= x0 && pxx <= x0 + W) line(pxx, y0 - rulerH + 0.f, pxx, y0 + H, ACCENT, 1.5f); }
    if (n == 0 && !drag_.on) text(x0 + W / 2, y0 + H / 2, T_BODY, DIM, state == 2 ? "PLAY KEYS: THEY ARE RECORDED" : "TAP A CELL, DRAG SIDEWAYS FOR A LONG NOTE", 1, false);
    text(roll.x + roll.w - 6, roll.y + 2 + rulerH + 9, 8.5f, DIM, zoom > 1.05f ? num("%.1f", zoom) + "X  DRAG THE RULER TO SCROLL" : std::string("PINCH TO ZOOM"), 2, false);
    rollOn_ = true; rollX0_ = x0; rollY0_ = y0; rollW_ = W; rollH_ = H; rollPt_ = pt; rollMinVisible_ = 2 * st;
    endClip();
    // ruler: drag sideways to scroll the time
    hit({x0, roll.y + 2, W, rulerH}, [this, sc0](float x, float) { rollTouchAt_ = now_; rulerDownX_ = x; rulerStartScroll_ = sc0; },
        [this, W, visible](float x, float) { rollTouchAt_ = now_; rollScroll_ = rulerStartScroll_ - (x - rulerDownX_) / W * visible; });
    // keyboard: a tap plays the note, a drag scrolls the pitch
    hit({roll.x + 2, y0, keyW - 2, H},
        [this, t, y0, rh, rows, base](float, float y) {
            rollTouchAt_ = now_; keyDownY_ = y; keyStartBase_ = rollBase_; keyMoved_ = false;
            auditionNote_ = base + rows - 1 - std::min(rows - 1, std::max(0, (int)((y - y0) / rh)));
            app_.session().noteOn(t, auditionNote_, 100);
        },
        [this, t, rh](float, float y) {
            if (!keyMoved_ && std::fabs(y - keyDownY_) > 8) { keyMoved_ = true; if (auditionNote_ >= 0) app_.session().noteOff(t, auditionNote_); auditionNote_ = -1; }
            if (keyMoved_) rollBase_ = keyStartBase_ + (y - keyDownY_) / rh;
        },
        [this, t] { if (auditionNote_ >= 0) app_.session().noteOff(t, auditionNote_); auditionNote_ = -1; });
    // grid
    auto tickAt = [sc0, x0, W, visible](float x) { return sc0 + (x - x0) / W * visible; };
    if (selMode_) {   // tap a note to pick or drop it, drag a box over empty cells to pick all inside; a tap on nothing drops the selection
        hit({x0, y0, W, H},
            [this, v, n, tickAt, y0, rh, rows, base, st](float x, float y) mutable {
                rollTouchAt_ = now_; rbX0_ = rbX1_ = x; rbY0_ = rbY1_ = y; rbOn_ = false; rollMoved_ = false; rollDownX_ = x; rollDownY_ = y;
                const int tick = (int)tickAt(x), note = base + rows - 1 - std::min(rows - 1, std::max(0, (int)((y - y0) / rh)));
                selDrag_ = -1;
                for (int i = 0; i < n; i++) if (v[i].note == note && tick >= v[i].tick && tick < v[i].tick + std::max(1, v[i].len)) { selDrag_ = i; selDragVal_ = (float)v[i].tick; break; }
                if (selDrag_ >= 0) { const Session::SeqRef q{v[selDrag_].tick, v[selDrag_].note}; bool had = false; for (size_t k = 0; k < sel_.size(); k++) if (sel_[k].tick == q.tick && sel_[k].note == q.note) { sel_.erase(sel_.begin() + (long)k); had = true; break; } if (!had) sel_.push_back(q); }
            },
            [this](float x, float y) { if (selDrag_ >= 0) return; if (std::fabs(x - rollDownX_) > 8 || std::fabs(y - rollDownY_) > 8) rbOn_ = true; rbX1_ = x; rbY1_ = y; },
            [this, v, n, sc0, x0, W, visible, y0, rh, rows, base]() mutable {
                auto px = [&](double tick) { return x0 + (float)((tick - sc0) / visible * W); };
                if (selDrag_ >= 0) return;
                if (!rbOn_) { sel_.clear(); return; }
                const float xa = std::min(rbX0_, rbX1_), xb = std::max(rbX0_, rbX1_), ya = std::min(rbY0_, rbY1_), yb = std::max(rbY0_, rbY1_);
                for (int i = 0; i < n; i++) {
                    const int row = base + rows - 1 - v[i].note; if (row < 0 || row >= rows) continue;
                    const float nx0 = px(v[i].tick), nx1 = px(v[i].tick + v[i].len), ny0 = y0 + row * rh, ny1 = ny0 + rh;
                    if (nx1 >= xa && nx0 <= xb && ny1 >= ya && ny0 <= yb && !isSel(v[i].tick, v[i].note)) sel_.push_back({v[i].tick, v[i].note});
                }
                rbOn_ = false;
            });
        if (rbOn_) { const Rect rb{std::min(rbX0_, rbX1_), std::min(rbY0_, rbY1_), std::fabs(rbX1_ - rbX0_), std::fabs(rbY1_ - rbY0_)}; rrect(rb, 0, theme::withAlpha(ACCENT, 50)); stroke(rb, 0, ACCENT, 1); }
        selectPanel({r.x, roll.y + roll.h + 4, r.w, 82}, v, n, pt, st);
    } else
    hit({x0, y0, W, H},
        [this, v, n, tickAt, y0, rh, rows, base, st, pt](float x, float y) mutable {
            const int tick = std::min(pt - st, std::max(0, (int)tickAt(x) / st * st)), note = base + rows - 1 - std::min(rows - 1, std::max(0, (int)((y - y0) / rh)));
            rollTouchAt_ = now_; rollDownX_ = x; rollDownY_ = y; rollMoved_ = false;
            drag_ = {};
            drag_.on = true; drag_.tick = tick; drag_.note = note; drag_.len = st; drag_.origLen = st;
            for (int i = 0; i < n; i++) if (v[i].note == note && tick >= v[i].tick && tick < v[i].tick + std::max(1, v[i].len)) {   // a finger on a note: it is that note that changes
                drag_.existing = true; drag_.tick = v[i].tick; drag_.len = drag_.origLen = v[i].len; break;
            }
        },
        [this, tickAt, st, pt](float x, float y) {
            if (!drag_.on) return;
            if (std::fabs(x - rollDownX_) > 8) rollMoved_ = true;
            if (!rollMoved_) return;
            const int end = ((int)tickAt(x) / st + 1) * st;   // the step under the finger is included
            drag_.len = std::min(pt - drag_.tick, std::max(st, end - drag_.tick)); drag_.moved = true;
        },
        [this, st] {
            if (!drag_.on) return;
            const NoteDrag d = drag_; drag_ = {};
            if (rollMoved_ && d.moved) { if (d.existing) app_.seqResizeAt(d.tick, d.note, d.len); else app_.seqToggleAt(d.tick, d.note, d.len); }
            else app_.seqToggleAt(d.tick, d.note, st);   // a tap: add a one-step note, or remove the one that was touched
        });
    // buttons: record into the pattern (same as holding SEQ on the FM-1), mute, clear
    // pattern banks A..H: tap one to play it next (at the end of the pattern while it plays); COPY then a bank copies this pattern there; SONG chains banks
    {
        const float sy = roll.y + roll.h + 4 + (selMode_ ? 0.f : 0.f) + panelH;
        const int cur = app_.patternSlot(t), pend = st_.seqSlotPending[t];
        const float cw = 28, sw = 44, gap = 3; float cx = r.x;
        text(cx + 2, sy + slotH / 2, 8.f, DIM, "PTN", 0, true); cx += 26;
        for (int q = 0; q < 8; q++) {
            Session::SeqView tmp[1]; int bb = 0; const int cnt = app_.session().seqSlotNotes(t, q, tmp, 1, &bb);
            const Rect c{cx + q * (cw + gap), sy, cw, slotH};
            const bool on = q == cur;
            rrect(c, R_CONTROL, on ? ACCENT : SURFACE); stroke(c, R_CONTROL, pend == q ? ACCENT : copyArm_ ? DIM : OUTLINE, pend == q || copyArm_ ? 2 : 1);
            text(c.x + c.w / 2, c.y + c.h / 2, 11, on ? ON_ACCENT : TEXT, std::string(1, (char)('A' + q)), 1, true);
            if (cnt > 0) circle(c.x + c.w - 6, c.y + 6, 2.5f, on ? ON_ACCENT : ACCENT);
            tap(c, [this, q] { if (copyArm_) { app_.copyPattern(q); copyArm_ = false; app_.say(std::string("COPIED TO ") + (char)('A' + q)); } else app_.selectPattern(q); });
        }
        const float ex = cx + 8 * (cw + gap) + 4;
        button({ex, sy, sw, slotH}, copyArm_ ? "TO..." : "COPY", copyArm_, [this] { copyArm_ = !copyArm_; }, 9.5f);
        button({ex + sw + gap, sy, sw, slotH}, st_.songOn ? "SONG*" : "SONG", st_.songOn, [this] { songOpen_ = true; }, 9.5f);
    }
    // bars: add or remove one at the end (new bars are empty; the notes behind a removed end are dropped, UNDO brings them back)
    const float by = r.y + r.h - 38, bwBar = 34, lblW = 64, cpW = 44, barsW = 2 * bwBar + cpW + lblW + 3 * 4, bw = (r.w - barsW - 5 * 6) / 5;
    const int nb = app_.seqBarsOf(t);
    button({r.x, by, bwBar, 38}, "-", false, [this, nb] { app_.seqSetBarCount(nb - 1); }, 16);
    text(r.x + bwBar + 4 + lblW / 2, by + 13, 13, TEXT, std::to_string(nb) + (nb == 1 ? " BAR" : " BARS"), 1, true);
    text(r.x + bwBar + 4 + lblW / 2, by + 28, 8.5f, DIM, "PATTERN LENGTH", 1, false);
    button({r.x + bwBar + 4 + lblW + 4, by, bwBar, 38}, "+", false, [this, nb] { app_.seqSetBarCount(nb + 1); }, 16);
    button({r.x + 2 * bwBar + 4 + lblW + 8, by, cpW, 38}, "+COPY", false, [this] { app_.seqAddBarCopy(); }, 9.f);   // a new bar with the notes of the last one
    int bx = 0;
    auto b = [&](const std::string& label, bool on, std::function<void()> f, bool accent = false) { button({r.x + barsW + 6 + bx * (bw + 6), by, bw, 38}, label, on, std::move(f), 10.5f, accent); bx++; };
    b(st_.keyTrig[t] ? "KEY TRIG" : "KEYS", st_.keyTrig[t], [this, t] { app_.setKeyTrigger(t, !st_.keyTrig[t]); }, st_.keyTrig[t]);   // OP-1 style: a key plays the pattern, transposed by its distance from C4. STOP / PLAY / REC are the buttons of the right panel
    b(st_.bounce == 1 ? "WAITING..." : st_.bounce == 2 ? "REC TAPE" : "TAPE", st_.bounce > 0, [this] { app_.seqBounce(); }, st_.bounce > 0);   // lit while it waits for its bar or records; tap again to cancel
    if (tapeH > 0) {   // TAPE lands the pattern on bar 1, 2, 3 ... of the tape (the lit bars are what it would fill), or on ALL of it
        const int bb = app_.bounceBar(), tb = std::max(1, st_.loopBars), pb = std::min(tb, std::max(1, nb));
        const float sy = r.y + r.h - 70, sh = 24, lw = 74, aw = 44, cell = (r.w - lw - aw - 8) / tb;
        text(r.x + 2, sy + sh / 2, 8.5f, DIM, "TAPE LANDS ON", 0, true);
        button({r.x + lw + 76 - 76 + 28, sy, aw, sh}, "ALL", bb == 0, [this] { app_.setBounceBar(0); }, 9.5f);
        for (int k = 1; k <= tb; k++) {
            const Rect c{r.x + lw + 28 + aw + 4 + (k - 1) * cell, sy, cell - 3, sh};
            const bool fits = k + pb - 1 <= tb, lit = bb != 0 && k >= bb && k < bb + pb;
            rrect(c, R_CONTROL, lit ? ACCENT : SURFACE);
            if (!lit) stroke(c, R_CONTROL, bb == 0 ? ACCENT : OUTLINE, 1);
            text(c.x + c.w / 2, c.y + sh / 2, 9.5f, lit ? ON_ACCENT : fits ? TEXT : DIM, std::to_string(k), 1, true);
            if (fits) tap(c, [this, k] { app_.setBounceBar(k); });
        }
    }
    b(st_.seqOne[t] ? "ONCE" : "LOOP", st_.seqOne[t], [this, t] { app_.setOneShot(t, !st_.seqOne[t]); });   // the pattern repeats (LOOP) or plays one pass and stops (ONCE)
    b("DICE", false, [this] { app_.seqDice(); });
    b("CLEAR", false, [this] { app_.seqClear(); showUndo("PATTERN CLEARED"); });   // it can be undone: no question, an UNDO that stays a few seconds
}

// ---- the pattern editor on the whole screen: the roll gets all the room, the controls of the pattern and of the transport on one row ----
void SamplerUI::seqFullScreen() {
    const int t = app_.track(); const float m = MARGIN, h = 34, gap = 4;
    if (app_.page() != SamplerApp::P_SQ) { seqFull_ = false; return; }
    std::vector<std::pair<std::string, std::function<void()>>> items; std::vector<bool> on;
    auto add = [&](const std::string& l, bool o, std::function<void()> f) { items.push_back({l, std::move(f)}); on.push_back(o); };
    add("EXIT", false, [this] { seqFull_ = false; });
    add("<", false, [this, t] { app_.selectTrack(std::max(0, t - 1)); });
    add("TRACK " + std::to_string(t + 1), false, [] {});
    add(">", false, [this, t] { app_.selectTrack(std::min(app_.trackCount() - 1, t + 1)); });
    add(st_.playing ? "TAPE: PAUSE" : "TAPE: PLAY", st_.playing, [this] { app_.transportPlay(); });
    add("METRO", app_.metronome(), [this] { app_.toggleMetro(); });
    add(st_.keyTrig[t] ? "KEYS: PATTERN" : "KEYS: SOUND", st_.keyTrig[t], [this, t] { app_.setKeyTrigger(t, !st_.keyTrig[t]); });
    static const char* q[] = {"AT ONCE", "BEAT", "BAR"};
    add(std::string("LAUNCH: ") + q[std::min(2, std::max(0, app_.state(t).trigQ))], false, [this, t] { app_.setTrigQuant(t, (app_.state(t).trigQ + 1) % 3); });
    add("< BEAT", false, [this] { app_.seqNudge(-app_.stepTicks()); });
    add("BEAT >", false, [this] { app_.seqNudge(app_.stepTicks()); });
    const float w = (W_ - 2 * m - gap * (items.size() - 1)) / items.size();
    for (size_t i = 0; i < items.size(); i++) button({m + i * (w + gap), m, w, h}, items[i].first, on[i], items[i].second, 9.f, i == 0);
    const bool run = app_.seqRunning(t);
    text(W_ - m, m + h + 12, T_CAPTION, run ? ACCENT : DIM, std::string("PATTERN ") + (run ? (st_.seqRun[t] ? "PLAYING" : "WAITING FOR THE ") + std::string(st_.seqRun[t] ? "" : "BEAT") : "STOPPED") + (st_.seqTrans[t] ? num("  %+.0f ST", st_.seqTrans[t]) : ""), 2, true);
    text(m, m + h + 12, T_CAPTION, DIM, "< BEAT / BEAT > SLIDE THE WHOLE PATTERN: LINE UP THE FIRST BEAT WITH THE METRONOME", 0, false);
    sequencerView({m, m + h + 22, W_ - 2 * m, H_ - 2 * m - h - 22});
}

// ---- the chop editor on the whole screen: the waveform with its marks (drag to move, tap to add or remove), AUTO / EVEN chops, and one button per chop to hear it ----
void SamplerUI::chopFullScreen() {
    const int t = app_.track(); const float m = MARGIN, h = 34, gap = 4;
    if (app_.isSynth(t) || app_.sampleLabel(t).empty()) { chopScreen_ = false; return; }
    const auto ts = app_.state(t);
    static const char* sens[] = {"LOW", "MID", "HIGH"};
    std::vector<std::pair<std::string, std::function<void()>>> items; std::vector<bool> on;
    auto add = [&](const std::string& l, bool o, std::function<void()> f) { items.push_back({l, std::move(f)}); on.push_back(o); };
    add("EXIT", false, [this] { chopScreen_ = false; });
    add("AUTO CHOP", false, [this] { chopEven_ = false; app_.autoChop(); });
    add("EVEN CHOP", false, [this] { chopEven_ = true; app_.evenChop(); });
    add("DELETE CHOP", false, [this] { chopDelete(); });
    add("CHOPS: " + std::to_string(SamplerApp::chopCount(ts.sliceCount)), false, [this] { app_.cycleChopCount(); if (chopEven_) app_.evenChop(); else app_.autoChop(); });
    add(std::string("SENS: ") + sens[ts.sliceSens < 0.3f ? 0 : ts.sliceSens < 0.6f ? 1 : 2], false, [this] { app_.cycleChopSens(); chopEven_ = false; app_.autoChop(); });
    add(ts.chopFull ? "KEY: WHOLE CHOP" : "KEY: HOLD", ts.chopFull != 0, [this, ts] { app_.setChopFull(!ts.chopFull); });
    const float w0 = (W_ - 2 * m - gap * (items.size() - 1)) / items.size();
    for (size_t i = 0; i < items.size(); i++) button({m + i * (w0 + gap), m, w0, h}, items[i].first, on[i], items[i].second, 9.5f, i == 0);
    const auto sl = app_.session().slicesOf(t); const int n = std::min<int>(16, (int)sl.size());
    text(m, m + h + 12, T_CAPTION, DIM, "DRAG A START OR END FLAG: ONLY THAT EDGE MOVES. TAP THE WAVE: NEW CHOP. THE NUMBERS BELOW PLAY AND SELECT EACH CHOP.", 0, false);
    text(W_ - m, m + h + 12, T_CAPTION, ACCENT, ts.chopFull ? "A KEY PLAYS THE WHOLE CHOP" : "THE CHOP SOUNDS WHILE THE KEY IS HELD", 2, true);
    const float padH = 44, wy = m + h + 22;
    const Rect w{m, wy, W_ - 2 * m, H_ - m - padH - 8 - wy};
    rrect(w, R_CARD, SURFACE); stroke(w, R_CARD, OUTLINE, 1);
    waveDraw(w);
    chopEdit(w);
    if (n > 0) {
        const float pw = (W_ - 2 * m - gap * (n - 1)) / n;
        for (int i = 0; i < n; i++) {   // down plays the chop, up lets it go: what KEY: HOLD / WHOLE CHOP means
            const Rect c{m + i * (pw + gap), H_ - m - padH, pw, padH};
            const bool lit = (padMask_ >> i) & 1;
            rrect(c, R_CONTROL, lit ? ACCENT : SURFACE); stroke(c, R_CONTROL, i == chopSel_ ? ACCENT : OUTLINE, i == chopSel_ ? 2 : 1);
            text(c.x + c.w / 2, c.y + c.h / 2, 12, lit ? ON_ACCENT : TEXT, std::to_string(i + 1), 1, true);
            hit(c, [this, i](float, float) { chopSel_ = i; padMask_ |= 1u << i; app_.padHit(i, 100, true); }, [](float, float) {}, [this, i]() { padMask_ &= ~(1u << i); app_.padHit(i, 0, false); });
        }
    }
}

// ---- song: banks of patterns one after the other ----
void SamplerUI::songSheet() {
    rrect({0, 0, W_, H_}, 0, theme::withAlpha(BG, 215));
    tap({0, 0, W_, H_}, [this] { songOpen_ = false; });
    const Rect box{W_ * 0.14f, 40, W_ * 0.72f, H_ - 80};
    rrect(box, R_CARD, SURFACE); stroke(box, R_CARD, ACCENT, 1);
    tap(box, [] {});
    text(box.x + 14, box.y + 20, T_TITLE, TEXT, "SONG", 0, true);
    text(box.x + 80, box.y + 21, T_CAPTION, DIM, "EVERY STEP PLAYS ITS PATTERN BANK ON ALL TRACKS, AS LONG AS THE LONGEST PATTERN", 0, false);
    button({box.x + box.w - 70, box.y + 6, 60, 28}, "CLOSE", false, [this] { songOpen_ = false; }, 10);
    const auto& chain = app_.songChain();
    // the chain: one chip per step, the playing one lit; tap a chip to take it out
    const float cw = 40, gap = 6; const int perRow = std::max(1, (int)((box.w - 28 + gap) / (cw + gap)));
    if (chain.empty()) text(box.x + box.w / 2, box.y + 74, T_BODY, DIM, "EMPTY: ADD BANKS BELOW", 1, false);
    for (size_t i = 0; i < chain.size(); i++) {
        const Rect c{box.x + 14 + (i % (size_t)perRow) * (cw + gap), box.y + 52 + (i / (size_t)perRow) * (cw + gap - 2), cw, cw - 4};
        const bool now = st_.songOn && (int)i == st_.songPos;
        rrect(c, R_CONTROL, now ? ACCENT : RAISED); stroke(c, R_CONTROL, OUTLINE, 1);
        text(c.x + c.w / 2, c.y + c.h / 2 - 3, 13, now ? ON_ACCENT : TEXT, std::string(1, (char)('A' + chain[i])), 1, true);
        text(c.x + c.w / 2, c.y + c.h - 7, 7.5f, now ? ON_ACCENT : DIM, std::to_string(i + 1), 1, false);
    }
    // adding
    const float by = box.y + box.h - 112;
    text(box.x + 14, by - 6, T_CAPTION, DIM, "ADD A STEP", 0, true);
    for (int q = 0; q < 8; q++) button({box.x + 14 + q * (cw + gap), by, cw, 34}, std::string(1, (char)('A' + q)), false, [this, q] { app_.songAdd(q); }, 12);
    const float bw = (box.w - 28 - 2 * 8) / 3, y2 = box.y + box.h - 66;
    button({box.x + 14, y2, bw, 42}, st_.songOn ? "SONG: ON" : "SONG: OFF", st_.songOn, [this] { app_.songSet(!app_.songOn()); }, 12, true);
    button({box.x + 14 + bw + 8, y2, bw, 42}, "REMOVE LAST", false, [this] { app_.songPop(); }, 11);
    button({box.x + 14 + 2 * (bw + 8), y2, bw, 42}, "CLEAR SONG", false, [this] { app_.songClear(); }, 11);
}

// ---- pads: a 4 x 4 grid to play a kit with; the harder you hit (lower on the pad) the louder ----
void SamplerUI::padsView(Rect r) {
    const int t = app_.track(); const auto& ts = app_.state(t);
    const float gap = 8, pw = (r.w - 3 * gap) / 4, ph = (r.h - 3 * gap) / 4;
    for (int i = 0; i < 16; i++) {
        const int row = i / 4, col = i % 4, pad = (3 - row) * 4 + col;   // pad 1 is bottom left, like on an MPC
        const Rect b{r.x + col * (pw + gap), r.y + row * (ph + gap), pw, ph};
        const bool sel = ts.selPad == pad, down = padMask_ >> pad & 1;
        rrect(b, R_CARD, down ? ACCENT : sel ? RAISED : SURFACE); stroke(b, R_CARD, sel || down ? ACCENT : OUTLINE, sel ? 2 : 1);
        text(b.x + 8, b.y + 14, 10, down ? ON_ACCENT : DIM, std::to_string(pad + 1), 0, true);
        text(b.x + b.w / 2, b.y + b.h / 2, 13, down ? ON_ACCENT : TEXT, fit(app_.padName(pad), b.w - 10, 13), 1, true);
        if (ts.padGroup[pad] > 0) text(b.x + b.w - 8, b.y + 14, 9, down ? ON_ACCENT : ACCENT, "CH" + std::to_string(ts.padGroup[pad]), 2, true);
        hit(b, [this, pad, b](float, float y) { if (padMask_ >> pad & 1) app_.padHit(pad, 0, false); padMask_ |= 1u << pad; app_.selectPad(pad); app_.padHit(pad, 40 + (int)(87.f * clamp01((y - b.y) / b.h)), true); },
            nullptr, [this, pad] { padMask_ &= ~(1u << pad); app_.padHit(pad, 0, false); });   // every finger has its own pad
    }
}

// ---- tombola: balls in a turning polygon; drag on the drum to turn it ----
void SamplerUI::tombolaView(Rect r) {
    const float bh = 38;
    Rect box{r.x, r.y, r.w, r.h - bh - 6};
    rrect(box, R_CARD, SURFACE); stroke(box, R_CARD, OUTLINE, 1);
    const float R = std::min(box.w, box.h) * 0.46f, cx = box.x + box.w / 2, cy = box.y + box.h / 2;
    Session::TombolaBall balls[Tombola::kMaxBalls]; float ang = 0; const int nb = app_.session().tombolaView(balls, Tombola::kMaxBalls, &ang);
    const int N = app_.tombolaSides();
    clip(box);
    const float apo = std::cos(kPi / N), half = std::sin(kPi / N);
    for (int k = 0; k < N; k++) {   // the walls
        const float a = ang + k * 2.f * kPi / N, px = std::cos(a) * apo, py = std::sin(a) * apo, da = a + kPi / 2.f, dx = std::cos(da), dy = std::sin(da);
        line(cx + (px - dx * half) * R, cy + (py - dy * half) * R, cx + (px + dx * half) * R, cy + (py + dy * half) * R, TEXT, 4);
    }
    for (int i = 0; i < nb; i++) { const float s = std::max(5.f, R * 0.03f * 1.4f); circle(cx + balls[i].x * R, cy + balls[i].y * R, s, balls[i].hit > 0.05f ? ACCENT : TEXT); }
    if (!nb) text(cx, cy, T_BODY, DIM, "DROP A BALL, OR PLAY KEYS. DRAG TO TURN THE DRUM", 1, false);
    endClip();
    hit(box, [this, cx, cy](float x, float y) { drumLastAngle_ = std::atan2(y - cy, x - cx); },
        [this, cx, cy](float x, float y) { const float a = std::atan2(y - cy, x - cx); float d = a - drumLastAngle_; d = std::atan2(std::sin(d), std::cos(d)); app_.session().tombolaTurn(d); drumLastAngle_ = a; });
    const float bw = (r.w - 2 * 6) / 3, by = r.y + r.h - bh;
    int bx = 0;
    auto b = [&](const std::string& label, std::function<void()> f, bool accent = false) { button({r.x + bx * (bw + 6), by, bw, bh}, label, false, std::move(f), 11, accent); bx++; };
    b("DROP A BALL", [this] { app_.tombolaDrop(); }, true);
    b("REVERSE", [this] { app_.tombolaReverse(); });
    b("EMPTY THE DRUM", [this] { app_.tombolaClear(); });
}

// the panel under the roll in SELECT mode: what to do with the picked notes, and four values to set on all of them
void SamplerUI::selectPanel(Rect r, const Session::SeqView* v, int n, int pt, int st) {
    const int t = app_.track(), cnt = (int)sel_.size();
    rrect(r, R_CARD, SURFACE); stroke(r, R_CARD, OUTLINE, 1);
    const Session::SeqView* first = nullptr;
    for (int i = 0; i < n && !first; i++) if (isSel(v[i].tick, v[i].note)) first = &v[i];
    text(r.x + 8, r.y + 14, 10.5f, cnt ? ACCENT : DIM, cnt ? std::to_string(cnt) + " PICKED" : "TAP NOTES OR DRAG A BOX", 0, true);
    // actions: the first row
    const float bw = (r.w - 16 - 8 * 4 - 96) / 8.f; float bx = r.x + 104;
    auto act = [&](const std::string& label, std::function<void()> f) { if (cnt) button({bx, r.y + 4, bw, 22}, label, false, std::move(f), 9.f); else { rrect({bx, r.y + 4, bw, 22}, R_CONTROL, BG); text(bx + bw / 2, r.y + 15, 9.f, OUTLINE, label, 1, true); } bx += bw + 4; };
    auto op = [this, t](int o, int arg) { app_.session().seqApply(t, sel_.data(), (int)sel_.size(), o, arg); };
    act("DEL", [this, op] { op(Session::OP_DELETE, 0); sel_.clear(); });
    act("COPY", [this, op, pt, st] {   // a copy of the picked notes right after them (the length of the span, to the next step), and the copies are the new selection
        int lo = 1 << 30, hi = 0; const auto s0 = sel_; Session::SeqView vv[kSeqMax]; const int nn = app_.session().seqNotes(app_.track(), vv, kSeqMax);
        for (int i = 0; i < nn; i++) if (isSel(vv[i].tick, vv[i].note)) { lo = std::min(lo, vv[i].tick); hi = std::max(hi, vv[i].tick + vv[i].len); }
        const int span = std::max(st, (hi - lo + st - 1) / st * st); op(Session::OP_DUP, span);
        sel_.clear(); for (auto& q : s0) sel_.push_back({(q.tick + span) % pt, q.note}); });
    act("-12", [this, op] { op(Session::OP_TRANSPOSE, -12); for (auto& q : sel_) q.note = std::max(0, q.note - 12); });
    act("-1", [this, op] { op(Session::OP_TRANSPOSE, -1); for (auto& q : sel_) q.note = std::max(0, q.note - 1); });
    act("+1", [this, op] { op(Session::OP_TRANSPOSE, 1); for (auto& q : sel_) q.note = std::min(127, q.note + 1); });
    act("+12", [this, op] { op(Session::OP_TRANSPOSE, 12); for (auto& q : sel_) q.note = std::min(127, q.note + 12); });
    act("<", [this, op, pt, st] { op(Session::OP_SHIFT, -st); for (auto& q : sel_) q.tick = ((q.tick - st) % pt + pt) % pt; });
    act(">", [this, op, pt, st] { op(Session::OP_SHIFT, st); for (auto& q : sel_) q.tick = (q.tick + st) % pt; });
    // values: velocity, chance, ratchet, nudge: dragged along the bar, applied when the finger lifts (one undo step)
    struct Val { const char* name; int op, lo, hi; int cur; };
    const Val vals[5] = {{"VELOCITY", Session::OP_VEL, 1, 127, first ? first->vel : 100}, {"CHANCE", Session::OP_PROB, 0, 100, first ? first->prob : 100},
                         {"RATCHET", Session::OP_RATCHET, 1, 8, first ? first->ratchet : 1}, {"NUDGE", Session::OP_NUDGE, -16, 15, first ? first->nudge : 0},
                         {"FILTER LOCK", Session::OP_CUT, 0, 127, first ? first->cut : 0}};
    const float vw = (r.w - 16 - 4 * 8) / 5.f;
    for (int k = 0; k < 5; k++) {
        const Rect b{r.x + 8 + k * (vw + 8), r.y + 36, vw, 40};
        const bool live = selDrag_ == 10000 + k;
        const float cur = live ? selDragVal_ : (float)vals[k].cur;
        text(b.x, b.y + 5, 8.5f, DIM, vals[k].name, 0, true);
        text(b.x + vw, b.y + 5, 10.5f, cnt ? TEXT : DIM, (vals[k].op == Session::OP_PROB ? std::to_string((int)std::lround(cur)) + "%" : vals[k].op == Session::OP_NUDGE ? (cur > 0 ? "+" : "") + std::to_string((int)std::lround(cur)) : std::to_string((int)std::lround(cur))), 2, true);
        const Rect sl{b.x, b.y + 16, vw, 18}; rrect(sl, 0, BG); stroke(sl, 0, OUTLINE, 1);
        const float frac = (cur - vals[k].lo) / (float)(vals[k].hi - vals[k].lo);
        rrect({sl.x, sl.y, std::max(2.f, sl.w * clamp01(frac)), sl.h}, 0, cnt ? ACCENT : OUTLINE);
        if (!cnt) continue;
        const int lo = vals[k].lo, hi = vals[k].hi, o = vals[k].op;
        hit({sl.x, sl.y - 8, sl.w, sl.h + 16},
            [this, k, sl, lo, hi](float x, float) { selDrag_ = 10000 + k; selDragVal_ = lo + clamp01((x - sl.x) / sl.w) * (hi - lo); },
            [this, sl, lo, hi](float x, float) { selDragVal_ = lo + clamp01((x - sl.x) / sl.w) * (hi - lo); },
            [this, op, o]() { op(o, (int)std::lround(selDragVal_)); selDrag_ = -1; });
    }
}

void SamplerUI::pagePanel(Rect r) {
    // five tabs by task, then the pages of the tab, then one plain sentence about the page, then its four knobs
    const int mode = SamplerApp::modeOf(app_.page()), mw = (r.w - 4 * 3) / 5.f;
    for (int m = 0; m < SamplerApp::kModes; m++) {
        Rect c{r.x + m * (mw + 3), r.y, (float)mw, 32};
        button(c, SamplerApp::modeName(m), m == mode, [this, m] { app_.gotoMode(m); }, 9.5f);
    }
    const int pn = SamplerApp::modePageCount(mode), pw = (r.w - 3 * 4) / 4.f;
    for (int i = 0; i < pn; i++) {   // the pages of the tab, four to a row
        const SamplerApp::Page pg = SamplerApp::modePages(mode)[i];
        Rect c{r.x + (i % 4) * (pw + 4), r.y + 36 + (i / 4) * 30, (float)pw, 26};
        button(c, kPageTab[pg], app_.page() == pg, [this, pg] { app_.gotoPage(pg); }, 9.5f);
    }
    const float rows = pn > 8 ? 3.f : pn > 4 ? 2.f : 1.f, hy = r.y + 36 + rows * 30 + 2;
    { const std::string h = SamplerApp::pageHelp(app_.page()); const bool fxMode = mode == 1 || mode == 3; if (fxMode) button({r.x + r.w - 82, hy, 82, 24}, "BROWSE FX", false, [this] { fxOpen_ = true; fxTrack_ = app_.modeOf(app_.page()) == 3; }, 9.f);
      const size_t per = (size_t)std::max(12.f, (r.w - (fxMode ? 90.f : 0.f)) / (8.f * textScale_ * 0.6f)); size_t a = 0; int ln = 0;   // wrapped at spaces, three lines at most
      while (a < h.size() && ln < 3) { size_t b = std::min(h.size(), a + per); if (b < h.size()) { const size_t sp = h.rfind(' ', b); if (sp != std::string::npos && sp > a) b = sp; } text(r.x, hy + 6 + ln * 11 * textScale_, 8.f, DIM, h.substr(a, b - a), 0, false); a = b + (b < h.size() && h[b] == ' ' ? 1 : 0); ln++; } }
    auto k = app_.knobs();
    const float top = hy + 38, rh = (r.y + r.h - top) / 4;
    for (int i = 0; i < 4; i++) {
        const auto& p = k[(size_t)i];
        const float y = top + i * rh;
        if (!p.valid) continue;
        text(r.x, y + 8, T_CAPTION, DIM, p.name, 0, true);
        text(r.x + r.w, y + 10, T_VALUE, p.isChoice ? ACCENT : TEXT, p.value, 2, true);
        Rect s{r.x, y + std::min(26.f, rh - 16), r.w, 14};   // never below its own row (the last row of a short panel spilled out)
        if (!p.segs.empty()) {   // a few choices: one button each, the lit one is the state
            const float sw = (r.w - 4.f * (p.segs.size() - 1)) / p.segs.size();
            for (size_t j = 0; j < p.segs.size(); j++)
                button({r.x + j * (sw + 4), y + std::min(20.f, rh - 32), sw, 30}, p.segs[j], (int)j == p.segCur, [this, i, j] { auto kk = app_.knobs(); if (kk[(size_t)i].pick) kk[(size_t)i].pick((int)j); }, 9.5f, (int)j == p.segCur);
            continue;
        }
        rrect(s, 0, SURFACE); stroke(s, 0, OUTLINE, 1);
        if (p.isChoice) {   // segments are not known here: a notch at the current position
            rrect({s.x + p.norm * (s.w - 8), s.y, 8, s.h}, 0, ACCENT);
        } else {
            rrect({s.x, s.y, std::max(2.f, s.w * clamp01(p.norm)), s.h}, 0, theme::KNOB[i & 3]);
        }
        Rect hr{s.x, s.y - 12, s.w, std::min(40.f, rh)};
        hit(hr, [this, i, hr](float x, float) { app_.knobs()[(size_t)i].set(clamp01((x - hr.x) / hr.w)); },
            [this, i, hr](float x, float) { app_.knobs()[(size_t)i].set(clamp01((x - hr.x) / hr.w)); });
    }
}

// ---- projects: save, load, export ----
void SamplerUI::projectSheet() {
    rrect({0, 0, W_, H_}, 0, theme::withAlpha(BG, 215));
    tap({0, 0, W_, H_}, [this] { projOpen_ = false; });
    const Rect box{W_ * 0.12f, 20, W_ * 0.76f, H_ - 40};
    rrect(box, R_CARD, SURFACE); stroke(box, R_CARD, ACCENT, 1);
    tap(box, [] {});
    projList_ = app_.projectList();
    const std::string cur = app_.currentProject();
    text(box.x + 14, box.y + 20, T_TITLE, TEXT, "PROJECTS", 0, true);
    text(box.x + 110, box.y + 21, T_CAPTION, DIM, cur == "autosave" ? "UNSAVED SESSION" : "OPEN: " + cur, 0, false);
    button({box.x + box.w - 70, box.y + 6, 60, 28}, "CLOSE", false, [this] { projOpen_ = false; }, 10);
    button({box.x + box.w - 70 - 124, box.y + 6, 118, 28}, "+ NEW PROJECT", false, [this] { if (app_.newProject()) projOpen_ = false; }, 10, true);   // keeps the current work, then a clean start
    const float colW = 196;
    projListRect_ = {box.x + 6, box.y + 44, box.w - colW - 18, box.h - 52 - 28};
    rrect(projListRect_, R_CONTROL, BG);
    const float rh = 36;
    projScroll_ = std::min(std::max(0.f, projScroll_), std::max(0.f, projList_.size() * rh - projListRect_.h));
    clip(projListRect_);
    if (projList_.empty()) text(projListRect_.x + projListRect_.w / 2, projListRect_.y + 40, T_BODY, DIM, "NO SAVED PROJECTS YET. SAVE AS NEW.", 1, false);
    for (size_t i = 0; i < projList_.size(); i++) {
        const float y = projListRect_.y + i * rh - projScroll_;
        if (y + rh < projListRect_.y || y > projListRect_.y + projListRect_.h) continue;
        const bool sel = (int)i == projSel_, open = projList_[i] == cur;
        if (sel) { rrect({projListRect_.x + 2, y + 1, projListRect_.w - 4, rh - 2}, R_CONTROL, RAISED); stroke({projListRect_.x + 2, y + 1, projListRect_.w - 4, rh - 2}, R_CONTROL, ACCENT, 1); }
        text(projListRect_.x + 12, y + rh / 2, T_BODY + 1, sel ? ACCENT : TEXT, fit(projList_[i], projListRect_.w - 90, T_BODY + 1), 0, sel);
        if (open) text(projListRect_.x + projListRect_.w - 10, y + rh / 2, T_CAPTION, DIM, "OPEN", 2, false);
    }
    endClip();
    hit(projListRect_, [this](float, float y) { projLastY_ = y; projMoved_ = 0; },
        [this](float, float y) { projScroll_ -= y - projLastY_; projMoved_ += std::fabs(y - projLastY_); projLastY_ = y; },
        [this, rh]() { if (projMoved_ > 8) return; const int idx = (int)((projLastY_ - projListRect_.y + projScroll_) / rh); if (idx >= 0 && idx < (int)projList_.size()) projSel_ = idx; });
    // actions
    const float bx = box.x + box.w - colW - 6, bw = colW;
    float by = box.y + 44; const float bh = 30, gap = 4;
    auto b = [&](const std::string& label, bool on, std::function<void()> f, bool accent = false) { button({bx, by, bw, bh}, label, on, std::move(f), 10.5f, accent); by += bh + gap; };
    const bool haveSel = projSel_ >= 0 && projSel_ < (int)projList_.size();
    b(cur == "autosave" ? "SAVE AS NEW" : "SAVE  " + cur, false, [this, cur] { app_.saveProject(cur == "autosave" ? app_.newProjectName() : cur); }, true);
    if (cur != "autosave") b("SAVE AS NEW", false, [this] { app_.saveProject(app_.newProjectName()); });
    b(haveSel ? "LOAD  " + projList_[(size_t)projSel_] : "LOAD (PICK ONE)", false, [this, haveSel] { if (haveSel) { app_.loadProject(projList_[(size_t)projSel_]); projOpen_ = false; } });
    dangerButton({bx, by, bw, bh}, "DELETE PROJECT", [this, haveSel] { if (!haveSel) return; const std::string nm = projList_[(size_t)projSel_]; askConfirm("DELETE THE PROJECT " + nm + "?", "ITS PATTERNS AND ITS TAPE ARE DELETED.", "YES, DELETE", [this, nm] { app_.deleteProject(nm); projSel_ = -1; }); }, 10.5f); by += bh + gap;
    b("EXPORT MIDI (PATTERNS)", false, [this] { app_.exportMidi(); });
    b("EXPORT MIX (WAV)", false, [this] { app_.exportMix(); });
    b("IMPORT MIDI FILE", false, [this] { app_.requestMidi(); });
    const std::string t = app_.toast();
    if (!t.empty()) text(box.x + 14, box.y + box.h - 16, 11, ACCENT, t, 0, true);
}

void SamplerUI::openLibrary() {
    app_.refreshLibrary(); sheetOpen_ = true; sheetScroll_ = 0;
    const auto& lib = app_.library(); const int cur = app_.libraryCursor();
    libGroup_ = cur >= 0 && cur < (int)lib.size() ? lib[(size_t)cur].group : (lib.empty() ? std::string() : lib[0].group);   // opens on the group of the sound that is loaded
}
// the instrument browser: a chip per group on top, then the sounds of that group as a grid of cards (tap = load it, the sheet stays open to browse and hear)
void SamplerUI::librarySheet() {
    rrect({0, 0, W_, H_}, 0, theme::withAlpha(BG, 200));
    tap({0, 0, W_, H_}, [this] { sheetOpen_ = false; });
    const Rect box{W_ * 0.14f, 14, W_ * 0.72f, H_ - 28};
    rrect(box, R_CARD, SURFACE); stroke(box, R_CARD, ACCENT, 1);
    tap(box, [] {});
    text(box.x + 14, box.y + 20, T_TITLE, TEXT, "INSTRUMENTS", 0, true);
    text(box.x + 150, box.y + 21, T_CAPTION, DIM, "LOADS ON TRACK " + std::to_string(app_.track() + 1), 0, false);
    button({box.x + box.w - 70, box.y + 6, 60, 28}, "CLOSE", false, [this] { sheetOpen_ = false; }, 10);
    button({box.x + box.w - 190, box.y + 6, 112, 28}, app_.audition() ? "AUDITION: ON" : "AUDITION: OFF", app_.audition(), [this] { app_.setAudition(!app_.audition()); }, 10);
    const auto& lib = app_.library();
    std::vector<std::string> groups; for (const auto& e : lib) if (std::find(groups.begin(), groups.end(), e.group) == groups.end()) groups.push_back(e.group);
    if (std::find(groups.begin(), groups.end(), libGroup_) == groups.end() && !groups.empty()) libGroup_ = groups[0];
    float cx = box.x + 8; const float cy = box.y + 44, ch = 28;   // the chips: they wrap to a second line when there are many groups
    float cyy = cy;
    for (const auto& g : groups) {
        const float cw = std::max(54.f, 12.f + g.size() * 7.5f);
        if (cx + cw > box.x + box.w - 8) { cx = box.x + 8; cyy += ch + 4; }
        int n = 0; for (const auto& e : lib) if (e.group == g) n++;
        button({cx, cyy, cw, ch}, g, g == libGroup_, [this, g] { libGroup_ = g; sheetScroll_ = 0; }, 9.5f);
        cx += cw + 4;
    }
    sheetList_ = {box.x + 8, cyy + ch + 8, box.w - 16, box.y + box.h - (cyy + ch + 8) - 8};
    std::vector<int> items; for (int i = 0; i < (int)lib.size(); i++) if (lib[(size_t)i].group == libGroup_) items.push_back(i);
    const int cols = box.w > 520 ? 3 : 2; const float gap = 6, cw = (sheetList_.w - gap * (cols - 1)) / cols, rh = 44;
    const int nrows = ((int)items.size() + cols - 1) / cols;
    sheetScroll_ = std::min(std::max(0.f, sheetScroll_), std::max(0.f, nrows * (rh + gap) - sheetList_.h));
    clip(sheetList_);
    for (int k = 0; k < (int)items.size(); k++) {
        const Rect c{sheetList_.x + (k % cols) * (cw + gap), sheetList_.y + (k / cols) * (rh + gap) - sheetScroll_, cw, rh};
        if (c.y + c.h < sheetList_.y || c.y > sheetList_.y + sheetList_.h) continue;
        const int e = items[(size_t)k]; const bool cur = e == app_.libraryCursor();
        rrect(c, R_CARD, cur ? RAISED : BG); stroke(c, R_CARD, cur ? ACCENT : OUTLINE, cur ? 2 : 1);
        text(c.x + 10, c.y + rh / 2 - 6, T_BODY + 1, cur ? ACCENT : TEXT, fit(lib[(size_t)e].label, cw - 20, T_BODY + 1), 0, true);
        text(c.x + 10, c.y + rh / 2 + 11, 8.5f, DIM, lib[(size_t)e].kit ? "DRUM KIT" : (lib[(size_t)e].path.rfind("synth:", 0) == 0 || lib[(size_t)e].path.rfind("patch:", 0) == 0) ? "SYNTH" : "SAMPLE", 0, false);
    }
    endClip();
    hit(sheetList_, [this](float x, float y) { sheetLastY_ = y; sheetDownX_ = x; sheetDownY_ = y; sheetMoved_ = 0; },
        [this](float, float y) { sheetScroll_ -= y - sheetLastY_; sheetMoved_ += std::fabs(y - sheetLastY_); sheetLastY_ = y; },
        [this, items, cols, cw, rh, gap]() {
            if (sheetMoved_ > 8) return;
            const int col = (int)((sheetDownX_ - sheetList_.x) / (cw + gap)), row = (int)((sheetDownY_ - sheetList_.y + sheetScroll_) / (rh + gap)), k = row * cols + col;
            if (col >= 0 && col < cols && row >= 0 && k >= 0 && k < (int)items.size()) app_.loadEntry(items[(size_t)k], app_.audition());
        });
}
// the FX browser: every effect by kind; the dot says which are on; tap one to go to its knob (MASTER = the whole mix, TRACK = the selected track)
void SamplerUI::fxSheet() {
    rrect({0, 0, W_, H_}, 0, theme::withAlpha(BG, 200));
    tap({0, 0, W_, H_}, [this] { fxOpen_ = false; });
    const Rect box{W_ * 0.14f, 14, W_ * 0.72f, H_ - 28};
    rrect(box, R_CARD, SURFACE); stroke(box, R_CARD, ACCENT, 1);
    tap(box, [] {});
    text(box.x + 14, box.y + 20, T_TITLE, TEXT, "EFFECTS", 0, true);
    button({box.x + box.w - 70, box.y + 6, 60, 28}, "CLOSE", false, [this] { fxOpen_ = false; }, 10);
    button({box.x + box.w - 250, box.y + 6, 80, 28}, "MASTER", !fxTrack_, [this] { fxTrack_ = false; }, 10);
    button({box.x + box.w - 166, box.y + 6, 92, 28}, "TRACK " + std::to_string(app_.track() + 1), fxTrack_, [this] { fxTrack_ = true; }, 10);
    const FxParams fp = fxTrack_ ? app_.trackParams(app_.track()) : app_.masterParams();
    const auto& cat = SamplerApp::fxCatalog();
    const float colW = (box.w - 16 - 16) / 3, rh = 28; float colY[3] = {box.y + 46, box.y + 46, box.y + 46};
    // three columns of kinds, the biggest kind first, each into the shortest column; a kind is a caption and its effects
    std::vector<std::string> kinds; for (const auto& f : cat) if (std::find(kinds.begin(), kinds.end(), f.cat) == kinds.end()) kinds.push_back(f.cat);
    auto count = [&](const std::string& k) { int n = 0; for (const auto& f : cat) if (k == f.cat) n++; return n; };
    std::stable_sort(kinds.begin(), kinds.end(), [&](const std::string& a, const std::string& b) { return count(a) > count(b); });
    for (const auto& kd : kinds) {
        const int col = (int)(std::min_element(colY, colY + 3) - colY); float& y = colY[col]; const float x = box.x + 8 + col * (colW + 8);
        text(x + 2, y + 8, T_CAPTION, DIM, kd, 0, true); y += 18;
        for (const auto& f : cat) {
            if (kd != f.cat) continue;
            const SamplerApp::Page pg = fxTrack_ ? f.track : f.master; const bool has = pg != SamplerApp::kPages, on = has && f.on && f.on(fp);
            const Rect c{x, y, colW, rh - 3};
            rrect(c, R_CONTROL, has ? SURFACE : BG); stroke(c, R_CONTROL, on ? ACCENT : OUTLINE, on ? 2 : 1);
            text(c.x + 24, c.y + c.h / 2, T_BODY - 1, has ? TEXT : DIM, fit(f.name, colW - 30, T_BODY - 1), 0, on);
            if (on) circle(c.x + 12, c.y + c.h / 2, 4.f, ACCENT); else if (has) circle(c.x + 12, c.y + c.h / 2, 3.f, OUTLINE);
            if (has) tap(c, [this, pg] { fxOpen_ = false; app_.gotoPage(pg); });
            y += rh;
        }
        y += 8;
    }
}
