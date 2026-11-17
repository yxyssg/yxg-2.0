; runtime_sqlite.asm — SQLite3 FFI wrapper（cdecl 约定，调用者清栈）
section .text

global _sqlite_open
global _sqlite_close
global _sqlite_exec
global _sqlite_prepare
global _sqlite_step
global _sqlite_finalize
global _sqlite_reset
global _sqlite_bind_int
global _sqlite_bind_int64
global _sqlite_bind_text
global _sqlite_bind_null
global _sqlite_column_int
global _sqlite_column_int64
global _sqlite_column_text
global _sqlite_column_bytes
global _sqlite_column_type
global _sqlite_last_insert_rowid
global _sqlite_changes
global _sqlite_errcode
global _sqlite_errmsg

extern sqlite3_open_v2
extern sqlite3_close_v2
extern sqlite3_exec
extern sqlite3_prepare_v2
extern sqlite3_step
extern sqlite3_finalize
extern sqlite3_reset
extern sqlite3_bind_int
extern sqlite3_bind_int64
extern sqlite3_bind_text
extern sqlite3_bind_null
extern sqlite3_column_int
extern sqlite3_column_int64
extern sqlite3_column_text
extern sqlite3_column_bytes
extern sqlite3_column_type
extern sqlite3_last_insert_rowid
extern sqlite3_changes
extern sqlite3_errcode
extern sqlite3_errmsg

; int _sqlite_open(string path, int flags)
;   返回 db 句柄（0 = 失败）
_sqlite_open:
    push ebp
    mov ebp, esp
    push ebx
    sub esp, 8
    lea eax, [ebp-8]
    push dword 0
    push dword [ebp+12]
    push eax
    push dword [ebp+8]
    call sqlite3_open_v2
    add esp, 16
    cmp eax, 0
    jne .fail
    mov eax, [ebp-8]
    add esp, 8
    pop ebx
    pop ebp
    ret
.fail:
    xor eax, eax
    add esp, 8
    pop ebx
    pop ebp
    ret

; int _sqlite_close(int db)
_sqlite_close:
    push ebp
    mov ebp, esp
    push dword [ebp+8]
    call sqlite3_close_v2
    add esp, 4
    pop ebp
    ret

; int _sqlite_exec(int db, string sql)
_sqlite_exec:
    push ebp
    mov ebp, esp
    push dword 0
    push dword 0
    push dword 0
    push dword [ebp+12]
    push dword [ebp+8]
    call sqlite3_exec
    add esp, 20
    pop ebp
    ret

; int _sqlite_prepare(int db, string sql)
_sqlite_prepare:
    push ebp
    mov ebp, esp
    push ebx
    sub esp, 8
    lea eax, [ebp-8]
    push dword 0
    push eax
    push dword -1
    push dword [ebp+12]
    push dword [ebp+8]
    call sqlite3_prepare_v2
    add esp, 20
    cmp eax, 0
    jne .fail
    mov eax, [ebp-8]
    add esp, 8
    pop ebx
    pop ebp
    ret
.fail:
    xor eax, eax
    add esp, 8
    pop ebx
    pop ebp
    ret

; int _sqlite_step(int stmt)  → 100=ROW, 101=DONE, 其他=错误
_sqlite_step:
    push ebp
    mov ebp, esp
    push dword [ebp+8]
    call sqlite3_step
    add esp, 4
    pop ebp
    ret

; int _sqlite_finalize(int stmt)
_sqlite_finalize:
    push ebp
    mov ebp, esp
    push dword [ebp+8]
    call sqlite3_finalize
    add esp, 4
    pop ebp
    ret

; int _sqlite_reset(int stmt)
_sqlite_reset:
    push ebp
    mov ebp, esp
    push dword [ebp+8]
    call sqlite3_reset
    add esp, 4
    pop ebp
    ret

; int _sqlite_bind_int(int stmt, int idx, int val)
_sqlite_bind_int:
    push ebp
    mov ebp, esp
    push dword [ebp+16]
    push dword [ebp+12]
    push dword [ebp+8]
    call sqlite3_bind_int
    add esp, 12
    pop ebp
    ret

; int _sqlite_bind_int64(int stmt, int idx, int64 val)  ← 64 位参数压两次
_sqlite_bind_int64:
    push ebp
    mov ebp, esp
    push dword [ebp+20]
    push dword [ebp+16]
    push dword [ebp+12]
    push dword [ebp+8]
    call sqlite3_bind_int64
    add esp, 16
    pop ebp
    ret

; int _sqlite_bind_text(int stmt, int idx, string val)
_sqlite_bind_text:
    push ebp
    mov ebp, esp
    push dword -1
    push dword -1
    push dword [ebp+16]
    push dword [ebp+12]
    push dword [ebp+8]
    call sqlite3_bind_text
    add esp, 20
    pop ebp
    ret

; int _sqlite_bind_null(int stmt, int idx)
_sqlite_bind_null:
    push ebp
    mov ebp, esp
    push dword [ebp+12]
    push dword [ebp+8]
    call sqlite3_bind_null
    add esp, 8
    pop ebp
    ret

; int _sqlite_column_int(int stmt, int col)
_sqlite_column_int:
    push ebp
    mov ebp, esp
    push dword [ebp+12]
    push dword [ebp+8]
    call sqlite3_column_int
    add esp, 8
    pop ebp
    ret

; int64 _sqlite_column_int64(int stmt, int col)
;   返回值高 32 位在 edx，低 32 位在 eax
_sqlite_column_int64:
    push ebp
    mov ebp, esp
    push dword [ebp+12]
    push dword [ebp+8]
    call sqlite3_column_int64
    add esp, 8
    pop ebp
    ret

; int _sqlite_column_text(int stmt, int col)  → char* 指针，0 = NULL
_sqlite_column_text:
    push ebp
    mov ebp, esp
    push dword [ebp+12]
    push dword [ebp+8]
    call sqlite3_column_text
    add esp, 8
    pop ebp
    ret

; int _sqlite_column_bytes(int stmt, int col)
_sqlite_column_bytes:
    push ebp
    mov ebp, esp
    push dword [ebp+12]
    push dword [ebp+8]
    call sqlite3_column_bytes
    add esp, 8
    pop ebp
    ret

; int _sqlite_column_type(int stmt, int col)
_sqlite_column_type:
    push ebp
    mov ebp, esp
    push dword [ebp+12]
    push dword [ebp+8]
    call sqlite3_column_type
    add esp, 8
    pop ebp
    ret

; int64 _sqlite_last_insert_rowid(int db)
_sqlite_last_insert_rowid:
    push ebp
    mov ebp, esp
    push dword [ebp+8]
    call sqlite3_last_insert_rowid
    add esp, 4
    pop ebp
    ret

; int _sqlite_changes(int db)
_sqlite_changes:
    push ebp
    mov ebp, esp
    push dword [ebp+8]
    call sqlite3_changes
    add esp, 4
    pop ebp
    ret

; int _sqlite_errcode(int db)
_sqlite_errcode:
    push ebp
    mov ebp, esp
    push dword [ebp+8]
    call sqlite3_errcode
    add esp, 4
    pop ebp
    ret

; int _sqlite_errmsg(int db)  → char* 指针
_sqlite_errmsg:
    push ebp
    mov ebp, esp
    push dword [ebp+8]
    call sqlite3_errmsg
    add esp, 4
    pop ebp
    ret