#include "Sampler.h"
#include "Slicer.h"
#include "Wav.h"
#include <cstdlib>
#include <cstring>

Sampler::Sampler(int engineRate) : sr_(engineRate) {
    for (int i = 0; i < kVoices; i++) paul_.emplace_back(new PaulStream);
}
Sampler::~Sampler() = default;

// the pitch of a sample as a MIDI note (autocorrelation of a mono window after the attack), 0 when it has none (drums, noise)
float Sampler::detectPitch(const std::vector<float>& st, int frames, int sr) {
    const int W = 2048;
    if (frames < W * 3) return 0.f;
    int best0 = frames / 20; float pkv = 0.f;   // the loudest stretch between 5 % and 60 % of the sample
    for (int a = frames / 20; a + 2 * W < frames * 3 / 5; a += W / 2) { float e = 0.f; for (int i = 0; i < W; i += 4) e += std::fabs(st[2 * (size_t)(a + i)]); if (e > pkv) { pkv = e; best0 = a; } }
    std::vector<float> x((size_t)W * 2); for (int i = 0; i < W * 2; i++) x[(size_t)i] = 0.5f * (st[2 * (size_t)(best0 + i)] + st[2 * (size_t)(best0 + i) + 1]);
    const int lmin = std::max(8, sr / 2000), lmax = std::min(W, sr / 40);
    std::vector<float> c((size_t)lmax + 2, 0.f); double e0 = 0; for (int i = 0; i < W; i++) e0 += (double)x[(size_t)i] * x[(size_t)i];
    if (e0 < 1e-6) return 0.f;
    for (int lag = lmin; lag <= lmax; lag++) { double s = 0, e1 = 0; for (int i = 0; i < W; i++) { s += (double)x[(size_t)i] * x[(size_t)(i + lag)]; e1 += (double)x[(size_t)(i + lag)] * x[(size_t)(i + lag)]; } c[(size_t)lag] = (float)(s / std::sqrt(e0 * e1 + 1e-12)); }
    float top = 0.f; for (int lag = lmin; lag <= lmax; lag++) top = std::max(top, c[(size_t)lag]);
    if (top < 0.8f) return 0.f;
    int lagBest = 0;   // the first strong peak (not a multiple of the period)
    for (int lag = lmin + 1; lag < lmax; lag++) if (c[(size_t)lag] > top * 0.92f && c[(size_t)lag] >= c[(size_t)lag - 1] && c[(size_t)lag] >= c[(size_t)lag + 1]) { lagBest = lag; break; }
    if (lagBest == 0) return 0.f;
    const float y0 = c[(size_t)lagBest - 1], y1 = c[(size_t)lagBest], y2 = c[(size_t)lagBest + 1], d = y0 - 2 * y1 + y2;
    const float lag = (float)lagBest + (std::fabs(d) > 1e-9f ? 0.5f * (y0 - y2) / d : 0.f);
    return 69.f + 12.f * std::log2((float)sr / lag / 440.f);
}

bool Sampler::loadSynth(int idx) {
    if (idx < 0 || idx >= kSynthCount) return false;
    const SynthPatch& p = kSynths[idx];
    // loudness: play the patch once and bring its peak to a sample-like level; the same render gives the overview
    SynthVoice sv; Adsr env; env.set(p.a, p.d, p.s, p.r, (float)sr_); env.gateOn(); const SynthMacros none;
    const int total = sr_ * 12 / 10, hold = sr_ * 8 / 10;
    std::vector<float> mono((size_t)total);
    float pk = 1e-4f;
    sv.start(p, 60, 1.f, (float)sr_, 1, none);
    for (int i = 0; i < total; i++) {
        if (i == hold) env.gateOff();
        float l, r; sv.next(0.f, none, l, r); const float e = env.next();
        mono[(size_t)i] = 0.5f * (l + r) * e; pk = std::max(pk, std::fabs(mono[(size_t)i]));
    }
    synthNorm_ = clampf(0.9f / pk, 0.05f, 8.f);
    peaks_.assign(512, 0.f);
    for (int b = 0; b < 512; b++) { const int a = (int)((long)total * b / 512), e = std::max(a + 1, (int)((long)total * (b + 1) / 512)); float m = 0.f; for (int i = a; i < e; i++) m = std::max(m, std::fabs(mono[(size_t)i])); peaks_[(size_t)b] = clampf(m * synthNorm_, 0.f, 1.f); }
    allOff(); orig_.reset(); proc_.reset(); synth_ = idx; label_ = p.name;
    setEnv(p.a, p.d, p.s, p.r);
    return true;
}

bool Sampler::load(const std::string& path) {
    if (path.rfind("synth:", 0) == 0) return loadSynth(std::atoi(path.c_str() + 6));
    auto buf = std::make_shared<SampleBuf>();
    if (!readWav(path, buf->d, buf->sr)) return false;
    buf->frames = (int)(buf->d.size() / 2);
    if (buf->frames < 64) return false;
    float pk = 0.f; for (float v : buf->d) pk = std::max(pk, std::fabs(v));
    if (pk > 1e-4f) { const float g = std::min(8.f, 0.9f / pk); for (float& v : buf->d) v *= g; }   // quiet files are brought up to a usable level
    peaks_.assign(512, 0.f);
    for (int b = 0; b < 512; b++) {
        const long a = (long)buf->frames * b / 512, e = std::max(a + 1, (long)buf->frames * (b + 1) / 512), step = std::max(1L, (e - a) / 64);
        float m = 0.f; for (long i = a; i < e; i += step) m = std::max(m, std::fabs(buf->d[(size_t)i * 2]));
        peaks_[(size_t)b] = m;
    }
    slices_ = findSlices(buf->d, buf->frames, buf->sr, 16);
    rootGuess_ = detectPitch(buf->d, buf->frames, buf->sr);
    const size_t slash = path.find_last_of("/\\"), dot = path.rfind('.');
    label_ = path.substr(slash == std::string::npos ? 0 : slash + 1);
    if (dot != std::string::npos && dot > (slash == std::string::npos ? 0 : slash)) label_ = label_.substr(0, label_.rfind('.'));
    orig_ = buf;
    return true;
}

void Sampler::setMode(int m) { m = std::min(kModes - 1, std::max(0, m)); if (m != mode_) { allOff(); mode_ = m; } }
void Sampler::setEnv(float a, float d, float s, float r) { atk_ = a; dec_ = d; sus_ = s; rel_ = r; }
void Sampler::setRange(float a, float b) { rangeA_ = clampf(a, 0.f, 1.f); rangeB_ = clampf(b, 0.f, 1.f); if (rangeB_ < rangeA_ + 0.005f) rangeB_ = std::min(1.f, rangeA_ + 0.005f); }
void Sampler::setProcessed(std::shared_ptr<const SampleBuf> p, float a, float b) { allOff(); proc_ = std::move(p); procA_ = a; procB_ = std::max(a + 0.001f, b); }

void Sampler::resolveWindow(Voice& v, const SampleBuf*& buf, double& ws, double& we) const {
    if (mode_ == KIT || !proc_) {
        buf = orig_.get();
        if (mode_ == KIT) {
            const int n = (int)slices_.size(), i = v.slice % n;
            ws = slices_[(size_t)i]; we = i + 1 < n ? slices_[(size_t)i + 1] : buf->frames;
        } else { ws = rangeA_ * buf->frames; we = rangeB_ * buf->frames; }
    } else {
        buf = proc_.get();   // a stretched copy of [procA, procB]: the window is the same range inside it
        const double span = procB_ - procA_;
        ws = clampf((float)((rangeA_ - procA_) / span), 0.f, 1.f) * buf->frames; we = clampf((float)((rangeB_ - procA_) / span), 0.f, 1.f) * buf->frames;
    }
    we = std::min<double>(we, buf->frames - 2); ws = std::min(ws, we - 16);
    if (ws < 0) ws = 0;
}

void Sampler::noteOn(int note, int vel) {
    if (!orig_ && synth_ < 0) return;
    int vi = -1; uint32_t oldest = 0xffffffffu;
    // too many sounding voices (each Paulstretch voice costs FFTs: at most four): the oldest one fades out in a few ms instead of being cut
    int active = 0; for (auto& v : voices_) active += v.on && !v.fading;
    if (active >= (mode_ == PAUL ? 4 : 8)) {
        int victim = -1;
        for (int i = 0; i < kVoices; i++) if (voices_[i].on && !voices_[i].fading && voices_[i].born < oldest) { oldest = voices_[i].born; victim = i; }
        if (victim >= 0) fadeOut(voices_[victim]);
    }
    for (int i = 0; i < kVoices; i++) if (!voices_[i].on) { vi = i; break; }
    if (vi < 0) {   // every slot is busy (voices still fading): the oldest fading one, else the oldest of all, has to go
        oldest = 0xffffffffu;
        for (int i = 0; i < kVoices; i++) if (voices_[i].fading && voices_[i].born < oldest) { oldest = voices_[i].born; vi = i; }
        if (vi < 0) { oldest = 0xffffffffu; for (int i = 0; i < kVoices; i++) if (voices_[i].born < oldest) { oldest = voices_[i].born; vi = i; } }
    }
    Voice& v = voices_[vi];
    v = Voice();
    v.on = true; v.gate = true; v.note = note; v.vel = clampf(vel / 127.f, 0.f, 1.f); v.born = ++bornCounter_;
    v.slice = ((note - 60) % 16 + 16) % 16;
    if (synth_ >= 0) {
        sv_[vi].start(kSynths[synth_], note, v.vel, (float)sr_, bornCounter_, mac_);
        v.env.set(atk_, dec_, sus_, rel_, (float)sr_);
        if (kSynths[synth_].engine == 3) v.env.set(0.001f, 0.01f, 1.f, 0.02f, (float)sr_);   // a drum hit plays itself out
        v.env.gateOn();
        return;
    }
    resolveWindow(v, v.buf, v.ws, v.we);
    v.pos = v.ws;
    v.env.set(atk_, dec_, sus_, rel_, (float)sr_);
    if (mode_ == ONESHOT) v.env.set(0.002f, 0.01f, 1.f, 0.01f, (float)sr_);
    v.env.gateOn();
    if (mode_ == PAUL) { v.paulIdx = vi; paul_[(size_t)vi]->start(v.buf->d.data(), v.buf->frames, v.ws, v.we, 8.f); }
}
void Sampler::fadeOut(Voice& v) {
    v.gate = false; v.fading = true;
    v.env.relCoef = std::exp(-4.6f / (0.008f * (float)sr_));   // 8 ms
    v.env.gateOff();
}
void Sampler::noteOff(int note) {
    if (mode_ == ONESHOT && synth_ < 0) return;   // always plays out
    if (synth_ >= 0 && kSynths[synth_].engine == 3) return;   // so does a drum hit
    for (auto& v : voices_) if (v.on && v.gate && v.note == note) { v.gate = false; v.env.gateOff(); }
}
void Sampler::allOff() { for (auto& v : voices_) v = Voice(); }
bool Sampler::hasVoices() const { for (auto& v : voices_) if (v.on) return true; return false; }

void Sampler::pumpPaul() {
    // the stream with the least data first; one half hop per block, two when a stream is getting low, more only when one is about to run dry
    for (int done = 0; done < 4; done++) {
        PaulStream* worst = nullptr; size_t low = ~(size_t)0;
        for (auto& v : voices_) if (v.on && v.paulIdx >= 0) { PaulStream& ps = *paul_[(size_t)v.paulIdx]; const size_t l = ps.lookahead(); if (l < 2 * PaulStream::HOP && l < low) { low = l; worst = &ps; } }
        if (!worst) return;
        const int allowed = low < 512 ? 4 : low < (size_t)PaulStream::HOP ? 2 : 1;
        if (done >= allowed) return;
        worst->stage();
    }
}

void Sampler::renderSynth(Voice& v, SynthVoice& sv, float* out, int n) {
    const float gain = 0.8f * v.vel * synthNorm_ * 0.9f;
    for (int i = 0; i < n; i++) {
        const float e = v.env.next();
        if (v.env.done() && !v.gate) { v.on = false; return; }
        float l, r; sv.next(bend_, mac_, l, r);
        if (sv.finished()) { v.on = false; return; }
        out[2 * i] += l * e * gain; out[2 * i + 1] += r * e * gain;
    }
}

void Sampler::render(float* out, int n) {
    if (synth_ >= 0) { for (int i = 0; i < kVoices; i++) if (voices_[i].on) renderSynth(voices_[i], sv_[i], out, n); return; }
    if (!orig_) return;
    if (mode_ == PAUL) pumpPaul();
    for (auto& v : voices_) if (v.on) renderVoice(v, out, n);
}

void Sampler::renderVoice(Voice& v, float* out, int n) {
    const float* d = v.buf->d.data(); const int frames = v.buf->frames;
    const double srcRate = (double)v.buf->sr / sr_;
    const double pitch = mode_ == KIT ? 1.0 : std::pow(2.0, (v.note - root_ + bend_) / 12.0);
    const double rate = srcRate * pitch;
    auto readAt = [&](double p, float& l, float& r) {
        const int i0 = std::min(frames - 2, std::max(0, (int)p)); const float f = (float)(p - i0);
        l = d[(size_t)i0 * 2] + (d[(size_t)i0 * 2 + 2] - d[(size_t)i0 * 2]) * f;
        r = d[(size_t)i0 * 2 + 1] + (d[(size_t)i0 * 2 + 3] - d[(size_t)i0 * 2 + 1]) * f;
    };
    const float gain = 0.8f * v.vel;
    for (int i = 0; i < n; i++) {
        float l = 0.f, r = 0.f;
        if (mode_ == PAUL && !v.paulReady) {   // a new voice is silent until its first hop is there (a few blocks at most)
            if (!paul_[(size_t)v.paulIdx]->ready()) continue;
            v.paulReady = true;
        }
        const float e = v.env.next();
        if (v.env.done() && !v.gate) { v.on = false; return; }
        if (mode_ == PAUL) {
            PaulStream& ps = *paul_[(size_t)v.paulIdx];
            ps.next(pitch, l, r);
            v.pos = ps.sourcePos();
        } else if (mode_ == GRAIN) {
            v.spawn -= 1.0;
            if (v.spawn <= 0.0) {
                for (auto& g : v.grain) if (!g.on) {
                    g.on = true; g.age = 0; g.len = (int)(sr_ * (0.06f + 0.12f * rnd()));
                    const float k = rnd(); const double mult = k < 0.15 ? 0.5 : k < 0.3 ? 2.0 : 1.0;
                    g.inc = rate * mult * (1.0 + (rnd() - 0.5) * 0.004) * (rnd() < 0.2f ? -1.0 : 1.0);
                    const double span = std::max(1.0, v.we - v.ws - g.len * std::fabs(g.inc));
                    g.pos = g.inc > 0 ? v.ws + rnd() * span : v.we - rnd() * span;
                    break;
                }
                v.spawn = sr_ * 0.018 * (0.6 + 0.8 * rnd());
            }
            for (auto& g : v.grain) {
                if (!g.on) continue;
                if (g.age >= g.len || g.pos < 1 || g.pos >= frames - 2) { g.on = false; continue; }
                const float w = 0.5f - 0.5f * std::cos(2.f * kPi * (g.age + 0.5f) / g.len);
                float gl, gr; readAt(g.pos, gl, gr); l += gl * w; r += gr * w;
                g.pos += g.inc; g.age++;
            }
            l *= 0.5f; r *= 0.5f;
        } else {
            double p = v.pos;
            if (mode_ == LOOPED) {
                const double xf = std::min(256.0, (v.we - v.ws) * 0.25);
                readAt(p, l, r);
                if (p >= v.we - xf) { const double t = (p - (v.we - xf)) / xf; float l2, r2; readAt(v.ws + (p - (v.we - xf)), l2, r2); l = l * (float)(1 - t) + l2 * (float)t; r = r * (float)(1 - t) + r2 * (float)t; }
                v.pos += rate;
                if (v.pos >= v.we) v.pos = v.ws + xf + (v.pos - v.we);
            } else {
                if (p >= v.we) { v.on = false; return; }
                readAt(p, l, r);
                const float tail = (float)std::min(1.0, (v.we - p) / (128.0 * rate));   // short fade at the end of the window
                l *= tail; r *= tail;
                v.pos += rate;
            }
        }
        out[2 * i] += l * e * gain; out[2 * i + 1] += r * e * gain;
    }
}

float Sampler::orginalPosOf(const Voice& v) const {
    if (!orig_) return 0.f;
    double pos = v.pos;
    if (v.buf == proc_.get() && proc_) pos = (procA_ + pos / std::max(1, v.buf->frames) * (procB_ - procA_)) * orig_->frames;
    else if (v.buf && v.buf->frames != orig_->frames) pos = pos / v.buf->frames * orig_->frames;
    return clampf((float)(pos / orig_->frames), 0.f, 1.f);
}

int Sampler::voicePositions(float* out, int maxN) const {
    int n = 0;
    if (synth_ >= 0) return 0;
    for (auto& v : voices_) {
        if (!v.on || n >= maxN) continue;
        if (mode_ == GRAIN) {
            for (auto& g : v.grain) if (g.on && n < maxN) {
                Voice tmp = v; tmp.pos = g.pos; out[n++] = orginalPosOf(tmp);
            }
        } else out[n++] = orginalPosOf(v);
    }
    return n;
}
