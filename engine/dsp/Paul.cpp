#include "Paul.h"
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
extern "C" {
#include "kiss_fftr.h"
}

struct PaulStream::Fft {
    kiss_fftr_cfg fwd = nullptr, inv = nullptr;
    Fft() { fwd = kiss_fftr_alloc(PaulStream::N, 0, nullptr, nullptr); inv = kiss_fftr_alloc(PaulStream::N, 1, nullptr, nullptr); }
    ~Fft() { free(fwd); free(inv); }
};

namespace {
// random phases from a table: a cosine and a sine per bin cost more than the FFT around them
struct PhaseTable { float c[4096], s[4096]; PhaseTable() { for (int i = 0; i < 4096; i++) { c[i] = std::cos(6.2831853f * i / 4096.f); s[i] = std::sin(6.2831853f * i / 4096.f); } } };
const PhaseTable& phases() { static const PhaseTable t; return t; }
}  // namespace

PaulStream::PaulStream() : fft_(new Fft), fifo_((size_t)N * 4 * 2, 0.f), ola_((size_t)HOP * 2, 0.f), win_((size_t)N), hopOut_((size_t)HOP * 2, 0.f) {
    for (int i = 0; i < N; i++) win_[(size_t)i] = std::sin(3.14159265f * (i + 0.5f) / N);   // sine window: its square sums to 1 at 50 % overlap
}
PaulStream::~PaulStream() = default;

void PaulStream::start(const float* src, int frames, double from, double to, float stretch) {
    src_ = src; frames_ = frames; from_ = std::max(0.0, from); to_ = std::min((double)frames, to); stretch_ = stretch;
    if (to_ - from_ < N) { from_ = std::max(0.0, std::min(from_, (double)frames - N)); to_ = std::min((double)frames, from_ + N); }
    srcPos_ = from_; std::fill(fifo_.begin(), fifo_.end(), 0.f); std::fill(ola_.begin(), ola_.end(), 0.f);
    fifoW_ = 0; readPos_ = 0; avail_ = 0; hopStage_ = 0; rng_ = rng_ * 747796405u + 2891336453u;
}

namespace { std::atomic<unsigned long> g_stages{0}; }
unsigned long PaulStream::stagesDone() { return g_stages.load(std::memory_order_relaxed); }

void PaulStream::stage() {   // one channel of the next hop: window, FFT, random phases, inverse FFT, overlap-add into hopOut_
    g_stages.fetch_add(1, std::memory_order_relaxed);
    const int c = hopStage_;
    const PhaseTable& ph = phases();
    float tin[N]; kiss_fft_cpx spec[N / 2 + 1];
    const double span = to_ - from_;
    double p = srcPos_;
    for (int i = 0; i < N; i++, p += 1.0) {
        if (p >= to_) p -= span;   // (the window is read around the end of the range: it wraps)
        const int i0 = std::min(frames_ - 1, (int)p);
        tin[i] = src_[(size_t)i0 * 2 + (size_t)c] * win_[(size_t)i];
    }
    kiss_fftr(fft_->fwd, tin, spec);
    for (int k = 0; k <= N / 2; k++) {
        const float mag = std::sqrt(spec[k].r * spec[k].r + spec[k].i * spec[k].i);
        rng_ = rng_ * 1664525u + 1013904223u;
        const uint32_t idx = rng_ >> 20;
        spec[k].r = mag * ph.c[idx]; spec[k].i = mag * ph.s[idx];
    }
    spec[0].i = 0; spec[N / 2].i = 0;
    kiss_fftri(fft_->inv, spec, tin);
    for (int i = 0; i < N; i++) tin[i] *= win_[(size_t)i] * (1.f / N);
    float* out = &hopOut_[(size_t)c * HOP]; float* ola = &ola_[(size_t)c * HOP];
    for (int i = 0; i < HOP; i++) { out[i] = tin[i] + ola[i]; ola[i] = tin[i + HOP]; }
    if (++hopStage_ == 2) { hopStage_ = 0; commitHop(); }
}

void PaulStream::commitHop() {
    const size_t cap = fifo_.size() / 2;
    for (int i = 0; i < HOP; i++) { fifo_[(fifoW_ % cap) * 2] = hopOut_[(size_t)i]; fifo_[(fifoW_ % cap) * 2 + 1] = hopOut_[(size_t)HOP + (size_t)i]; fifoW_++; }
    avail_ += HOP;
    srcPos_ += HOP / stretch_; while (srcPos_ >= to_) srcPos_ -= (to_ - from_);
}

void PaulStream::next(double rate, float& l, float& r) {
    const size_t cap = fifo_.size() / 2;
    while (avail_ < (size_t)(readPos_ + 4)) { stage(); stage(); }   // emergency only: the audio thread did not get to top the stream up in time
    const size_t i0 = (size_t)readPos_; const float f = (float)(readPos_ - i0);
    const size_t a = (fifoW_ - avail_ + i0) % cap, b = (a + 1) % cap;
    l = fifo_[a * 2] + (fifo_[b * 2] - fifo_[a * 2]) * f; r = fifo_[a * 2 + 1] + (fifo_[b * 2 + 1] - fifo_[a * 2 + 1]) * f;
    readPos_ += rate;
    if (readPos_ >= 2048) { const size_t drop = (size_t)readPos_; avail_ -= drop; readPos_ -= (double)drop; }   // keep the read pointer near the start of the data
}
