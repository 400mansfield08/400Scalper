#pragma once
// 400Scalper — lock-free structures shared between the audio thread and the UI.

#include <atomic>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <cmath>

namespace scalper {

// Decimated min/max history for the scope, one ring per band.
// Each entry covers kDecim samples: input min/max (pre-clip, latency aligned) and output min/max.
struct VizEntry { float inMin, inMax, outMin, outMax; };

class VizHistory
{
public:
    static constexpr int kDecim = 4;
    static constexpr int kBands = 4;

    void prepare (double sampleRate)
    {
        capacity = size_t (std::ceil (7.0 * sampleRate / kDecim));
        for (auto& r : rings) r.assign (capacity, VizEntry { 0, 0, 0, 0 });
        count.store (0);
        entryRate = sampleRate / kDecim;
    }

    // audio thread
    void push (const VizEntry* perBand)
    {
        if (capacity == 0) return;
        const uint64_t c = count.load (std::memory_order_relaxed);
        const size_t idx = size_t (c % capacity);
        for (int b = 0; b < kBands; ++b) rings[size_t (b)][idx] = perBand[b];
        count.store (c + 1, std::memory_order_release);
    }

    // UI thread: copies entries [from, now) for one band. Returns the new 'now'.
    // If 'from' is too old it is moved forward. 'maxEntries' limits the copy from the end.
    uint64_t read (int band, uint64_t from, size_t maxEntries, std::vector<VizEntry>& out, uint64_t& actualFrom) const
    {
        out.clear();
        const uint64_t now = count.load (std::memory_order_acquire);
        if (capacity == 0) { actualFrom = now; return now; }
        uint64_t start = from;
        const uint64_t safe = capacity > 1024 ? capacity - 1024 : capacity / 2;
        if (now > safe && start < now - safe) start = now - safe;
        if (now - start > maxEntries) start = now - maxEntries;
        if (start > now) start = now;
        const auto& ring = rings[size_t (std::clamp (band, 0, kBands - 1))];
        out.reserve (size_t (now - start));
        for (uint64_t i = start; i < now; ++i) out.push_back (ring[size_t (i % capacity)]);
        actualFrom = start;
        return now;
    }

    double rate() const { return entryRate; }

private:
    size_t capacity = 0;
    double entryRate = 12000.0;
    std::vector<VizEntry> rings[kBands];
    std::atomic<uint64_t> count { 0 };
};

// Latest input samples (mono, after In Gain) for the spectrum display.
class SpectrumTap
{
public:
    static constexpr int kSize = 4096;
    void push (float x)
    {
        const uint32_t p = pos.load (std::memory_order_relaxed);
        buf[p % kSize] = x;
        pos.store (p + 1, std::memory_order_release);
    }
    void read (float* out, int n) const   // newest n samples, oldest first
    {
        const uint32_t p = pos.load (std::memory_order_acquire);
        for (int i = 0; i < n; ++i) out[i] = buf[(p - uint32_t (n) + uint32_t (i)) % kSize];
    }
private:
    float buf[kSize] {};
    std::atomic<uint32_t> pos { 0 };
};

// Peak meters: audio thread raises, UI takes and resets.
struct PeakMeter
{
    std::atomic<float> peak { 0.0f };
    void raise (float v)
    {
        float cur = peak.load (std::memory_order_relaxed);
        while (v > cur && ! peak.compare_exchange_weak (cur, v, std::memory_order_relaxed)) {}
    }
    float take() { return peak.exchange (0.0f, std::memory_order_relaxed); }
};

} // namespace scalper
