// Tombola (after the OP-1 sequencer): balls bounce around inside a turning polygon and every hit on a wall plays the ball's note.
// Units: the drum has radius 1, time in seconds. The physics runs on the audio thread (a few sub steps per block); the UI reads a snapshot.
#pragma once
#include "Common.h"
#include <vector>

class Tombola {
public:
    static constexpr int kMaxBalls = 18;
    struct Ball { int track, note; float x, y, vx, vy, r, hit, cool; };
    struct Hit { int track, note, vel, len; float pan; };   // len in ms

    float spin = 0.8f, grav = 0.55f, mass = 0.72f, shape = 0.6f;   // rad/s, 0..1, 0..1, 0..1 (3..8 sides)
    int sides() const { return 3 + (int)std::lround(shape * 5.f); }
    float angle() const { return theta_; }
    void turn(float rad) { theta_ += rad; }
    const std::vector<Ball>& balls() const { return b_; }
    void clear() { b_.clear(); }

    void add(int track, int note, uint32_t seed) {
        if ((int)b_.size() >= kMaxBalls) b_.erase(b_.begin());
        rng_ ^= seed * 2654435761u + 0x9e3779b9u;
        const float a = rnd() * 6.2831853f;
        b_.push_back({track, note, std::cos(a) * 0.2f, std::sin(a) * 0.2f - 0.3f, (rnd() - 0.5f) * 0.7f, 0.f, 0.03f, 0.f, 0.f});
    }

    // advance by `seconds`; the hits of this step are appended to `out`
    void run(float seconds, std::vector<Hit>& out) {
        const int N = 5; const float h = std::min(seconds, 1.f / 30.f) / N;
        for (int s = 0; s < N; s++) sub(h, out);
    }

private:
    float rnd() { rng_ = rng_ * 1664525u + 1013904223u; return (rng_ >> 8) / 16777216.f; }
    void sub(float dt, std::vector<Hit>& out) {
        const int n = sides();
        const float g = grav * 17.5f, e = 0.18f + mass * 0.78f, halfT = 0.012f, apo = std::cos(kPi / n), half = std::sin(kPi / n);
        theta_ += spin * dt;
        struct Seg { float ax, ay, bx, by; } segs[8];
        for (int k = 0; k < n; k++) {
            const float a = theta_ + k * 2.f * kPi / n, px = std::cos(a) * apo, py = std::sin(a) * apo, da = a + kPi / 2.f, dx = std::cos(da), dy = std::sin(da);
            segs[k] = {px - dx * half, py - dy * half, px + dx * half, py + dy * half};
        }
        for (int i = (int)b_.size() - 1; i >= 0; i--) {
            Ball& b = b_[(size_t)i];
            b.vy += g * dt; b.x += b.vx * dt; b.y += b.vy * dt;
            b.hit = std::max(0.f, b.hit - dt * 4.f); b.cool = std::max(0.f, b.cool - dt);
            if (std::hypot(b.x, b.y) > 2.4f) { b_.erase(b_.begin() + i); continue; }   // fell out through a gap: gone
            for (int k = 0; k < n; k++) {
                const Seg& sg = segs[k]; const float ex = sg.bx - sg.ax, ey = sg.by - sg.ay, L2 = ex * ex + ey * ey;
                const float u = clampf(((b.x - sg.ax) * ex + (b.y - sg.ay) * ey) / L2, 0.f, 1.f), px = sg.ax + ex * u, py = sg.ay + ey * u;
                float dx = b.x - px, dy = b.y - py, dist = std::hypot(dx, dy); const float lim = b.r + halfT;
                if (dist >= lim) continue;
                if (dist < 1e-6f) { dx = -ey; dy = ex; dist = std::hypot(dx, dy); }
                const float nx = dx / dist, ny = dy / dist;
                b.x = px + nx * lim; b.y = py + ny * lim;
                const float wvx = -spin * py, wvy = spin * px;   // the wall moves with the drum
                float rvx = b.vx - wvx, rvy = b.vy - wvy; const float vn = rvx * nx + rvy * ny;
                if (vn >= 0.f) continue;
                const float imp = (1.f + e) * vn;
                rvx -= imp * nx; rvy -= imp * ny; rvx *= 0.985f; rvy *= 0.985f;
                b.vx = wvx + rvx; b.vy = wvy + rvy;
                const float st = std::min(1.f, std::fabs(vn) / 5.3f);   // how hard it hit
                if (st > 0.06f && b.cool <= 0.f) { out.push_back({b.track, b.note, (int)(20 + st * 100), (int)(200 + st * 400), clampf(px, -1.f, 1.f)}); b.hit = 1.f; b.cool = 0.055f; }
            }
            for (int j = i - 1; j >= 0; j--) {   // ball against ball
                Ball& o = b_[(size_t)j]; const float dx = o.x - b.x, dy = o.y - b.y, dist = std::hypot(dx, dy), mn = b.r + o.r;
                if (dist <= 0.f || dist >= mn) continue;
                const float ux = dx / dist, uy = dy / dist, pen = (mn - dist) / 2.f;
                b.x -= ux * pen; b.y -= uy * pen; o.x += ux * pen; o.y += uy * pen;
                const float rel = (o.vx - b.vx) * ux + (o.vy - b.vy) * uy;
                if (rel < 0.f) { const float imp = -(1.f + e * 0.8f) * rel / 2.f; b.vx -= imp * ux; b.vy -= imp * uy; o.vx += imp * ux; o.vy += imp * uy; }
            }
        }
    }
    std::vector<Ball> b_; float theta_ = 0.f; uint32_t rng_ = 12345;
};
