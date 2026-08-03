// src/pipeline/include/send_diag.h
//
// RTMP_DARBOGAZ Faz 1: frame thread gönderim teşhisi — saf 1Hz agregatör.
//
// Amaç: capture→encode→RTMP_Write→audio-drain zincirinin her halkasının süresini
// ve "hatta gerçekten yazılan kare" sayısını (wire-fps) saniyelik pencerede
// toplayıp tek stderr satırına indirger. Kayıp noktası hipotezlerini (H1 send
// bloklaması / H2 WGC teslimi / H3 encode) ayrıştırmanın veri kaynağıdır.
// Bulgu bağlamı: ffmpeg dinleyicisi ~20fps ölçerken iç fps metriği ~59 gösterir —
// o metrik döngü temposu sayar (null iterasyon dahil); teslim hiçbir yerde
// görünmez. wire-fps (n_sendv_ok) bu boşluğu kapatır.
//
// Saf/test-edilebilir: yalnız <cstdint>/<string> — QPC/D3D/RTMP bilmez; çağıran
// süreleri mikrosaniye olarak verir. Tek thread (frame thread) varsayımı —
// kilit yok (bkz. pipeline.cpp run_frame: capture/encode/send aynı thread).
// p99 raporlanmaz: pencere başına ≤~60 örnekte p99 ≈ max — max yeterli sinyal.
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>

namespace rj {

// Bir 1sn penceresinin toplulaştırılmış istatistikleri (süreler mikrosaniye).
// Faz 2 ekleri: capn (null-iterasyon capture — TryGetNextFrame poll maliyeti,
// timeout-bekleme hipotezinin verisi), d3d (d3d11_frame_cb bloğu, gpu_sub_
// interop dahil), prev (emit_wgc_preview / DXGI map preview), tot (run_frame
// iş toplamı, pace HARİÇ), pace (FramePacer bekleme — 60Hz temposunun kaynağı).
// cap+enc+d3d+prev toplamı ile tot farkı = hâlâ ölçülmeyen bölge.
struct SendDiagStats {
    uint32_t cap_avg_us   = 0;  // next_frame() süresi — yalnız tex'li iterasyonlar
    uint32_t cap_max_us   = 0;
    uint32_t capn_avg_us  = 0;  // next_frame() süresi — null iterasyonlar
    uint32_t capn_max_us  = 0;
    uint32_t enc_avg_us   = 0;  // encode_frame() süresi (senkron send DAHİL)
    uint32_t enc_max_us   = 0;
    // Faz 3 enc mikro-split: enc ≈ encp + lock + ecb (+µs sapma). lock senkron
    // NVENC'in gerçek encode beklemesi; ecb on_packet (sendV+audioDrain dahil).
    uint32_t encp_avg_us  = 0;  // nvEncEncodePicture (map+submit+unmap)
    uint32_t encp_max_us  = 0;
    uint32_t lock_avg_us  = 0;  // nvEncLockBitstream bloklu beklemesi
    uint32_t lock_max_us  = 0;
    uint32_t ecb_avg_us   = 0;  // on_packet callback toplamı
    uint32_t ecb_max_us   = 0;
    uint32_t d3d_avg_us   = 0;  // d3d11_frame_cb bloğu (get_frame_images + cb)
    uint32_t d3d_max_us   = 0;
    uint32_t prev_avg_us  = 0;  // preview yolu (DXGI map / emit_wgc_preview)
    uint32_t prev_max_us  = 0;
    uint32_t tot_avg_us   = 0;  // run_frame iş toplamı (pace hariç)
    uint32_t tot_max_us   = 0;
    uint32_t pace_avg_us  = 0;  // pacer_.pace() süresi
    uint32_t pace_max_us  = 0;
    uint32_t sendv_avg_us = 0;  // OutputSubsystem::send (video RTMP_Write)
    uint32_t sendv_max_us = 0;
    uint32_t senda_avg_us = 0;  // audio_bridge_.drain (AAC encode + send_audio)
    uint32_t senda_max_us = 0;
    uint32_t n_frames     = 0;  // tex'li iterasyon sayısı
    uint32_t n_null       = 0;  // "yeni kare yok" iterasyonu (S1-ek4: drop değil)
    uint32_t n_prev_miss  = 0;  // Faz 3(c): DO_NOT_WAIT map hazır değildi — atlandı
    uint32_t n_sendv_ok   = 0;  // wire-fps: başarıyla yazılan video karesi/sn
    uint32_t n_sendv_fail = 0;
    uint32_t n_senda      = 0;
};

class SendDiag {
public:
    void begin_window(uint64_t now_us) { window_start_us_ = now_us; }

    void record_capture(uint32_t us, bool had_frame) {
        // Faz 2: null/tex ayrı toplanır — null'un ~0 çıkması TryGetNextFrame'in
        // beklemeden döndüğünün kanıtı; karışık avg bu sinyali gizliyordu.
        if (had_frame) { cap_.add(us); ++n_frames_; }
        else           { capn_.add(us); ++n_null_; }
    }
    void record_encode(uint32_t us)  { enc_.add(us); }
    // Faz 3: enc mikro-split — encode_frame sonrası NvencEncoder::last_timings()
    // ile beslenir (tek thread; encoder yoksa çağrılmaz).
    void record_enc_split(uint32_t encp_us, uint32_t lock_us, uint32_t ecb_us) {
        encp_.add(encp_us); lock_.add(lock_us); ecb_.add(ecb_us);
    }
    void record_preview_miss() { ++n_prev_miss_; }
    void record_interop(uint32_t us) { d3d_.add(us); }   // d3d11_frame_cb bloğu
    void record_preview(uint32_t us) { prev_.add(us); }  // preview yolu
    void record_total(uint32_t us)   { tot_.add(us); }   // run_frame işi (pace hariç)
    void record_pace(uint32_t us)    { pace_.add(us); }
    void record_send_video(uint32_t us, bool ok) {
        sendv_.add(us);
        if (ok) ++n_sendv_ok_; else ++n_sendv_fail_;
    }
    void record_audio_drain(uint32_t us) { senda_.add(us); }

    // Pencere (1sn) dolduysa `out`u doldurur, pencereyi sıfırlar, true döner.
    bool maybe_flush(uint64_t now_us, SendDiagStats* out) {
        if (now_us - window_start_us_ < kWindowUs) return false;
        out->cap_avg_us   = cap_.avg();
        out->cap_max_us   = cap_.max_us;
        out->capn_avg_us  = capn_.avg();
        out->capn_max_us  = capn_.max_us;
        out->enc_avg_us   = enc_.avg();
        out->enc_max_us   = enc_.max_us;
        out->encp_avg_us  = encp_.avg();
        out->encp_max_us  = encp_.max_us;
        out->lock_avg_us  = lock_.avg();
        out->lock_max_us  = lock_.max_us;
        out->ecb_avg_us   = ecb_.avg();
        out->ecb_max_us   = ecb_.max_us;
        out->d3d_avg_us   = d3d_.avg();
        out->d3d_max_us   = d3d_.max_us;
        out->prev_avg_us  = prev_.avg();
        out->prev_max_us  = prev_.max_us;
        out->tot_avg_us   = tot_.avg();
        out->tot_max_us   = tot_.max_us;
        out->pace_avg_us  = pace_.avg();
        out->pace_max_us  = pace_.max_us;
        out->sendv_avg_us = sendv_.avg();
        out->sendv_max_us = sendv_.max_us;
        out->senda_avg_us = senda_.avg();
        out->senda_max_us = senda_.max_us;
        out->n_frames     = n_frames_;
        out->n_null       = n_null_;
        out->n_prev_miss  = n_prev_miss_;
        out->n_sendv_ok   = n_sendv_ok_;
        out->n_sendv_fail = n_sendv_fail_;
        out->n_senda      = senda_.count;
        reset(now_us);
        return true;
    }

private:
    static constexpr uint64_t kWindowUs = 1'000'000;

    struct Acc {
        uint64_t sum_us = 0;
        uint32_t max_us = 0;
        uint32_t count  = 0;
        void add(uint32_t us) {
            sum_us += us;
            if (us > max_us) max_us = us;
            ++count;
        }
        uint32_t avg() const {
            return count ? static_cast<uint32_t>(sum_us / count) : 0u;
        }
    };

    void reset(uint64_t now_us) {
        cap_ = {}; capn_ = {}; enc_ = {}; sendv_ = {}; senda_ = {};
        d3d_ = {}; prev_ = {}; tot_ = {}; pace_ = {};
        encp_ = {}; lock_ = {}; ecb_ = {};
        n_frames_ = n_null_ = n_prev_miss_ = n_sendv_ok_ = n_sendv_fail_ = 0;
        window_start_us_ = now_us;
    }

    uint64_t window_start_us_ = 0;
    Acc cap_, capn_, enc_, sendv_, senda_;
    Acc d3d_, prev_, tot_, pace_;
    Acc encp_, lock_, ecb_;
    uint32_t n_frames_ = 0, n_null_ = 0, n_prev_miss_ = 0;
    uint32_t n_sendv_ok_ = 0, n_sendv_fail_ = 0;
};

// Stats → tek satır insan-okur teşhis formatı ("[SendDiag] ...").
// Süreler ms (bir ondalık): teşhiste 16.7ms kare bütçesiyle doğrudan kıyas için.
inline std::string format_send_diag(const SendDiagStats& s) {
    char buf[512];
    auto ms = [](uint32_t us) { return static_cast<double>(us) / 1000.0; };
    std::snprintf(
        buf, sizeof(buf),
        "[SendDiag] wire_fps=%u frames=%u null=%u "
        "cap=%.1f/%.1fms capNull=%.1f/%.1fms enc=%.1f/%.1fms "
        "encP=%.1f/%.1fms lock=%.1f/%.1fms encCb=%.1f/%.1fms "
        "d3d=%.1f/%.1fms prev=%.1f/%.1fms(miss=%u) sendV=%.1f/%.1fms(fail=%u) "
        "audioDrain=%.1f/%.1fms(n=%u) tot=%.1f/%.1fms pace=%.1f/%.1fms",
        s.n_sendv_ok, s.n_frames, s.n_null,
        ms(s.cap_avg_us), ms(s.cap_max_us),
        ms(s.capn_avg_us), ms(s.capn_max_us),
        ms(s.enc_avg_us), ms(s.enc_max_us),
        ms(s.encp_avg_us), ms(s.encp_max_us),
        ms(s.lock_avg_us), ms(s.lock_max_us),
        ms(s.ecb_avg_us), ms(s.ecb_max_us),
        ms(s.d3d_avg_us), ms(s.d3d_max_us),
        ms(s.prev_avg_us), ms(s.prev_max_us), s.n_prev_miss,
        ms(s.sendv_avg_us), ms(s.sendv_max_us), s.n_sendv_fail,
        ms(s.senda_avg_us), ms(s.senda_max_us), s.n_senda,
        ms(s.tot_avg_us), ms(s.tot_max_us),
        ms(s.pace_avg_us), ms(s.pace_max_us));
    return buf;
}

}  // namespace rj
