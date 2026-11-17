; ============================================================
; runtime_thread.asm -- 基础线程库 (Win32 / NASM)
; cdecl 调用约定
; 线程表由 runtime_gc.asm 定义
; ============================================================

%define THREAD_INFO_SIZE 24
%define MAX_THREADS 64

section .text

extern _CreateThread@24
extern _WaitForSingleObject@8
extern _CloseHandle@4
extern _GetCurrentThreadId@0
extern _Sleep@4
extern _OpenThread@12
extern _GetExitCodeThread@8
extern _AllocMemory
extern _FreeMemory
extern _InitializeSRWLock@4
extern _AcquireSRWLockExclusive@4
extern _ReleaseSRWLockExclusive@4

extern _thread_list_head
extern _thread_list_lock
extern _thread_lock_state

global _thread_create
global _thread_join
global _thread_self
global _thread_sleep

section .text

_thread_create:
    push ebp
    mov ebp, esp

    push 0
    push 0
    push dword [ebp+8]
    push thread_wrapper
    push 0
    push 0
    call _CreateThread@24

    pop ebp
    ret

_thread_join:
    push ebp
    mov ebp, esp
    push ebx
    sub esp, 4

    mov eax, [ebp+8]
    test eax, eax
    jz .done

    push -1
    push eax
    call _WaitForSingleObject@8

    lea eax, [ebp-8]
    push eax
    push dword [ebp+8]
    call _GetExitCodeThread@8

    mov ebx, [ebp-8]

    push dword [ebp+8]
    call _CloseHandle@4

    mov eax, ebx
.done:
    add esp, 4
    pop ebx
    pop ebp
    ret

_thread_self:
    call _GetCurrentThreadId@0
    ret

_thread_sleep:
    mov eax, [esp+4]
    push eax
    call _Sleep@4
    ret

_thread_register:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi

    push 0
    push THREAD_INFO_SIZE
    call _AllocMemory
    add esp, 8
    test eax, eax
    jz .done
    mov edi, eax

    call _GetCurrentThreadId@0
    mov [edi+8], eax

    push eax
    push 0
    push 0x000A
    call _OpenThread@12
    mov [edi+4], eax

    mov eax, dword [fs:4]
    mov [edi+12], eax
    mov eax, dword [fs:8]
    mov [edi+16], eax

    lea eax, [_thread_list_lock]
    push eax
    call _AcquireSRWLockExclusive@4

    mov eax, [_thread_list_head]
    mov [edi], eax
    mov [_thread_list_head], edi

    lea eax, [_thread_list_lock]
    push eax
    call _ReleaseSRWLockExclusive@4

.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

_thread_unregister:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi

    call _GetCurrentThreadId@0
    mov ebx, eax

    lea eax, [_thread_list_lock]
    push eax
    call _AcquireSRWLockExclusive@4

    lea edi, [_thread_list_head]
    xor esi, esi
.find:
    mov esi, [edi]
    test esi, esi
    jz .notfound
    cmp [esi+8], ebx
    je .found
    mov edi, esi
    jmp .find
.found:
    mov eax, [esi]
    mov [edi], eax
.notfound:

    lea eax, [_thread_list_lock]
    push eax
    call _ReleaseSRWLockExclusive@4

    test esi, esi
    jz .done
    push dword [esi+4]
    call _CloseHandle@4
    push esi
    call _FreeMemory

.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

thread_wrapper:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi

    mov esi, [ebp+8]

    call _thread_register

    test esi, esi
    jz .null_func
    mov ebx, [esi+8]
    test ebx, ebx
    jz .null_func
    mov ecx, [esi+12]
    test ecx, ecx
    jz .call_no_env
    push ecx
    call ebx
    add esp, 4
    jmp .after_call
.call_no_env:
    call ebx
    jmp .after_call
.null_func:
    xor eax, eax
.after_call:
    push eax

    call _thread_unregister

    pop eax
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret 4