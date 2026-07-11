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

#ifdef _WIN32

namespace rj {

namespace {

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

LONG seh_filter(EXCEPTION_POINTERS* ep, SehSite /*site*/, SehCapture* out) noexcept {
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
        out->escalate = false;  // I10-c'de doldurulacak
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
}

} // namespace rj

#endif // _WIN32
