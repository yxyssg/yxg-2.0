global _print
global _set_float_mode
global _set_float_precision
global _print_err

extern _GetStdHandle@4
extern _WriteConsoleA@20
extern _WriteFile@20
extern _InitializeCriticalSection@4
extern _EnterCriticalSection@4
extern _LeaveCriticalSection@4

STD_OUTPUT_HANDLE equ -11
INVALID_HANDLE_VALUE  equ -1
NULL equ 0

FLOAT_MODE_MINIMAL equ 0
FLOAT_MODE_FIXED equ 1

DEFAULT_FLOAT_PRECISION equ 6
MAX_FLOAT_PRECISION equ 6

section .data
    stdout_handle dd 0
    float_mode db FLOAT_MODE_MINIMAL
    float_precision dd DEFAULT_FLOAT_PRECISION

    true_str db 'true', 0
    false_str db 'false', 0
    zero_str db '0.0', 0
    minus_sign db '-', 0

    epsilon_iter dd 0x3D100000

section .rodata
  _err_true  db `true`,0
  _err_false db `false`,0

section .bss
    buffer resb 256
    chars_written resd 1
    float_buf resb 64
    _err_buf resb 40
    _err_written resd 1

    print_cs resb 24
    print_err_cs resb 24
    print_cs_state resd 1
    print_err_cs_state resd 1

section .text

__get_stdout:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov eax, [stdout_handle]
    test eax, eax
    jnz .done
    push STD_OUTPUT_HANDLE
    call _GetStdHandle@4
    cmp eax, INVALID_HANDLE_VALUE
    je .done
    mov [stdout_handle], eax
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

__write_string:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov esi, [ebp+8]
    mov ebx, [ebp+12]
    test ebx, ebx
    jz .done
    call __get_stdout
    cmp eax, INVALID_HANDLE_VALUE
    je .done
    push NULL
    push chars_written
    push ebx
    push esi
    push eax
    call _WriteFile@20
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

__itoa:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov eax, [ebp+8]
    test eax, eax
    jns .positive
    neg eax
.positive:
    mov edi, buffer + 255
    mov byte [edi], 0
    mov ebx, 10
    xor ecx, ecx
    test eax, eax
    jnz .convert
    dec edi
    mov byte [edi], '0'
    inc ecx
    jmp .done
.convert:
    xor edx, edx
    div ebx
    add dl, '0'
    dec edi
    mov [edi], dl
    inc ecx
    test eax, eax
    jnz .convert
.done:
    mov eax, edi
    mov edx, ecx
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

__utoa:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov eax, [ebp+8]
    mov edi, buffer + 255
    mov byte [edi], 0
    mov ebx, 10
    xor ecx, ecx
    test eax, eax
    jnz .convert
    dec edi
    mov byte [edi], '0'
    inc ecx
    jmp .done
.convert:
    xor edx, edx
    div ebx
    add dl, '0'
    dec edi
    mov [edi], dl
    inc ecx
    test eax, eax
    jnz .convert
.done:
    mov eax, edi
    mov edx, ecx
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

__ftoa:
    push ebp
    mov ebp, esp
    sub esp, 32
    push ebx
    push esi
    push edi

    fld dword [ebp+8]

    fldz
    fcomip st1
    je .zero_case
    fld dword [ebp+8]
    fabs
    fld dword [epsilon_iter]
    fcomip st1
    fstp st0
    jae .zero_case

    fld dword [ebp+8]
    ftst
    fstsw ax
    sahf
    jae .positive
    mov byte [ebp-1], 1
    fchs
    jmp .pos_done
.positive:
    mov byte [ebp-1], 0
.pos_done:

    mov eax, [ebp+16]
    cmp al, FLOAT_MODE_FIXED
    je .fixed_mode
    mov ecx, [float_precision]
    cmp ecx, MAX_FLOAT_PRECISION
    jle .prec_done
    mov ecx, MAX_FLOAT_PRECISION
    jmp .prec_done
.fixed_mode:
    mov ecx, [ebp+12]
    cmp ecx, 0
    jge .fixed_prec_ok
    xor ecx, ecx
.fixed_prec_ok:
    cmp ecx, MAX_FLOAT_PRECISION
    jle .prec_done
    mov ecx, MAX_FLOAT_PRECISION
.prec_done:
    mov [ebp-12], ecx

    mov eax, 1
    mov ebx, ecx
    test ebx, ebx
    jz .pow_done
.pow_loop:
    imul eax, 10
    dec ebx
    jnz .pow_loop
.pow_done:
    mov [ebp-16], eax

    fld st0
    fimul dword [ebp-16]
    frndint
    fistp dword [ebp-8]
    fstp st0

    mov eax, [ebp-8]
    xor edx, edx
    div dword [ebp-16]
    mov [ebp-20], edx
    mov [ebp-24], eax

    mov edi, float_buf
    cmp byte [ebp-1], 0
    je .no_sign
    mov byte [edi], '-'
    inc edi
.no_sign:
    push dword [ebp-24]
    call __itoa
    add esp, 4
    push esi
    mov esi, eax
    mov ecx, edx
    rep movsb
    pop esi
    mov [ebp-28], edi

    mov eax, [ebp-20]
    cmp eax, [ebp-16]
    jl .no_carry
    sub eax, [ebp-16]
    mov [ebp-20], eax
    inc dword [ebp-24]
    mov edi, float_buf
    cmp byte [ebp-1], 0
    je .no_sign2
    mov byte [edi], '-'
    inc edi
.no_sign2:
    push dword [ebp-24]
    call __itoa
    add esp, 4
    push esi
    mov esi, eax
    mov ecx, edx
    rep movsb
    pop esi
    mov [ebp-28], edi
.no_carry:

    mov ecx, [ebp-12]
    test ecx, ecx
    jz .no_frac_part

    cmp dword [ebp-20], 0
    jne .normal_frac

    cmp byte [ebp+16], FLOAT_MODE_MINIMAL
    jne .normal_frac

    mov edi, [ebp-28]
    mov byte [edi], '.'
    inc edi
    mov byte [edi], '0'
    inc edi
    jmp .finish

.normal_frac:
    mov byte [edi], '.'
    inc edi
    mov [ebp-28], edi

    mov eax, [ebp-20]
    mov ebx, ecx
    push edi
    add edi, ebx
    dec edi
    std
    pushfd
    mov ebx, 10
.digit_loop:
    xor edx, edx
    div ebx
    add dl, '0'
    mov [edi], dl
    dec edi
    loop .digit_loop
    popfd
    cld
    pop edi
    add edi, [ebp-12]

    cmp byte [ebp+16], FLOAT_MODE_MINIMAL
    jne .finish

    dec edi
    mov esi, edi
.find_nonzero:
    cmp esi, [ebp-28]
    jb .all_zero_found
    cmp byte [esi], '0'
    jne .found
    dec esi
    jmp .find_nonzero

.all_zero_found:
    mov edi, [ebp-28]
    mov byte [edi], '0'
    inc edi
    jmp .finish

.found:
    inc esi
    mov edi, esi
    jmp .finish

.no_frac_part:
    mov edi, [ebp-28]

.finish:
    mov edx, edi
    sub edx, float_buf
    mov eax, float_buf
    pop edi
    pop esi
    pop ebx
    mov esp, ebp
    pop ebp
    ret

.zero_case:
    fstp st0
    mov eax, zero_str
    mov edx, 3
    pop edi
    pop esi
    pop ebx
    mov esp, ebp
    pop ebp
    ret

__print_cs_slow_init:
    mov eax, 0
    mov ecx, 1
    lock cmpxchg dword [print_cs_state], ecx
    jne .wait
    lea eax, [print_cs]
    push eax
    call _InitializeCriticalSection@4
    mov dword [print_cs_state], 2
    ret
.wait:
    cmp dword [print_cs_state], 2
    jne .wait
    ret

__print_err_cs_slow_init:
    mov eax, 0
    mov ecx, 1
    lock cmpxchg dword [print_err_cs_state], ecx
    jne .wait
    lea eax, [print_err_cs]
    push eax
    call _InitializeCriticalSection@4
    mov dword [print_err_cs_state], 2
    ret
.wait:
    cmp dword [print_err_cs_state], 2
    jne .wait
    ret

_print:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi

    cmp dword [print_cs_state], 2
    je .cs_ready
    call __print_cs_slow_init
.cs_ready:
    lea eax, [print_cs]
    push eax
    call _EnterCriticalSection@4

    mov eax, [ebp+12]

    cmp eax, 0
    je .int_case
    cmp eax, 1
    je .float_case
    cmp eax, 2
    je .char_case
    cmp eax, 3
    je .string_case
    cmp eax, 4
    je .bool_case
    cmp eax, 5
    je .uint_case
    cmp eax, 6
    je .ufloat_case
    jmp .done

.int_case:
    mov eax, [ebp+8]
    test eax, eax
    jns .int_positive

    push eax
    push 1
    push minus_sign
    call __write_string
    add esp, 8
    pop eax
    neg eax
    push eax
    call __itoa
    add esp, 4
    push edx
    push eax
    call __write_string
    add esp, 8
    jmp .done
.int_positive:
    push eax
    call __itoa
    add esp, 4
    push edx
    push eax
    call __write_string
    add esp, 8
    jmp .done

.float_case:
    push dword [float_mode]
    push dword [float_precision]
    push dword [ebp+8]
    call __ftoa
    add esp, 12
    push edx
    push eax
    call __write_string
    add esp, 8
    jmp .done

.char_case:
    mov eax, [ebp+8]
    cmp eax, 255
    jbe .char_is_value
    mov eax, [eax]
    and eax, 0xFF
.char_is_value:
    push eax
    call __write_char
    add esp, 4
    jmp .done

.string_case:
    mov esi, [ebp+8]
    xor ecx, ecx
.str_len_loop:
    cmp byte [esi + ecx], 0
    je .str_len_done
    inc ecx
    jmp .str_len_loop
.str_len_done:
    push ecx
    push esi
    call __write_string
    add esp, 8
    jmp .done

.bool_case:
    mov eax, [ebp+8]
    test eax, eax
    jz .bool_false
    cmp eax, 1
    je .bool_true
    mov eax, [eax]
    test eax, eax
    jz .bool_false
.bool_true:
    mov eax, true_str
    mov edx, 4
    jmp .bool_out
.bool_false:
    mov eax, false_str
    mov edx, 5
.bool_out:
    push edx
    push eax
    call __write_string
    add esp, 8
    jmp .done

.uint_case:
    push dword [ebp+8]
    call __utoa
    add esp, 4
    push edx
    push eax
    call __write_string
    add esp, 8
    jmp .done

.ufloat_case:
    push dword [float_mode]
    push dword [float_precision]
    push dword [ebp+8]
    call __ftoa
    add esp, 12
    push edx
    push eax
    call __write_string
    add esp, 8

.done:
    pop edi
    pop esi
    pop ebx
    lea eax, [print_cs]
    push eax
    call _LeaveCriticalSection@4
    pop ebp
    ret

__write_char:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov eax, [ebp+8]
    mov [buffer], al
    push 1
    push buffer
    call __write_string
    add esp, 8
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

_set_float_mode:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov eax, [ebp+8]
    and eax, 1
    mov [float_mode], al
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

_set_float_precision:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov eax, [ebp+8]
    test eax, eax
    jns .positive
    mov byte [float_mode], FLOAT_MODE_MINIMAL
    jmp .done
.positive:
    cmp eax, MAX_FLOAT_PRECISION
    jle .set_prec
    mov eax, MAX_FLOAT_PRECISION
.set_prec:
    mov [float_precision], eax
    mov byte [float_mode], FLOAT_MODE_FIXED
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

; void _print_err(int value, int type)
_print_err:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi

    cmp dword [print_err_cs_state], 2
    je .cs_ready
    call __print_err_cs_slow_init
.cs_ready:
    lea eax, [print_err_cs]
    push eax
    call _EnterCriticalSection@4

    mov eax, [ebp+8]      ; value
    mov ecx, [ebp+12]     ; type
    cmp ecx, 3
    je .do_str
    cmp ecx, 2
    je .do_char
    cmp ecx, 4
    je .do_bool
    cmp ecx, 1
    je .do_float
.do_int:
    mov edi, _err_buf + 38
    mov byte [edi], 0
    mov ebx, 10
    mov ecx, eax
    test eax, eax
    jns .int_loop
    neg eax
.int_loop:
    xor edx, edx
    div ebx
    add dl, '0'
    dec edi
    mov [edi], dl
    test eax, eax
    jnz .int_loop
    test ecx, ecx
    jns .int_done
    dec edi
    mov byte [edi], '-'
.int_done:
    mov esi, edi
    mov ecx, _err_buf + 38
    sub ecx, edi
    jmp .write
.do_char:
    mov byte [_err_buf], al
    mov byte [_err_buf+1], 0
    mov esi, _err_buf
    mov ecx, 1
    jmp .write
.do_str:
    mov esi, eax
    mov edi, esi
.str_len:
    cmp byte [edi], 0
    je .str_done
    inc edi
    jmp .str_len
.str_done:
    mov ecx, edi
    sub ecx, esi
    jmp .write
.do_bool:
    test eax, eax
    jz .is_false
    mov esi, _err_true
    mov ecx, 4
    jmp .write
.is_false:
    mov esi, _err_false
    mov ecx, 5
    jmp .write
.do_float:
    push dword [float_mode]
    push dword [float_precision]
    push dword [ebp+8]
    call __ftoa
    add esp, 12
    mov esi, eax
    mov ecx, edx
    jmp .write
.write:
    mov edi, ecx
    push -12
    call _GetStdHandle@4
    cmp eax, INVALID_HANDLE_VALUE
    je .write_done
    push 0
    push _err_written
    push edi
    push esi
    push eax
    call _WriteConsoleA@20
    test eax, eax
    jnz .write_done
    push -12
    call _GetStdHandle@4
    cmp eax, INVALID_HANDLE_VALUE
    je .write_done
    push NULL
    push _err_written
    push edi
    push esi
    push eax
    call _WriteFile@20
.write_done:
    pop edi
    pop esi
    pop ebx
    lea eax, [print_err_cs]
    push eax
    call _LeaveCriticalSection@4
    pop ebp
    ret
    ret
