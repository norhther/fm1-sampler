// FM1-DAW design system. One palette, one set of radii and spacings; the FM-1 screen (surface/Surface.cpp) uses the same hues.
// Colours are ARGB. Sizes are dp.
#pragma once
#include <cmath>
#include <cstdint>

namespace theme {
constexpr uint32_t argb(int r, int g, int b, int a = 255) { return (uint32_t)a << 24 | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b; }
constexpr uint32_t withAlpha(uint32_t c, int a) { return (c & 0x00ffffffu) | (uint32_t)a << 24; }

// Lo-fi look: two base colours (paper and ink, in a dark and a light variant) and one accent. No gradients, 1 px lines, square corners.
// The colours are variables so the phone can switch variant at run time (theme::apply); everything else names them through this header.
inline uint32_t BG, SURFACE, RAISED, OUTLINE, TEXT, DIM, ACCENT, ON_ACCENT, DANGER;   // DANGER: what cannot be taken back (erase, delete)
inline uint32_t VIOLET, GREEN, RED, AMBER;   // old names kept: they now all resolve to paper / accent, so state is told by fill and position, not hue
inline uint32_t KNOB[4];                     // colours of the four page knobs (the OP-1 themes use its encoder colours: blue, green, white, orange)
inline bool light = false;
inline int id = 0;                           // 0 DARK, 1 LIGHT, 2 OP-1, 3 OP-1 BODY, 4 HIGH CONTRAST
constexpr int kThemes = 5;
inline const char* name(int i) { static const char* n[kThemes] = {"DARK", "LIGHT", "OP-1", "OP-1 BODY", "HI-CONTRAST"}; return n[i < 0 || i >= kThemes ? 0 : i]; }
inline void apply(int theme) {
    id = theme < 0 || theme >= kThemes ? 0 : theme; light = id == 1 || id == 3;
    const uint32_t paper = argb(237, 235, 228), ink = argb(20, 20, 26);
    ACCENT = argb(232, 163, 61);                       // amber: playhead, selection, recording
    if (id == 0) { BG = ink; SURFACE = argb(28, 28, 36); RAISED = argb(40, 40, 50); OUTLINE = argb(66, 66, 78); TEXT = paper; DIM = argb(138, 136, 128); ON_ACCENT = ink; }
    else if (id == 1) { BG = paper; SURFACE = argb(226, 223, 214); RAISED = argb(212, 209, 198); OUTLINE = argb(168, 165, 154); TEXT = ink; DIM = argb(110, 108, 100); ON_ACCENT = ink; }
    else if (id == 2) {   // the OP-1 screen: near black, off-white line art, orange for what is active
        BG = argb(12, 12, 14); SURFACE = argb(21, 21, 23); RAISED = argb(32, 32, 35); OUTLINE = argb(58, 57, 53); TEXT = argb(244, 243, 239); DIM = argb(117, 114, 109);
        ACCENT = argb(255, 106, 19); ON_ACCENT = argb(12, 12, 14);
    } else if (id == 4) { // high contrast: pure black and white, yellow for what is active, strong lines
        BG = argb(0, 0, 0); SURFACE = argb(0, 0, 0); RAISED = argb(38, 38, 38); OUTLINE = argb(255, 255, 255); TEXT = argb(255, 255, 255); DIM = argb(200, 200, 200);
        ACCENT = argb(255, 224, 0); ON_ACCENT = argb(0, 0, 0);
    } else {              // the OP-1 body: brushed light grey, black ink, orange keys
        BG = argb(201, 198, 191); SURFACE = argb(222, 219, 212); RAISED = argb(233, 231, 225); OUTLINE = argb(156, 152, 145); TEXT = argb(26, 26, 28); DIM = argb(95, 92, 88);
        ACCENT = argb(255, 106, 19); ON_ACCENT = argb(255, 255, 255);
    }
    DANGER = id == 1 || id == 3 ? argb(190, 30, 30) : id == 4 ? argb(255, 90, 90) : argb(235, 74, 64);
    VIOLET = TEXT; GREEN = TEXT; RED = ACCENT; AMBER = ACCENT;
    if (id >= 2 && id < 4) { KNOB[0] = argb(47, 125, 225); KNOB[1] = argb(62, 196, 109); KNOB[2] = id == 2 ? argb(244, 243, 239) : argb(26, 26, 28); KNOB[3] = argb(255, 106, 19); }
    else for (auto& k : KNOB) k = TEXT;
}
inline void apply(bool lightVariant) { apply(lightVariant ? 1 : 0); }
inline const bool kInit = (apply(0), true);

constexpr float R_CARD = 2, R_CONTROL = 2, GAP = 6, MARGIN = 8;
constexpr float T_CAPTION = 10, T_BODY = 12, T_VALUE = 15, T_TITLE = 18, T_NUMBER = 22;

// Tracks are told apart by number and position, not hue.
inline uint32_t trackColor(int i) { return KNOB[i & 3]; }
}  // namespace theme
