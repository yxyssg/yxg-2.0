; runtime_exception.asm —— 最终版（含 FPU 保存 + 未捕获 Error() 虚方法）
;
; 异常帧布局（56 字节）：
;   [0]   prev      [4]   saved_esp  [8]  saved_ebp  [12] saved_ebx
;   [16]  saved_esi [20]  saved_edi  [24] saved_eip  [28..56) FPU 环境
;
; 全部 cdecl，参数由调用方清理。

section .bss
  _exc_frame_head resd 1

section .text

extern _print
extern _ExitProcess@4
extern _exc_current
extern _exc_class_parents
extern _exc_error_handlers

global _exc_frame_push
global _exc_frame_pop
global _exc_get_current
global _exc_setjmp
global _exc_longjmp
global _error_is_instance
global _error_raise

_exc_frame_push:
    mov eax, [esp+4]
    mov ecx, [_exc_frame_head]
    mov [eax], ecx
    mov [_exc_frame_head], eax
    ret

_exc_frame_pop:
    mov eax, [_exc_frame_head]
    test eax, eax
    jz .done
    mov eax, [eax]
    mov [_exc_frame_head], eax
.done:
    ret

_exc_get_current:
    mov eax, [_exc_current]
    ret

_exc_setjmp:
    mov eax, [esp+4]
    mov ecx, [esp]
    lea edx, [esp+4]
    mov [eax+4],  edx
    mov [eax+8],  ebp
    mov [eax+12], ebx
    mov [eax+16], esi
    mov [eax+20], edi
    mov [eax+24], ecx
    fnstenv [eax+28]
    xor eax, eax
    ret

_exc_longjmp:
    mov eax, [esp+4]
    mov ecx, [esp+8]
    mov esp, [eax+4]
    mov ebp, [eax+8]
    mov ebx, [eax+12]
    mov esi, [eax+16]
    mov edi, [eax+20]
    mov edx, [eax+24]
    fldenv [eax+28]
    mov eax, ecx
    jmp edx

_error_is_instance:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    test eax, eax
    jz .no
    mov eax, [eax]
.loop:
    cmp eax, [ebp+12]
    je .yes
    test eax, eax
    jz .no
    mov ecx, _exc_class_parents
    mov eax, [ecx + eax*4]
    jmp .loop
.yes:
    mov eax, 1
    pop ebp
    ret
.no:
    xor eax, eax
    pop ebp
    ret

_error_raise:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    mov [_exc_current], eax

    mov ecx, [_exc_frame_head]
    test ecx, ecx
    jz .uncaught

    push 1
    push ecx
    call _exc_longjmp
    add esp, 8

.uncaught:
    mov eax, [ebp+8]
    test eax, eax
    jz .noobj
    mov ecx, [eax]
    mov ecx, [ecx*4 + _exc_error_handlers]
    test ecx, ecx
    jz .noobj
    push eax
    call ecx
    add esp, 4
    jmp .exit
.noobj:
    mov eax, [ebp+8]
    test eax, eax
    jz .nomsg
    mov eax, [eax+4]
    test eax, eax
    jz .nomsg
    push 3
    push eax
    call _print
    add esp, 8
    jmp .nl
.nomsg:
    push 3
    push _err_unknown_msg
    call _print
    add esp, 8
.nl:
    push 3
    push _err_newline
    call _print
    add esp, 8
.exit:
    push 1
    call _ExitProcess@4
    pop ebp
    ret

section .rodata
  _err_unknown_msg db 10,`Uncaught exception`,0
  _err_newline     db 10,0