#include "SamplerUI.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace theme;

namespace {
float clamp01(float v) { return std::min(1.f, std::max(0.f, v)); }
std::string num(const char* f, double v) { char b[48]; snprintf(b, sizeof b, f, v); return b; }
const char* kPageTab[] = {"SAMPLE", "SENDS", "ENV", "LFO", "ARP", "TAPE", "MIXER", "KEY", "LOOP", "LENGTH", "INSERT", "LIVE", "PATTERN", "TEMPO", "TRACK", "TIMING", "TONE", "MOD", "LIVE 2"};   // by Page
}  // namespace

SamplerUI::SamplerUI(SamplerApp& app) : app_(app) {}

// ---- primitives ----
void SamplerUI::rrect(Rect r, float rad, uint32_t c) { UiOp o; o.type = UiOp::RRECT; o.f[0] = r.x; o.f[1] = r.y; o.f[2] = r.w; o.f[3] = r.h; o.f[4] = rad; o.color = c; ops_.push_back(o); }
void SamplerUI::stroke(Rect r, float rad, uint32_t c, float w) { UiOp o; o.type = UiOp::STROKE; o.f[0] = r.x; o.f[1] = r.y; o.f[2] = r.w; o.f[3] = r.h; o.f[4] = rad; o.f[5] = w; o.color = c; ops_.push_back(o); }
void SamplerUI::text(float x, float cy, float size, uint32_t c, const std::string& s, int align, bool bold) {
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
    const size_t maxc = (size_t)std::max(1.f, width / (size * 0.60f));
    return s.size() <= maxc ? s : s.substr(0, maxc > 1 ? maxc - 1 : 1) + "\xE2\x80\xA6";
}
void SamplerUI::touch(int action, float x, float y, uint32_t now) {
    now_ = now;
    if (action == 0) {
        haveActive_ = false;
        for (size_t i = hits_.size(); i-- > 0;) if (hits_[i].r.has(x, y)) { active_ = hits_[i]; haveActive_ = true; if (active_.down) active_.down(x, y); break; }
    } else if (action == 1) {
        if (haveActive_ && active_.move) active_.move(x, y);
    } else if (haveActive_) {
        if (active_.up) active_.up();
        haveActive_ = false;
    }
}
// two fingers on the piano roll: the distance between them in x zooms the time, in y the pitch range; what is under the fingers stays under them
void SamplerUI::pinch(int phase, float x1, float y1, float x2, float y2) {
    if (phase == 2) { pinchOn_ = false; return; }
    const float cx = 0.5f * (x1 + x2), cy = 0.5f * (y1 + y2), dx = std::fabs(x1 - x2), dy = std::fabs(y1 - y2), minD = 60.f;   // (tiny distances would make the zoom jump)
    if (phase == 0) {
        pinchOn_ = false;
        if (!rollOn_ || cx < rollX0_ || cx > rollX0_ + rollW_ || cy < rollY0_ || cy > rollY0_ + rollH_) return;
        haveActive_ = false; rollMoved_ = true; drag_ = {};   // the first finger was a touch of the roll: it must not become a tap or a stretched note
        if (auditionNote_ >= 0) { app_.session().noteOff(app_.track(), auditionNote_); auditionNote_ = -1; }
        pinchOn_ = true; pinchDx0_ = std::max(dx, minD); pinchDy0_ = std::max(dy, minD);
        pinchZoom0_ = rollZoom_; pinchRows0_ = rollRowsF_; pinchBase0_ = rollBase_;
        const float vis0 = rollPt_ / rollZoom_;
        pinchTick0_ = rollScroll_ + (cx - rollX0_) / rollW_ * vis0;
        pinchYFrac0_ = (cy - rollY0_) / rollH_;
        pinchNote0_ = rollBase_ + rollRowsF_ * (1.f - pinchYFrac0_);
        return;
    }
    if (!pinchOn_) return;
    const float zoomMax = std::min(32.f, std::max(1.f, rollPt_ / (float)rollMinVisible_));
    rollZoom_ = std::min(zoomMax, std::max(1.f, pinchZoom0_ * std::max(dx, minD) / pinchDx0_));
    rollRowsF_ = std::min(60.f, std::max(5.f, pinchRows0_ * pinchDy0_ / std::max(dy, minD)));    // fingers apart = fewer, bigger rows
    const float vis = rollPt_ / rollZoom_, yFrac = (cy - rollY0_) / rollH_;
    rollScroll_ = pinchTick0_ - (cx - rollX0_) / rollW_ * vis;                                      // the tick that was between the fingers still is
    rollBase_ = pinchNote0_ - rollRowsF_ * (1.f - yFrac);
}

void SamplerUI::button(Rect r, const std::string& label, bool on, std::function<void()> f, float size, bool accent) {
    rrect(r, R_CONTROL, on ? ACCENT : SURFACE);
    if (!on) stroke(r, R_CONTROL, accent ? ACCENT : OUTLINE, 1);
    text(r.x + r.w / 2, r.y + r.h / 2, size, on ? ON_ACCENT : accent ? ACCENT : TEXT, fit(label, r.w - 6, size), 1, true);
    tap(r, std::move(f));
}

// ---- frame ----
void SamplerUI::frame(uint32_t now) {
    now_ = now;
    st_ = app_.status();
    ops_.clear(); hits_.clear(); rollOn_ = false;
    rrect({0, 0, W_, H_}, 0, BG);
    const float tb = 44, left = 176, right = 252;
    topBar({0, 0, W_, tb});
    trackList({MARGIN, tb + MARGIN, left - MARGIN, H_ - tb - 2 * MARGIN});
    sampleArea({left + MARGIN, tb + MARGIN, W_ - left - right - 2 * MARGIN, H_ - tb - 2 * MARGIN});
    pagePanel({W_ - right, tb + MARGIN, right - MARGIN, H_ - tb - 2 * MARGIN});
    if (sheetOpen_) librarySheet();
}

void SamplerUI::topBar(Rect r) {
    rrect(r, 0, SURFACE);
    line(0, r.h, W_, r.h, OUTLINE, 1);
    const float cy = r.h / 2;
    float x = MARGIN;
    // PLAY / STOP
    Rect pl{x, 6, 44, r.h - 12};
    rrect(pl, R_CONTROL, RAISED);
    if (st_.playing) { rrect({pl.x + 15, cy - 8, 5, 16}, 0, ACCENT); rrect({pl.x + 24, cy - 8, 5, 16}, 0, ACCENT); }
    else tri(pl.x + 16, cy - 9, pl.x + 16, cy + 9, pl.x + 30, cy, TEXT);
    tap(pl, [this] { app_.transportPlay(); });
    x += 52;
    // REC: arms the first take on a clean looper, overdub afterwards
    Rect rc{x, 6, 74, r.h - 12};
    const bool recOn = st_.freeRecording || st_.track_clip_rec[app_.track()], armed = st_.freeArmed;
    rrect(rc, R_CONTROL, recOn ? ACCENT : RAISED);
    if (armed) stroke(rc, R_CONTROL, ACCENT, 2);
    circle(rc.x + 18, cy, 6, recOn ? ON_ACCENT : ACCENT);
    text(rc.x + 28, cy, 9.5f, recOn ? ON_ACCENT : TEXT, app_.loopExists() ? "OVERDUB" : "REC", 0, true);
    tap(rc, [this] { app_.loopRecord(); });
    x += 82;
    button({x, 6, 56, r.h - 12}, "SAVE", false, [this] { app_.loopSave(); }, 11);
    x += 62;
    button({x, 6, 56, r.h - 12}, "CLEAR", false, [this] { app_.loopClear(); }, 11);
    x += 70;
    text(x, cy - 6, 12, st_.freeRecording || recOn || armed ? ACCENT : TEXT, app_.loopText(), 0, true);
    text(x, cy + 11, 9.5f, DIM, std::string("LOOP SOURCE: ") + (app_.knobs(SamplerApp::P_SAVE)[1].value), 0, false);
    tap({x - 4, 0, 230, r.h}, [this] { auto k = app_.knobs(SamplerApp::P_SAVE); k[1].step(k[1].norm > 0.5f ? -1 : 1); });
    // link state and load
    const uint32_t lc = linkMidi_ && linkAudio_ ? TEXT : linkMidi_ ? ACCENT : DIM;
    circle(W_ - 22, cy, 5, lc);
    text(W_ - 34, cy - 6, 10, DIM, linkMidi_ ? "FM-1" : "NO FM-1", 2, true);
    text(W_ - 34, cy + 9, 9, DIM, "CPU " + num("%.0f", st_.cpu_permille / 10.0) + "%", 2, false);
}

void SamplerUI::trackList(Rect r) {
    const float rh = (r.h - 7 * 4) / 8;
    for (int i = 0; i < SamplerApp::kTracks; i++) {
        Rect row{r.x, r.y + i * (rh + 4), r.w, rh};
        const bool sel = i == app_.track();
        rrect(row, R_CARD, sel ? RAISED : SURFACE);
        stroke(row, R_CARD, sel ? ACCENT : OUTLINE, sel ? 2 : 1);
        text(row.x + 14, row.y + rh / 2, T_TITLE, sel ? ACCENT : TEXT, std::to_string(i + 1), 1, true);
        const std::string lab = app_.sampleLabel(i);
        text(row.x + 30, row.y + rh / 2 - 6, 11, lab.empty() ? DIM : TEXT, fit(lab.empty() ? "EMPTY" : lab, r.w - 40, 11), 0, true);
        // level and loop mark
        const float lv = std::min(1.f, st_.track_peak[i] * 1.5f);
        rrect({row.x + 30, row.y + rh - 12, r.w - 44, 3}, 0, OUTLINE);
        rrect({row.x + 30, row.y + rh - 12, (r.w - 44) * lv, 3}, 0, TEXT);
        if (st_.track_clip_rec[i]) circle(row.x + r.w - 10, row.y + 10, 4, ACCENT);
        else if (st_.track_has_clip[i]) circle(row.x + r.w - 10, row.y + 10, 3.5f, TEXT);
        tap(row, [this, i] { app_.selectTrack(i); });
    }
}

void SamplerUI::sampleArea(Rect r) {
    const int t = app_.track();
    const auto ts = app_.state(t);
    const std::string lab = app_.sampleLabel(t);
    text(r.x, r.y + 12, T_TITLE, lab.empty() ? DIM : TEXT, fit(lab.empty() ? "EMPTY TRACK: PICK A SAMPLE" : lab, r.w - 120, T_TITLE), 0, true);
    text(r.x + r.w, r.y + 12, T_CAPTION, DIM, "TRACK " + std::to_string(t + 1), 2, true);
    if (app_.page() == SamplerApp::P_SQ) { sequencerView({r.x, r.y + 28, r.w, r.h - 28}); return; }   // the sequencer page shows the pattern instead of the waveform, with all the room there is
    // algorithm (a synth has none)
    const float cw = (r.w - 5 * 4) / 6;
    if (app_.isSynth(t)) text(r.x, r.y + 44, T_CAPTION, DIM, "BUILT-IN SYNTH  PLAY KEYS, SHAPE IT ON THE ENV, LFO AND FX PAGES", 0, false);
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
    auto pk = app_.peaks(t, bins);
    const float cy = w.y + w.h / 2, bw = w.w / bins;
    for (int i = 0; i < (int)pk.size(); i++) {
        const float pos = (i + 0.5f) / bins, v = std::min(1.f, pk[(size_t)i] * 1.1f);
        const bool in = pos >= ts.start && pos <= ts.end;
        rrect({w.x + i * bw + 0.5f, cy - std::max(1.f, v * (w.h / 2 - 8)), std::max(1.f, bw - 1), std::max(2.f, v * (w.h - 16))}, 0, in ? TEXT : OUTLINE);
    }
    if (pk.empty()) text(w.x + w.w / 2, cy, T_BODY, DIM, "NO SAMPLE", 1, false);
    for (int j = 0; j < st_.voiceN[t]; j++) {   // where the sounding voices are reading
        const float vx = w.x + 2 + st_.voicePos[t][j] * (w.w - 4);
        line(vx, w.y + 22, vx, w.y + w.h - 10, ACCENT, 1.5f);
        circle(vx, w.y + w.h - 10, 3.5f, ACCENT);
    }
    }
    after_wave:
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
    tap(nm, [this] { sheetOpen_ = true; sheetScroll_ = std::max(0.f, (float)app_.libraryCursor() * 34.f - 100.f); app_.refreshLibrary(); });
    // microphone and disk
    Rect br{r.x, lr.y + lr.h + 8, r.w, 38};
    const int ms = app_.micState();
    static const char* micLab[] = {"RECORD FROM MIC", "WAITING FOR SOUND...", "STOP AND KEEP", "SAVING..."};
    button({br.x, br.y, br.w * 0.6f - 3, br.h}, micLab[ms], ms == 2, [this] { app_.micSample(); }, 12, ms != 0);
    button({br.x + br.w * 0.6f + 3, br.y, br.w * 0.4f - 3, br.h}, "LOAD FILE FROM DISK", false, [this] { app_.requestFile(); }, 11);
    const std::string ts_ = app_.toast();
    if (!ts_.empty()) text(r.x, br.y + br.h + 14, 11, ACCENT, ts_, 0, true);
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
    // toolbar: undo, redo, zoom in time, zoom in pitch, fit, grid
    const float tbh = 26, bw8 = (r.w - 7 * 4) / 8.6f;   // the last button (GRID) is a bit wider
    int tx = 0;
    auto tb = [&](const std::string& label, bool on, std::function<void()> f) { button({r.x + tx * (bw8 + 4), r.y, tx == 7 ? bw8 * 1.6f : bw8, tbh}, label, on, std::move(f), 9.5f); tx++; };
    tb("UNDO", st_.seqUndoN[t] > 0, [this] { app_.seqUndo(); });
    tb("REDO", st_.seqRedoN[t] > 0, [this] { app_.seqRedo(); });
    const float zoomMax = std::min(32.f, std::max(1.f, pt / (2.f * st)));
    tb("TIME -", false, [this] { rollZoom_ = std::max(1.f, rollZoom_ / 1.6f); });
    tb("TIME +", false, [this, zoomMax] { rollZoom_ = std::min(zoomMax, rollZoom_ * 1.6f); });
    tb("ROWS -", false, [this] { const float mid = rollBase_ + rollRowsF_ / 2.f; rollRowsF_ = std::min(60.f, rollRowsF_ * 1.4f); rollBase_ = mid - rollRowsF_ / 2.f; });   // more rows = smaller
    tb("ROWS +", false, [this] { const float mid = rollBase_ + rollRowsF_ / 2.f; rollRowsF_ = std::max(5.f, rollRowsF_ / 1.4f); rollBase_ = mid - rollRowsF_ / 2.f; });
    tb("FIT", false, [this] { rollFitTrack_ = -1; });
    tb(std::string("GRID ") + gridName(app_.seqGridIndex() ? app_.seqGridIndex() : 4), false, [this] { app_.cycleSeqGrid(); });
    // the roll
    const float keyW = 34, rulerH = 14;
    Rect roll{r.x, r.y + tbh + 4, r.w, r.h - tbh - 4 - 46};
    rrect(roll, R_CARD, SURFACE); stroke(roll, R_CARD, OUTLINE, 1);
    rollZoom_ = std::min(zoomMax, std::max(1.f, rollZoom_)); rollRowsF_ = std::min(60.f, std::max(5.f, rollRowsF_));
    const int rows = (int)std::lround(rollRowsF_), visible = std::max(2 * st, (int)(pt / rollZoom_)); const float zoom = (float)pt / visible;
    const float x0 = roll.x + 2 + keyW, y0 = roll.y + 2 + rulerH, W = roll.w - 4 - keyW, H = roll.h - 4 - rulerH, rh = H / rows;
    const double playTick = std::fmod(st_.beatPos * kPpq, (double)pt);
    if (zoom > 1 && (st_.playing || state > 0) && !haveActive_ && (playTick < rollScroll_ || playTick >= rollScroll_ + visible)) rollScroll_ = (float)(std::floor(playTick / visible) * visible);   // the view pages along with the playhead
    rollScroll_ = std::min((float)(pt - visible), std::max(0.f, rollScroll_));
    rollBase_ = std::min(127.f - rows, std::max(0.f, rollBase_));
    const int base = (int)std::floor(rollBase_);
    auto px = [&](double tick) { return x0 + (float)((tick - rollScroll_) / visible * W); };
    clip(roll);
    for (int i = 0; i < rows; i++) {   // rows from the top: the highest note first; black keys are a little darker
        const int nn = base + rows - 1 - i, pc = nn % 12; const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
        if (black) rrect({x0, y0 + i * rh, W, rh}, 0, theme::withAlpha(BG, 110));
        line(x0, y0 + (i + 1) * rh, x0 + W, y0 + (i + 1) * rh, pc == 0 ? OUTLINE : theme::withAlpha(OUTLINE, 70), 1);
        // the keyboard strip
        rrect({roll.x + 2, y0 + i * rh, keyW - 2, rh}, 0, black ? BG : RAISED);
        line(roll.x + 2, y0 + (i + 1) * rh, roll.x + keyW, y0 + (i + 1) * rh, OUTLINE, 1);
        if (pc == 0 || rh >= 13) text(roll.x + 5, y0 + i * rh + rh / 2, 8.5f, pc == 0 ? ACCENT : DIM, std::string(SamplerApp::noteName(pc)) + std::to_string(nn / 12 - 1), 0, pc == 0);
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
    for (int i = 0; i < n; i++) {
        const int row = base + rows - 1 - v[i].note;
        if (row < 0 || row >= rows || v[i].tick + v[i].len < rollScroll_ || v[i].tick > rollScroll_ + visible) continue;
        const bool grabbed = drag_.on && drag_.existing && drag_.tick == v[i].tick && drag_.note == v[i].note;
        const int len = grabbed ? drag_.len : v[i].len;
        const Rect nr{px(v[i].tick), y0 + row * rh + 1.f, std::max(4.f, (float)len / visible * W), std::max(3.f, rh - 2)};
        rrect(nr, 2, grabbed ? ACCENT : TEXT);
        if (nr.w > 14) line(nr.x + nr.w - 4, nr.y + 3, nr.x + nr.w - 4, nr.y + nr.h - 3, grabbed ? ON_ACCENT : BG, 1.5f);   // a grip at the end: drag it to stretch the note
    }
    if (drag_.on && !drag_.existing) {   // the new note under the finger
        const int row = base + rows - 1 - drag_.note;
        if (row >= 0 && row < rows) rrect({px(drag_.tick), y0 + row * rh + 1.f, std::max(4.f, (float)drag_.len / visible * W), std::max(3.f, rh - 2)}, 2, ACCENT);
    }
    if (st_.playing || state > 0) { const float pxx = px(playTick); if (pxx >= x0 && pxx <= x0 + W) line(pxx, y0 - rulerH + 0.f, pxx, y0 + H, ACCENT, 1.5f); }
    if (n == 0 && !drag_.on) text(x0 + W / 2, y0 + H / 2, T_BODY, DIM, state == 2 ? "PLAY KEYS: THEY ARE RECORDED" : "TAP A CELL, DRAG SIDEWAYS FOR A LONG NOTE", 1, false);
    text(roll.x + roll.w - 6, roll.y + 2 + rulerH + 9, 8.5f, DIM, zoom > 1.05f ? num("%.1f", zoom) + "X  DRAG THE RULER TO SCROLL" : std::string("PINCH TO ZOOM"), 2, false);
    rollOn_ = true; rollX0_ = x0; rollY0_ = y0; rollW_ = W; rollH_ = H; rollPt_ = pt; rollMinVisible_ = 2 * st;
    endClip();
    // ruler: drag sideways to scroll the time
    hit({x0, roll.y + 2, W, rulerH}, [this](float x, float) { rulerDownX_ = x; rulerStartScroll_ = rollScroll_; },
        [this, W, visible](float x, float) { rollScroll_ = rulerStartScroll_ - (x - rulerDownX_) / W * visible; });
    // keyboard: a tap plays the note, a drag scrolls the pitch
    hit({roll.x + 2, y0, keyW - 2, H},
        [this, t, y0, rh, rows, base](float, float y) {
            keyDownY_ = y; keyStartBase_ = rollBase_; keyMoved_ = false;
            auditionNote_ = base + rows - 1 - std::min(rows - 1, std::max(0, (int)((y - y0) / rh)));
            app_.session().noteOn(t, auditionNote_, 100);
        },
        [this, t, rh](float, float y) {
            if (!keyMoved_ && std::fabs(y - keyDownY_) > 8) { keyMoved_ = true; if (auditionNote_ >= 0) app_.session().noteOff(t, auditionNote_); auditionNote_ = -1; }
            if (keyMoved_) rollBase_ = keyStartBase_ + (y - keyDownY_) / rh;
        },
        [this, t] { if (auditionNote_ >= 0) app_.session().noteOff(t, auditionNote_); auditionNote_ = -1; });
    // grid
    auto tickAt = [this, x0, W, visible](float x) { return rollScroll_ + (x - x0) / W * visible; };
    hit({x0, y0, W, H},
        [this, v, n, tickAt, y0, rh, rows, base, st, pt](float x, float y) mutable {
            const int tick = std::min(pt - st, std::max(0, (int)tickAt(x) / st * st)), note = base + rows - 1 - std::min(rows - 1, std::max(0, (int)((y - y0) / rh)));
            rollDownX_ = x; rollDownY_ = y; rollMoved_ = false;
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
    const float bw = (r.w - 3 * 6) / 4, by = r.y + r.h - 38;
    int bx = 0;
    auto b = [&](const std::string& label, bool on, std::function<void()> f, bool accent = false) { button({r.x + bx * (bw + 6), by, bw, 38}, label, on, std::move(f), 11, accent); bx++; };
    b(state == 2 ? "RECORDING: TAP TO LOOP" : "RECORD PATTERN", state == 2, [this, state] { app_.setSeqState(state == 2 ? 1 : 2); }, true);
    b(state == 0 ? "PATTERN OFF" : "PATTERN ON", state != 0, [this, state] { app_.setSeqState(state == 0 ? 1 : 0); });
    b("DICE", false, [this] { app_.seqDice(); });
    b("CLEAR PATTERN", false, [this] { app_.seqClear(); });
}

void SamplerUI::pagePanel(Rect r) {
    // four modes (OP-1 style), then the four pages of the current mode
    const int mode = SamplerApp::modeOf(app_.page()), mw = (r.w - 3 * 4) / 4.f;
    for (int m = 0; m < SamplerApp::kModes; m++) {
        Rect c{r.x + m * (mw + 4), r.y, (float)mw, 30};
        button(c, SamplerApp::modeName(m), m == mode, [this, m] { app_.gotoMode(m); }, 11);
    }
    const int pn = SamplerApp::modePageCount(mode);
    for (int i = 0; i < pn; i++) {   // the pages of the mode, four to a row
        const SamplerApp::Page pg = SamplerApp::modePages(mode)[i];
        Rect c{r.x + (i % 4) * (mw + 4), r.y + 34 + (i / 4) * 30, (float)mw, 26};
        button(c, kPageTab[pg], app_.page() == pg, [this, pg] { app_.gotoPage(pg); }, 9.5f);
    }
    const float extra = pn > 4 ? 30.f : 0.f;
    auto k = app_.knobs();
    const float top = r.y + 72 + extra, rh = (r.h - 72 - extra) / 4;
    for (int i = 0; i < 4; i++) {
        const auto& p = k[(size_t)i];
        const float y = top + i * rh;
        if (!p.valid) continue;
        text(r.x, y + 8, T_CAPTION, DIM, "K" + std::to_string(i + 1) + "  " + p.name, 0, true);
        text(r.x + r.w, y + 10, T_VALUE, p.isChoice ? ACCENT : TEXT, p.value, 2, true);
        Rect s{r.x, y + 26, r.w, 14};
        rrect(s, 0, SURFACE); stroke(s, 0, OUTLINE, 1);
        if (p.isChoice) {   // segments are not known here: a notch at the current position
            rrect({s.x + p.norm * (s.w - 8), s.y, 8, s.h}, 0, ACCENT);
        } else {
            rrect({s.x, s.y, std::max(2.f, s.w * clamp01(p.norm)), s.h}, 0, TEXT);
        }
        Rect hr{s.x, y + 14, s.w, 40};
        hit(hr, [this, i, hr](float x, float) { app_.knobs()[(size_t)i].set(clamp01((x - hr.x) / hr.w)); },
            [this, i, hr](float x, float) { app_.knobs()[(size_t)i].set(clamp01((x - hr.x) / hr.w)); });
    }
}

void SamplerUI::librarySheet() {
    rrect({0, 0, W_, H_}, 0, theme::withAlpha(BG, 200));
    tap({0, 0, W_, H_}, [this] { sheetOpen_ = false; });
    const Rect box{W_ * 0.22f, 24, W_ * 0.56f, H_ - 48};
    rrect(box, R_CARD, SURFACE); stroke(box, R_CARD, ACCENT, 1);
    tap(box, [] {});
    text(box.x + 14, box.y + 20, T_TITLE, TEXT, "SAMPLE LIBRARY", 0, true);
    button({box.x + box.w - 70, box.y + 6, 60, 28}, "CLOSE", false, [this] { sheetOpen_ = false; }, 10);
    sheetList_ = {box.x + 6, box.y + 44, box.w - 12, box.h - 52};
    const auto& lib = app_.library();
    // rows: a header per group, then its entries
    struct Row { bool head; std::string text; int entry; };
    std::vector<Row> rows;
    std::string g;
    for (int i = 0; i < (int)lib.size(); i++) { if (lib[(size_t)i].group != g) { g = lib[(size_t)i].group; rows.push_back({true, g, -1}); } rows.push_back({false, lib[(size_t)i].label, i}); }
    const float rh = 34;
    sheetScroll_ = std::min(std::max(0.f, sheetScroll_), std::max(0.f, rows.size() * rh - sheetList_.h));
    clip(sheetList_);
    for (size_t i = 0; i < rows.size(); i++) {
        const float y = sheetList_.y + i * rh - sheetScroll_;
        if (y + rh < sheetList_.y || y > sheetList_.y + sheetList_.h) continue;
        if (rows[i].head) { text(sheetList_.x + 8, y + rh / 2, T_CAPTION, DIM, rows[i].text, 0, true); line(sheetList_.x, y + rh - 3, sheetList_.x + sheetList_.w, y + rh - 3, OUTLINE, 1); continue; }
        const bool cur = rows[i].entry == app_.libraryCursor();
        if (cur) { rrect({sheetList_.x, y + 1, sheetList_.w, rh - 2}, R_CONTROL, RAISED); stroke({sheetList_.x, y + 1, sheetList_.w, rh - 2}, R_CONTROL, ACCENT, 1); }
        text(sheetList_.x + 14, y + rh / 2, T_BODY + 1, cur ? ACCENT : TEXT, fit(rows[i].text, sheetList_.w - 30, T_BODY + 1), 0, cur);
    }
    endClip();
    hit(sheetList_, [this](float, float y) { sheetLastY_ = y; sheetMoved_ = 0; },
        [this](float, float y) { sheetScroll_ -= y - sheetLastY_; sheetMoved_ += std::fabs(y - sheetLastY_); sheetLastY_ = y; },
        [this, rows, rh]() {
            if (sheetMoved_ > 8) return;
            const int idx = (int)((sheetLastY_ - sheetList_.y + sheetScroll_) / rh);
            if (idx >= 0 && idx < (int)rows.size() && !rows[(size_t)idx].head) { app_.loadEntry(rows[(size_t)idx].entry); sheetOpen_ = false; }
        });
}
