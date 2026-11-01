// SPDX-License-Identifier: GPL-3.0-only
// Note generators, small ports of the ideas in Tessera (GPL-3.0, Theo Dainis: gen.c, from Turing Machine, Euclidean rhythms, norns' Awake and
// Bounce, EuroPi's Poly) and X0X's TB-3PO acid line generator (schwung-tb3po): each fills a pattern from a seed in the current scale.
// Pure functions: the same seed gives the same pattern.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
namespace patgen {
struct Note { int tick, len, note, vel; };
enum { TURING, EUCLID, MARKOV, MORPH, AWAKE, POLY, BOUNCE, ACID, COUNT };
inline const char* name(int k) { static const char* n[COUNT] = {"TURING", "EUCLID", "MARKOV", "MORPH", "AWAKE", "POLY", "BOUNCE", "ACID"}; return n[((k % COUNT) + COUNT) % COUNT]; }

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed * 2654435761u + 0x9E3779B9u) { if (!s) s = 1; next(); next(); }
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    int below(int n) { return n <= 1 ? 0 : (int)(next() % (uint32_t)n); }
    bool chance(int pct) { return below(100) < pct; }
};

// degree d of the scale (sc[0..n-1] semitones), d may span octaves; around C3 + root
inline int pitch(int d, const int* sc, int n, int root, int base = 48) {
    const int o = d >= 0 ? d / n : -((-d + n - 1) / n);
    return std::min(127, std::max(0, base + root + 12 * o + sc[d - o * n]));
}

inline std::vector<Note> generate(int kind, int steps, int st, const int* sc, int n, int root, uint32_t seed) {
    std::vector<Note> out; Rng r(seed + (uint32_t)kind * 7919u);
    if (n < 1) return out;
    const int len = st * 3 / 4;
    auto put = [&](int i, int deg, int vel, int l = -1) { if ((int)out.size() < 256) out.push_back({i * st, l < 0 ? len : l, pitch(deg, sc, n, root), std::min(127, std::max(1, vel))}); };
    auto vel = [&] { return 70 + r.below(50); };
    switch (((kind % COUNT) + COUNT) % COUNT) {
        case TURING: {   // a shift register that recirculates its bit and sometimes flips it: a melody that repeats and slowly changes
            const int L = 5 + r.below(12); uint32_t reg = r.next() & 0xFFFFu; if (!reg) reg = 0xA5A5u;
            for (int i = 0; i < steps; i++) {
                uint32_t bit = reg & 1u; if (r.chance(25)) bit ^= 1u;
                reg = (reg >> 1) | (bit << (L - 1)); reg &= (1u << L) - 1u; if (!reg) reg = 1;
                if (((reg >> 1) & 3u) == 0) continue;
                put(i, (int)((reg & 0xFFu) % (uint32_t)(n * 2)), vel());
            }
        } break;
        case EUCLID: {   // pulses spread as evenly as possible over 16 steps, rotated; each hit a fixed pitch from a short pattern
            const int cyc = 16, p = 3 + r.below(6), rot = r.below(cyc); int deg[8]; for (int& d : deg) d = r.below(n * 2);
            int h = 0;
            for (int i = 0; i < steps; i++) { const int j = (i + rot) % cyc; if ((j * p) % cyc < p) { put(i, deg[h++ % 8], i % 4 == 0 ? 105 : vel()); } }
        } break;
        case MARKOV: {   // the next degree favours near ones; TENSION widens the leaps, DENSITY thins the steps
            const int tension = 20 + r.below(60), dens = 60 + r.below(40); int cur = r.below(n); const int top = n * 2;
            for (int i = 0; i < steps; i++) {
                if (!r.chance(dens)) continue;
                float w[48], tot = 0.f; for (int d = 0; d < top; d++) { w[d] = std::exp(-std::abs(d - cur) * (1.6f - tension / 100.f)); tot += w[d]; }
                float x = (float)(r.next() >> 8) / 16777216.f * tot; int d = 0; while (d < top - 1 && x > w[d]) x -= w[d++];
                cur = d; put(i, cur, vel());
            }
        } break;
        case MORPH: {   // two rhythms and a point between them: a step plays with the chance the crossfade gives it
            const uint32_t A = r.next() & 0xFFFFu, B = r.next() & 0xFFFFu; const int m = r.below(101); int cur = r.below(n);
            for (int i = 0; i < steps; i++) {
                const int a = (A >> (i & 15)) & 1, b = (B >> (i & 15)) & 1;
                if (!r.chance((a * (100 - m) + b * m) * 9 / 10 + 5)) continue;
                cur = std::min(n * 2 - 1, std::max(0, cur + r.below(5) - 2)); put(i, cur, vel());
            }
        } break;
        case AWAKE: {   // two loops of different lengths: A's value is the degree (0 a rest), B transposes it
            const int la = 5 + r.below(5), lb = 3 + r.below(5); int a[16], b[16];
            for (int i = 0; i < 16; i++) { a[i] = r.chance(25) ? 0 : 1 + r.below(8); b[i] = r.below(8); }
            for (int i = 0; i < steps; i++) { const int v = a[i % la]; if (!v || !r.chance(90)) continue; put(i, (v - 1 + b[i % lb]) % (n * 2), vel()); }
        } break;
        case POLY: {   // four clock dividers advance a four-step pitch sequence; dividers that land together play once, louder
            int div[4], p[4], pos = 0; for (int k = 0; k < 4; k++) { div[k] = 2 + r.below(8); p[k] = r.below(n * 2); }
            for (int i = 0; i < steps; i++) {
                int c = 0; for (int k = 0; k < 4; k++) if (i % div[k] == 0) c++;
                if (!c) continue;
                pos = (pos + c) % 4; put(i, p[pos], 70 + std::min(3, c) * 15);
            }
        } break;
        case BOUNCE: {   // a ball in a box: each wall it hits plays a note, from where along the wall
            int x = r.below(256), y = r.below(256), dx = (r.chance(50) ? 1 : -1) * (14 + r.below(30)), dy = (r.chance(50) ? 1 : -1) * (14 + r.below(30));
            for (int i = 0; i < steps; i++) {
                x += dx; y += dy; int along = -1;
                if (x < 0 || x > 255) { dx = -dx; x = std::min(255, std::max(0, x)); along = y; }
                if (y < 0 || y > 255) { dy = -dy; y = std::min(255, std::max(0, y)); along = x; }
                if (along >= 0) put(i, along * n * 2 / 256, vel());
            }
        } break;
        case ACID: {   // a 303 line: rests, octave jumps, accents (velocity 120) and slides (a note that runs into the next)
            for (int i = 0; i < steps; i++) {
                if (r.chance(22)) continue;
                const int o = r.chance(20) ? -1 : r.chance(25) ? 1 : 0, d = r.below(n);
                const bool acc = r.chance(26), slide = r.chance(20);
                put(i, d + o * n, acc ? 120 : 78, slide ? st + st / 4 : len);
            }
        } break;
    }
    return out;
}
}  // namespace patgen
