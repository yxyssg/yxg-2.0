; ============================================================
; runtime_file.asm -- File 标准库运行库 v2（Win32 / NASM）
;   导出符号 = native 方法的编译器 mangling 名
;   全部为 cdecl：调用方清栈，this/第一个参数在 [ebp+8]
;   Win32 API 为 stdcall（@N）：被调方清栈，call 后不可 add esp
;   字符串按 0 结尾扫描长度；GC 字符串额外在 [ptr-4] 存长度头
;   [this+0]=class_id, [this+4]=vtable, [this+8]=第一个字段
; ============================================================

THIS_OFF equ 8

section .text

extern _gc_alloc
extern _CreateFileA@28
extern _ReadFile@20
extern _WriteFile@20
extern _CloseHandle@4
extern _GetFileSize@8
extern _SetFilePointer@16
extern _DeleteFileA@4
extern _GetFileAttributesA@4
extern _FlushFileBuffers@4
extern _GetCurrentDirectoryA@8
extern _SetCurrentDirectoryA@4
extern _GetTempPathA@8
extern _CopyFileA@12
extern _MoveFileA@8
extern _CreateDirectoryA@8
extern _RemoveDirectoryA@4
extern _FindFirstFileA@8
extern _FindNextFileA@8
extern _FindClose@4
extern _GetModuleFileNameA@12

global File__open_string_string
global File__closeRaw
global File__readAllRaw
global File__readLineRaw
global File__writeRaw_string
global File__getSizeRaw
global File__readBytesRaw_int
global File__writeBytesRaw_string_int
global File__strByteLen_string
global File_seek_int_int
global File_tell
global File_atEnd
global File_flush
global File_exists_string
global File_delete_string
global File_getCurrentDir
global File_setCurrentDir_string
global File_getTempDir
global File_copy_string_string
global File_move_string_string
global File_createDir_string
global File_removeDir_string
global File_getFileSize_string
global File__listDirRaw_string
global File__readIntoRaw_string_int
global File_isDir_string
global File_getExeDir

; ---- 内部：esi = 字符串，输出 ecx = 长度 ----
_strlen_file:
    xor ecx, ecx
.lp:
    cmp byte [esi + ecx], 0
    je .done
    inc ecx
    jmp .lp
.done:
    ret

; ---- 内部：[ebp+8]=path [ebp+12]=mode -> eax=句柄(0=失败) ----
file_open_internal:
    push ebp
    mov ebp, esp
    push ebx
    push edi

    xor edi, edi
    mov eax, 0x80000000           ; GENERIC_READ（r 模式只读）
    mov ebx, 3                    ; OPEN_EXISTING
    mov ecx, dword [ebp + 12]
    test ecx, ecx
    jz .have_mode
    mov dl, byte [ecx]
    cmp dl, 'w'
    jne .try_a
    mov eax, 0xC0000000           ; GENERIC_READ | GENERIC_WRITE
    mov ebx, 2                    ; CREATE_ALWAYS
    jmp .check_plus
.try_a:
    cmp dl, 'a'
    jne .check_plus
    mov eax, 0xC0000000           ; GENERIC_READ | GENERIC_WRITE
    mov ebx, 4                    ; OPEN_ALWAYS
    mov edi, 1
.check_plus:
    cmp byte [ecx + 1], '+'
    jne .have_mode
    or eax, 0x40000000            ; GENERIC_WRITE
.have_mode:
    push 0                        ; hTemplateFile
    push 0x80                     ; FILE_ATTRIBUTE_NORMAL
    push ebx                      ; dwCreationDisposition
    push 0                        ; lpSecurityAttributes
    push 3                        ; FILE_SHARE_READ | FILE_SHARE_WRITE
    push eax                      ; dwDesiredAccess
    push dword [ebp + 8]          ; lpFileName
    call _CreateFileA@28
    cmp eax, -1
    jne .ok
    xor eax, eax
    jmp .done
.ok:
    test edi, edi
    jz .done
    push eax                      ; 保存句柄
    push 2                        ; FILE_END
    push 0
    push 0
    push eax
    call _SetFilePointer@16
    pop eax                       ; 恢复句柄
.done:
    pop edi
    pop ebx
    pop ebp
    ret

; ---- 打开，返回句柄（eax），0 = 失败 ----
File__open_string_string:
    push ebp
    mov ebp, esp
    push dword [ebp + 16]
    push dword [ebp + 12]
    call file_open_internal
    add esp, 8
    pop ebp
    ret

File__closeRaw:
    push ebp
    mov ebp, esp
    mov ecx, dword [ebp + 8]
    mov eax, [ecx + THIS_OFF]
    test eax, eax
    jz .done
    push eax
    call _CloseHandle@4
.done:
    pop ebp
    ret

File__getSizeRaw:
    push ebp
    mov ebp, esp
    mov ecx, dword [ebp + 8]
    mov eax, [ecx + THIS_OFF]
    test eax, eax
    jnz .ok
    xor eax, eax
    pop ebp
    ret
.ok:
    push 0
    push eax
    call _GetFileSize@8
    cmp eax, -1
    jne .ret
    xor eax, eax
.ret:
    pop ebp
    ret

; ---- 读取整个文件；句柄无效返回空串 ----
File__readAllRaw:
    push ebp
    mov ebp, esp
    push esi
    push edi
    sub esp, 8                   ; [ebp-4] = bytesRead
    mov ecx, dword [ebp + 8]
    mov eax, [ecx + THIS_OFF]
    test eax, eax
    jnz .opened
    jmp .empty
.opened:
    ; 先定位到文件头：readAll 语义 = 整个文件，而非从当前指针开始
    push 0                       ; FILE_BEGIN
    push 0
    push 0
    push eax
    call _SetFilePointer@16
    mov eax, dword [ebp + 8]
    mov eax, [eax + THIS_OFF]
    push 0
    push eax
    call _GetFileSize@8
    cmp eax, -1
    jne .size_ok
    xor eax, eax
.size_ok:
    mov esi, eax
    lea eax, [esi + 8]
    add eax, 3
    and eax, -4
    push eax
    call _gc_alloc
    add esp, 4
    mov [eax], esi               ; 长度头
    lea edi, [eax + 4]
    mov eax, dword [ebp + 8]
    mov eax, [eax + THIS_OFF]
    lea ecx, [ebp - 4]
    push 0
    push ecx
    push esi
    push edi
    push eax
    call _ReadFile@20
    mov ecx, dword [ebp - 4]
    mov byte [edi + ecx], 0
    mov eax, edi
    jmp .ret
.empty:
    push 8
    call _gc_alloc
    add esp, 4
    mov dword [eax], 0
    mov byte [eax + 4], 0
    add eax, 4
.ret:
    add esp, 8
    pop edi
    pop esi
    pop ebp
    ret

; ---- 逐字节读一行（不含换行）；EOF 返回空串 ----
File__readLineRaw:
    push ebp
    mov ebp, esp
    push esi
    push edi
    sub esp, 24                  ; [ebp-4]cap [ebp-8]len [ebp-12]块
                                 ; [ebp-16]字节 [ebp-20]bytesRead [ebp-24]保存字符
    mov ecx, dword [ebp + 8]
    mov eax, [ecx + THIS_OFF]
    test eax, eax
    jnz .opened
    jmp .empty
.opened:
    mov dword [ebp - 4], 64
    mov dword [ebp - 8], 0
    push 72
    call _gc_alloc
    add esp, 4
    mov dword [ebp - 12], eax
.loop:
    mov ecx, dword [ebp + 8]
    mov eax, [ecx + THIS_OFF]
    lea edx, [ebp - 20]
    push 0
    push edx
    push 1
    lea edx, [ebp - 16]
    push edx
    push eax
    call _ReadFile@20
    cmp dword [ebp - 20], 0
    je .finish
    mov al, byte [ebp - 16]
    mov byte [ebp - 24], al
    cmp al, 10
    je .finish
    cmp al, 13
    je .loop                    ; 跳过 '\r'，由紧随的 '\n' 结束本行
    mov edx, dword [ebp - 8]
    cmp edx, dword [ebp - 4]
    jb .store
    mov eax, dword [ebp - 4]
    shl eax, 1
    mov dword [ebp - 4], eax
    add eax, 8
    add eax, 3
    and eax, -4
    push eax
    call _gc_alloc
    add esp, 4
    mov esi, dword [ebp - 12]
    lea esi, [esi + 4]
    lea edi, [eax + 4]
    mov ecx, dword [ebp - 8]
    push eax
    cld
    rep movsb
    pop eax
    mov dword [ebp - 12], eax
.store:
    mov esi, dword [ebp - 12]
    lea esi, [esi + 4]
    add esi, dword [ebp - 8]
    mov al, byte [ebp - 24]
    mov [esi], al
    inc dword [ebp - 8]
    jmp .loop
.finish:
    mov esi, dword [ebp - 12]
    mov ecx, dword [ebp - 8]
    mov [esi], ecx
    lea eax, [esi + 4]
    mov byte [eax + ecx], 0
    jmp .ret
.empty:
    push 8
    call _gc_alloc
    add esp, 4
    mov dword [eax], 0
    mov byte [eax + 4], 0
    add eax, 4
.ret:
    add esp, 24
    pop edi
    pop esi
    pop ebp
    ret

; ---- 读取 count 字节；EOF 返回短串/空串；句柄无效返回空串 ----
File__readBytesRaw_int:
    push ebp
    mov ebp, esp
    push esi
    push edi
    sub esp, 4                    ; [ebp-4] = bytesRead
    mov ecx, dword [ebp + 8]
    mov eax, [ecx + THIS_OFF]
    test eax, eax
    jnz .opened
    jmp .empty
.opened:
    mov esi, dword [ebp + 12]     ; count
    test esi, esi
    js .empty
    lea eax, [esi + 8]
    add eax, 3
    and eax, -4
    push eax
    call _gc_alloc
    add esp, 4
    mov dword [eax], 0
    lea edi, [eax + 4]
    mov ecx, dword [ebp + 8]
    mov eax, [ecx + THIS_OFF]
    lea ecx, [ebp - 4]
    push 0
    push ecx
    push esi
    push edi
    push eax
    call _ReadFile@20
    mov ecx, dword [ebp - 4]
    mov [edi - 4], ecx            ; 长度头 = 实际读到的字节数
    mov byte [edi + ecx], 0
    mov eax, edi
    jmp .ret
.empty:
    push 8
    call _gc_alloc
    add esp, 4
    mov dword [eax], 0
    mov byte [eax + 4], 0
    add eax, 4
.ret:
    add esp, 4
    pop edi
    pop esi
    pop ebp
    ret

; ---- 返回实际写入字节数 ----
File__writeRaw_string:
    push ebp
    mov ebp, esp
    push esi
    sub esp, 4                   ; [ebp-4] = written
    mov ecx, dword [ebp + 8]
    mov eax, [ecx + THIS_OFF]
    test eax, eax
    jnz .ok
    xor eax, eax
    jmp .ret
.ok:
    mov edx, dword [ebp + 12]
    mov esi, edx
    call _strlen_file
    lea esi, [ebp - 4]
    push 0
    push esi
    push ecx
    push edx
    push eax
    call _WriteFile@20
    mov eax, dword [ebp - 4]
.ret:
    add esp, 4
    pop esi
    pop ebp
    ret

; ---- 按显式长度写入 len 字节；二进制安全 ----
; [ebp+8]=this, [ebp+12]=data, [ebp+16]=len
File__writeBytesRaw_string_int:
    push ebp
    mov ebp, esp
    push esi
    sub esp, 4                    ; [ebp-4] = written
    mov ecx, dword [ebp + 8]
    mov eax, [ecx + THIS_OFF]
    test eax, eax
    jnz .ok
    xor eax, eax
    jmp .ret
.ok:
    mov ecx, dword [ebp + 16]     ; len
    mov edx, dword [ebp + 12]     ; data
    lea esi, [ebp - 4]
    push 0
    push esi
    push ecx
    push edx
    push eax
    call _WriteFile@20
    mov eax, dword [ebp - 4]
.ret:
    add esp, 4
    pop esi
    pop ebp
    ret

; ---- 读 GC 字符串长度头（二进制安全）；字面量无头，勿对字面量使用 ----
File__strByteLen_string:
    mov eax, dword [esp + 4]
    mov eax, [eax - 4]
    ret

; ---- 定位：origin 0=头 1=当前 2=尾 ----
File_seek_int_int:
    push ebp
    mov ebp, esp
    mov eax, dword [ebp + 8]
    mov eax, [eax + THIS_OFF]
    test eax, eax
    jz .bad
    push dword [ebp + 16]
    push 0
    push dword [ebp + 12]
    push eax
    call _SetFilePointer@16
    jmp .ret
.bad:
    mov eax, -1
.ret:
    pop ebp
    ret

File_tell:
    push ebp
    mov ebp, esp
    mov eax, dword [ebp + 8]
    mov eax, [eax + THIS_OFF]
    test eax, eax
    jz .bad
    push 1                        ; FILE_CURRENT
    push 0
    push 0
    push eax
    call _SetFilePointer@16
    jmp .ret
.bad:
    mov eax, -1
.ret:
    pop ebp
    ret

; ---- 是否在文件末尾：当前指针 >= 文件大小 ----
File_atEnd:
    push ebp
    mov ebp, esp
    sub esp, 8                    ; [ebp-4] = pos, [ebp-8] = size
    mov eax, dword [ebp + 8]
    mov eax, [eax + THIS_OFF]     ; 句柄
    test eax, eax
    jz .bad
    push 1                        ; FILE_CURRENT
    push 0
    push 0
    push eax
    call _SetFilePointer@16
    mov [ebp-4], eax              ; 保存当前文件指针
    mov eax, dword [ebp + 8]
    mov eax, [eax + THIS_OFF]     ; 重新获取句柄
    push 0
    push eax
    call _GetFileSize@8
    cmp eax, -1
    jne .cmp
    mov eax, 1
    jmp .ret
.cmp:
    mov [ebp-8], eax              ; 保存文件大小
    mov ecx, [ebp-4]              ; 恢复指针
    mov eax, [ebp-8]              ; 恢复大小
    cmp ecx, eax
    setae al
    movzx eax, al
    jmp .ret
.bad:
    mov eax, 1
.ret:
    mov esp, ebp
    pop ebp
    ret

File_flush:
    mov eax, dword [esp + 4]
    mov eax, [eax + THIS_OFF]
    test eax, eax
    jz .done
    push eax
    call _FlushFileBuffers@4
.done:
    ret

File_exists_string:
    push dword [esp + 4]
    call _GetFileAttributesA@4
    cmp eax, -1
    setne al
    movzx eax, al
    ret

File_delete_string:
    push dword [esp + 4]
    call _DeleteFileA@4
    test eax, eax
    setne al
    movzx eax, al
    ret

File_getCurrentDir:
    push ebp
    mov ebp, esp
    push edi
    push 0
    push 0
    call _GetCurrentDirectoryA@8
    mov ecx, eax
    lea eax, [ecx + 8]
    add eax, 3
    and eax, -4
    push eax
    call _gc_alloc
    add esp, 4
    mov edi, eax
    mov dword [edi], 0
    lea edi, [eax + 4]
    push edi
    push ecx
    call _GetCurrentDirectoryA@8
    mov [edi - 4], eax            ; 长度头 = 实际字符数（不含结尾 NUL）
    mov byte [edi + eax], 0
    mov eax, edi
    pop edi
    pop ebp
    ret

File_setCurrentDir_string:
    push dword [esp + 4]
    call _SetCurrentDirectoryA@4
    test eax, eax
    setne al
    movzx eax, al
    ret

File_getTempDir:
    push ebp
    mov ebp, esp
    push edi
    push 0
    push 0
    call _GetTempPathA@8
    mov ecx, eax
    lea eax, [ecx + 8]
    add eax, 3
    and eax, -4
    push eax
    call _gc_alloc
    add esp, 4
    mov edi, eax
    mov dword [edi], 0
    lea edi, [eax + 4]
    push edi
    push ecx
    call _GetTempPathA@8
    mov [edi - 4], eax            ; 长度头 = 实际字符数（不含结尾 NUL）
    mov byte [edi + eax], 0
    mov eax, edi
    pop edi
    pop ebp
    ret

File_copy_string_string:
    mov eax, dword [esp + 4]
    mov ecx, dword [esp + 8]
    push 0
    push ecx
    push eax
    call _CopyFileA@12
    test eax, eax
    setne al
    movzx eax, al
    ret

File_move_string_string:
    mov eax, dword [esp + 4]      ; src = 现有文件
    mov ecx, dword [esp + 8]      ; dst = 新路径
    push ecx
    push eax
    call _MoveFileA@8
    test eax, eax
    setne al
    movzx eax, al
    ret

File_createDir_string:
    push 0
    push dword [esp + 8]
    call _CreateDirectoryA@8
    test eax, eax
    setne al
    movzx eax, al
    ret

File_removeDir_string:
    push dword [esp + 4]
    call _RemoveDirectoryA@4
    test eax, eax
    setne al
    movzx eax, al
    ret

; ---- 静态：不打开句柄直接查大小；失败返回 -1 ----
File_getFileSize_string:
    push ebp
    mov ebp, esp
    push ebx
    push 0
    push 0x80
    push 3
    push 0
    push 3
    push 0x80000000
    push dword [ebp + 8]
    call _CreateFileA@28
    cmp eax, -1
    jne .ok
    mov eax, -1
    jmp .ret
.ok:
    mov ebx, eax
    push 0
    push ebx
    call _GetFileSize@8
    push eax
    push ebx
    call _CloseHandle@4
    pop eax
.ret:
    pop ebx
    pop ebp
    ret

; ---- 判断路径是否为目录 ----
File_isDir_string:
    push dword [esp + 4]
    call _GetFileAttributesA@4
    cmp eax, -1
    je .no
    test eax, 0x10                ; FILE_ATTRIBUTE_DIRECTORY
    jnz .yes
.no:
    xor eax, eax
    ret
.yes:
    mov eax, 1
    ret

; ---- 流式读入用户缓冲：返回实际读取字节数；0 = EOF/失败 ----
; [ebp+8]=this, [ebp+12]=buf, [ebp+16]=len
File__readIntoRaw_string_int:
    push ebp
    mov ebp, esp
    sub esp, 4                    ; [ebp-4] = bytesRead
    mov ecx, dword [ebp + 8]
    mov eax, [ecx + THIS_OFF]     ; 句柄
    test eax, eax
    jz .zero
    mov ecx, dword [ebp + 16]     ; len
    test ecx, ecx
    js .zero
    mov edx, dword [ebp + 12]     ; buf 数据指针
    lea esi, [ebp - 4]
    push 0
    push esi
    push ecx
    push edx
    push eax
    call _ReadFile@20
    test eax, eax
    jz .zero
    mov ecx, dword [ebp - 4]
    mov edx, dword [ebp + 12]
    mov [edx - 4], ecx            ; 回写 GC 长度头（buf 必须是 GC string）
    mov byte [edx + ecx], 0
    mov eax, ecx
    jmp .ret
.zero:
    xor eax, eax
.ret:
    mov esp, ebp
    pop ebp
    ret

; ---- 目录遍历：返回 '\n' 分隔的条目名（不含 '.'/'..'）；失败或空目录返回空串 ----
; [ebp+8] = path（NUL 结尾）
File__listDirRaw_string:
    push ebp
    mov ebp, esp
    push esi
    push edi
    push ebx
    sub esp, 352
    ; [ebp-320 .. ebp-1]  = WIN32_FIND_DATAA (320B)
    ; [ebp-324]           = hFind
    ; [ebp-328]           = 结果块
    ; [ebp-332]           = 结果容量
    ; [ebp-336]           = 结果长度
    ; [ebp-340]           = 模式串数据指针
    ; [ebp-344]           = path 长度
    ; [ebp-348]           = 条目名长度

    mov dword [ebp - 324], 0
    mov dword [ebp - 332], 64
    mov dword [ebp - 336], 0
    push 72
    call _gc_alloc
    add esp, 4
    mov [ebp - 328], eax

    ; 构造搜索模式 path(\|/)*
    mov esi, dword [ebp + 8]
    test esi, esi
    jz .notfound
    call _strlen_file
    mov [ebp - 344], ecx
    lea eax, [ecx + 10]
    add eax, 3
    and eax, -4
    push eax
    call _gc_alloc
    add esp, 4
    mov dword [eax], 0
    lea edi, [eax + 4]
    mov [ebp - 340], edi
    mov esi, dword [ebp + 8]
    mov ecx, [ebp - 344]
    cld
    rep movsb
    mov ecx, [ebp - 344]
    test ecx, ecx
    jz .pat_tail
    cmp byte [esi - 1], '\'
    je .pat_tail
    cmp byte [esi - 1], '/'
    je .pat_tail
    mov byte [edi], '\'
    inc edi
.pat_tail:
    mov byte [edi], '*'
    inc edi
    mov byte [edi], 0

    lea ecx, [ebp - 320]
    push ecx
    push dword [ebp - 340]
    call _FindFirstFileA@8
    cmp eax, -1
    je .notfound
    mov [ebp - 324], eax

.loop:
    lea esi, [ebp - 276]          ; cFileName = WIN32_FIND_DATAA + 44
    cmp byte [esi], '.'
    jne .append
    cmp byte [esi + 1], 0
    je .next
    cmp byte [esi + 1], '.'
    jne .append
    cmp byte [esi + 2], 0
    je .next
.append:
    call _strlen_file              ; ecx = 条目名长度
    mov [ebp - 348], ecx
    mov edx, [ebp - 336]
    add edx, ecx
    inc edx
    cmp edx, [ebp - 332]
    jbe .have_room
    mov eax, [ebp - 332]
    shl eax, 1
    mov [ebp - 332], eax
    lea eax, [eax + 8]
    add eax, 3
    and eax, -4
    push eax
    call _gc_alloc
    add esp, 4
    mov edx, [ebp - 328]
    mov ecx, [ebp - 336]
    mov [eax], ecx
    push eax
    push esi
    mov esi, edx
    add esi, 4
    mov edi, eax
    add edi, 4
    mov ecx, [ebp - 336]
    cld
    rep movsb
    pop esi
    pop eax
    mov [ebp - 328], eax
.have_room:
    mov edi, [ebp - 328]
    add edi, 4
    add edi, [ebp - 336]
    mov ecx, [ebp - 348]
    cld
    rep movsb
    mov byte [edi], 10            ; '\n'
    mov ecx, [ebp - 348]
    add dword [ebp - 336], ecx
    inc dword [ebp - 336]
.next:
    lea ecx, [ebp - 320]
    push ecx
    push dword [ebp - 324]
    call _FindNextFileA@8
    test eax, eax
    jnz .loop

    push dword [ebp - 324]
    call _FindClose@4

    mov ecx, [ebp - 336]
    test ecx, ecx
    jz .finish
    mov edi, [ebp - 328]
    cmp byte [edi + 4 + ecx - 1], 10
    jne .finish
    dec dword [ebp - 336]
    mov ecx, [ebp - 336]
.finish:
    mov edi, [ebp - 328]
    mov [edi], ecx
    mov byte [edi + 4 + ecx], 0
    lea eax, [edi + 4]
    jmp .ret

.notfound:
    mov edi, [ebp - 328]
    mov dword [edi], 0
    mov byte [edi + 4], 0
    lea eax, [edi + 4]
.ret:
    add esp, 352
    pop ebx
    pop edi
    pop esi
    pop ebp
    ret

File_getExeDir:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    sub esp, 276

    lea eax, [ebp-288]
    push 260
    push eax
    push 0
    call _GetModuleFileNameA@12
    test eax, eax
    jz .fail
    cmp eax, 260
    jae .fail

    mov ebx, eax

    lea edx, [ebp-288]
    lea esi, [ebp-288]
    add esi, ebx
    dec esi
.find_sep:
    cmp esi, edx
    jl .fail
    mov al, byte [esi]
    cmp al, 92
    je .found
    cmp al, 47
    je .found
    dec esi
    jmp .find_sep
.found:
    sub esi, edx
    mov ebx, esi

    mov eax, ebx
    add eax, 8
    add eax, 3
    and eax, -4
    push eax
    call _gc_alloc
    add esp, 4
    test eax, eax
    jz .fail

    mov dword [eax], ebx
    lea edi, [eax + 4]
    push eax

    lea esi, [ebp-288]
    mov ecx, ebx
    cld
    rep movsb
    mov byte [edi], 0

    pop eax
    add eax, 4
    jmp .ret

.fail:
    xor eax, eax

.ret:
    add esp, 276
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret