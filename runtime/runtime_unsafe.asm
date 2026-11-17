%define UNSAFE_HEADER_SIZE  8
%define MAGIC_ALLOC         0x0A110C
%define MAGIC_ADDRESS       0x0ADD00

section .text

global _unsafe_alloc
global _unsafe_address_alloc
global _unsafe_free

extern _AllocMemory
extern _FreeMemory
extern _VirtualAlloc@16
extern _VirtualFree@12

; ------------------------------------------------------------
; void* _unsafe_alloc(int elem_size, int count)
; 使用 HeapAlloc，头部标记为 MAGIC_ALLOC
; ------------------------------------------------------------
_unsafe_alloc:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    mov ecx, [ebp+12]
    mul ecx
    mov ebx, eax

    add eax, UNSAFE_HEADER_SIZE
    push 0
    push eax
    call _AllocMemory
    add esp, 8
    test eax, eax
    jz .done

    mov dword [eax], MAGIC_ALLOC
    mov [eax+4], ebx
    add eax, UNSAFE_HEADER_SIZE
.done:
    pop ebp
    ret

; ------------------------------------------------------------
; void* _unsafe_address_alloc(int addr, int size)
; 在指定地址分配内存，使用 VirtualAlloc(MEM_RESERVE|MEM_COMMIT)
; 头部标记为 MAGIC_ADDRESS
; ------------------------------------------------------------
_unsafe_address_alloc:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    mov ebx, [ebp+12]

    mov ecx, ebx
    add ecx, UNSAFE_HEADER_SIZE

    push 0x04
    push 0x3000
    push ecx
    push eax
    call _VirtualAlloc@16
    test eax, eax
    jz .done

    mov dword [eax], MAGIC_ADDRESS
    mov [eax+4], ebx
    add eax, UNSAFE_HEADER_SIZE
.done:
    pop ebp
    ret

; ------------------------------------------------------------
; void _unsafe_free(void* ptr)
; 根据头部 magic 选择释放方式
; ------------------------------------------------------------
_unsafe_free:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    test eax, eax
    jz .done

    sub eax, UNSAFE_HEADER_SIZE
    mov ecx, [eax]

    cmp ecx, MAGIC_ALLOC
    je .free_heap

    cmp ecx, MAGIC_ADDRESS
    je .free_virtual

    jmp .done

.free_heap:
    push eax
    call _FreeMemory
    jmp .done

.free_virtual:
    push 0x8000
    push 0
    push eax
    call _VirtualFree@12

.done:
    pop ebp
    ret