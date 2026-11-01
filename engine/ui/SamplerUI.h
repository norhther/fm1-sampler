// SamplerUI: the phone interface of the sampler + looper as draw ops with its own touch handling, laid out for a 400-unit high landscape screen.
// It shows and edits exactly what the FM-1 shows (SamplerApp::knobs), so both always agree.
#pragma once
#include "../app/SamplerApp.h"
#include "Theme.h"
#include "UiOp.h"
#include <functional>
#include <string>
#include <vector>

class SamplerUI {
public:
    explicit SamplerUI(SamplerApp& app);
    void setSize(float w, float h, float topInset, float bottomInset) { W_ = w; H_ = h; top_ = topInset; bottom_ = bottomInset; }
    void setLink(bool midi, bool usbAudio) { linkMidi_ = midi; linkAudio_ = usbAudio; }
    void frame(uint32_t nowMs);
    const std::vector<UiOp>& ops() const { return ops_; }
    void touch(int action /*0 down 1 move 2 up*/, float x, float y, uint32_t nowMs) { touchP(0, action, x, y, nowMs); }
    void touchP(int id, int action, float x, float y, uint32_t nowMs);   // one finger by its id: every finger has its own control under it (the pads play together)
    void cancelTouches();                                                 // all fingers lifted at once (the system took the touch away)
    bool takeHaptic() { const bool h = haptic_; haptic_ = false; return h; }                     // a touch landed on a control: the phone should tick
    std::string takeAnnounce() { std::string s; s.swap(announce_); return s; }                    // a message for the screen reader (TalkBack)
    void openMenu() { menuOpen_ = true; }
    bool menuOpen() const { return menuOpen_; }
    bool tourOpen() const { return tourOpen_; }
    void openTour() { tourOpen_ = true; tourStep_ = 0; menuOpen_ = false; }
    int selectedCount() const { return (int)sel_.size(); }       // SELECT mode of the roll (for the tests)
    bool isSelected(int tick, int note) const { return isSel(tick, note); }
    bool songSheetOpen() const { return songOpen_; }
    void openSongSheet() { songOpen_ = true; }
    bool projectSheetOpen() const { return projOpen_; }
    void openChopEditor() { chopScreen_ = true; }
    void openProjectSheet() { projOpen_ = true; projSel_ = -1; }
    float rollZoom() const { return rollZoom_; }       // for the tests: time zoom of the piano roll (1 = the whole pattern) and the pitch rows shown
    float rollRows() const { return rollRowsF_; }
    float rollBase() const { return rollBase_; }
    float rollScroll() const { return rollScroll_; }
    void rollGrid(float& x, float& y, float& w, float& h) const { x = rollX0_; y = rollY0_; w = rollW_; h = rollH_; }   // the note grid of the last frame
    bool pinch(int phase /*0 start 1 move 2 end*/, float x1, float y1, float x2, float y2);   // true: the two fingers are a pinch of the piano roll   // two fingers on the piano roll: horizontally apart = zoom in time, vertically = pitch

private:
    struct Rect { float x, y, w, h; bool has(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; } };
    struct Hit { Rect r; std::function<void(float, float)> down, move; std::function<void()> up, lp; };   // lp: a finger that rests for half a second
    void rrect(Rect r, float rad, uint32_t c);
    void stroke(Rect r, float rad, uint32_t c, float w = 1);
    void text(float x, float cy, float size, uint32_t c, const std::string& s, int align = 0, bool bold = false);
    void line(float x1, float y1, float x2, float y2, uint32_t c, float w = 1);
    void circle(float cx, float cy, float r, uint32_t c);
    void tri(float x1, float y1, float x2, float y2, float x3, float y3, uint32_t c);
    void clip(Rect r);
    void endClip();
    void hit(Rect r, std::function<void(float, float)> down, std::function<void(float, float)> move = nullptr, std::function<void()> up = nullptr);
    void tap(Rect r, std::function<void()> f);
    std::string fit(const std::string& s, float width, float size) const;
    void button(Rect r, const std::string& label, bool on, std::function<void()> f, float size = 11, bool accent = false);
    void dangerButton(Rect r, const std::string& label, std::function<void()> f, float size = 11);   // erases something that cannot come back: red, with a warning mark
    void longPress(Rect r, std::function<void()> onTap, std::function<void()> onLong);               // tap = onTap when the finger lifts, rest for half a second = onLong

    void topBar(Rect r);
    void trackList(Rect r);
    void sampleArea(Rect r);
    void pagePanel(Rect r);
    void librarySheet();
    void projectSheet();
    void songSheet();
    void sequencerView(Rect r);
    void tombolaView(Rect r);
    void padsView(Rect r);

    SamplerApp& app_;
    float W_ = 860, H_ = 400, top_ = 0, bottom_ = 0;
    std::vector<UiOp> ops_;
    std::vector<Hit> hits_;
    struct Slot { Hit h{}; bool on = false, lpFired = false, lpMoved = false; uint32_t downAt = 0; float dx = 0, dy = 0; } slot_[16];   // a finger each
    Slot* cur_ = nullptr;
    bool anyTouch() const { for (auto& s : slot_) if (s.on) return true; return false; }
    uint16_t padMask_ = 0;   // the pads that are down
    uint32_t now_ = 0;
    Session::Status st_{};
    bool linkMidi_ = false, linkAudio_ = false, sheetOpen_ = false;
    float sheetScroll_ = 0, sheetLastY_ = 0, sheetMoved_ = 0;
    Rect sheetList_{0, 0, 0, 0};
    float rollBase_ = 54, rollScroll_ = 0, rollDownX_ = 0, rollDownY_ = 0, rollStartBase_ = 54, rollStartScroll_ = 0; bool rollMoved_ = false;   // piano roll: lowest note shown, first tick shown, touch state
    float rollZoom_ = 1.f, rollRowsF_ = 12.f;   // (12 rows: big enough to hit with a finger)
    // a finger on the grid: a new note being stretched, or an existing one being resized (tap = remove)
    struct NoteDrag { bool on = false, existing = false, moved = false; int tick = 0, note = 0, len = 0, origLen = 0; } drag_;
    int rollFitTrack_ = -1, auditionNote_ = -1; float keyDownY_ = 0, keyStartBase_ = 0; bool keyMoved_ = false;
    int clearAsk_ = 0; float tempoDragX_ = 0;   // the tape clear dialog: 0 closed, 1 the whole tape, 2 + n track n
    void tapeView(Rect r);
    void waveDraw(Rect w); void chopEdit(Rect w); void chopFullScreen(); bool chopScreen_ = false, chopEven_ = false;   // the sample waveform, its chop marks, and the chop editor on the whole screen
    void seqFullScreen(); bool seqFull_ = false;   // the pattern editor on the whole screen
    void newLoopSheet(); bool newLoopOpen_ = false, nlSeq_ = false; int nlBars_ = 2;   // NEW LOOP: tape or sequencer, how many bars
    // one dialog for what cannot be undone, the menu, the first-run tour, the mixer, the long-press menu of a track
    struct Confirm { std::string title, body, yes; std::function<void()> go; } confirm_;
    bool confirmOpen_ = false;
    void askConfirm(const std::string& title, const std::string& body, const std::string& yes, std::function<void()> go) { confirm_ = {title, body, yes, std::move(go)}; confirmOpen_ = true; }
    void askTapeClear(int track);   // -1: the whole tape
    void confirmSheet(); void menuSheet(); void tourSheet(); void trackMenuSheet(); void mixerView(Rect r); void flowStrip(Rect r); void undoToast();
    void showUndo(const std::string& msg) { undoMsg_ = msg; undoMsgAt_ = now_ ? now_ : 1; }
    bool menuOpen_ = false, tourOpen_ = false, tourSeen_ = false, haptic_ = false; int tourStep_ = 0, trackMenu_ = -1;
    float textScale_ = 1.f; uint32_t undoMsgAt_ = 0;
    std::string announce_, lastToast_, undoMsg_;
    Rect rTracks_{0, 0, 0, 0}, rPanel_{0, 0, 0, 0}, rArea_{0, 0, 0, 0}, rRec_{0, 0, 0, 0}, rUndo_{0, 0, 0, 0}, rTempo_{0, 0, 0, 0};   // where things are, for the tour
    bool songOpen_ = false, copyArm_ = false; bool projOpen_ = false; int projSel_ = -1; float projScroll_ = 0, projLastY_ = 0, projMoved_ = 0; uint32_t projDelAt_ = 0; std::vector<std::string> projList_; Rect projListRect_{0, 0, 0, 0};
    // SELECT mode of the roll: notes picked by tap or by a box, edited together
    int drumView_ = 0;   // the roll as a drum grid: 0 off, 1 pads 1-8, 2 pads 9-16 (one row per pad)
    int chopIdx_ = -1, chopSel_ = 0, chopHold_ = 0; bool chopMoved_ = false, chopNew_ = false, chopEndH_ = false;   // KIT: the chop handle under the finger, the selected chop
    std::vector<float> chopS_, chopE_;                                    // the chops while a finger moves a handle (and a few frames after, until the session has them)
    void chopsNow(std::vector<float>& s, std::vector<float>& e) const;    // starts and ends of the chops, 0..1
    void chopDelete();
    int padDown_ = -1; bool selMode_ = false, rbOn_ = false; std::vector<Session::SeqRef> sel_; float rbX0_ = 0, rbY0_ = 0, rbX1_ = 0, rbY1_ = 0; int selDrag_ = -1; float selDragVal_ = 0;
    bool isSel(int tick, int note) const { for (auto& r : sel_) if (r.tick == tick && r.note == note) return true; return false; }
    void selectPanel(Rect r, const Session::SeqView* v, int n, int pt, int st);
    uint32_t rollTouchAt_ = 0;   // the view does not page along with the playhead for a while after a touch on the roll
    float rulerDownX_ = 0, rulerStartScroll_ = 0, drumLastAngle_ = 0;   // piano roll zoom: how many times the pattern is stretched in time, how many pitch rows are shown
    // the roll of the last frame (for the pinch) and the state of a pinch in progress
    bool rollOn_ = false, pinchOn_ = false; float rollX0_ = 0, rollY0_ = 0, rollW_ = 1, rollH_ = 1; int rollPt_ = 1, rollMinVisible_ = 1;
    float pinchDx0_ = 1, pinchDy0_ = 1, pinchZoom0_ = 1, pinchRows0_ = 12, pinchBase0_ = 54, pinchTick0_ = 0, pinchNote0_ = 60, pinchYFrac0_ = 0.5f;
};
