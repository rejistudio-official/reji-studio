/*
 * zig_win32_compat.c
 * MinGW/Zig static lib'i MSVC exe'ye baglamak icin compat stub'lar.
 * ___chkstk_ms -> zig_win32_compat.asm (non-standard calling convention)
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* GCC Stack Smashing Protector sembolleri — MinGW ekler, MSVC CRT'de yok */
void* __stack_chk_guard = (void*)(uintptr_t)0xDEADBEEFDEADBEEFull;
void __cdecl __stack_chk_fail(void) { ExitProcess(255); }

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
