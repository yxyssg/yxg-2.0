; ============================================================================
; 字符串和字符拼接库 - NASM x86 (32位) 汇编实现 - 完全修复版
; 调用约定: cdecl (参数从右向左压栈，调用者清理栈)
; 所有函数要求目标缓冲区足够大，且假设指针有效
; 全局符号前面加下划线，兼容Unix风格链接
; 
; 修复内容:
; 1. 修正所有长度计算函数 - 正确使用EDI扫描源字符串
; 2. 优化寄存器使用，避免破坏未保存的寄存器
; 3. 添加栈帧保护，确保EBP正确恢复
; 4. 完善边界条件处理
; ============================================================================

section .text
global _str_length, _str_copy, _str_concat
global _char_to_str, _char_concat, _char_pair_to_str
global _str_plus_char, _char_plus_str
global _str_equal, _str_not_equal, _str_substring
global _str_plus_char_2, _char_plus_str_2
global _str_len_str, _str_char_at_str, _str_concat_str
global _str_to_string, _str_substring_str

extern _AllocMemory
extern _FreeMemory
extern _gc_alloc
extern rodata_start_label
extern rodata_end_label

; ----------------------------------------------------------------------------
; size_t _str_length(const char *str)
; 功能: 计算字符串长度（不含终止符'\0'）
; 参数: [ebp+8] = 字符串指针
; 返回: eax = 长度
; 寄存器: 保存ECX, EDI
; ----------------------------------------------------------------------------

_str_length:
    push    ebp
    mov     ebp, esp
    push    ecx
    push    edi
    push    esi

    mov     edi, [ebp+8]
    test    edi, edi
    jz      .null_ptr

    mov     eax, rodata_start_label
    cmp     edi, eax
    jb      .use_gc
    mov     eax, rodata_end_label
    cmp     edi, eax
    jae     .use_gc

    xor     ecx, ecx
    or      ecx, -1
    xor     eax, eax
    repne   scasb
    not     ecx
    dec     ecx
    mov     eax, ecx
    jmp     .done

.use_gc:
    mov     eax, [edi - 4]
    jmp     .done

.null_ptr:
    xor     eax, eax

.done:
    pop     esi
    pop     edi
    pop     ecx
    pop     ebp
    ret
; ----------------------------------------------------------------------------
; char* _str_copy(char *dest, const char *src)
; 功能: 将src字符串（包括终止符）复制到dest
; 参数: [ebp+8] = dest, [ebp+12] = src
; 返回: eax = dest
; 修复: 正确使用EDI扫描src长度，而非错误使用dest
; ----------------------------------------------------------------------------
_str_copy:
    push    ebp
    mov     ebp, esp
    push    esi
    push    edi
    push    ecx

    mov     edi, [ebp+12]       ; 先用edi = src 扫描长度
    test    edi, edi
    jz      .copy_null
    
    xor     ecx, ecx
    or      ecx, -1
    xor     eax, eax
    repne   scasb
    not     ecx                ; ecx = src长度+1 (包括终止符)
    jmp     .do_copy
    
.copy_null:
    xor     ecx, ecx          ; 空字符串，只复制终止符
    
.do_copy:
    mov     esi, [ebp+12]      ; src
    mov     edi, [ebp+8]       ; dest
    test    edi, edi
    jz      .copy_error
    
    rep     movsb             ; 复制整个字符串（包括0）
    mov     eax, [ebp+8]      ; 返回dest
    jmp     .done
    
.copy_error:
    xor     eax, eax          ; dest为空指针，返回NULL
    
.done:
    pop     ecx
    pop     edi
    pop     esi
    pop     ebp
    ret

; ----------------------------------------------------------------------------
; char* _str_concat(char *dest, const char *src)
; 功能: 将src拼接到dest末尾（覆盖dest原终止符，然后添加新终止符）
; 参数: [ebp+8] = dest, [ebp+12] = src
; 返回: eax = dest
; 修复: 正确使用EDI扫描src长度，保存dest终止符位置
; ----------------------------------------------------------------------------
_str_concat:
    push    ebp
    mov     ebp, esp
    push    esi
    push    edi
    push    ecx

    ; 检查参数
    mov     edi, [ebp+8]       ; dest
    test    edi, edi
    jz      .concat_error
    mov     esi, [ebp+12]      ; src
    test    esi, esi
    jz      .concat_done      ; src为空，直接返回dest

    ; 1. 找到dest的终止符位置
    xor     ecx, ecx
    or      ecx, -1
    xor     eax, eax
    repne   scasb
    dec     edi               ; edi指向dest的终止符位置

    ; 2. 计算src的长度（包括终止符）
    push    edi               ; 保存dest的写入点
    mov     edi, esi          ; edi = src  - 关键修复！
    xor     ecx, ecx
    or      ecx, -1
    xor     eax, eax
    repne   scasb
    not     ecx              ; ecx = src长度+1
    pop     edi              ; 恢复dest的写入点

    ; 3. 执行拼接
    mov     esi, [ebp+12]    ; src
    rep     movsb            ; 复制src（包括终止符）

.concat_done:
    mov     eax, [ebp+8]     ; 返回dest
    jmp     .done

.concat_error:
    xor     eax, eax         ; dest为空指针，返回NULL

.done:
    pop     ecx
    pop     edi
    pop     esi
    pop     ebp
    ret

; ----------------------------------------------------------------------------
; char* _char_to_str(int ch)
; 功能: 分配新字符串, 写入 ch 和 NUL 终止符
; 参数: [ebp+8] = ch (仅低字节有效)
; 返回: eax = 新字符串指针
; 说明: 与语义层注册的 _char_to_str(int)->string 保持一致
; ----------------------------------------------------------------------------
_char_to_str:
    push    ebp
    mov     ebp, esp
    ; 分配 4(header) + 1(data) + 1(NUL) = 6 字节
    push    6
    call    _gc_alloc
    add     esp, 4
    test    eax, eax
    jz      .done               ; 分配失败返回 NULL
    mov     ecx, [ebp+8]        ; ch
    mov     dword [eax], 1      ; 长度头 = 1
    mov     [eax+4], cl         ; 数据字节
    mov     byte [eax+5], 0     ; NUL 终止符
    add     eax, 4              ; 返回数据区指针
.done:
    pop     ebp
    ret

; ----------------------------------------------------------------------------
; char* _char_to_str_buf(char *dest, char ch)
; 功能: 将单个字符写入已分配的目标缓冲 (内部辅助函数)
; 参数: [ebp+8] = dest, [ebp+12] = ch (仅低字节有效)
; 返回: eax = dest
; ----------------------------------------------------------------------------
_char_to_str_buf:
    push    ebp
    mov     ebp, esp
    push    edi
    mov     edi, [ebp+8]       ; dest
    test    edi, edi
    jz      .null_dest
    mov     al, byte [ebp+12]  ; 字符
    mov     [edi], al
    mov     byte [edi+1], 0    ; 终止符
    mov     eax, [ebp+8]
    jmp     .done
.null_dest:
    xor     eax, eax
.done:
    pop     edi
    pop     ebp
    ret

; ----------------------------------------------------------------------------
; char* _char_concat(char *dest, char ch)
; 功能: 将字符ch拼接到dest字符串末尾（覆盖原终止符，然后添加新终止符）
; 参数: [ebp+8] = dest, [ebp+12] = ch (低字节)
; 返回: eax = dest
; ----------------------------------------------------------------------------
_char_concat:
    push    ebp
    mov     ebp, esp
    push    edi
    push    ecx

    mov     edi, [ebp+8]       ; dest
    test    edi, edi
    jz      .null_dest
    
    xor     ecx, ecx
    or      ecx, -1
    xor     eax, eax
    repne   scasb             ; 找到终止符
    dec     edi              ; edi指向终止符位置

    mov     al, byte [ebp+12] ; 字符
    mov     [edi], al        ; 覆盖终止符
    mov     byte [edi+1], 0  ; 添加新终止符
    mov     eax, [ebp+8]
    jmp     .done
    
.null_dest:
    xor     eax, eax

.done:
    pop     ecx
    pop     edi
    pop     ebp
    ret

; ----------------------------------------------------------------------------
; char* _char_pair_to_str(char *dest, char ch1, char ch2)
; 功能: 将两个字符拼接成字符串存入dest（ch1 + ch2 + '\0'）
; 参数: [ebp+8] = dest, [ebp+12] = ch1, [ebp+16] = ch2
; 返回: eax = dest
; ----------------------------------------------------------------------------
_char_pair_to_str:
    push    ebp
    mov     ebp, esp
    push    edi

    mov     edi, [ebp+8]
    test    edi, edi
    jz      .null_dest
    
    mov     al, byte [ebp+12]  ; 第一个字符
    mov     [edi], al
    mov     al, byte [ebp+16]  ; 第二个字符
    mov     [edi+1], al
    mov     byte [edi+2], 0    ; 终止符
    mov     eax, [ebp+8]
    jmp     .done
    
.null_dest:
    xor     eax, eax

.done:
    pop     edi
    pop     ebp
    ret

; ----------------------------------------------------------------------------
; char* _str_plus_char(char *dest, const char *str, char ch)
; 功能: 将字符串str和字符ch拼接（str在前，ch在后）存入dest
; 注意: dest可以和str相同（原地追加），此时dest必须足够大
; 参数: [ebp+8] = dest, [ebp+12] = str, [ebp+16] = ch
; 返回: eax = dest
; ----------------------------------------------------------------------------
_str_plus_char:
    push    ebp
    mov     ebp, esp
    push    esi
    push    edi
    push    ecx
    push    edx              ; 保存ch

    ; 检查参数
    mov     edi, [ebp+8]     ; dest
    test    edi, edi
    jz      .error
    mov     esi, [ebp+12]    ; str
    test    esi, esi
    jz      .error
    
    mov     dl, byte [ebp+16]; 保存字符到dl

    ; 判断是否原地操作
    cmp     edi, esi
    je      .in_place

    ; 非原地操作：先复制str
    push    edi              ; 保存dest起始位置
    
    ; 计算str长度
    mov     edi, esi         ; edi = str
    xor     ecx, ecx
    or      ecx, -1
    xor     eax, eax
    repne   scasb
    not     ecx             ; ecx = str长度+1
    
    pop     edi             ; 恢复dest
    mov     esi, [ebp+12]   ; str
    rep     movsb           ; 复制str
    ; edi现在指向dest末尾（包含终止符）
    dec     edi             ; 指向终止符位置
    jmp     .append_char

.in_place:
    ; 原地操作：直接找到dest的终止符
    xor     ecx, ecx
    or      ecx, -1
    xor     eax, eax
    repne   scasb
    dec     edi             ; 指向终止符

.append_char:
    ; 追加字符
    mov     [edi], dl       ; 覆盖终止符
    mov     byte [edi+1], 0 ; 新终止符
    mov     eax, [ebp+8]    ; 返回dest
    jmp     .done

.error:
    xor     eax, eax

.done:
    pop     edx
    pop     ecx
    pop     edi
    pop     esi
    pop     ebp
    ret

; ----------------------------------------------------------------------------
; char* _char_plus_str(char *dest, char ch, const char *str)
; 功能: 将字符ch和字符串str拼接（ch在前，str在后）存入dest
; 参数: [ebp+8] = dest, [ebp+12] = ch, [ebp+16] = str
; 返回: eax = dest
; 修复: 正确计算str长度
; ----------------------------------------------------------------------------
_char_plus_str:
    push    ebp
    mov     ebp, esp
    push    esi
    push    edi
    push    ecx
    push    edx

    ; 检查参数
    mov     edi, [ebp+8]     ; dest
    test    edi, edi
    jz      .error
    mov     esi, [ebp+16]    ; str
    test    esi, esi
    jz      .copy_char_only  ; str为空，只复制字符

    ; 先放入字符ch
    mov     al, byte [ebp+12]
    mov     [edi], al
    inc     edi              ; edi指向下一个位置

    ; 计算str长度
    push    edi
    mov     edi, esi         ; edi = str - 关键修复！
    xor     ecx, ecx
    or      ecx, -1
    xor     eax, eax
    repne   scasb
    not     ecx             ; ecx = str长度+1
    pop     edi
    
    ; 复制str
    mov     esi, [ebp+16]   ; str
    rep     movsb           ; 复制str（包括终止符）
    mov     eax, [ebp+8]
    jmp     .done

.copy_char_only:
    ; 只复制字符和终止符
    mov     al, byte [ebp+12]
    mov     [edi], al
    mov     byte [edi+1], 0
    mov     eax, [ebp+8]
    jmp     .done

.error:
    xor     eax, eax

.done:
    pop     edx
    pop     ecx
    pop     edi
    pop     esi
    pop     ebp
    ret

; ----------------------------------------------------------------------------
; int _str_equal(const char *str1, const char *str2)
; 功能: 比较两个字符串是否完全相等（包括长度）
; 参数: [ebp+8] = str1, [ebp+12] = str2
; 返回: eax = 1（相等）或 0（不相等）
; 说明: 
;   - 两个指针均为 NULL 时视为相等（返回 1）
;   - 仅一个为 NULL 时视为不相等（返回 0）
;   - 正常比较逐字符进行，直到遇到不同的字符或同时遇到终止符
; 寄存器: 保存 esi, edi, ebx（被调用者保存）
; ----------------------------------------------------------------------------
_str_equal:
    push    ebp
    mov     ebp, esp
    push    esi
    push    edi
    push    ebx

    mov     esi, [ebp+8]        ; str1
    mov     edi, [ebp+12]       ; str2

    ; 处理空指针情况
    test    esi, esi
    jnz     .str1_not_null
    test    edi, edi
    jnz     .not_equal          ; str1==NULL, str2!=NULL → 不等
    ; 两者均为 NULL
    mov     eax, 1
    jmp     .done

.str1_not_null:
    test    edi, edi
    jz      .not_equal          ; str1!=NULL, str2==NULL → 不等

.loop:
    mov     al, [esi]           ; 取 str1 当前字符
    mov     bl, [edi]           ; 取 str2 当前字符
    cmp     al, bl
    jne     .not_equal          ; 字符不同 → 不等
    test    al, al              ; 是否为终止符 '\0'
    jz      .equal              ; 同时遇到终止符（因为字符相同）→ 相等
    inc     esi
    inc     edi
    jmp     .loop

.not_equal:
    xor     eax, eax            ; 返回 0
    jmp     .done

.equal:
    mov     eax, 1              ; 返回 1

.done:
    pop     ebx
    pop     edi
    pop     esi
    pop     ebp
    ret

; ----------------------------------------------------------------------------
; int _str_not_equal(const char *str1, const char *str2)
; 功能: 判断两个字符串是否不相等
; 参数: [ebp+8] = str1, [ebp+12] = str2
; 返回: eax = 1（不相等）或 0（相等）
; 说明: 直接调用 _str_equal 后取反，保证与 _str_equal 的空指针处理一致
; ----------------------------------------------------------------------------
_str_not_equal:
    push    ebp
    mov     ebp, esp
    ; 调用 _str_equal 进行比较
    push    dword [ebp+12]   ; 第二个参数 str2
    push    dword [ebp+8]    ; 第一个参数 str1
    call    _str_equal
    add     esp, 8           ; 清理栈参数
    xor     eax, 1           ; 取反：相等(1)→0，不等(0)→1
    pop     ebp
    ret

; ============================================================
; _str_substring(char* str, int start, int length) → char*
; 返回新分配的子串，调用者负责释放
; ============================================================
_str_substring:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    
    mov esi, [ebp+8]        ; str
    mov ebx, [ebp+12]       ; start
    mov ecx, [ebp+16]       ; length
    
    ; 参数校验
    cmp ebx, 0
    jl .empty_str
    cmp ecx, 0
    jle .empty_str
    
    ; 分配新内存 (length + 1)
    mov eax, ecx
    inc eax
    push 0
    push eax
    call _AllocMemory
    add esp, 8
    
    mov edi, eax            ; edi = 新字符串
    
    ; 复制字符
    add esi, ebx            ; esi = str + start
    mov ecx, [ebp+16]       ; length
    rep movsb
    
    ; null 终止
    mov byte [edi], 0
    
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret
    
.empty_str:
    push 0
    push 1
    call _AllocMemory
    add esp, 8
    mov byte [eax], 0
    
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

; ============================================================
; _str_char_at(char* str, int index) → int
; ============================================================
global _str_char_at
_str_char_at:
    push ebp
    mov ebp, esp
    push esi
    
    mov esi, [ebp+8]        ; str
    mov eax, [ebp+12]       ; index
    
    cmp eax, 0
    jl .oob
    
    push eax
    push esi
    call _str_length
    add esp, 4
    pop ecx
    
    cmp ecx, eax
    jge .oob
    
    movzx eax, byte [esi+ecx]
    pop esi
    pop ebp
    ret
    
.oob:
    mov eax, -1
    pop esi
    pop ebp
    ret

; ============================================================
; _str_plus_char_2(str, ch) → 新字符串
; 适配编译器生成的 2 参数调用，转为标准 3 参数调用
; 参数1 [ebp+8]  = str (原字符串)
; 参数2 [ebp+12] = ch  (要追加的字符)
; 返回: eax = 新字符串指针
; ============================================================
_str_plus_char_2:
    push ebp
    mov  ebp, esp
    sub  esp, 4               ; [ebp-4] 用来保存 GC 块指针
    push ebx
    push esi
    push edi

    mov  esi, [ebp+8]        ; str 指针

    push esi
    call _str_length
    add  esp, 4               ; eax = len

    mov  ebx, eax             ; ebx = len

    lea  eax, [ebx + 2 + 4]   ; len + 1(char) + 1(NUL) + 4(header)
    push eax
    call _gc_alloc
    add  esp, 4               ; eax = GC 块地址

    mov  [ebp-4], eax         ; 保存 GC 块地址
    mov  edi, eax

    lea  ecx, [ebx + 1]       ; 新长度
    mov  [edi], ecx           ; 写入长度头

    lea  edi, [edi + 4]       ; data 区域

    mov  ecx, ebx
    cmp  ecx, 0
    je   .no_copy
    cld
    rep  movsb

.no_copy:
    mov  al, [ebp+12]         ; ch
    mov  [edi], al
    inc  edi
    mov  byte [edi], 0

    mov  eax, [ebp-4]         ; GC 块地址
    add  eax, 4               ; 返回 data 指针

    pop  edi
    pop  esi
    pop  ebx
    leave
    ret

; ============================================================
; _char_plus_str_2(ch, str) → 新字符串
; 适配编译器生成的 2 参数调用，转为标准 3 参数调用
; 参数1 [ebp+8]  = ch  (字符)
; 参数2 [ebp+12] = str (原字符串)
; 返回: eax = 新字符串指针
; ============================================================
_char_plus_str_2:
    push ebp
    mov  ebp, esp
    sub  esp, 4               ; [ebp-4] 保存 GC 块地址
    push ebx
    push esi
    push edi
    mov  esi, [ebp+12]        ; str 指针
    ; 1. 计算原字符串长度
    push esi
    call _str_length
    add  esp, 4               ; eax = len
    mov  ebx, eax             ; ebx = len
    ; 2. 分配 GC 内存: 4(长度头) + 1(字符) + len + 1(NUL) = len + 6
    lea  eax, [ebx + 6]
    push eax
    call _gc_alloc
    add  esp, 4               ; eax = GC 块地址
    mov  [ebp-4], eax         ; 保存 GC 块地址
    mov  edi, eax             ; edi = GC 块起始
    lea  ecx, [ebx + 1]       ; 新长度 = 原长度 + 1
    mov  [edi], ecx           ; 写入长度头
    lea  edi, [edi + 4]       ; edi 指向 data 区域
    ; 3. 先写入字符 ch
    mov  al, [ebp+8]
    mov  [edi], al
    inc  edi
    ; 4. 复制原字符串
    mov  ecx, ebx
    test ecx, ecx
    jz   .no_copy
    cld
    rep  movsb
.no_copy:
    ; 5. 写入 NUL 终止符
    mov  byte [edi], 0
    ; 6. 返回值 = data 部分指针 = GC块 + 4
    mov  eax, [ebp-4]
    add  eax, 4
    pop  edi
    pop  esi
    pop  ebx
    leave
    ret

; ============================================================
; str 操作函数（接收胖指针 {ptr, len}）
; ============================================================

; _str_len(str s) → int
_str_len_str:
    mov eax, [esp+8]
    ret

; _str_char_at_str(str s, int index) → int
_str_char_at_str:
    mov ecx, [esp+4]
    mov eax, [esp+8]
    mov edx, [esp+12]
    movzx eax, byte [ecx + edx]
    ret

; _str_concat_str(str a, str b) → string（返回 GC 管理的 string）
; a = {ptr: [esp+4], len: [esp+8]}
; b = {ptr: [esp+12], len: [esp+16]}
_str_concat_str:
    push ebp
    mov ebp, esp
    push esi
    push edi
    push ebx
    mov eax, [ebp+12]
    add eax, [ebp+20]
    add eax, 5
    push eax
    call _gc_alloc
    add esp, 4
    mov ebx, eax
    mov edi, eax
    mov ecx, [ebp+12]
    add ecx, [ebp+20]
    mov [edi], ecx
    add edi, 4
    mov esi, [ebp+8]
    mov ecx, [ebp+12]
    rep movsb
    mov esi, [ebp+16]
    mov ecx, [ebp+20]
    rep movsb
    mov byte [edi], 0
    lea eax, [ebx + 4]
    pop ebx
    pop edi
    pop esi
    pop ebp
    ret

; ============================================================
; _str_to_string(str s) → string
; 将胖指针 {ptr, len} 转换为 GC 管理的 C 字符串（分配新内存并复制）
; 参数: [esp+4]=s.ptr, [esp+8]=s.len
; 返回: eax = 新 string 的数据指针（GC 块 + 4）
; ============================================================
_str_to_string:
    push ebp
    mov ebp, esp
    push esi
    push edi
    push ebx

    mov ebx, [ebp+12]

    mov eax, ebx
    add eax, 5
    push eax
    call _gc_alloc
    add esp, 4

    push eax

    mov esi, [ebp+8]
    mov edi, eax
    mov [edi], ebx
    add edi, 4

    mov ecx, ebx
    cld
    rep movsb
    mov byte [edi], 0

    pop eax
    add eax, 4

    pop ebx
    pop edi
    pop esi
    pop ebp
    ret

; ============================================================
; _str_substring_str(str s, int start, int n) → str
; 零拷贝切片：返回新的胖指针，指向原数据
; str 返回值通过隐藏指针传递（调用者在栈上分配空间，传指针为第一参数）
; 参数: [ebp+8] = 隐藏返回值指针
;       [ebp+12]= s.ptr, [ebp+16]= s.len
;       [ebp+20]= start, [ebp+24]= n
; 结果: *hidden = {s.ptr + start, min(n, s.len - start)}
; ============================================================
_str_substring_str:
    push ebp
    mov ebp, esp
    mov edx, [ebp+8]
    mov eax, [ebp+12]
    mov ecx, [ebp+16]
    add eax, [ebp+20]
    mov ebx, [ebp+24]
    sub ecx, [ebp+20]
    jle .zero_len
    cmp ebx, ecx
    jle .ok
    mov ebx, ecx
.ok:
    cmp ebx, 0
    jl .zero_len
    mov [edx], eax
    mov [edx+4], ebx
    jmp .done
.zero_len:
    xor eax, eax
    mov [edx], eax
    mov [edx+4], eax
.done:
    pop ebp
    ret

; ============================================================================
; 结束
; ============================================================================