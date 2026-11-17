; runtime_iocp.asm -- IOCP 骨架
; OVERLAPPED_EX 布局:
;   +0   OVERLAPPED (20 字节)
;   +20  buffer_ptr
;   +24  buffer_len
;   +28  callback
;   +32  env
;   +36  fd
;   +40  WSABUF {len, buf}
;   +48  flags
;   +52  user
;   +56  addr_buf (128)
;   +184 type (0=accept, 1=recv, 2=send)
;   总计 192 字节

extern _CreateIoCompletionPort@16
extern _GetQueuedCompletionStatus@20
extern _CreateThread@24
extern _WSARecv@28
extern _WSASend@28
extern _AcceptEx@32
extern _WSAGetLastError@0
extern _GetLastError@0
extern _closesocket@4
extern _AllocMemory
extern _socket_create
extern _GetProcessHeap@0
extern _HeapAlloc@12
extern _HeapFree@12

global _iocp_init
global _iocp_start_worker
global _socket_accept_async
global _socket_recv_async
global _socket_send_async
global _ov_get_fd
global _ov_get_buffer
global _ov_get_capacity
global _ov_get_user
global _ov_set_user
global _ov_set_callback
global _iocp_get_handle
global _iocp_associate

section .data
_g_iocp_handle dd 0

section .text

_iocp_init:
    push ebp
    mov ebp, esp
    push 0
    push 0
    push 0
    push -1
    call _CreateIoCompletionPort@16
    mov [_g_iocp_handle], eax
    test eax, eax
    jz .fail
    mov eax, 1
    pop ebp
    ret
.fail:
    xor eax, eax
    pop ebp
    ret

_iocp_start_worker:
    push ebp
    mov ebp, esp
    push 0
    push 0
    push dword [ebp+8]
    push _iocp_worker_impl
    push 0
    push 0
    call _CreateThread@24
    pop ebp
    ret

_iocp_worker_impl:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    sub esp, 16
.loop:
    push -1
    lea eax, [ebp - 12]
    push eax
    lea eax, [ebp - 8]
    push eax
    lea eax, [ebp - 4]
    push eax
    push dword [_g_iocp_handle]
    call _GetQueuedCompletionStatus@20
    mov [ebp - 16], eax
    test eax, eax
    jnz .have_event
    cmp dword [ebp - 12], 0
    je .loop
.have_event:
    mov edi, [ebp - 12]
    test edi, edi
    jz .loop
    mov eax, [edi + 28]
    test eax, eax
    jz .loop
    xor edx, edx
    cmp dword [ebp - 16], 0
    jne .dispatch
    call _GetLastError@0
    mov edx, eax
    mov eax, [edi + 28]
.dispatch:
    push edx
    push dword [ebp - 4]
    push edi
    call eax
    add esp, 12
    mov edi, [ebp - 12]
    test edi, edi
    jz .loop
    mov ecx, [edi + 184]
    cmp ecx, 1
    jne .free_ctx
    push dword [edi + 20]
    call _iocp_free
    add esp, 4
    mov edi, [ebp - 12]
.free_ctx:
    push edi
    call _iocp_free
    add esp, 4
    jmp .loop

_socket_accept_async:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov esi, [ebp+8]
    push 192
    call _iocp_alloc
    add esp, 4
    test eax, eax
    jnz .alloc_ok
    mov eax, -3
    jmp .ret
.alloc_ok:
    mov edi, eax
    push 192
    push edi
    call _mem_zero
    add esp, 8
    push 6
    push 1
    push 2
    call _socket_create_internal
    add esp, 12
    test eax, eax
    jns .sock_ok
    push edi
    call _iocp_free
    add esp, 4
    mov eax, -4
    jmp .ret
.sock_ok:
    mov ebx, eax
    mov [edi + 36], ebx
    mov eax, [ebp+12]
    mov [edi + 28], eax
    push 0
    push edi
    push dword [_g_iocp_handle]
    push ebx
    call _CreateIoCompletionPort@16
    test eax, eax
    jnz .iocp_ok
    push ebx
    call _closesocket@4
    push edi
    call _iocp_free
    add esp, 4
    mov eax, -5
    jmp .ret
.iocp_ok:
    lea eax, [edi + 56]
    push edi
    push 0
    push 32
    push 32
    push 0
    push eax
    push ebx
    push esi
    call _AcceptEx@32
    test eax, eax
    jnz .accept_ok
    call _WSAGetLastError@0
    cmp eax, 997
    je .accept_ok
    push ebx
    call _closesocket@4
    push edi
    call _iocp_free
    add esp, 4
    mov eax, -6
    jmp .ret
.accept_ok:
    mov eax, 1
.ret:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

_socket_recv_async:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov esi, [ebp+8]
    mov ebx, [ebp+12]
    push 192
    call _iocp_alloc
    add esp, 4
    test eax, eax
    jz .fail1
    mov edi, eax
    push 192
    push edi
    call _mem_zero
    add esp, 8
    push ebx
    call _iocp_alloc
    add esp, 4
    test eax, eax
    jz .fail_ctx
    mov [edi + 20], eax
    mov [edi + 24], ebx
    mov eax, [ebp+16]
    mov [edi + 28], eax
    mov [edi + 36], esi
    mov dword [edi + 184], 1
    lea ecx, [edi + 40]
    mov [ecx + 0], ebx
    mov eax, [edi + 20]
    mov [ecx + 4], eax
    mov dword [edi + 48], 0
    push 0
    push edi
    lea eax, [edi + 48]
    push eax
    push 0
    push 1
    push ecx
    push esi
    call _WSARecv@28
    cmp eax, 0
    je .ok
    call _WSAGetLastError@0
    cmp eax, 997
    je .ok
    push dword [edi + 20]
    call _iocp_free
    add esp, 4
    jmp .fail_ctx
.ok:
    mov eax, [edi + 20]
    jmp .ret
.fail_ctx:
    push edi
    call _iocp_free
    add esp, 4
.fail1:
    mov eax, -1
.ret:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

_socket_send_async:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov esi, [ebp+8]
    mov ebx, [ebp+12]
    mov ecx, [ebp+16]
    push 192
    call _iocp_alloc
    add esp, 4
    test eax, eax
    jz .fail
    mov edi, eax
    push 192
    push edi
    call _mem_zero
    add esp, 8
    mov [edi + 20], ebx
    mov [edi + 24], ecx
    mov eax, [ebp+20]
    mov [edi + 28], eax
    mov [edi + 36], esi
    mov dword [edi + 184], 2
    lea eax, [edi + 40]
    mov [eax + 0], ecx
    mov [eax + 4], ebx
    mov dword [edi + 48], 0
    push 0
    push edi
    push dword [edi + 48]
    push 0
    push 1
    push eax
    push esi
    call _WSASend@28
    cmp eax, 0
    je .ok
    call _WSAGetLastError@0
    cmp eax, 997
    je .ok
    push edi
    call _iocp_free
    add esp, 4
    jmp .fail
.ok:
    mov eax, 1
    jmp .ret
.fail:
    xor eax, eax
.ret:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

_ov_get_fd:
    mov eax, [esp+4]
    mov eax, [eax + 36]
    ret

_ov_get_buffer:
    mov eax, [esp+4]
    mov eax, [eax + 20]
    ret

_ov_get_capacity:
    mov eax, [esp+4]
    mov eax, [eax + 24]
    ret

_ov_get_user:
    mov eax, [esp+4]
    mov eax, [eax + 52]
    ret

_ov_set_user:
    mov eax, [esp+4]
    mov ecx, [esp+8]
    mov [eax + 52], ecx
    ret

_ov_set_callback:
    mov eax, [esp+4]
    mov ecx, [esp+8]
    mov [eax + 28], ecx
    ret

_mem_zero:
    push ebp
    mov ebp, esp
    push edi
    mov edi, [ebp+8]
    mov ecx, [ebp+12]
    xor eax, eax
    cld
    rep stosb
    pop edi
    pop ebp
    ret

_iocp_alloc:
    push ebp
    mov ebp, esp
    call _GetProcessHeap@0
    test eax, eax
    jz .fail
    push dword [ebp+8]
    push 8
    push eax
    call _HeapAlloc@12
    pop ebp
    ret
.fail:
    xor eax, eax
    pop ebp
    ret

_iocp_free:
    push ebp
    mov ebp, esp
    cmp dword [ebp+8], 0
    je .done
    call _GetProcessHeap@0
    test eax, eax
    jz .done
    push dword [ebp+8]
    push 0
    push eax
    call _HeapFree@12
.done:
    pop ebp
    ret

_iocp_get_handle:
    mov eax, [_g_iocp_handle]
    ret

_iocp_associate:
    push ebp
    mov ebp, esp
    push 0
    push dword [ebp+12]
    push dword [_g_iocp_handle]
    push dword [ebp+8]
    call _CreateIoCompletionPort@16
    test eax, eax
    jz .fail
    mov eax, 1
    pop ebp
    ret
.fail:
    xor eax, eax
    pop ebp
    ret

_socket_create_internal:
    push ebp
    mov ebp, esp
    push dword [ebp+16]
    push dword [ebp+12]
    push dword [ebp+8]
    call _socket_create
    add esp, 12
    pop ebp
    ret