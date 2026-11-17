; gc.asm
; 保守 GC + STW 多线程 + 页位图加速

%define HEADER_SIZE     16
%define MARK_STACK_SIZE 65536
%define MAX_THREADS     64
%define CONTEXT_SIZE    720
%define CTX_ESP         0x0C4
%define CTX_EAX         0x0B0
%define CTX_EBX         0x0A4
%define CTX_ECX         0x0AC
%define CTX_EDX         0x0A8
%define CTX_ESI         0x0A0
%define CTX_EDI         0x09C
%define CTX_EBP         0x0B4
%define CONTEXT_FULL    0x00010007

%define PAGE_SHIFT      12
%define PAGE_SIZE       4096
%define PAGE_MASK       0xFFFFF000
%define SLOTS_PER_PAGE  1024
%define BITMAP_BYTES    128
%define PAGE_ENTRY_SIZE 136
%define PAGE_HASH_SIZE  4096
%define PAGE_HASH_MASK  (PAGE_HASH_SIZE - 1)

section .text

global _gc_init
global _gc_alloc
global _gc_collect
global _gc_force
global _gc_status
global _gc_alloc_ptr_array
global _thread_list_head
global _thread_list_lock
global _thread_lock_state

extern gc_data_start_label
extern gc_data_end_label
extern gc_bss_start_label
extern gc_bss_end_label
extern gc_static_data_start_label
extern gc_static_data_end_label
extern gc_static_bss_start_label
extern gc_static_bss_end_label

extern _AllocMemory
extern _FreeMemory
extern _ExitProcess@4
extern _InitializeSRWLock@4
extern _AcquireSRWLockShared@4
extern _ReleaseSRWLockShared@4
extern _AcquireSRWLockExclusive@4
extern _ReleaseSRWLockExclusive@4
extern _SuspendThread@4
extern _ResumeThread@4
extern _GetThreadContext@8
extern _GetCurrentThreadId@0

section .data
alloc_list:       dd 0
total_allocated:  dd 0
stack_bottom:     dd 0
_thread_list_head:  dd 0

section .bss
mark_stack_ptr:   resd 1
mark_stack:       resd MARK_STACK_SIZE
stack_overflow:   resd 1
gc_lock:          resd 1
gc_snap_handles:  resd MAX_THREADS
gc_snap_stacks:   resd MAX_THREADS
gc_snap_count:    resd 1
gc_context_buf:   resb CONTEXT_SIZE
page_table:       resd PAGE_HASH_SIZE
_thread_list_lock:  resd 1
_thread_lock_state: resd 1

section .text

_gc_init:
    mov eax, dword [fs:4]
    mov [stack_bottom], eax
    mov dword [alloc_list], 0
    mov dword [total_allocated], 0
    mov dword [stack_overflow], 0
    mov dword [_thread_list_head], 0
    lea eax, [gc_lock]
    push eax
    call _InitializeSRWLock@4
    lea eax, [_thread_list_lock]
    push eax
    call _InitializeSRWLock@4
    mov dword [_thread_lock_state], 2
    ret

; ------------------------------------------------------------
; 页位图辅助
; ------------------------------------------------------------

; 输入：eax = page_addr（页对齐）
; 输出：eax = PageEntry* 或 0
; 破坏：ecx, edx
page_find:
    mov ecx, eax
    shr ecx, PAGE_SHIFT
    and ecx, PAGE_HASH_MASK
    mov edx, [page_table + ecx*4]
.next:
    test edx, edx
    jz .not_found
    cmp [edx + 4], eax
    je .found
    mov edx, [edx]
    jmp .next
.not_found:
    xor eax, eax
    ret
.found:
    mov eax, edx
    ret

; 输入：eax = page_addr
; 输出：eax = PageEntry* 或 0（OOM）
; 破坏：ecx, edx, esi, edi
page_get_or_create:
    push ebx
    mov ebx, eax
    call page_find
    test eax, eax
    jnz .done

    push 0
    push PAGE_ENTRY_SIZE
    call _AllocMemory
    add esp, 8
    test eax, eax
    jz .done

    push edi
    mov edi, eax
    push eax
    mov ecx, PAGE_ENTRY_SIZE / 4
    xor eax, eax
    rep stosd
    pop eax
    pop edi

    mov [eax + 4], ebx

    mov ecx, ebx
    shr ecx, PAGE_SHIFT
    and ecx, PAGE_HASH_MASK
    mov edx, [page_table + ecx*4]
    mov [eax], edx
    mov [page_table + ecx*4], eax
.done:
    pop ebx
    ret

; 输入：eax = user_ptr
; 在页位图中登记为对象起始
; 破坏：ecx, edx, esi, edi
page_register:
    push ebx
    mov ebx, eax
    and eax, PAGE_MASK
    call page_get_or_create
    test eax, eax
    jz .done
    mov ecx, ebx
    shr ecx, 2
    and ecx, SLOTS_PER_PAGE - 1
    mov edx, ecx
    shr edx, 3
    and ecx, 7
    mov bl, 1
    shl bl, cl
    or [eax + 8 + edx], bl
.done:
    pop ebx
    ret

; 输入：eax = user_ptr
; 从页位图注销
; 破坏：ecx, edx, esi, edi
page_unregister:
    push ebx
    mov ebx, eax
    and eax, PAGE_MASK
    call page_find
    test eax, eax
    jz .done
    mov ecx, ebx
    shr ecx, 2
    and ecx, SLOTS_PER_PAGE - 1
    mov edx, ecx
    shr edx, 3
    and ecx, 7
    mov bl, 1
    shl bl, cl
    not bl
    and [eax + 8 + edx], bl
.done:
    pop ebx
    ret

; ------------------------------------------------------------
; 分配/释放
; ------------------------------------------------------------
_gc_alloc:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi

    lea eax, [gc_lock]
    push eax
    call _AcquireSRWLockExclusive@4

    cmp dword [stack_overflow], 0
    jne .skip_gc_check
    mov eax, [total_allocated]
    cmp eax, 16777216
    jae .need_gc_locked
.skip_gc_check:

    push dword [ebp+8]
    call alloc_impl
    add esp, 4
    test eax, eax
    jnz .alloc_ok

    lea eax, [gc_lock]
    push eax
    call _ReleaseSRWLockExclusive@4
    jmp .do_gc_unlocked

.need_gc_locked:
    call _gc_collect_body

    push dword [ebp+8]
    call alloc_impl
    add esp, 4
    test eax, eax
    jnz .alloc_ok

    push 1
    call _ExitProcess@4

.do_gc_unlocked:
    call _gc_collect

    lea eax, [gc_lock]
    push eax
    call _AcquireSRWLockExclusive@4

    push dword [ebp+8]
    call alloc_impl
    add esp, 4
    test eax, eax
    jnz .alloc_ok

    push 1
    call _ExitProcess@4

.alloc_ok:
    push eax
    lea eax, [gc_lock]
    push eax
    call _ReleaseSRWLockExclusive@4
    pop eax

.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

alloc_impl:
    push ebp
    mov ebp, esp
    push ebx
    mov eax, [ebp+8]
    add eax, HEADER_SIZE
    push 0
    push eax
    call _AllocMemory
    add esp, 8
    test eax, eax
    jz .fail
    mov edx, eax
    lea ebx, [edx + HEADER_SIZE]
    mov ecx, [ebp+8]
    mov eax, [alloc_list]
    mov [edx], eax
    mov [alloc_list], ebx
    mov [edx+4], ecx
    mov dword [edx+8], 0
    mov dword [edx+12], 0
    add [total_allocated], ecx

    mov eax, ebx
    call page_register

    mov eax, ebx
    pop ebx
    pop ebp
    ret
.fail:
    pop ebx
    pop ebp
    ret

_gc_collect:
    lea eax, [gc_lock]
    push eax
    call _AcquireSRWLockExclusive@4

    call _gc_collect_body

    lea eax, [gc_lock]
    push eax
    call _ReleaseSRWLockExclusive@4
    ret

_gc_collect_body:
    pushad
    cld
    mov dword [mark_stack_ptr], 0
    mov dword [stack_overflow], 0

    call gc_snapshot_threads
    call gc_suspend_threads

    mov edi, esp
    mov esi, [stack_bottom]
    call scan_memory_range

    mov edi, gc_data_start_label
    mov esi, gc_data_end_label
    call scan_memory_range
    mov edi, gc_bss_start_label
    mov esi, gc_bss_end_label
    call scan_memory_range
    mov edi, gc_static_data_start_label
    mov esi, gc_static_data_end_label
    call scan_memory_range
    mov edi, gc_static_bss_start_label
    mov esi, gc_static_bss_end_label
    call scan_memory_range

    call gc_scan_other_threads
    call gc_resume_threads

    call process_mark_stack
    call sweep

    mov edi, mark_stack
    mov ecx, MARK_STACK_SIZE
    xor eax, eax
    rep stosd

    mov dword [stack_overflow], 0

    popad
    ret

gc_snapshot_threads:
    push ebp
    mov ebp, esp
    push esi
    push edi

    lea eax, [_thread_list_lock]
    push eax
    call _AcquireSRWLockShared@4

    mov dword [gc_snap_count], 0
    call _GetCurrentThreadId@0
    mov edi, eax

    mov esi, [_thread_list_head]
.snap_loop:
    test esi, esi
    jz .snap_done
    cmp [esi+8], edi
    je .snap_next
    mov eax, [gc_snap_count]
    cmp eax, MAX_THREADS
    jae .snap_done
    mov ecx, [esi+4]
    mov [gc_snap_handles + eax*4], ecx
    mov ecx, [esi+12]
    mov [gc_snap_stacks + eax*4], ecx
    inc dword [gc_snap_count]
.snap_next:
    mov esi, [esi]
    jmp .snap_loop
.snap_done:

    lea eax, [_thread_list_lock]
    push eax
    call _ReleaseSRWLockShared@4

    pop edi
    pop esi
    pop ebp
    ret

gc_suspend_threads:
    push ebp
    mov ebp, esp
    push ebx
    push esi

    mov ecx, [gc_snap_count]
    xor ebx, ebx
.sus_loop:
    cmp ebx, ecx
    jae .sus_done
    mov eax, [gc_snap_handles + ebx*4]
    push ecx
    push eax
    call _SuspendThread@4
    pop ecx
    inc ebx
    jmp .sus_loop
.sus_done:

    pop esi
    pop ebx
    pop ebp
    ret

gc_resume_threads:
    push ebp
    mov ebp, esp
    push ebx
    push esi

    mov ecx, [gc_snap_count]
    xor ebx, ebx
.res_loop:
    cmp ebx, ecx
    jae .res_done
    mov eax, [gc_snap_handles + ebx*4]
    push ecx
    push eax
    call _ResumeThread@4
    pop ecx
    inc ebx
    jmp .res_loop
.res_done:

    pop esi
    pop ebx
    pop ebp
    ret

gc_scan_other_threads:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi

    mov ecx, [gc_snap_count]
    xor ebx, ebx
.each_thread:
    cmp ebx, ecx
    jae .all_done

    mov eax, [gc_snap_handles + ebx*4]
    mov dword [gc_context_buf], CONTEXT_FULL
    lea edx, [gc_context_buf]
    push edx
    push eax
    call _GetThreadContext@8
    test eax, eax
    jz .skip_thread

    mov edi, [gc_context_buf + CTX_ESP]
    mov esi, [gc_snap_stacks + ebx*4]
    cmp edi, esi
    jae .scan_regs
    push ecx
    call scan_memory_range
    pop ecx

.scan_regs:
    mov eax, [gc_context_buf + CTX_EAX]
    push ecx
    call maybe_mark
    mov eax, [gc_context_buf + CTX_EBX]
    call maybe_mark
    mov eax, [gc_context_buf + CTX_ECX]
    call maybe_mark
    mov eax, [gc_context_buf + CTX_EDX]
    call maybe_mark
    mov eax, [gc_context_buf + CTX_ESI]
    call maybe_mark
    mov eax, [gc_context_buf + CTX_EDI]
    call maybe_mark
    mov eax, [gc_context_buf + CTX_EBP]
    call maybe_mark
    pop ecx

.skip_thread:
    inc ebx
    jmp .each_thread

.all_done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

scan_memory_range:
    cmp edi, esi
    jae .done
.loop:
    mov eax, [edi]
    test eax, eax
    jz .skip
    cmp eax, 0x00010000
    jb .skip
    cmp eax, 0x7FFFFFF0
    ja .skip
    test eax, 3
    jnz .skip
    call maybe_mark
.skip:
    add edi, 4
    cmp edi, esi
    jb .loop
.done:
    ret

; 输入：eax = 候选指针
; 用页位图 O(1) 定位对象起始并标记
maybe_mark:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    sub esp, 16

    cmp eax, PAGE_SIZE
    jb .done
    cmp eax, 0x7FFFFFF0
    ja .done

    mov [ebp-16], eax

    and eax, PAGE_MASK
    mov [ebp-20], eax
    call page_find
    test eax, eax
    jz .done
    mov [ebp-24], eax

    mov eax, [ebp-16]
    shr eax, 2
    and eax, SLOTS_PER_PAGE - 1
    mov [ebp-28], eax

.scan:
    mov ecx, [ebp-28]
    mov edx, [ebp-24]
    mov esi, ecx
    shr esi, 3
    movzx eax, byte [edx + 8 + esi]
    and ecx, 7
    bt eax, ecx
    jnc .next

    mov eax, [ebp-28]
    shl eax, 2
    add eax, [ebp-20]

    mov edx, [eax - HEADER_SIZE + 4]
    add edx, eax
    cmp [ebp-16], edx
    jb .found

.next:
    cmp dword [ebp-28], 0
    je .done
    dec dword [ebp-28]
    jmp .scan

.found:
    lea edx, [eax - HEADER_SIZE]
    cmp byte [edx + 8], 0
    jne .done
    mov ecx, [mark_stack_ptr]
    cmp ecx, MARK_STACK_SIZE
    jae .overflow
    mov byte [edx + 8], 1
    mov [mark_stack + ecx*4], eax
    inc dword [mark_stack_ptr]
    jmp .done

.overflow:
    mov dword [stack_overflow], 1

.done:
    add esp, 16
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

process_mark_stack:
.while:
    cmp dword [mark_stack_ptr], 0
    je .done
    dec dword [mark_stack_ptr]
    mov edx, [mark_stack_ptr]
    mov esi, [mark_stack + edx*4]

    cmp esi, PAGE_SIZE
    jb .while
    cmp esi, 0x7FFFFFF0
    ja .while
    test esi, 3
    jnz .while

    test dword [esi - HEADER_SIZE + 12], 1
    jz .regular_scan

    mov edi, [esi - HEADER_SIZE + 4]
    mov ecx, edi
    shr ecx, 2
    jz .while
.ptr_array_loop:
    mov eax, [esi]
    push ecx
    push edi
    call maybe_mark
    pop edi
    pop ecx
    add esi, 4
    dec ecx
    jnz .ptr_array_loop
    jmp .while

.regular_scan:
    mov edi, [esi - HEADER_SIZE + 4]
    cmp edi, 0x10000000
    ja .while

    lea edx, [esi + edi]
    add edx, 3
    and edx, 0xFFFFFFFC
    cmp esi, edx
    jae .while
.scan_loop:
    mov eax, [esi]
    push edx
    call maybe_mark
    pop edx
    add esi, 4
    cmp esi, edx
    jb .scan_loop
    jmp .while
.done:
    ret

sweep:
    cmp dword [stack_overflow], 0
    jne .clear_only

    lea edi, [alloc_list]
.loop:
    mov esi, [edi]
    test esi, esi
    jz .done
    lea edx, [esi - HEADER_SIZE]
    cmp byte [edx+8], 0
    jne .keep
    mov ecx, [edx]
    mov [edi], ecx
    mov eax, [edx+4]
    sub [total_allocated], eax
    mov eax, esi
    push edx
    call page_unregister
    pop edx
    push edx
    call _FreeMemory
    jmp .loop
.keep:
    mov byte [edx+8], 0
    lea edi, [edx]
    jmp .loop
.done:
    ret

.clear_only:
    mov esi, [alloc_list]
.clear_loop:
    test esi, esi
    jz .clear_done
    lea edx, [esi - HEADER_SIZE]
    mov byte [edx+8], 0
    mov esi, [edx]
    jmp .clear_loop
.clear_done:
    ret

_gc_force:
    call _gc_collect
    ret

_gc_status:
    mov eax, [total_allocated]
    ret

; 输入：[ebp+8] = count
; 输出：eax = 指针数组数据起始（count * 4 字节，已清零）
; 数组每个元素被视为 GC 指针
_gc_alloc_ptr_array:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi

    lea eax, [gc_lock]
    push eax
    call _AcquireSRWLockExclusive@4

    cmp dword [stack_overflow], 0
    jne .skip_gc_check
    mov eax, [total_allocated]
    cmp eax, 16777216
    jae .need_gc_locked
.skip_gc_check:

    mov eax, [ebp+8]
    shl eax, 2
    push eax
    call alloc_impl
    add esp, 4
    test eax, eax
    jnz .alloc_ok

    lea eax, [gc_lock]
    push eax
    call _ReleaseSRWLockExclusive@4
    jmp .do_gc_unlocked

.need_gc_locked:
    call _gc_collect_body
    mov eax, [ebp+8]
    shl eax, 2
    push eax
    call alloc_impl
    add esp, 4
    test eax, eax
    jnz .alloc_ok
    push 1
    call _ExitProcess@4

.do_gc_unlocked:
    call _gc_collect
    lea eax, [gc_lock]
    push eax
    call _AcquireSRWLockExclusive@4
    mov eax, [ebp+8]
    shl eax, 2
    push eax
    call alloc_impl
    add esp, 4
    test eax, eax
    jnz .alloc_ok
    push 1
    call _ExitProcess@4

.alloc_ok:
    mov esi, eax
    mov edx, [eax - HEADER_SIZE + 4]
    or dword [eax - HEADER_SIZE + 12], 1

    mov edi, esi
    mov ecx, [ebp+8]
    xor eax, eax
    cld
    rep stosd

    mov eax, esi
    push eax
    lea eax, [gc_lock]
    push eax
    call _ReleaseSRWLockExclusive@4
    pop eax

    pop edi
    pop esi
    pop ebx
    pop ebp
    ret