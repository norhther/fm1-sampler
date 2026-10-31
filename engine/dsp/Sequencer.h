// Note sequencer helpers: the tick grid, quantize snapping and the tick enumeration of one audio block. Pure functions, no state.
// A pattern is a list of notes {tick, len} on a grid of kPpq ticks per beat; the session plays it from the transport position, so it stays
// locked to the loop. Quantize is applied at playback (the recorded ticks are kept), so it can be changed or switched off at any time.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

constexpr int kPpq = 192;      // ticks per beat: divides by 3 and 4, so every grid below is exact
constexpr int kSeqMax = 256;   // notes per pattern
constexpr int kGrids = 8;

// a note of a pattern; prob 0..100 = chance that it plays, ratchet = repeats inside its length (1..8), nudge = micro timing in ticks (-16..15)
struct SeqNote { uint16_t tick, len; uint8_t note, vel; uint8_t prob = 100, ratchet = 1; int8_t nudge = 0; uint8_t cut = 0; };   // cut: a filter lock (0 none, 1..127 = 80 Hz .. 12 kHz) that this note sets on its track

// a note in one 64 bit word (the UI reads patterns without locks): tick 14 | len 14 | note 7 | vel 7 | prob 7 | ratchet - 1: 3 | nudge + 16: 5 | cut 7 (a pattern is at most 16 bars = 12288 ticks)
inline uint64_t packNote(const SeqNote& n) {
    return (uint64_t)(n.tick & 0x3fff) | (uint64_t)(n.len & 0x3fff) << 14 | (uint64_t)(n.note & 0x7f) << 28 | (uint64_t)(n.vel & 0x7f) << 35 | (uint64_t)std::min<int>(100, n.prob) << 42
         | (uint64_t)(std::max<int>(1, std::min<int>(8, n.ratchet)) - 1) << 49 | (uint64_t)(n.nudge + 16) << 52 | (uint64_t)(n.cut & 0x7f) << 57;
}
inline SeqNote unpackNote(uint64_t v) {
    SeqNote n; n.tick = (uint16_t)(v & 0x3fff); n.len = (uint16_t)(v >> 14 & 0x3fff); n.note = (uint8_t)(v >> 28 & 0x7f); n.vel = (uint8_t)(v >> 35 & 0x7f); n.prob = (uint8_t)(v >> 42 & 0x7f);
    n.ratchet = (uint8_t)((v >> 49 & 7) + 1); n.nudge = (int8_t)((int)(v >> 52 & 31) - 16); n.cut = (uint8_t)(v >> 57 & 0x7f); return n;
}

// 0 OFF, 1 1/4, 2 1/8, 3 1/8T, 4 1/16, 5 1/16T, 6 1/32, 7 1/32T
inline int gridTicks(int g) { static const int t[kGrids] = {0, 192, 96, 64, 48, 32, 24, 16}; return t[g < 0 ? 0 : g >= kGrids ? kGrids - 1 : g]; }
inline const char* gridName(int g) { static const char* n[kGrids] = {"OFF", "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T"}; return n[g < 0 ? 0 : g >= kGrids ? kGrids - 1 : g]; }
inline int patternTicks(int bars) { return bars * 4 * kPpq; }

// nearest grid line (ties go forward), wrapped into the pattern
inline int snapTick(int tick, int grid, int patTicks) {
    const int g = gridTicks(grid);
    if (g == 0) return tick % patTicks;
    return ((tick + g / 2) / g * g) % patTicks;
}

struct TickHit { int offset; int64_t tick; };   // sample offset inside the block, tick number counted from the loop start

// Every tick boundary inside a block of `n` samples that starts at transport position `ps` (samples). `tl` = samples per tick.
// `loop` > 0: the transport wraps at that many samples, the ticks restart at 0 after the wrap.
inline void tickHits(double ps, int n, double tl, double loop, std::vector<TickHit>& out) {
    out.clear();
    auto seg = [&](int s0, int c, double p0) {
        if (c <= 0) return;
        for (int64_t j = (int64_t)std::ceil(p0 / tl - 1e-9); j * tl < p0 + c - 1e-9; j++) {
            int off = s0 + (int)std::floor(j * tl - p0 + 0.5);
            off = off < s0 ? s0 : off > s0 + c - 1 ? s0 + c - 1 : off;
            out.push_back({off, j});
        }
    };
    if (loop > 0 && ps + n > loop) {
        const int w = std::max(0, std::min(n, (int)std::ceil(loop - ps)));
        seg(0, w, ps);
        seg(w, n - w, 0.0);
    } else seg(0, n, ps);
}
