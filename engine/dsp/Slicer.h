// Finds drum hits in a sample: the start frames of up to `count` slices, sorted. Equal slices when the sample has no clear onsets.
#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

// The energy bin `pb` is where the hit PEAKS; the hit starts earlier. Walk back down the attack to where it is under 12 % of the peak,
// then to the first frame that is really loud, back off 1 ms (so the click is kept) and settle on a zero crossing.
inline int onsetStart(const std::vector<float>& x, const std::vector<float>& e, int pb, int hop, int sr) {
    int b = pb; while (b > 0 && e[(size_t)b - 1] <= e[(size_t)b] && e[(size_t)b - 1] > 0.12f * e[(size_t)pb]) b--;
    const int frames = (int)(x.size() / 2), lo = std::max(0, (b - 1) * hop), hi = std::min(frames - 1, (pb + 2) * hop);
    float pk = 0.f; for (int i = lo; i < hi; i++) pk = std::max(pk, std::max(std::fabs(x[(size_t)i * 2]), std::fabs(x[(size_t)i * 2 + 1])));
    int s = lo; for (int i = lo; i < hi; i++) if (std::max(std::fabs(x[(size_t)i * 2]), std::fabs(x[(size_t)i * 2 + 1])) > 0.1f * pk) { s = i; break; }
    s = std::max(0, s - sr / 1000);
    for (int i = s, stop = std::max(0, s - sr / 500); i > stop; i--) if ((x[(size_t)i * 2] <= 0.f) != (x[(size_t)(i - 1) * 2] <= 0.f)) { s = i; break; }
    return s;
}

// `ends` (optional): where each hit has died away (never past the next start); left empty when the slices are equal parts.
inline std::vector<int> findSlices(const std::vector<float>& stereo, int frames, int sr, int count = 16, float sens = 1.f, std::vector<int>* ends = nullptr) {   // sens: 0.5 finds only the clear hits .. 2 finds the soft ones too
    std::vector<int> starts; if (ends) ends->clear();
    std::vector<float> e; int hop = 0;
    hop = std::max(64, sr / 200); const int bins = frames / hop;
    if (bins >= 8) {
        e.assign((size_t)bins, 0.f);
        float peak = 1e-6f;
        for (int b = 0; b < bins; b++) {
            float s = 0.f; for (int i = b * hop; i < (b + 1) * hop; i++) s += std::fabs(stereo[(size_t)i * 2]) + std::fabs(stereo[(size_t)i * 2 + 1]);
            e[(size_t)b] = s / (2.f * hop); peak = std::max(peak, e[(size_t)b]);
        }
        std::vector<std::pair<float, int>> cand;   // onset strength, bin
        for (int b = 2; b < bins - 1; b++) {
            float avg = 0.f; int k = 0; for (int j = std::max(0, b - 12); j < b - 1; j++) { avg += e[(size_t)j]; k++; }
            avg = avg / std::max(1, k) + 0.01f * peak;
            const float ratio = e[(size_t)b] / avg;
            if (ratio > 2.5f / std::max(0.25f, sens) && e[(size_t)b] > 0.05f * peak / std::max(0.25f, sens) && e[(size_t)b] >= e[(size_t)b - 1] && e[(size_t)b] >= e[(size_t)b + 1]) cand.push_back({ratio * e[(size_t)b], b});
        }
        std::sort(cand.begin(), cand.end(), [](auto& a, auto& b) { return a.first > b.first; });
        const int minGap = std::max(1, sr / 15 / hop);   // 65 ms
        for (auto& c : cand) {
            if ((int)starts.size() >= count) break;
            bool near = false; for (int s : starts) if (std::abs(s / hop - c.second) < minGap) near = true;
            if (!near) starts.push_back(onsetStart(stereo, e, c.second, hop, sr));
        }
        std::sort(starts.begin(), starts.end());
        starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
        if (starts.size() >= 2 && starts[0] > hop * 4) starts.insert(starts.begin(), 0);
    }
    if (starts.size() < 2) { starts.clear(); for (int i = 0; i < count; i++) starts.push_back((int)((int64_t)frames * i / count)); return starts; }
    if (ends) {   // a chop ends when its energy has stayed under 5 % of its own peak for 15 ms, or at the next start
        for (size_t i = 0; i < starts.size(); i++) {
            const int next = i + 1 < starts.size() ? starts[i + 1] : frames, b0 = starts[i] / hop, b1 = std::min((int)e.size(), std::max(b0 + 1, next / hop));
            float pk = 0.f; for (int b = b0; b < b1; b++) pk = std::max(pk, e[(size_t)b]);
            int end = next;
            for (int b = b0; b + 2 < b1; b++) if (e[(size_t)b] < 0.05f * pk && e[(size_t)b + 1] < 0.05f * pk && e[(size_t)b + 2] < 0.05f * pk) { end = std::min(next, (b + 2) * hop); break; }
            ends->push_back(std::max(starts[i] + hop, end));
        }
    }
    return starts;
}
