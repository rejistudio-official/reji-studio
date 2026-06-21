; zig_win32_compat.asm
; ___chkstk_ms — MinGW/Zig stack probe, MSVC exe'ye baglamak icin.
; RAX = ayrilacak stack byte sayisi. RSP'yi degistirmez.
; MinGW kaynak: mingw-w64/mingwex/math/chkstk_ms.S
PUBLIC ___chkstk_ms

.code

___chkstk_ms PROC
    push    rcx
    push    rax
    cmp     rax, 1000h
    lea     rcx, [rsp + 18h]        ; caller RSP (2 push + ret adresi = 18h)
    jb      probe_done

probe_loop:
    sub     rcx, 1000h              ; bir sayfa (4KB) asagi in
    or      QWORD PTR [rcx], 0     ; guard page fault'u tetikle
    sub     rax, 1000h
    cmp     rax, 1000h
    jae     probe_loop

probe_done:
    sub     rcx, rax               ; kalan < 4KB
    or      QWORD PTR [rcx], 0     ; son sayfayi commit et
    pop     rax
    pop     rcx
    ret
___chkstk_ms ENDP

END
