// The library's synth groups: what kind of sound a patch is, from its engine and its name ("Syn Bass", "Syn Leads", ...). The prefix keeps them apart from the sample folders.
#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include "Synth.h"
constexpr int kSynthCats = 10;
inline const char* synthCatName(int c) {
    static const char* n[kSynthCats] = {"Syn Bass", "Syn Leads", "Syn Pads", "Syn Keys", "Syn Bells", "Syn Organs", "Syn Brass", "Syn Kits", "Syn Animals", "Syn FX"};
    return n[c < 0 ? 0 : c >= kSynthCats ? kSynthCats - 1 : c];
}
inline int synthCategory(const SynthPatch& p) {
    if (isKitEngine(p.engine) || p.kit > 0) return 7;
    if (p.engine == 12) return 8;
    std::string n; for (const char* c = p.name; *c; c++) n += (char)std::tolower((unsigned char)*c);
    auto has = [&](std::initializer_list<const char*> ks) { for (const char* k : ks) if (n.find(k) != std::string::npos) return true; return false; };
    if (has({"bass", "303", "sub ", "acid", "rumble"})) return 0;
    if (has({"lead", "hoover", "whistle", "stab", "blip", "super saw", "square", "reso", "rave", "arp", "trompette", "shawm", "sync"})) return 1;
    if (has({"pad", "string", " str", " ens", "choir", "aah", "monks", "wraith", "space", "sweep", "fantasy", "fat poly", "dub chord", "vox", "whisper", "vocal", "drone"})) return 2;
    if (has({"bell", "marimba", "vibes", "glock", "kalimba", "steel drum", "gamelan", "wood block", "bowl", "xylo", "crotale", "tine", "koto"})) return 4;
    if (has({"organ", "harmonium", "cathedral", "gurdy", "bagpipe", "vielle", "wheel"})) return 5;
    if (has({"brass", "horn", "tuba", "flute"})) return 6;
    if (has({"pluck", "harp", "gtr", "banjo", "lute", "piano", "key", "clav", "ep"})) return 3;
    return 9;
}
