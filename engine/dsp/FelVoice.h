// SPDX-License-Identifier: GPL-3.0-only
// Felucca-Engines (MIT) ANALOG / TRIO / LOFI / NOISE as one voice: engine 14 .. 17 of SynthVoice.
#pragma once
#include "FelAnalog.h"
#include "FelChip.h"
namespace fel {
class Voice {
public:
    void start(int kind, const float* x, float sr, uint32_t seed) {
        kind_ = kind;
        switch (kind) { case 0: an_.start(x, sr, seed); break; case 1: tr_.start(x, sr, seed); break; case 2: lo_.start(x, sr, seed); break; default: no_.start(x, sr, seed); }
    }
    float next(float hz, float tone, float motion) {
        switch (kind_) { case 0: return an_.next(hz, tone, motion); case 1: return tr_.next(hz, tone, motion); case 2: return lo_.next(hz, tone, motion); default: return no_.next(hz, tone, motion); }
    }
private:
    int kind_ = 0; Analog an_; Trio tr_; Lofi lo_; Noise no_;
};
}  // namespace fel
