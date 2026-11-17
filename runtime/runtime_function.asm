; ============================================================
; runtime_function.asm — yxg.Function 类的 native 方法实现
; 平台: Win32 / i386 / cdecl / NASM (Intel 语法)
;
; 对象布局 (由 Semantics.resolve_class_members 与 Codegen 的
;           IR_ALLOC_CLASS 共同确定):
;   [this+0]   class_id    (4 字节)
;   [this+4]   vtable 指针  (4 字节)
;   [this+8]   _raw        (函数指针位模式)
;   [this+12]  _has_arg    (0 或 1)
;
; 参数传递 (cdecl):
;   [esp+0]    返回地址
;   [esp+4]    this        (Function*)
;   [esp+8]    第一个显式参数
;
; 编译:
;   nasm -g -f win32 -O0 runtime_function.asm -o runtime_function.obj
; ============================================================

section .text

global Function_call
global Function_call_int
global Function_raw
global Function_isNull


; ------------------------------------------------------------
; int Function.call()
;   调用 _raw 指向的无参函数
;   若 this 或 _raw 为 0，返回 0
; ------------------------------------------------------------
Function_call:
    push ebp
    mov  ebp, esp
    mov  eax, [ebp+8]           ; eax = this
    test eax, eax
    jz   .L_call_null
    mov  ecx, [eax+8]           ; ecx = this->_raw
    test ecx, ecx
    jz   .L_call_null
    mov  edx, [eax+12]          ; edx = this->_env
    test edx, edx
    jz   .L_call_no_env
    push edx
    call ecx
    add  esp, 4
    pop  ebp
    ret
.L_call_no_env:
    call ecx
    pop  ebp
    ret
.L_call_null:
    xor  eax, eax
    pop  ebp
    ret


; ------------------------------------------------------------
; int Function.call(int a)
;   调用 _raw 指向的单参函数，返回其返回值
;   若 this 或 _raw 为 0，返回 0
; ------------------------------------------------------------
Function_call_int:
    push ebp
    mov  ebp, esp
    mov  eax, [ebp+8]           ; eax = this
    test eax, eax
    jz   .L_call_int_null
    mov  ecx, [eax+8]           ; ecx = this->_raw
    test ecx, ecx
    jz   .L_call_int_null
    mov  edx, [eax+12]          ; edx = this->_env
    test edx, edx
    jz   .L_call_int_no_env
    push dword [ebp+12]         ; 显式参数 a
    push edx                    ; _env（首参）
    call ecx
    add  esp, 8
    pop  ebp
    ret
.L_call_int_no_env:
    push dword [ebp+12]         ; 显式参数 a
    call ecx
    add  esp, 4
    pop  ebp
    ret
.L_call_int_null:
    xor  eax, eax
    pop  ebp
    ret

; ------------------------------------------------------------
; int Function.raw()
;   返回 _raw 字段 (函数指针的位模式)
; ------------------------------------------------------------
Function_raw:
    mov  eax, [esp+4]
    test eax, eax
    jz   .L_raw_null
    mov  eax, [eax+8]
    ret
.L_raw_null:
    xor  eax, eax
    ret


; ------------------------------------------------------------
; bool Function.isNull()
;   返回 1 若 this == 0 或 this->_raw == 0，否则返回 0
; ------------------------------------------------------------
Function_isNull:
    mov  eax, [esp+4]
    test eax, eax
    jz   .L_isnull_yes
    mov  eax, [eax+8]
    test eax, eax
    jz   .L_isnull_yes
    xor  eax, eax
    ret
.L_isnull_yes:
    mov  eax, 1
    ret