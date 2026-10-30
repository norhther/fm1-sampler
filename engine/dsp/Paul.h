// Extreme time stretch ("Paulstretch"): overlapping windows of the source are turned into spectra, the phases are randomised, and the
// result is overlap-added, so the sound is smeared into an endless pad. One stream per voice.
#pragma once
#include <memory>
#include <vector>

class PaulStream {
public:
    PaulStream();
    ~PaulStream();
    // the stereo source (interleaved, `frames` long, must outlive the stream) and the window [from, to) in frames
    void start(const float* src, int frames, double from, double to, float stretch);
    // one stereo frame at `rate` frames of the stream per call. The stream is generated AHEAD of the reading position by stage(): the audio
    // thread calls it for at most one stream per block, so the chords of several voices (which start in the same block and so would all need their
    // next hop in the same block) do not pile up their FFTs. next() only generates by itself in an emergency (the stream ran dry).
    void next(double rate, float& l, float& r);
    double sourcePos() const { return srcPos_; }
    size_t lookahead() const { return avail_ > (size_t)readPos_ ? avail_ - (size_t)readPos_ : 0; }   // frames generated and not read yet
    bool ready() const { return lookahead() >= (size_t)HOP; }                                         // a full hop is there: the voice can start sounding
    void stage();                                                                                    // one half hop of work (one channel); two of them make a hop
    static unsigned long stagesDone();            // total of stage() calls so far, for the tests (how much work a block did)
    static constexpr int N = 4096, HOP = N / 2;   // 4096: a hop (two FFTs per channel) stays cheap enough for a phone's audio thread
private:
    void commitHop();
    struct Fft; std::unique_ptr<Fft> fft_;
    const float* src_ = nullptr; int frames_ = 0; double from_ = 0, to_ = 0, srcPos_ = 0; float stretch_ = 8.f;
    std::vector<float> fifo_, ola_, win_, hopOut_;
    size_t fifoW_ = 0; double readPos_ = 0; size_t avail_ = 0; uint32_t rng_ = 1; int hopStage_ = 0;   // hopStage_: the channel the next stage() works on
};
