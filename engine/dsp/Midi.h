// Standard MIDI files: patterns out (one track per sequencer track, kPpq ticks per beat) and notes in.
#pragma once
#include "Sequencer.h"
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

struct MidiNote { int tick, len, note, vel; };
struct MidiSong { float bpm = 120.f; std::vector<std::vector<MidiNote>> tracks; };

namespace midifile {
inline void put32(std::string& s, uint32_t v) { for (int i = 3; i >= 0; i--) s += (char)(v >> (8 * i)); }
inline void put16(std::string& s, uint16_t v) { s += (char)(v >> 8); s += (char)v; }
inline void putVar(std::string& s, uint32_t v) {
    char b[5]; int n = 0; b[n++] = (char)(v & 0x7f);
    while ((v >>= 7) > 0) b[n++] = (char)((v & 0x7f) | 0x80);
    while (n > 0) s += b[--n];
}
}  // namespace midifile

// format 1: a tempo track, then one track per non-empty list of notes (channel = track index)
inline bool writeMidi(const std::string& path, const MidiSong& song) {
    using namespace midifile;
    std::string out; out += "MThd"; put32(out, 6); put16(out, 1);
    int used = 0; for (auto& t : song.tracks) used += !t.empty();
    put16(out, (uint16_t)(1 + used)); put16(out, (uint16_t)kPpq);
    auto chunk = [&](const std::string& body) { out += "MTrk"; put32(out, (uint32_t)body.size()); out += body; };
    { std::string t; const uint32_t us = (uint32_t)(60000000.0 / std::max(20.f, song.bpm));
      putVar(t, 0); t += (char)0xff; t += (char)0x51; t += (char)3; t += (char)(us >> 16); t += (char)(us >> 8); t += (char)us;
      putVar(t, 0); t += (char)0xff; t += (char)0x58; t += (char)4; t += (char)4; t += (char)2; t += (char)24; t += (char)8;   // 4/4
      putVar(t, 0); t += (char)0xff; t += (char)0x2f; t += (char)0; chunk(t); }
    int ch = 0;
    for (auto& notes : song.tracks) {
        if (notes.empty()) continue;
        struct Ev { int tick, order; uint8_t a, b; bool on; };
        std::vector<Ev> ev;
        for (auto& n : notes) { ev.push_back({n.tick, 1, (uint8_t)n.note, (uint8_t)std::max(1, std::min(127, n.vel)), true}); ev.push_back({n.tick + std::max(1, n.len), 0, (uint8_t)n.note, 0, false}); }
        std::stable_sort(ev.begin(), ev.end(), [](const Ev& x, const Ev& y) { return x.tick != y.tick ? x.tick < y.tick : x.order < y.order; });   // offs before ons on the same tick
        std::string t; int last = 0;
        for (auto& e : ev) { putVar(t, (uint32_t)(e.tick - last)); last = e.tick; t += (char)((e.on ? 0x90 : 0x80) | (ch & 15)); t += (char)e.a; t += (char)e.b; }
        putVar(t, 0); t += (char)0xff; t += (char)0x2f; t += (char)0; chunk(t);
        ch++;
    }
    std::ofstream f(path, std::ios::binary); if (!f) return false;
    f.write(out.data(), (std::streamsize)out.size()); return (bool)f;
}

// reads format 0 or 1; every track that has notes becomes one list (ticks converted to kPpq)
inline bool readMidi(const std::string& path, MidiSong& song) {
    std::ifstream f(path, std::ios::binary); if (!f) return false;
    std::string d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    size_t p = 0;
    auto r32 = [&](size_t at) { return (uint32_t)((uint8_t)d[at] << 24 | (uint8_t)d[at + 1] << 16 | (uint8_t)d[at + 2] << 8 | (uint8_t)d[at + 3]); };
    auto r16 = [&](size_t at) { return (int)((uint8_t)d[at] << 8 | (uint8_t)d[at + 1]); };
    if (d.size() < 14 || d.compare(0, 4, "MThd") != 0) return false;
    const int ntrk = r16(10), div = r16(12); p = 8 + r32(4);
    if (div <= 0 || (div & 0x8000)) return false;   // SMPTE time is not supported
    song = MidiSong(); bool tempo = false;
    for (int t = 0; t < ntrk && p + 8 <= d.size(); t++) {
        if (d.compare(p, 4, "MTrk") != 0) return false;
        size_t q = p + 8; const size_t end = std::min(d.size(), q + r32(p + 4)); p = end;
        int64_t tick = 0; int status = 0; std::vector<MidiNote> notes; int open[16][128]; for (auto& a : open) for (int& v : a) v = -1;
        std::vector<MidiNote> tmp;
        while (q < end) {
            uint32_t dt = 0; while (q < end) { const uint8_t b = (uint8_t)d[q++]; dt = dt << 7 | (b & 0x7f); if (!(b & 0x80)) break; }
            tick += dt;
            if (q >= end) break;
            uint8_t b = (uint8_t)d[q];
            if (b == 0xff) {   // meta
                if (q + 2 > end) break; const uint8_t type = (uint8_t)d[q + 1]; q += 2;
                uint32_t len = 0; while (q < end) { const uint8_t c = (uint8_t)d[q++]; len = len << 7 | (c & 0x7f); if (!(c & 0x80)) break; }
                if (type == 0x51 && len == 3 && q + 3 <= end && !tempo) { const uint32_t us = (uint8_t)d[q] << 16 | (uint8_t)d[q + 1] << 8 | (uint8_t)d[q + 2]; if (us > 0) { song.bpm = (float)(60000000.0 / us); tempo = true; } }
                q += len; continue;
            }
            if (b == 0xf0 || b == 0xf7) { q++; uint32_t len = 0; while (q < end) { const uint8_t c = (uint8_t)d[q++]; len = len << 7 | (c & 0x7f); if (!(c & 0x80)) break; } q += len; continue; }
            if (b & 0x80) { status = b; q++; }
            const int type = status & 0xf0, ch = status & 15;
            const int need = (type == 0xc0 || type == 0xd0) ? 1 : 2;
            if (q + need > end) break;
            const int a = (uint8_t)d[q], v = need > 1 ? (uint8_t)d[q + 1] : 0; q += need;
            const int kt = (int)(tick * kPpq / div);
            if (type == 0x90 && v > 0) { if (open[ch][a] < 0) { tmp.push_back({kt, 0, a, v}); open[ch][a] = (int)tmp.size() - 1; } }
            else if (type == 0x80 || (type == 0x90 && v == 0)) { const int i = open[ch][a]; if (i >= 0) { tmp[(size_t)i].len = std::max(1, kt - tmp[(size_t)i].tick); open[ch][a] = -1; } }
        }
        for (auto& n : tmp) if (n.len > 0) notes.push_back(n);
        if (!notes.empty()) song.tracks.push_back(std::move(notes));
    }
    return !song.tracks.empty();
}
