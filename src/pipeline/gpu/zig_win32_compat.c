/*
 * zig_win32_compat.c
 * MinGW/Zig static lib'i MSVC exe'ye baglamak icin compat stub'lar.
 * ___chkstk_ms -> zig_win32_compat.asm (non-standard calling convention)
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")

/* GCC Stack Smashing Protector sembolleri — MinGW ekler, MSVC CRT'de yok.
 * Sabit canary SSP'yi etkisiz kilar; BCryptGenRandom ile rastgele baslatilir.
 * Fallback: BCrypt basarisiz olursa sabit deger kalir (linking hatasini onler). */
void* __stack_chk_guard = (void*)(uintptr_t)0xDEADBEEFDEADBEEFull;
void __cdecl __stack_chk_fail(void) { ExitProcess(255); }

static void init_stack_guard(void) {
    uintptr_t v = 0;
    if (BCryptGenRandom(NULL, (PUCHAR)&v, sizeof(v),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0) {
        __stack_chk_guard = (void*)v;
    }
}

#ifdef _MSC_VER
/* MSVC: .CRT$XCU bolumune fonksiyon isaretcisi yerlestir —
 * CRT bu tablodaki tum isaretcileri main() oncesinde cagirır. */
#pragma section(".CRT$XCU", read)
__declspec(allocate(".CRT$XCU")) static void (*p_init_stack_guard)(void) = init_stack_guard;
#else
/* GCC/Clang: __attribute__((constructor)) ile ayni davranis */
__attribute__((constructor)) static void init_stack_guard_ctor(void) { init_stack_guard(); }
#endif

/*
 * LdrRegisterDllNotification — ntdll.dll'de var ama ntdll.lib'de yok.
 * Zig std runtime DLL yuklenme bildirimlerini kaydetmek icin kullanir.
 */
typedef LONG (NTAPI *PfnLdrReg)(ULONG, PVOID, PVOID, PVOID*);
LONG NTAPI LdrRegisterDllNotification(
        ULONG Flags, PVOID NotifFn, PVOID Ctx, PVOID* Cookie) {
    static PfnLdrReg s_fn = NULL;
    if (!s_fn) {
        HMODULE h = GetModuleHandleW(L"ntdll.dll");
        if (h) s_fn = (PfnLdrReg)GetProcAddress(h, "LdrRegisterDllNotification");
    }
    return s_fn ? s_fn(Flags, NotifFn, Ctx, Cookie) : 0xC0000002L;
}
