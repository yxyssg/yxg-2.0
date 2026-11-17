; ============================================================
; runtime_time.asm — YXG Time 库运行时
; 高精度计时基于 QueryPerformanceCounter
; ============================================================

extern _QueryPerformanceCounter@4
extern _QueryPerformanceFrequency@4
extern _Sleep@4
extern _GetTickCount@0

section .data
    qpc_freq_lo    dd 0
    qpc_freq_hi    dd 0
    freq_ready     dd 0
    thousand_f     dd 1000.0
    millis_tmp     dd 0

section .bss
    qpc_count_lo   resd 1
    qpc_count_hi   resd 1

section .text

init_freq:
    cmp dword [freq_ready], 0
    jne .done
    push qpc_freq_lo
    call _QueryPerformanceFrequency@4
    test eax, eax
    jnz .ok
    mov dword [qpc_freq_lo], 1000
    mov dword [qpc_freq_hi], 0
.ok:
    mov dword [freq_ready], 1
.done:
    ret

global Time_millis
Time_millis:
    call init_freq
    push qpc_count_lo
    call _QueryPerformanceCounter@4
    test eax, eax
    jz .fallback
    fild qword [qpc_count_lo]
    fild qword [qpc_freq_lo]
    fdivp
    fmul dword [thousand_f]
    fistp dword [millis_tmp]
    mov eax, [millis_tmp]
    ret
.fallback:
    call _GetTickCount@0
    ret

global Time_now
Time_now:
    call init_freq
    push qpc_count_lo
    call _QueryPerformanceCounter@4
    test eax, eax
    jz .fallback
    fild qword [qpc_count_lo]
    fild qword [qpc_freq_lo]
    fdivp
    ret
.fallback:
    call _GetTickCount@0
    mov [millis_tmp], eax
    fild dword [millis_tmp]
    fdiv dword [thousand_f]
    ret

global Time_delay_int
Time_delay_int:
    push ebp
    mov ebp, esp
    push dword [ebp+8]
    call _Sleep@4
    pop ebp
    ret