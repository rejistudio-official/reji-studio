// src/pipeline/include/seh_filter.h
//
// Paylaşımlı SEH (__try/__except) filtresi — pipeline tarafındaki tüm SEH
// leaf'leri için tek merkez. V8/I10.
//
// Amaç: sürücü/FFI sınırında oluşan *beklenen* geçici hataları (self-healing
// için) yakalarken, ciddi/kurtarılamaz istisnaları maskelememek:
//   * EXCEPTION_STACK_OVERFLOW / BREAKPOINT / SINGLE_STEP → yutulmaz
//     (CONTINUE_SEARCH). Stack overflow'da guard-page restore edilmediğinden
//     __except içinde iş yapmak güvenilmezdir; yukarı iletilir.
//   * EXCEPTION_ACCESS_VIOLATION ve diğerleri → yakalanır (EXECUTE_HANDLER)
//     ama görünürlük (I10-b) ve eskalasyon valfi (I10-c) bu commit serisinin
//     sonraki adımlarında eklenir. Bu dosyanın ilk halinde davranış yalnız
//     pass-through eklemekle sınırlıdır (AV hâlâ sessizce yutulur).
//
// SEH kısıtı: filter bir AV'den SONRA, olası bozuk yığın/heap durumunda
// çalışır. Bu yüzden filter yolunda kilit alınmaz ve heap'e dokunulmaz
// (bkz. seh_filter.cpp — kilitsiz, tahsissiz sayaç).
#pragma once

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstddef>
#include <cstdint>

namespace rj {

// I10-c eskalasyon valfi parametreleri: aynı sitede kısa pencerede tekrar eden
// ACCESS_VIOLATION, geçici sürücü hıçkırığından çok gerçek bellek bozulmasına
// işaret eder — bu eşik aşıldığında yutmayı bırak, __fastfail ile sonlan.
inline constexpr uint64_t kSehAvWindowMs  = 60'000;  // 60 sn
inline constexpr uint32_t kSehAvThreshold = 3;       // pencerede >=3 AV → eskale

// Her SEH leaf çağrı sitesi için ayrı kimlik — eskalasyon valfi (I10-c) site
// başına sayaç tutar. Sıra önemsiz; Count dizilerin boyutunu verir.
enum class SehSite : std::size_t {
    // command_router.cpp
    CmdDrain,
    WsDequeue,
    GetWsPort,
    // pipeline.cpp
    StartMonitor,
    UninitCom,
    ShutdownSubsys,
    // output_subsystem.cpp
    SrtSend,
    // metrics_subsystem.cpp
    MetricsPush,
    // recovery_coordinator.cpp
    ConnectionLost,
    // copy_optimizer.cpp
    CopyOptWait,
    CopyOptShutdown,
    // audio/wasapi_capture.cpp
    WasapiGetBuffer,
    WasapiReleaseBuffer,
    WasapiNextPacket,
    WasapiAudioStop,
    WasapiShutdownLeaf,
    // output/srt_output.cpp
    SrtLeafSendmsg,
    SrtLeafClose,
    SrtLeafEpollRelease,
    SrtLeafSetsockopt,
    SrtLeafGlobalCleanup,

    Count
};

// Filter'ın __except ifadesinde POD out-param olarak doldurduğu bağlam.
// __try DIŞINDA (C++ serbest) seh_report() ile işlenir.
struct SehCapture {
    unsigned long code   = 0;        // EXCEPTION_* kodu
    void*         address = nullptr; // ExceptionRecord->ExceptionAddress
    bool          fired   = false;   // handler seçildi mi (yakalandı mı)
    bool          escalate = false;  // I10-c: eşik aşıldı mı (fastfail)
};

// Ciddi/kurtarılamaz kodları pass-through (CONTINUE_SEARCH) olarak sınıflar.
// Saf fonksiyon — birim testte doğrudan doğrulanır (sentetik kod).
// true  → yukarı ilet (yutma); false → yakalanabilir.
inline bool seh_is_passthrough(unsigned long code) noexcept {
    return code == EXCEPTION_STACK_OVERFLOW ||
           code == EXCEPTION_BREAKPOINT     ||
           code == EXCEPTION_SINGLE_STEP;
}

// I10-c: site-başı AV sayacını günceller; pencerede eşik aşıldıysa true döner.
// Kilitsiz/tahsissiz (sabit atomik dizi) — bir AV filtresinde güvenle çağrılır
// (kilit/heap yok). now_ms enjekte edilebilir → birim testte doğrudan doğrulanır.
bool seh_register_av(SehSite site, uint64_t now_ms) noexcept;

// __except filter ifadesi. GetExceptionInformation() sonucunu alır, kararı
// verir ve bağlamı *out'a yazar. Pass-through kodlarda EXCEPTION_CONTINUE_SEARCH,
// aksi halde EXCEPTION_EXECUTE_HANDLER döner.
LONG seh_filter(EXCEPTION_POINTERS* ep, SehSite site, SehCapture* out) noexcept;

// __try DIŞINDA çağrılır (cap.fired ise). Yakalanan istisnayı ERROR seviyesinde
// senkron loglar; eşik aşıldıysa (cap.escalate) FATAL log + __fastfail.
// (Log gövdeleri I10-b/I10-c'de eklenir.)
void seh_report(const SehCapture& cap, SehSite site) noexcept;

} // namespace rj

#endif // _WIN32
