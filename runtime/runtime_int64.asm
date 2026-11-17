; runtime_int64.asm  —  64 位整数运算运行时 (不依赖 libgcc)
; 调用约定:
;   被除数  edx:eax
;   除数    ecx:ebx
; 返回:
;   _div64 / _udiv64   商  在 edx:eax
;   _mod64 / _umod64   余数 在 edx:eax

section .data
    int64_min_str db "-9223372036854775808", 0
    zero_str db "0", 0

section .bss
    int64_str_buf resb 24

section .text
extern _gc_alloc

; ─── 无符号核心：edx:eax ÷ ecx:ebx → 商 edx:eax, 余数 ecx:ebx ───
_xg_udiv64_core:
    push esi
    push edi
    push ebp

    ; 除数为 0 → 返回 0
    mov  esi, ecx
    or   esi, ebx
    jz   .core_zero

    ; 除数 < 2³² → 快速路径
    cmp  ecx, 0
    jne  .core_full

    push eax
    mov  eax, edx
    xor  edx, edx
    div  ebx
    mov  edi, eax         ; edi = 商高
    pop  eax
    div  ebx              ; eax = 商低, edx = 余数
    mov  ecx, 0
    mov  ebx, edx         ; ecx:ebx = 余数
    mov  edx, edi         ; edx:eax = 商
    jmp  .core_ret

.core_full:
    ; 除数保存在 edi:esi（利用 esi 暂存除数低）
    mov  edi, ecx          ; 除数高
    mov  esi, ebx          ; 除数低

    ; 余数初始化为 0 (ebp:ebx)
    xor  ebp, ebp          ; 余数高
    xor  ebx, ebx          ; 余数低

    ; 被除数 edx:eax，商将逐位构建在原被除数位置
    mov  ecx, 64           ; 循环计数器

.core_loop:
    ; 被除数 edx:eax 左移 1 位，最高位进入 CF
    shl  eax, 1
    rcl  edx, 1
    ; 余数 ebp:ebx 带进位左移，吸收被除数移出的位
    rcl  ebx, 1
    rcl  ebp, 1

    ; 比较余数与除数 (ebp:ebx >= edi:esi ?)
    cmp  ebp, edi
    ja   .core_sub
    jb   .core_nosub
    cmp  ebx, esi
    jb   .core_nosub

.core_sub:
    sub  ebx, esi
    sbb  ebp, edi
    inc  eax               ; 商最低位置 1（左移后 eax 最低位为 0）

.core_nosub:
    dec  ecx
    jnz  .core_loop

    ; 循环结束：商在 edx:eax，余数在 ebp:ebx
    ; 将余数移至 ecx:ebx 满足调用约定
    mov  ecx, ebp
    ; ebx 已是余数低
    jmp  .core_ret

.core_zero:
    xor  eax, eax
    xor  edx, edx
    xor  ebx, ebx
    xor  ecx, ecx

.core_ret:
    pop  ebp
    pop  edi
    pop  esi
    ret


; ─── uint64 / uint64 → 商 ───
global _udiv64
_udiv64:
    call _xg_udiv64_core
    ret

; ─── uint64 % uint64 → 余数 ───
global _umod64
_umod64:
    call _xg_udiv64_core
    mov  eax, ebx
    mov  edx, ecx
    ret

; ─── int64 / int64 → 商 ───
global _div64
_div64:
    push ebp
    ; 符号 = (被除数 ^ 除数) 的符号位
    mov  ebp, edx
    xor  ebp, ecx

    ; 被除数取绝对值
    test edx, edx
    jns  .div_a_ok
    not  eax
    not  edx
    add  eax, 1
    adc  edx, 0
.div_a_ok:
    ; 除数取绝对值
    test ecx, ecx
    jns  .div_b_ok
    not  ebx
    not  ecx
    add  ebx, 1
    adc  ecx, 0
.div_b_ok:
    call _xg_udiv64_core

    ; 恢复符号
    test ebp, ebp
    jns  .div_out
    not  eax
    not  edx
    add  eax, 1
    adc  edx, 0
.div_out:
    pop  ebp
    ret

; ─── int64 % int64 → 余数 ───
global _mod64
_mod64:
    push ebp
    ; 余数符号 = 被除数符号
    mov  ebp, edx

    ; 被除数取绝对值
    test edx, edx
    jns  .mod_a_ok
    not  eax
    not  edx
    add  eax, 1
    adc  edx, 0
.mod_a_ok:
    ; 除数取绝对值
    test ecx, ecx
    jns  .mod_b_ok
    not  ebx
    not  ecx
    add  ebx, 1
    adc  ecx, 0
.mod_b_ok:
    call _xg_udiv64_core
    mov  eax, ebx
    mov  edx, ecx

    ; 余数符号 = 被除数符号
    test ebp, ebp
    jns  .mod_out
    not  eax
    not  edx
    add  eax, 1
    adc  edx, 0
.mod_out:
    pop  ebp
    ret

; ─── 除以 10 辅助函数 (edx:eax ÷ 10 → 商 edx:eax, 余数 ebx) ───
_div10:
    push ecx
    push esi
    mov  esi, 10
    ; 第一步：高32位除以10
    mov  ecx, eax          ; 暂存低32位到 ecx
    mov  eax, edx          ; 高32位移入 eax
    xor  edx, edx          ; 高32位除法前清空 edx
    div  esi               ; eax = 商(高), edx = 余数(高)
    push eax               ; 将高的商压栈保存
    ; 第二步：结合余数和原低32位再除以10
    mov  eax, ecx          ; 低32位放入 eax，此时 edx 为高余数
    div  esi               ; edx:eax / 10 → eax=商(低), edx=余数
    mov  ebx, edx          ; 余数 → ebx
    pop  edx               ; 高商 → edx
    ; 现在 edx:eax = 完整的商, ebx = 余数
    pop  esi
    pop  ecx
    ret

; ─── int64 → 字符串 (返回指针在 eax) ───
global _int64_to_str
_int64_to_str:
    push ebp
    mov  ebp, esp
    sub  esp, 12
    push ebx
    push esi
    push edi

    ; 取出 64 位值
    mov  eax, [ebp+8]          ; 低32位
    mov  edx, [ebp+12]         ; 高32位

    ; 检查是否为 0
    mov  ecx, eax
    or   ecx, edx
    jnz  .not_zero
    mov  eax, zero_str
    jmp  .ret

.not_zero:
    ; 检查是否为 -2^63
    cmp  edx, 0x80000000
    jne  .not_min
    test eax, eax
    jnz  .not_min
    mov  eax, int64_min_str
    jmp  .ret

.not_min:
    xor  esi, esi             ; 符号标志 = 0
    test edx, edx
    jns  .pos
    inc  esi
    not  eax
    not  edx
    add  eax, 1
    adc  edx, 0

.pos:
    mov  [ebp-8], eax         ; 绝对值低32位
    mov  [ebp-4], edx         ; 绝对值高32位
    test esi, esi
    setnz byte [ebp-9]           ; 符号标志

    lea  edi, [int64_str_buf + 23]
    mov  byte [edi], 0
    dec  edi

.loop:
    mov  eax, [ebp-8]
    mov  edx, [ebp-4]
    call _div10
    add  bl, '0'
    mov  [edi], bl
    dec  edi
    mov  [ebp-8], eax
    mov  [ebp-4], edx
    mov  ecx, eax
    or   ecx, edx
    jnz  .loop

    cmp  byte [ebp-9], 1
    jne  .no_sign
    mov  byte [edi], '-'
    dec  edi
.no_sign:
    inc  edi
    mov  eax, edi

.ret:
    pop  edi
    pop  esi
    pop  ebx
    mov  esp, ebp
    pop  ebp
    ret