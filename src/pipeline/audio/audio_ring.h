#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>

// SPSC (tek uretici / tek tuketici) lock-free ses ring'i. Uretici WASAPI capture
// supervisor thread'i (on_audio -> push), tuketici encode/output thread'i
// (drain -> consume -> AAC encode -> transport.send_audio). Boylece tum RTMP
// yazimlari tek thread'de kalir (kilit yok, mevcut invariant korunur).
//
// Doluluk politikasi: lock-free SPSC'de "en eski slot'u dusur" GUVENLI DEGIL
// (head yalniz tuketiciye ait). Bu yuzden uretici tarafinda DOLUYKEN EN YENIYI
// DUSUR + drop sayaci (dusuk gecikme > tam butunluk). Slot depolamasi heap'te
// (ctor'da bir kez) — object stack'te kucuk kalir; push/consume allocation-free.
//
// Header-only (yalniz STL) — dogrudan birim testlenebilir (tests/test_audio_ring.cpp).
namespace reji::pipeline::audio {

class AudioRing {
public:
    static constexpr uint32_t kCapacity           = 16;     // slot sayisi (2'nin kuvveti)
    static constexpr uint32_t kMaxSamplesPerChunk = 8192;   // 4096 cerceve stereo (~85ms)

    AudioRing() : slots_(std::make_unique<Slot[]>(kCapacity)) {}

    AudioRing(const AudioRing&)            = delete;
    AudioRing& operator=(const AudioRing&) = delete;

    /// Uretici (capture thread). Ornekleri kopyalar. Dolu / gecersiz / asiri-boyut
    /// durumunda false doner ve drop sayacini artirir (allocation-free).
    bool push(const float* samples, uint32_t frames, uint32_t channels,
              uint32_t sample_rate, int64_t pts_us) noexcept {
        const uint64_t n = static_cast<uint64_t>(frames) * channels;
        if (!samples || n == 0 || n > kMaxSamplesPerChunk) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        const uint64_t tail = tail_.load(std::memory_order_relaxed);
        const uint64_t head = head_.load(std::memory_order_acquire);
        if (tail - head >= kCapacity) {           // dolu -> en yeniyi dusur
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        Slot& s = slots_[tail & kMask];
        s.pts_us      = pts_us;
        s.frames      = frames;
        s.channels    = channels;
        s.sample_rate = sample_rate;
        s.count       = static_cast<uint32_t>(n);
        std::memcpy(s.data, samples, static_cast<size_t>(n) * sizeof(float));
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    /// Tuketici (encode thread). Bir chunk varsa fn(samples, frames, channels,
    /// sample_rate, pts_us) cagirir (slot fn suresince kararli), sonra ilerler.
    /// @return chunk tuketildiyse true; ring bossa false.
    template <class F>
    bool consume(F&& fn) noexcept {
        const uint64_t head = head_.load(std::memory_order_relaxed);
        const uint64_t tail = tail_.load(std::memory_order_acquire);
        if (head == tail) return false;           // bos
        Slot& s = slots_[head & kMask];
        fn(static_cast<const float*>(s.data), s.frames, s.channels,
           s.sample_rate, s.pts_us);
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    uint32_t dropped() const noexcept { return dropped_.load(std::memory_order_relaxed); }

    uint32_t size() const noexcept {
        return static_cast<uint32_t>(
            tail_.load(std::memory_order_acquire) - head_.load(std::memory_order_acquire));
    }

    bool empty() const noexcept { return size() == 0; }

private:
    static constexpr uint64_t kMask = kCapacity - 1;

    struct Slot {
        int64_t  pts_us      = 0;
        uint32_t frames      = 0;
        uint32_t channels    = 0;
        uint32_t sample_rate = 0;
        uint32_t count       = 0;
        float    data[kMaxSamplesPerChunk];
    };

    std::unique_ptr<Slot[]> slots_;
    std::atomic<uint64_t>   head_{0};   // yalniz tuketici yazar
    std::atomic<uint64_t>   tail_{0};   // yalniz uretici yazar
    std::atomic<uint32_t>   dropped_{0};
};

} // namespace reji::pipeline::audio
