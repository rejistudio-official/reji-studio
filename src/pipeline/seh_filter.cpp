// src/pipeline/seh_filter.cpp
//
// Paylaşımlı SEH filtresi implementasyonu — V8/I10. Bkz. seh_filter.h.
//
// Bu commit (I10-a): yalnız SO/BP/SS pass-through + bağlam yakalama.
// AV ve diğer kodlar yakalanır ama seh_report henüz sessizdir (davranış,
// eski çıplak EXCEPTION_EXECUTE_HANDLER ile aynı — görünürlük I10-b'de,
// eskalasyon I10-c'de eklenir).
#include "seh_filter.h"

#ifdef _WIN32

namespace rj {

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

void seh_report(const SehCapture& /*cap*/, SehSite /*site*/) noexcept {
    // I10-a: henüz sessiz (eski davranış korunuyor).
    // I10-b: ERROR seviyesinde senkron log eklenecek.
    // I10-c: eskalasyon (FATAL log + __fastfail) eklenecek.
}

} // namespace rj

#endif // _WIN32
