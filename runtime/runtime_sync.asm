; ============================================================
; runtime_sync.asm -- 同步原语 (Win32 / NASM)
; 全部 cdecl
; ============================================================

section .text

extern _CreateMutexA@12
extern _ReleaseMutex@4
extern _CreateEventA@16
extern _SetEvent@4
extern _ResetEvent@4
extern _CreateSemaphoreA@16
extern _ReleaseSemaphore@12
extern _WaitForSingleObject@8
extern _CloseHandle@4

global _sync_mutex_create
global _sync_mutex_lock
global _sync_mutex_unlock
global _sync_mutex_destroy
global _sync_event_create
global _sync_event_set
global _sync_event_reset
global _sync_event_wait
global _sync_event_wait_timeout
global _sync_event_destroy
global _sync_semaphore_create
global _sync_semaphore_acquire
global _sync_semaphore_release
global _sync_semaphore_destroy

; ------------------------------------------------------------
; int _sync_mutex_create(void) → handle
; ------------------------------------------------------------
_sync_mutex_create:
    push 0
    push 0
    push 0
    call _CreateMutexA@12
    ret

; void _sync_mutex_lock(int h)
_sync_mutex_lock:
    push ebp
    mov ebp, esp
    push -1
    push dword [ebp+8]
    call _WaitForSingleObject@8
    pop ebp
    ret

; void _sync_mutex_unlock(int h)
_sync_mutex_unlock:
    push dword [esp+4]
    call _ReleaseMutex@4
    ret

; void _sync_mutex_destroy(int h)
_sync_mutex_destroy:
    push dword [esp+4]
    call _CloseHandle@4
    ret

; ------------------------------------------------------------
; int _sync_event_create(void) → handle
; 自动重置事件，初始未触发
; ------------------------------------------------------------
_sync_event_create:
    push 0
    push 0
    push 0
    push 0
    call _CreateEventA@16
    ret

; void _sync_event_set(int h)
_sync_event_set:
    push dword [esp+4]
    call _SetEvent@4
    ret

; void _sync_event_reset(int h)
_sync_event_reset:
    push dword [esp+4]
    call _ResetEvent@4
    ret

; void _sync_event_wait(int h)
_sync_event_wait:
    push ebp
    mov ebp, esp
    push -1
    push dword [ebp+8]
    call _WaitForSingleObject@8
    pop ebp
    ret

; int _sync_event_wait_timeout(int h, int ms) → 1=signaled, 0=timeout
_sync_event_wait_timeout:
    push ebp
    mov ebp, esp
    push dword [ebp+12]
    push dword [ebp+8]
    call _WaitForSingleObject@8
    cmp eax, 0
    sete al
    movzx eax, al
    pop ebp
    ret

; void _sync_event_destroy(int h)
_sync_event_destroy:
    push dword [esp+4]
    call _CloseHandle@4
    ret

; ------------------------------------------------------------
; int _sync_semaphore_create(int initial, int maxCount) → handle
; ------------------------------------------------------------
_sync_semaphore_create:
    push ebp
    mov ebp, esp
    push 0
    push dword [ebp+12]
    push dword [ebp+8]
    push 0
    call _CreateSemaphoreA@16
    pop ebp
    ret

; void _sync_semaphore_acquire(int h)
_sync_semaphore_acquire:
    push ebp
    mov ebp, esp
    push -1
    push dword [ebp+8]
    call _WaitForSingleObject@8
    pop ebp
    ret

; void _sync_semaphore_release(int h)
_sync_semaphore_release:
    push ebp
    mov ebp, esp
    push 0
    push 1
    push dword [ebp+8]
    call _ReleaseSemaphore@12
    pop ebp
    ret

; void _sync_semaphore_destroy(int h)
_sync_semaphore_destroy:
    push dword [esp+4]
    call _CloseHandle@4
    ret