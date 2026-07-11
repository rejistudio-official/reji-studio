// src/pipeline/seh_filter.cpp
//
// Paylaşımlı SEH filtresi implementasyonu — V8/I10. Bkz. seh_filter.h.
//
// I10-a: SO/BP/SS pass-through + bağlam yakalama.
// I10-b: yakalanan istisna ERROR seviyesinde senkron loglanır (artık sessiz
//        değil — I10'un asıl şikayeti "teşhis imkânsızlığı" giderilir).
// I10-c: eskalasyon valfi (site-başı sayaç + __fastfail) eklenecek.
#include "seh_filter.h"

#include <cstdio>
#include <atomic>
#include <intrin.h>  // __fastfail

#ifdef _WIN32

namespace rj {

namespace {

// I10-c: site-başı AV sayacı — kilitsiz, tahsissiz. Bir AV filtresinde (olası
// bozuk heap/stack) kilit almak deadlock, heap'e dokunmak tehlikeli olurdu;
// bu yüzden derleme-zamanı sabit boyutlu atomik diziler. Her site pratikte
// tek thread'den (leaf'ler thread-yerel) çağrılır → tek-yazar, yarış zararsız.
std::atomic<uint64_t> g_av_window_ms[static_cast<std::size_t>(SehSite::Count)];
std::atomic<uint32_t> g_av_count[static_cast<std::size_t>(SehSite::Count)];

// SehSite → okunabilir isim (log için). Sıra enum ile birebir olmalı.
const char* site_name(SehSite site) noexcept {
    static const char* kNames[] = {
        "CmdDrain", "WsDequeue", "GetWsPort",
        "StartMonitor", "UninitCom", "ShutdownSubsys",
        "SrtSend", "MetricsPush", "ConnectionLost",
        "CopyOptWait", "CopyOptShutdown",
        "WasapiGetBuffer", "WasapiReleaseBuffer", "WasapiNextPacket",
        "WasapiAudioStop", "WasapiShutdownLeaf",
        "SrtLeafSendmsg", "SrtLeafClose", "SrtLeafEpollRelease",
        "SrtLeafSetsockopt", "SrtLeafGlobalCleanup",
    };
    static_assert(sizeof(kNames) / sizeof(kNames[0]) ==
                      static_cast<std::size_t>(SehSite::Count),
                  "site_name tablosu SehSite ile senkron olmali");
    const std::size_t idx = static_cast<std::size_t>(site);
    return idx < static_cast<std::size_t>(SehSite::Count) ? kNames[idx] : "?";
}

} // namespace

bool seh_register_av(SehSite site, uint64_t now_ms) noexcept {
    const std::size_t idx = static_cast<std::size_t>(site);
    if (idx >= static_cast<std::size_t>(SehSite::Count)) return false;

    const uint64_t start = g_av_window_ms[idx].load(std::memory_order_relaxed);
    uint32_t cnt;
    if (now_ms - start > kSehAvWindowMs) {
        // Pencere süresi doldu (veya ilk çağrı: start==0) → yeni pencere.
        g_av_window_ms[idx].store(now_ms, std::memory_order_relaxed);
        g_av_count[idx].store(1, std::memory_order_relaxed);
        cnt = 1;
    } else {
        cnt = g_av_count[idx].fetch_add(1, std::memory_order_relaxed) + 1;
    }
    return cnt >= kSehAvThreshold;
}

LONG seh_filter(EXCEPTION_POINTERS* ep, SehSite site, SehCapture* out) noexcept {
    const unsigned long code =
        (ep && ep->ExceptionRecord) ? ep->ExceptionRecord->ExceptionCode : 0;

    // Ciddi/kurtarılamaz istisnalar → yukarı ilet (yutma).
    if (seh_is_passthrough(code)) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // Yakalanabilir istisna: bağlamı POD out-param'a yaz (log/eskalasyon
    // __try dışında seh_report ile işlenecek).
    if (out) {
        out->code    = code;
        out->address = (ep && ep->ExceptionRecord)
                           ? ep->ExceptionRecord->ExceptionAddress
                           : nullptr;
        out->fired   = true;
        // I10-c: yalnız ACCESS_VIOLATION eskalasyon valfine tabi. Tekrar eden
        // AV = geçici sürücü hıçkırığı değil, gerçek bozulma olasılığı yüksek.
        // GetTickCount64/atomik erişim SEH-güvenli (kilit/heap yok).
        out->escalate = (code == EXCEPTION_ACCESS_VIOLATION) &&
                        seh_register_av(site, GetTickCount64());
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

void seh_report(const SehCapture& cap, SehSite site) noexcept {
    if (!cap.fired) return;

    // I10-b: yakalanan (yutulan) istisnayı ERROR seviyesinde SENKRON logla.
    // Senkronluk kritik: I10-c'de eskalasyon __fastfail ile normal unwind
    // yapmadan sonlanacağından, log satırının çağrı anında flush edilmiş
    // olması gerekir — aksi halde tam ihtiyaç duyulan anda kaybolur.
    char buf[256];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE,
                "[reji][ERROR] SEH yakalandi (yutuldu): site=%s code=0x%08lX addr=%p",
                site_name(site), cap.code, cap.address);
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");
    fprintf(stderr, "%s\n", buf);
    fflush(stderr);

    // I10-c: eskalasyon valfi. Aynı sitede pencerede eşik aşıldıysa yutmayı
    // bırak — FATAL logu SENKRON yazıp __fastfail ile sonlan. __fastfail normal
    // unwind yapmaz; bu yüzden yukarıdaki fflush + aşağıdaki log çağrı anında
    // diske/DebugView'a gitmiş olmalı (WER minidump'ı faulting context'i ayrıca
    // yakalar; bizim yapılandırılmış satırımız ek teşhis değeri taşır).
    if (cap.escalate) {
        char fbuf[256];
        _snprintf_s(fbuf, sizeof(fbuf), _TRUNCATE,
                    "[reji][FATAL] SEH AV esigi asildi: site=%s (%llu sn icinde >=%u AV) "
                    "code=0x%08lX addr=%p — surec sonlandiriliyor (__fastfail)",
                    site_name(site),
                    static_cast<unsigned long long>(kSehAvWindowMs / 1000),
                    kSehAvThreshold, cap.code, cap.address);
        OutputDebugStringA(fbuf);
        OutputDebugStringA("\n");
        fprintf(stderr, "%s\n", fbuf);
        fflush(stderr);
        __fastfail(FAST_FAIL_FATAL_APP_EXIT);
    }
}

} // namespace rj

#endif // _WIN32
