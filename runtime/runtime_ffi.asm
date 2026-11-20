; ============================================================================
; runtime_ffi.asm -- 2.5.1 dynamic FFI support
;
; Provides:
;   int  _yxg_lib_open(string path)          LoadLibraryA(path) -> HMODULE
;   int  _yxg_lib_sym(int h, string name)    GetProcAddress(h,name) -> addr
;   void _yxg_lib_close(int h)               FreeLibrary(h)
;   int  _yxg_last_error()                   GetLastError()
;
;   int  _yxg_callcdecl%d(addr, a1..a%d)     cdecl call  (caller cleans stack)
;   int  _yxg_callstdcall%d(addr, a1..a%d)   stdcall call (callee cleans stack)
;     d = 0..4
;
; The YXG side passes `string` as a pointer, which matches LPCSTR.
; ============================================================================
    global _yxg_lib_open
    global _yxg_lib_sym
    global _yxg_lib_close
    global _yxg_last_error

    global _yxg_callcdecl0
    global _yxg_callcdecl1
    global _yxg_callcdecl2
    global _yxg_callcdecl3
    global _yxg_callcdecl4
    global _yxg_callstdcall0
    global _yxg_callstdcall1
    global _yxg_callstdcall2
    global _yxg_callstdcall3
    global _yxg_callstdcall4
    global _yxg_callcdecl5
    global _yxg_callstdcall5
    global _yxg_callcdecl6
    global _yxg_callstdcall6
    global _yxg_callcdecl7
    global _yxg_callstdcall7
    global _yxg_callcdecl8
    global _yxg_callstdcall8
    global _yxg_callstdcall4_ss

extern _LoadLibraryA@4
extern _GetProcAddress@8
extern _FreeLibrary@4
extern _GetLastError@0

section .text

; ---------------------------------------------------------------------------
; int _yxg_lib_open(string path)
; ---------------------------------------------------------------------------
_yxg_lib_open:
    push ebp
    mov  ebp, esp
    push dword [ebp + 8]
    call _LoadLibraryA@4
    pop  ebp
    ret

; ---------------------------------------------------------------------------
; int _yxg_lib_sym(int handle, string name)
; ---------------------------------------------------------------------------
_yxg_lib_sym:
    push ebp
    mov  ebp, esp
    push dword [ebp + 12]
    push dword [ebp + 8]
    call _GetProcAddress@8
    pop  ebp
    ret

; ---------------------------------------------------------------------------
; void _yxg_lib_close(int handle)
; ---------------------------------------------------------------------------
_yxg_lib_close:
    push ebp
    mov  ebp, esp
    push dword [ebp + 8]
    call _FreeLibrary@4
    pop  ebp
    ret

; ---------------------------------------------------------------------------
; int _yxg_last_error(void)
; ---------------------------------------------------------------------------
_yxg_last_error:
    push ebp
    mov  ebp, esp
    call _GetLastError@0
    pop  ebp
    ret

; ---------------------------------------------------------------------------
; cdecl wrappers.  YXG 的 native 声明是 cdecl，所以 wrapper 自己清栈。
; 参数布局: [ebp+8]=addr, [ebp+12]=a1, [ebp+16]=a2, ...
; ---------------------------------------------------------------------------
_yxg_callcdecl0:
    push ebp
    mov  ebp, esp
    call dword [ebp + 8]
    pop  ebp
    ret

_yxg_callcdecl1:
    push ebp
    mov  ebp, esp
    push dword [ebp + 12]
    call dword [ebp + 8]
    add  esp, 4
    pop  ebp
    ret

_yxg_callcdecl2:
    push ebp
    mov  ebp, esp
    push dword [ebp + 16]
    push dword [ebp + 12]
    call dword [ebp + 8]
    add  esp, 8
    pop  ebp
    ret

_yxg_callcdecl3:
    push ebp
    mov  ebp, esp
    push dword [ebp + 20]
    push dword [ebp + 16]
    push dword [ebp + 12]
    call dword [ebp + 8]
    add  esp, 12
    pop  ebp
    ret

_yxg_callcdecl4:
    push ebp
    mov  ebp, esp
    push dword [ebp + 24]
    push dword [ebp + 20]
    push dword [ebp + 16]
    push dword [ebp + 12]
    call dword [ebp + 8]
    add  esp, 16
    pop  ebp
    ret

; ---------------------------------------------------------------------------
; stdcall wrappers.  被调用者清栈，wrapper 不再 add esp。
; ---------------------------------------------------------------------------
_yxg_callstdcall0:
    push ebp
    mov  ebp, esp
    call dword [ebp + 8]
    pop  ebp
    ret

_yxg_callstdcall1:
    push ebp
    mov  ebp, esp
    push dword [ebp + 12]
    call dword [ebp + 8]
    pop  ebp
    ret

_yxg_callstdcall2:
    push ebp
    mov  ebp, esp
    push dword [ebp + 16]
    push dword [ebp + 12]
    call dword [ebp + 8]
    pop  ebp
    ret

_yxg_callstdcall3:
    push ebp
    mov  ebp, esp
    push dword [ebp + 20]
    push dword [ebp + 16]
    push dword [ebp + 12]
    call dword [ebp + 8]
    pop  ebp
    ret

_yxg_callstdcall4:
    push ebp
    mov  ebp, esp
    push dword [ebp + 24]
    push dword [ebp + 20]
    push dword [ebp + 16]
    push dword [ebp + 12]
    call dword [ebp + 8]
    pop  ebp
    ret

; 同 _yxg_callstdcall4，但第 3、4 个参数声明为 string（YXG 侧类型检查用，
; 栈布局完全一样，直接跳过去）。
_yxg_callstdcall4_ss:
    jmp  _yxg_callstdcall4

; ============================================================================

; ============================================================================
; 2.5.2 Encoding: GBK(936) <-> UTF-8(65001)
; ABI：返回数据指针，长度在 data-4，用 _gc_alloc(len+8)。
; 输入用显式长度（GC 对象/字面量都不保证 NUL 结尾）。
; ============================================================================
extern _MultiByteToWideChar@24
extern _WideCharToMultiByte@32
extern _gc_alloc

GBK_CP   equ 936
UTF8_CP  equ 65001
ENC_MAX  equ 8192

section .bss
enc_wide   resb ENC_MAX*2
enc_out    resb ENC_MAX

section .text

global _yxg_gbk_to_utf8
_yxg_gbk_to_utf8:
    push ebp
    mov  ebp, esp
    push ebx
    push esi
    push edi

    mov  esi, dword [ebp + 8]
    mov  ebx, dword [ebp + 12]
    test esi, esi
    jz   .g2u_fail
    test ebx, ebx
    jz   .g2u_fail

    push ENC_MAX
    push enc_wide
    push ebx
    push esi
    push 0
    push GBK_CP
    call _MultiByteToWideChar@24
    test eax, eax
    jz   .g2u_fail
    mov  edi, eax

    push 0
    push 0
    push ENC_MAX
    push enc_out
    push edi
    push enc_wide
    push 0
    push UTF8_CP
    call _WideCharToMultiByte@32
    test eax, eax
    jz   .g2u_fail
    mov  ebx, eax

    lea  eax, [ebx + 8]
    add  eax, 3
    and  eax, -4
    push eax
    call _gc_alloc
    add  esp, 4
    test eax, eax
    jz   .g2u_fail
    mov  [eax], ebx
    lea  edi, [eax + 4]
    push edi
    mov  ecx, ebx
    mov  esi, enc_out
    rep  movsb
    mov  byte [edi], 0
    pop  eax
    jmp  .g2u_done
.g2u_fail:
    xor  eax, eax
.g2u_done:
    pop  edi
    pop  esi
    pop  ebx
    pop  ebp
    ret

global _yxg_utf8_to_gbk
_yxg_utf8_to_gbk:
    push ebp
    mov  ebp, esp
    push ebx
    push esi
    push edi

    mov  esi, dword [ebp + 8]
    mov  ebx, dword [ebp + 12]
    test esi, esi
    jz   .u2g_fail
    test ebx, ebx
    jz   .u2g_fail

    push ENC_MAX
    push enc_wide
    push ebx
    push esi
    push 0
    push UTF8_CP
    call _MultiByteToWideChar@24
    test eax, eax
    jz   .u2g_fail
    mov  edi, eax

    push 0
    push 0
    push ENC_MAX
    push enc_out
    push edi
    push enc_wide
    push 0
    push GBK_CP
    call _WideCharToMultiByte@32
    test eax, eax
    jz   .u2g_fail
    mov  ebx, eax

    lea  eax, [ebx + 8]
    add  eax, 3
    and  eax, -4
    push eax
    call _gc_alloc
    add  esp, 4
    test eax, eax
    jz   .u2g_fail
    mov  [eax], ebx
    lea  edi, [eax + 4]
    push edi
    mov  ecx, ebx
    mov  esi, enc_out
    rep  movsb
    mov  byte [edi], 0
    pop  eax
    jmp  .u2g_done
.u2g_fail:
    xor  eax, eax
.u2g_done:
    pop  edi
    pop  esi
    pop  ebx
    pop  ebp
    ret
_yxg_callcdecl5:
    push ebp
    mov  ebp, esp
    push dword [ebp + 28]
    push dword [ebp + 24]
    push dword [ebp + 20]
    push dword [ebp + 16]
    push dword [ebp + 12]
    call dword [ebp + 8]
    add  esp, 20
    pop  ebp
    ret

_yxg_callcdecl6:
    push ebp
    mov  ebp, esp
    push dword [ebp + 32]
    push dword [ebp + 28]
    push dword [ebp + 24]
    push dword [ebp + 20]
    push dword [ebp + 16]
    push dword [ebp + 12]
    call dword [ebp + 8]
    add  esp, 24
    pop  ebp
    ret

_yxg_callcdecl7:
    push ebp
    mov  ebp, esp
    push dword [ebp + 36]
    push dword [ebp + 32]
    push dword [ebp + 28]
    push dword [ebp + 24]
    push dword [ebp + 20]
    push dword [ebp + 16]
    push dword [ebp + 12]
    call dword [ebp + 8]
    add  esp, 28
    pop  ebp
    ret

_yxg_callcdecl8:
    push ebp
    mov  ebp, esp
    push dword [ebp + 40]
    push dword [ebp + 36]
    push dword [ebp + 32]
    push dword [ebp + 28]
    push dword [ebp + 24]
    push dword [ebp + 20]
    push dword [ebp + 16]
    push dword [ebp + 12]
    call dword [ebp + 8]
    add  esp, 32
    pop  ebp
    ret

_yxg_callstdcall5:
    push ebp
    mov  ebp, esp
    push dword [ebp + 28]
    push dword [ebp + 24]
    push dword [ebp + 20]
    push dword [ebp + 16]
    push dword [ebp + 12]
    call dword [ebp + 8]
    pop  ebp
    ret

_yxg_callstdcall6:
    push ebp
    mov  ebp, esp
    push dword [ebp + 32]
    push dword [ebp + 28]
    push dword [ebp + 24]
    push dword [ebp + 20]
    push dword [ebp + 16]
    push dword [ebp + 12]
    call dword [ebp + 8]
    pop  ebp
    ret

_yxg_callstdcall7:
    push ebp
    mov  ebp, esp
    push dword [ebp + 36]
    push dword [ebp + 32]
    push dword [ebp + 28]
    push dword [ebp + 24]
    push dword [ebp + 20]
    push dword [ebp + 16]
    push dword [ebp + 12]
    call dword [ebp + 8]
    pop  ebp
    ret

_yxg_callstdcall8:
    push ebp
    mov  ebp, esp
    push dword [ebp + 40]
    push dword [ebp + 36]
    push dword [ebp + 32]
    push dword [ebp + 28]
    push dword [ebp + 24]
    push dword [ebp + 20]
    push dword [ebp + 16]
    push dword [ebp + 12]
    call dword [ebp + 8]
    pop  ebp
    ret

; 含 string 参数的同布局别名（栈布局与整型版本相同，直接 jmp）
    global _yxg_callcdecl9
    global _yxg_callstdcall9
    global _yxg_callcdecl10
    global _yxg_callstdcall10
    global _yxg_callcdecl11
    global _yxg_callstdcall11
    global _yxg_callcdecl12
    global _yxg_callstdcall12
global _yxg_callstdcall6_s
_yxg_callstdcall6_s:
    jmp  _yxg_callstdcall6

global _yxg_callstdcall6_s_s
_yxg_callstdcall6_s_s:
    jmp  _yxg_callstdcall6

global _yxg_callstdcall8_s_s
global _yxg_calln_stdcall
global _yxg_calln_cdecl
global _yxg_callcdecl13
global _yxg_callstdcall13
global _yxg_callcdecl14
global _yxg_callstdcall14
global _yxg_callcdecl15
global _yxg_callstdcall15
global _yxg_callcdecl16
global _yxg_callstdcall16
_yxg_callstdcall8_s_s:
    jmp  _yxg_callstdcall8
_yxg_callcdecl9:
    push ebp
    mov  ebp, esp
    push dword [ebp + 12]
    push dword [ebp + 16]
    push dword [ebp + 20]
    push dword [ebp + 24]
    push dword [ebp + 28]
    push dword [ebp + 32]
    push dword [ebp + 36]
    push dword [ebp + 40]
    push dword [ebp + 44]
    call dword [ebp + 8]
    add  esp, 36
    pop  ebp
    ret

_yxg_callstdcall9:
    push ebp
    mov  ebp, esp
    push dword [ebp + 12]
    push dword [ebp + 16]
    push dword [ebp + 20]
    push dword [ebp + 24]
    push dword [ebp + 28]
    push dword [ebp + 32]
    push dword [ebp + 36]
    push dword [ebp + 40]
    push dword [ebp + 44]
    call dword [ebp + 8]
    pop  ebp
    ret

_yxg_callcdecl10:
    push ebp
    mov  ebp, esp
    push dword [ebp + 12]
    push dword [ebp + 16]
    push dword [ebp + 20]
    push dword [ebp + 24]
    push dword [ebp + 28]
    push dword [ebp + 32]
    push dword [ebp + 36]
    push dword [ebp + 40]
    push dword [ebp + 44]
    push dword [ebp + 48]
    call dword [ebp + 8]
    add  esp, 40
    pop  ebp
    ret

_yxg_callstdcall10:
    push ebp
    mov  ebp, esp
    push dword [ebp + 12]
    push dword [ebp + 16]
    push dword [ebp + 20]
    push dword [ebp + 24]
    push dword [ebp + 28]
    push dword [ebp + 32]
    push dword [ebp + 36]
    push dword [ebp + 40]
    push dword [ebp + 44]
    push dword [ebp + 48]
    call dword [ebp + 8]
    pop  ebp
    ret

_yxg_callcdecl11:
    push ebp
    mov  ebp, esp
    push dword [ebp + 12]
    push dword [ebp + 16]
    push dword [ebp + 20]
    push dword [ebp + 24]
    push dword [ebp + 28]
    push dword [ebp + 32]
    push dword [ebp + 36]
    push dword [ebp + 40]
    push dword [ebp + 44]
    push dword [ebp + 48]
    push dword [ebp + 52]
    call dword [ebp + 8]
    add  esp, 44
    pop  ebp
    ret

_yxg_callstdcall11:
    push ebp
    mov  ebp, esp
    push dword [ebp + 12]
    push dword [ebp + 16]
    push dword [ebp + 20]
    push dword [ebp + 24]
    push dword [ebp + 28]
    push dword [ebp + 32]
    push dword [ebp + 36]
    push dword [ebp + 40]
    push dword [ebp + 44]
    push dword [ebp + 48]
    push dword [ebp + 52]
    call dword [ebp + 8]
    pop  ebp
    ret

_yxg_callcdecl12:
    push ebp
    mov  ebp, esp
    push dword [ebp + 12]
    push dword [ebp + 16]
    push dword [ebp + 20]
    push dword [ebp + 24]
    push dword [ebp + 28]
    push dword [ebp + 32]
    push dword [ebp + 36]
    push dword [ebp + 40]
    push dword [ebp + 44]
    push dword [ebp + 48]
    push dword [ebp + 52]
    push dword [ebp + 56]
    call dword [ebp + 8]
    add  esp, 48
    pop  ebp
    ret

_yxg_callstdcall12:
    push ebp
    mov  ebp, esp
    push dword [ebp + 12]
    push dword [ebp + 16]
    push dword [ebp + 20]
    push dword [ebp + 24]
    push dword [ebp + 28]
    push dword [ebp + 32]
    push dword [ebp + 36]
    push dword [ebp + 40]
    push dword [ebp + 44]
    push dword [ebp + 48]
    push dword [ebp + 52]
    push dword [ebp + 56]
    call dword [ebp + 8]
    pop  ebp
    ret

; ---------------------------------------------------------------------------
; 13..16 参数（更高参数个数的 Win32 API 很少，到 16 已覆盖实际需求）
; ---------------------------------------------------------------------------

_yxg_callcdecl13:
    push ebp
    mov  ebp, esp
    push dword [ebp + 60]
    push dword [ebp + 56]
    push dword [ebp + 52]
    push dword [ebp + 48]
    push dword [ebp + 44]
    push dword [ebp + 40]
    push dword [ebp + 36]
    push dword [ebp + 32]
    push dword [ebp + 28]
    push dword [ebp + 24]
    push dword [ebp + 20]
    push dword [ebp + 16]
    push dword [ebp + 12]
    call dword [ebp + 8]
    add  esp, 52
    pop  ebp
    ret

_yxg_callcdecl14:
    push ebp
    mov  ebp, esp
    push dword [ebp + 64]
    push dword [ebp + 60]
    push dword [ebp + 56]
    push dword [ebp + 52]
    push dword [ebp + 48]
    push dword [ebp + 44]
    push dword [ebp + 40]
    push dword [ebp + 36]
    push dword [ebp + 32]
    push dword [ebp + 28]
    push dword [ebp + 24]
    push dword [ebp + 20]
    push dword [ebp + 16]
    push dword [ebp + 12]
    call dword [ebp + 8]
    add  esp, 56
    pop  ebp
    ret

_yxg_callcdecl15:
    push ebp
    mov  ebp, esp
    push dword [ebp + 68]
    push dword [ebp + 64]
    push dword [ebp + 60]
    push dword [ebp + 56]
    push dword [ebp + 52]
    push dword [ebp + 48]
    push dword [ebp + 44]
    push dword [ebp + 40]
    push dword [ebp + 36]
    push dword [ebp + 32]
    push dword [ebp + 28]
    push dword [ebp + 24]
    push dword [ebp + 20]
    push dword [ebp + 16]
    push dword [ebp + 12]
    call dword [ebp + 8]
    add  esp, 60
    pop  ebp
    ret

_yxg_callcdecl16:
    push ebp
    mov  ebp, esp
    push dword [ebp + 72]
    push dword [ebp + 68]
    push dword [ebp + 64]
    push dword [ebp + 60]
    push dword [ebp + 56]
    push dword [ebp + 52]
    push dword [ebp + 48]
    push dword [ebp + 44]
    push dword [ebp + 40]
    push dword [ebp + 36]
    push dword [ebp + 32]
    push dword [ebp + 28]
    push dword [ebp + 24]
    push dword [ebp + 20]
    push dword [ebp + 16]
    push dword [ebp + 12]
    call dword [ebp + 8]
    add  esp, 64
    pop  ebp
    ret

_yxg_callstdcall13:
    push ebp
    mov  ebp, esp
    push dword [ebp + 60]
    push dword [ebp + 56]
    push dword [ebp + 52]
    push dword [ebp + 48]
    push dword [ebp + 44]
    push dword [ebp + 40]
    push dword [ebp + 36]
    push dword [ebp + 32]
    push dword [ebp + 28]
    push dword [ebp + 24]
    push dword [ebp + 20]
    push dword [ebp + 16]
    push dword [ebp + 12]
    call dword [ebp + 8]
    pop  ebp
    ret

_yxg_callstdcall14:
    push ebp
    mov  ebp, esp
    push dword [ebp + 64]
    push dword [ebp + 60]
    push dword [ebp + 56]
    push dword [ebp + 52]
    push dword [ebp + 48]
    push dword [ebp + 44]
    push dword [ebp + 40]
    push dword [ebp + 36]
    push dword [ebp + 32]
    push dword [ebp + 28]
    push dword [ebp + 24]
    push dword [ebp + 20]
    push dword [ebp + 16]
    push dword [ebp + 12]
    call dword [ebp + 8]
    pop  ebp
    ret

_yxg_callstdcall15:
    push ebp
    mov  ebp, esp
    push dword [ebp + 68]
    push dword [ebp + 64]
    push dword [ebp + 60]
    push dword [ebp + 56]
    push dword [ebp + 52]
    push dword [ebp + 48]
    push dword [ebp + 44]
    push dword [ebp + 40]
    push dword [ebp + 36]
    push dword [ebp + 32]
    push dword [ebp + 28]
    push dword [ebp + 24]
    push dword [ebp + 20]
    push dword [ebp + 16]
    push dword [ebp + 12]
    call dword [ebp + 8]
    pop  ebp
    ret

_yxg_callstdcall16:
    push ebp
    mov  ebp, esp
    push dword [ebp + 72]
    push dword [ebp + 68]
    push dword [ebp + 64]
    push dword [ebp + 60]
    push dword [ebp + 56]
    push dword [ebp + 52]
    push dword [ebp + 48]
    push dword [ebp + 44]
    push dword [ebp + 40]
    push dword [ebp + 36]
    push dword [ebp + 32]
    push dword [ebp + 28]
    push dword [ebp + 24]
    push dword [ebp + 20]
    push dword [ebp + 16]
    push dword [ebp + 12]
    call dword [ebp + 8]
    pop  ebp
    ret

; ---------------------------------------------------------------------------
; 通用调用器：任意参数个数
;   _yxg_calln_stdcall(addr, int* args, int count)
;   _yxg_calln_cdecl  (addr, int* args, int count)
; args[0] 是第一个参数（最后压栈）。stdcall 由被调用者清栈；
; cdecl 由这里在 call 后 add esp, count*4。
; ---------------------------------------------------------------------------
_yxg_calln_stdcall:
    push ebp
    mov  ebp, esp
    push ebx
    push esi
    push edi
    mov  ecx, [ebp + 16]
    test ecx, ecx
    jle  .nsc_call
    mov  esi, [ebp + 12]
    lea  esi, [esi + ecx*4 - 4]
.nsc_push:
    push dword [esi]
    sub  esi, 4
    dec  ecx
    jnz  .nsc_push
.nsc_call:
    call dword [ebp + 8]
    pop  edi
    pop  esi
    pop  ebx
    pop  ebp
    ret

_yxg_calln_cdecl:
    push ebp
    mov  ebp, esp
    push ebx
    push esi
    push edi
    mov  ecx, [ebp + 16]
    test ecx, ecx
    jle  .ncc_call
    mov  esi, [ebp + 12]
    lea  esi, [esi + ecx*4 - 4]
.ncc_push:
    push dword [esi]
    sub  esi, 4
    dec  ecx
    jnz  .ncc_push
.ncc_call:
    call dword [ebp + 8]
    mov  ecx, [ebp + 16]
    shl  ecx, 2
    add  esp, ecx
    pop  edi
    pop  esi
    pop  ebx
    pop  ebp
    ret
