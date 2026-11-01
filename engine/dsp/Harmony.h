// SPDX-License-Identifier: GPL-3.0-only
// Chord progressions in the current scale (the idea of Tessera's CHORD track and QNT CHRD, GPL-3.0, Theo Dainis): one chord a bar, every chord a
// triad of the scale built on its degree, and a key that snaps to the nearest tone of the chord playing now, so any key you hit sounds right.
#pragma once
#include <cstdint>
#include <cstdlib>
namespace harmony {
enum { POP, FIFTIES, TWO_FIVE_ONE, CIRCLE, TWO, WALK, COUNT };
inline const char* name(int p) { static const char* n[COUNT] = {"POP", "50S", "2-5-1", "CIRCLE", "TWO", "WALK"}; return n[p < 0 ? 0 : p >= COUNT ? COUNT - 1 : p]; }
inline const char* numeral(int degree) { static const char* r[8] = {"I", "II", "III", "IV", "V", "VI", "VII", "I"}; return r[((degree % 7) + 7) % 7]; }
// the scale degree (0 = the root) of the chord in bar `bar`; WALK is four chords from the seed, starting at the root
inline int degreeAt(int prog, int bar, uint32_t seed) {
    static const int seq[COUNT - 1][8] = {{0, 4, 5, 3, 0, 4, 5, 3}, {0, 5, 3, 4, 0, 5, 3, 4}, {1, 4, 0, 0, 1, 4, 0, 0}, {0, 3, 6, 2, 5, 1, 4, 0}, {0, 3, 0, 3, 0, 3, 0, 3}};
    bar = ((bar % 8) + 8) % 8;
    if (prog == WALK) { if (bar % 4 == 0) return 0; uint32_t s = seed * 2654435761u + (uint32_t)(bar % 4) * 40503u; s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (int)(s % 7u); }
    return seq[prog < 0 ? 0 : prog >= COUNT - 1 ? COUNT - 2 : prog][bar];
}
// the chord's semitones above the scale root (triad, or the seventh too), for a scale of n steps sc[]
inline int tones(const int* sc, int n, int degree, bool seventh, int* out) {
    const int c = seventh ? 4 : 3;
    for (int i = 0; i < c; i++) { const int d = degree + 2 * i; out[i] = (sc[d % n] + 12 * (d / n)) % 12; }
    return c;
}
// the note nearest to `note` (ties go down) whose pitch class is a tone of the chord (root = the scale's root, pitch class 0..11)
inline int snapToChord(int note, const int* pcs, int count, int root) {
    for (int off = 0; off < 12; off++)
        for (int sgn : {-1, 1}) {
            const int n = note + sgn * off, pc = (((n - root) % 12) + 12) % 12;
            for (int i = 0; i < count; i++) if (pcs[i] == pc && n >= 0 && n <= 127) return n;
        }
    return note;
}
}  // namespace harmony
