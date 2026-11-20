; ============================================================================
; runtime_tls.asm -- HTTPS 客户端（Windows SChannel / SSPI）  [修订版]
;
; 汇编:  nasm -f win32 runtime_tls.asm
; 链接:  secur32.lib  ws2_32.lib
; 约定:  cdecl（调用者清栈），返回值在 eax，保留 ebx/esi/edi/ebp
;
; 导出函数
;   int    _tls_connect(int sock, const char *host)   成功返回 1..MAX_CTX，失败 0
;   int    _tls_write(int ctx, const char *data, int len)   返回 len，失败 -1
;   int    _tls_read_buf(int ctx, char *buf, int maxlen)    返回字节数，0=EOF，-1=失败
;   char*  _tls_read(int ctx, int maxlen)    返回静态缓冲区中以 0 结尾的字符串
;                                            EOF 返回空串，失败返回 NULL
;   void   _tls_close(int ctx)               发送 close_notify 并释放上下文
;   int    _tls_last_error(void)
;   int    _tls_last_sec_status(void)
;
; tls_last_err:
;   0 成功  1 取凭据失败  2 首次 ISC 失败  3 握手 ISC 失败  4 无空闲槽
;   5 send 失败  6 recv 失败  7 缓冲区溢出  8 QueryContextAttributes 失败
;   9 EncryptMessage 失败  10 DecryptMessage 失败（含不支持的重协商）
;
; 注意：socket 由调用者创建并关闭；_tls_close 不会 closesocket。
;       内部使用全局共享缓冲区，非线程安全。
; ============================================================================

extern _AcquireCredentialsHandleA@36
extern _InitializeSecurityContextA@48
extern _DeleteSecurityContext@4
extern _QueryContextAttributesA@12
extern _EncryptMessage@16
extern _DecryptMessage@16
extern _ApplyControlToken@8
extern _gc_alloc
extern _send@16
extern _recv@16

SECPKG_CRED_OUTBOUND      equ 2
SEC_E_OK                  equ 0
SEC_I_CONTINUE_NEEDED     equ 0x00090312
SEC_I_CONTEXT_EXPIRED     equ 0x00090317
SEC_E_INCOMPLETE_MESSAGE  equ 0x80090318

SECBUFFER_VERSION         equ 0
SECBUFFER_EMPTY           equ 0
SECBUFFER_DATA            equ 1
SECBUFFER_TOKEN           equ 2
SECBUFFER_EXTRA           equ 5
SECBUFFER_STREAM_TRAILER  equ 6
SECBUFFER_STREAM_HEADER   equ 7

SECPKG_ATTR_STREAM_SIZES  equ 4
SCHANNEL_SHUTDOWN         equ 1
SCHANNEL_CRED_VERSION     equ 4

SP_PROT_TLS1_2_CLIENT     equ 0x00000800   ; 需要 1.0/1.1 可 OR 0x80 / 0x200
SCH_CRED_NO_DEFAULT_CREDS equ 0x00000010
SCH_CRED_AUTO_CRED_VALIDATION equ 0x00000020
SCH_USE_STRONG_CRYPTO     equ 0x00400000

ISC_REQ_REPLAY_DETECT     equ 0x00000004
ISC_REQ_SEQUENCE_DETECT   equ 0x00000008
ISC_REQ_CONFIDENTIALITY   equ 0x00000010
ISC_REQ_EXTENDED_ERROR    equ 0x00004000
ISC_REQ_STREAM            equ 0x00008000
ISC_FLAGS equ ISC_REQ_CONFIDENTIALITY | ISC_REQ_REPLAY_DETECT | ISC_REQ_SEQUENCE_DETECT | ISC_REQ_STREAM | ISC_REQ_EXTENDED_ERROR

MAX_CTX  equ 16
INSZ     equ 20480        ; 每连接的密文接收缓冲（一条 TLS 记录最大约 18437 字节）
PLSZ     equ 16384        ; 每连接的明文缓冲（一条记录的最大明文）
OUTSZ    equ 20480        ; 全局发送 / 握手输出缓冲
RDSZ     equ 16384        ; _tls_read 返回字符串用的静态缓冲

; ---- 连接槽结构 ------------------------------------------------------------
S_CTX     equ 0           ; CtxtHandle（8 字节）
S_SOCK    equ 8
S_USED    equ 12
S_HDR     equ 16          ; SecPkgContext_StreamSizes（20 字节）
S_TRL     equ 20
S_MAXMSG  equ 24
S_NBUF    equ 28
S_BLK     equ 32
S_INLEN   equ 36          ; 密文缓冲里已有字节数
S_PLOFF   equ 40          ; 明文缓冲已消费偏移
S_PLLEN   equ 44          ; 明文缓冲剩余字节
S_EOF     equ 48          ; 1 = 对端已关闭 / context expired
S_IN      equ 64
S_PL      equ S_IN + INSZ
SLOTSZ    equ S_PL + PLSZ

section .data
tls_pkgname db "Microsoft Unified Security Protocol Provider", 0

section .bss
alignb 16
tls_last_err    resd 1
tls_last_status resd 1
tls_cred        resb 16
tls_cred_valid  resd 1
tls_slots       resb MAX_CTX * SLOTSZ
tls_out         resb OUTSZ
tls_rd          resb RDSZ + 1

section .text

global _tls_last_error
_tls_last_error:
    mov  eax, [tls_last_err]
    ret

global _tls_last_sec_status
_tls_last_sec_status:
    mov  eax, [tls_last_status]
    ret

; ---------------------------------------------------------------------------
; 内部：eax = ctx(1..MAX_CTX) -> eax = 槽地址，无效返回 0
; ---------------------------------------------------------------------------
get_slot:
    dec  eax
    cmp  eax, MAX_CTX
    jae  .bad                       ; 无符号比较，同时挡掉 ctx=0
    imul eax, eax, SLOTSZ
    add  eax, tls_slots
    cmp  dword [eax + S_USED], 0
    je   .bad
    ret
.bad:
    xor  eax, eax
    ret

; ---------------------------------------------------------------------------
; 内部：eax = sock, edx = buf, ecx = len  ->  eax = 0 成功 / -1 失败
; 循环发送直到全部发完
; ---------------------------------------------------------------------------
send_all:
    push ebx
    push esi
    push edi
    mov  ebx, eax
    mov  esi, edx
    mov  edi, ecx
.loop:
    test edi, edi
    jle  .ok
    push 0                          ; flags
    push edi                        ; len
    push esi                        ; buf
    push ebx                        ; sock
    call _send@16
    cmp  eax, 0
    jle  .fail
    add  esi, eax
    sub  edi, eax
    jmp  .loop
.ok:
    xor  eax, eax
    jmp  .ret
.fail:
    mov  eax, -1
.ret:
    pop  edi
    pop  esi
    pop  ebx
    ret

; 初始化输出 SecBuffer(-92) 和 SecBufferDesc(-80)  —— 仅 _tls_connect 使用
%macro SETUP_OUT 0
    mov  dword [ebp - 92], OUTSZ
    mov  dword [ebp - 88], SECBUFFER_TOKEN
    mov  dword [ebp - 84], tls_out
    mov  dword [ebp - 80], SECBUFFER_VERSION
    mov  dword [ebp - 76], 1
    lea  eax, [ebp - 92]
    mov  [ebp - 72], eax
%endmacro

; ---------------------------------------------------------------------------
; int _tls_connect(int sock, const char *host)
;
; 栈帧（先 sub esp，再 push ebx/esi/edi，保存区在帧下方，互不重叠）：
;   [ebp-4]    槽地址
;   [ebp-8]    pfContextAttr 输出
;   [ebp-12]   返回给调用者的句柄（槽序号+1）
;   [ebp-16]   最近一次 ISC 返回值
;   [ebp-20]   已创建 SSPI 上下文标志
;   [ebp-32]   输入 SecBufferDesc  (12)   -32..-21
;   [ebp-64]   输入 SecBuffer[2]   (24)   -64..-41
;   [ebp-80]   输出 SecBufferDesc  (12)   -80..-69
;   [ebp-92]   输出 SecBuffer      (12)   -92..-81
;   [ebp-160]  SCHANNEL_CRED       (56)   -160..-105
; ---------------------------------------------------------------------------
FRAME_CONN equ 192

global _tls_connect
_tls_connect:
    push ebp
    mov  ebp, esp
    sub  esp, FRAME_CONN
    push ebx
    push esi
    push edi

    mov  dword [ebp - 16], SEC_I_CONTINUE_NEEDED
    mov  dword [ebp - 20], 0

    ; ---------------- 1) 凭据（全局共享，只取一次） ----------------
    cmp  dword [tls_cred_valid], 0
    jne  .cred_ready

    lea  edi, [ebp - 160]
    mov  ecx, 14
    xor  eax, eax
    cld
    rep  stosd                                  ; 清 56 字节
    mov  dword [ebp - 160], SCHANNEL_CRED_VERSION
    mov  dword [ebp - 160 + 32], SP_PROT_TLS1_2_CLIENT          ; grbitEnabledProtocols
    mov  dword [ebp - 160 + 48], SCH_CRED_NO_DEFAULT_CREDS | SCH_CRED_AUTO_CRED_VALIDATION | SCH_USE_STRONG_CRYPTO

    push 0                                      ; ptsExpiry
    push tls_cred                               ; phCredential
    push 0                                      ; pvGetKeyArgument
    push 0                                      ; pGetKeyFn
    lea  eax, [ebp - 160]
    push eax                                    ; pAuthData
    push 0                                      ; pvLogonID
    push SECPKG_CRED_OUTBOUND                   ; fCredentialUse
    push tls_pkgname                            ; pszPackage
    push 0                                      ; pszPrincipal
    call _AcquireCredentialsHandleA@36
    test eax, eax
    jz   .cred_ok
    mov  [tls_last_status], eax
    mov  dword [tls_last_err], 1
    xor  eax, eax
    jmp  .done
.cred_ok:
    mov  dword [tls_cred_valid], 1

.cred_ready:
    ; ---------------- 2) 找空闲槽 ----------------
    xor  esi, esi
    mov  ebx, tls_slots
.find_slot:
    cmp  esi, MAX_CTX
    jae  .no_slot
    cmp  dword [ebx + S_USED], 0
    je   .got_slot
    add  ebx, SLOTSZ
    inc  esi
    jmp  .find_slot

.no_slot:
    mov  dword [tls_last_err], 4
    xor  eax, eax
    jmp  .done

.got_slot:
    mov  [ebp - 4], ebx
    lea  eax, [esi + 1]
    mov  [ebp - 12], eax                        ; 返回的句柄
    mov  edi, ebx
    mov  ecx, 16
    xor  eax, eax
    cld
    rep  stosd                                  ; 清 64 字节槽头
    mov  eax, [ebp + 8]
    mov  [ebx + S_SOCK], eax
    mov  dword [ebx + S_USED], 1                ; 占用（失败时再释放）

    ; ---------------- 3) 第一次 ISC：生成 ClientHello ----------------
    SETUP_OUT
    mov  dword [ebp - 8], 0

    push 0                                      ; ptsExpiry
    lea  eax, [ebp - 8]
    push eax                                    ; pfContextAttr
    lea  eax, [ebp - 80]
    push eax                                    ; pOutput
    lea  eax, [ebx + S_CTX]
    push eax                                    ; phNewContext（直接写进槽）
    push 0                                      ; Reserved2
    push 0                                      ; pInput
    push 0                                      ; TargetDataRep（Schannel 忽略）
    push 0                                      ; Reserved1
    push ISC_FLAGS                              ; fContextReq
    push dword [ebp + 12]                       ; pszTargetName = host（SNI + 证书主机名校验）
    push 0                                      ; phContext = NULL（新建）
    push tls_cred                               ; phCredential
    call _InitializeSecurityContextA@48
    mov  [tls_last_status], eax
    cmp  eax, SEC_I_CONTINUE_NEEDED
    je   .first_ok
    mov  dword [tls_last_err], 2
    jmp  .fail

.first_ok:
    mov  dword [ebp - 20], 1                    ; 上下文已创建
    mov  ecx, [ebp - 92]                        ; 输出 token 长度
    test ecx, ecx
    jz   .hs_loop
    mov  eax, [ebx + S_SOCK]
    mov  edx, tls_out
    call send_all
    test eax, eax
    jnz  .fail_send

    ; ---------------- 4) 握手循环 ----------------
.hs_loop:
    ; 需要 recv：缓冲为空，或上一轮 INCOMPLETE_MESSAGE
    cmp  dword [ebx + S_INLEN], 0
    je   .hs_recv
    cmp  dword [ebp - 16], SEC_E_INCOMPLETE_MESSAGE
    jne  .hs_isc

.hs_recv:
    mov  eax, [ebx + S_INLEN]
    mov  ecx, INSZ
    sub  ecx, eax
    jle  .fail_ovf
    lea  edx, [ebx + eax + S_IN]
    push 0                                      ; flags
    push ecx                                    ; len
    push edx                                    ; buf
    push dword [ebx + S_SOCK]
    call _recv@16
    cmp  eax, 0
    jle  .fail_recv
    add  [ebx + S_INLEN], eax

.hs_isc:
    ; 输入：buf[0]=TOKEN(已收数据)，buf[1]=EMPTY（接收 EXTRA 信息）
    mov  eax, [ebx + S_INLEN]
    mov  [ebp - 64], eax
    mov  dword [ebp - 60], SECBUFFER_TOKEN
    lea  eax, [ebx + S_IN]
    mov  [ebp - 56], eax
    mov  dword [ebp - 52], 0
    mov  dword [ebp - 48], SECBUFFER_EMPTY
    mov  dword [ebp - 44], 0
    mov  dword [ebp - 32], SECBUFFER_VERSION
    mov  dword [ebp - 28], 2
    lea  eax, [ebp - 64]
    mov  [ebp - 24], eax

    SETUP_OUT                                   ; 每轮都要重置 cbBuffer

    push 0                                      ; ptsExpiry
    lea  eax, [ebp - 8]
    push eax                                    ; pfContextAttr
    lea  eax, [ebp - 80]
    push eax                                    ; pOutput
    push 0                                      ; phNewContext = NULL
    push 0                                      ; Reserved2
    lea  eax, [ebp - 32]
    push eax                                    ; pInput
    push 0                                      ; TargetDataRep
    push 0                                      ; Reserved1
    push ISC_FLAGS                              ; fContextReq
    push dword [ebp + 12]                       ; pszTargetName
    lea  eax, [ebx + S_CTX]
    push eax                                    ; phContext
    push tls_cred                               ; phCredential
    call _InitializeSecurityContextA@48
    mov  [tls_last_status], eax
    mov  [ebp - 16], eax

    cmp  eax, SEC_E_INCOMPLETE_MESSAGE
    je   .hs_loop                               ; 数据不够，回去继续 recv
    cmp  eax, SEC_E_OK
    je   .hs_tok
    cmp  eax, SEC_I_CONTINUE_NEEDED
    jne  .hs_err

.hs_tok:
    mov  ecx, [ebp - 92]                        ; 本轮要发送的 token
    test ecx, ecx
    jz   .hs_extra
    mov  eax, [ebx + S_SOCK]
    mov  edx, tls_out
    call send_all
    test eax, eax
    jnz  .fail_send

.hs_extra:
    ; 输入 buf[1] 若为 EXTRA，说明末尾有未消费数据，前移保留
    cmp  dword [ebp - 48], SECBUFFER_EXTRA
    jne  .no_extra
    mov  ecx, [ebp - 52]
    mov  eax, [ebx + S_INLEN]
    sub  eax, ecx
    lea  esi, [ebx + eax + S_IN]
    lea  edi, [ebx + S_IN]
    mov  [ebx + S_INLEN], ecx
    cld
    rep  movsb
    jmp  .after_extra
.no_extra:
    mov  dword [ebx + S_INLEN], 0
.after_extra:
    cmp  dword [ebp - 16], SEC_E_OK
    jne  .hs_loop

    ; ---------------- 5) 握手完成：取记录大小，供 write/read 使用 ----------------
    lea  eax, [ebx + S_HDR]
    push eax                                    ; pBuffer
    push SECPKG_ATTR_STREAM_SIZES               ; ulAttribute
    lea  eax, [ebx + S_CTX]
    push eax                                    ; phContext
    call _QueryContextAttributesA@12
    test eax, eax
    jnz  .fail_qry

    mov  dword [tls_last_err], 0
    mov  eax, [ebp - 12]
    jmp  .done

; ---------------- 失败出口 ----------------
.hs_err:
    mov  dword [tls_last_err], 3
    jmp  .fail
.fail_send:
    mov  dword [tls_last_err], 5
    jmp  .fail
.fail_recv:
    mov  dword [tls_last_err], 6
    jmp  .fail
.fail_ovf:
    mov  dword [tls_last_err], 7
    jmp  .fail
.fail_qry:
    mov  [tls_last_status], eax
    mov  dword [tls_last_err], 8
.fail:
    mov  ebx, [ebp - 4]
    cmp  dword [ebp - 20], 0
    je   .fail_noctx
    lea  eax, [ebx + S_CTX]
    push eax
    call _DeleteSecurityContext@4
.fail_noctx:
    mov  dword [ebx + S_USED], 0
    xor  eax, eax

.done:
    pop  edi
    pop  esi
    pop  ebx
    mov  esp, ebp
    pop  ebp
    ret

; ---------------------------------------------------------------------------
; int _tls_write(int ctx, const char *data, int len)
;
; 栈帧：[ebp-48..-1] SecBuffer[4]  [ebp-60..-49] SecBufferDesc
;       [ebp-64] 本块长度  [ebp-68] 剩余  [ebp-72] 源指针
; ---------------------------------------------------------------------------
global _tls_write
_tls_write:
    push ebp
    mov  ebp, esp
    sub  esp, 96
    push ebx
    push esi
    push edi

    mov  eax, [ebp + 8]
    call get_slot
    test eax, eax
    jz   .w_err
    mov  ebx, eax
    cmp  dword [ebp + 16], 0
    jle  .w_zero

    mov  eax, [ebp + 16]
    mov  [ebp - 68], eax
    mov  eax, [ebp + 12]
    mov  [ebp - 72], eax

.w_loop:
    mov  edx, [ebp - 68]
    test edx, edx
    jle  .w_ok

    ; chunk = min(剩余, cbMaximumMessage, OUTSZ - header - trailer)
    mov  ecx, [ebx + S_MAXMSG]
    mov  eax, OUTSZ
    sub  eax, [ebx + S_HDR]
    sub  eax, [ebx + S_TRL]
    cmp  ecx, eax
    jbe  .w_c1
    mov  ecx, eax
.w_c1:
    cmp  ecx, edx
    jbe  .w_c2
    mov  ecx, edx
.w_c2:
    mov  [ebp - 64], ecx

    ; 明文拷到 tls_out + header
    mov  esi, [ebp - 72]
    mov  edi, tls_out
    add  edi, [ebx + S_HDR]
    cld
    rep  movsb

    ; SecBuffer[0] = STREAM_HEADER
    mov  eax, [ebx + S_HDR]
    mov  [ebp - 48], eax
    mov  dword [ebp - 44], SECBUFFER_STREAM_HEADER
    mov  dword [ebp - 40], tls_out
    ; SecBuffer[1] = DATA
    mov  eax, [ebp - 64]
    mov  [ebp - 36], eax
    mov  dword [ebp - 32], SECBUFFER_DATA
    mov  eax, tls_out
    add  eax, [ebx + S_HDR]
    mov  [ebp - 28], eax
    ; SecBuffer[2] = STREAM_TRAILER
    mov  eax, [ebx + S_TRL]
    mov  [ebp - 24], eax
    mov  dword [ebp - 20], SECBUFFER_STREAM_TRAILER
    mov  eax, tls_out
    add  eax, [ebx + S_HDR]
    add  eax, [ebp - 64]
    mov  [ebp - 16], eax
    ; SecBuffer[3] = EMPTY
    mov  dword [ebp - 12], 0
    mov  dword [ebp - 8], SECBUFFER_EMPTY
    mov  dword [ebp - 4], 0
    ; SecBufferDesc
    mov  dword [ebp - 60], SECBUFFER_VERSION
    mov  dword [ebp - 56], 4
    lea  eax, [ebp - 48]
    mov  [ebp - 52], eax

    push 0                                      ; MessageSeqNo
    lea  eax, [ebp - 60]
    push eax                                    ; pMessage
    push 0                                      ; fQOP
    lea  eax, [ebx + S_CTX]
    push eax                                    ; phContext
    call _EncryptMessage@16
    mov  [tls_last_status], eax
    test eax, eax
    jnz  .w_enc_err

    ; 实际密文长度 = header + data + trailer（Encrypt 会回填各自 cbBuffer）
    mov  ecx, [ebp - 48]
    add  ecx, [ebp - 36]
    add  ecx, [ebp - 24]
    mov  eax, [ebx + S_SOCK]
    mov  edx, tls_out
    call send_all
    test eax, eax
    jnz  .w_send_err

    mov  eax, [ebp - 64]
    add  [ebp - 72], eax
    sub  [ebp - 68], eax
    jmp  .w_loop

.w_ok:
    mov  dword [tls_last_err], 0
    mov  eax, [ebp + 16]
    jmp  .w_ret
.w_zero:
    xor  eax, eax
    jmp  .w_ret
.w_enc_err:
    mov  dword [tls_last_err], 9
    jmp  .w_err
.w_send_err:
    mov  dword [tls_last_err], 5
.w_err:
    mov  eax, -1
.w_ret:
    pop  edi
    pop  esi
    pop  ebx
    mov  esp, ebp
    pop  ebp
    ret

; ---------------------------------------------------------------------------
; int _tls_read_buf(int ctx, char *buf, int maxlen)
;   返回 >0 字节数；0 = 对端关闭；-1 = 失败（见 tls_last_err）
;   阻塞直到至少有 1 字节明文、EOF 或出错
;
; 栈帧：[ebp-4] 需要 recv 标志  [ebp-52..-5] SecBuffer[4]  [ebp-64..-53] SecBufferDesc
; ---------------------------------------------------------------------------
global _tls_read_buf
_tls_read_buf:
    push ebp
    mov  ebp, esp
    sub  esp, 96
    push ebx
    push esi
    push edi

    mov  eax, [ebp + 8]
    call get_slot
    test eax, eax
    jz   .rb_err
    mov  ebx, eax
    cmp  dword [ebp + 16], 0
    jle  .rb_eof                                ; maxlen<=0：什么也不读
    mov  dword [ebp - 4], 0

.rb_loop:
    mov  ecx, [ebx + S_PLLEN]
    test ecx, ecx
    jnz  .rb_serve                              ; 有现成明文
    cmp  dword [ebx + S_EOF], 0
    jne  .rb_eof
    cmp  dword [ebp - 4], 0
    jne  .rb_recv                               ; 上轮 INCOMPLETE_MESSAGE
    cmp  dword [ebx + S_INLEN], 0
    jne  .rb_decrypt

.rb_recv:
    mov  eax, [ebx + S_INLEN]
    mov  ecx, INSZ
    sub  ecx, eax
    jle  .rb_ovf
    lea  edx, [ebx + eax + S_IN]
    push 0
    push ecx
    push edx
    push dword [ebx + S_SOCK]
    call _recv@16
    cmp  eax, 0
    jl   .rb_rerr
    je   .rb_closed
    add  [ebx + S_INLEN], eax
    mov  dword [ebp - 4], 0

.rb_decrypt:
    ; buf[0] = DATA（全部密文），buf[1..3] = EMPTY
    mov  eax, [ebx + S_INLEN]
    mov  [ebp - 52], eax
    mov  dword [ebp - 48], SECBUFFER_DATA
    lea  eax, [ebx + S_IN]
    mov  [ebp - 44], eax
    mov  dword [ebp - 40], 0
    mov  dword [ebp - 36], SECBUFFER_EMPTY
    mov  dword [ebp - 32], 0
    mov  dword [ebp - 28], 0
    mov  dword [ebp - 24], SECBUFFER_EMPTY
    mov  dword [ebp - 20], 0
    mov  dword [ebp - 16], 0
    mov  dword [ebp - 12], SECBUFFER_EMPTY
    mov  dword [ebp - 8], 0
    mov  dword [ebp - 64], SECBUFFER_VERSION
    mov  dword [ebp - 60], 4
    lea  eax, [ebp - 52]
    mov  [ebp - 56], eax

    push 0                                      ; pfQOP
    push 0                                      ; MessageSeqNo
    lea  eax, [ebp - 64]
    push eax                                    ; pMessage
    lea  eax, [ebx + S_CTX]
    push eax                                    ; phContext
    call _DecryptMessage@16
    mov  [tls_last_status], eax

    cmp  eax, SEC_E_INCOMPLETE_MESSAGE
    je   .rb_need
    cmp  eax, SEC_I_CONTEXT_EXPIRED
    je   .rb_expired
    cmp  eax, SEC_E_OK
    jne  .rb_derr                               ; 含 SEC_I_RENEGOTIATE（暂不支持）

    ; 找 DATA 与 EXTRA 缓冲
    lea  eax, [ebp - 52]
    mov  ecx, 4
    xor  esi, esi                               ; 明文指针
    xor  edi, edi                               ; 明文长度
    xor  edx, edx                               ; EXTRA 长度
.rb_scan:
    cmp  dword [eax + 4], SECBUFFER_DATA
    jne  .rb_s1
    test esi, esi
    jnz  .rb_s3
    mov  esi, [eax + 8]
    mov  edi, [eax]
    jmp  .rb_s3
.rb_s1:
    cmp  dword [eax + 4], SECBUFFER_EXTRA
    jne  .rb_s3
    test edx, edx
    jnz  .rb_s3
    mov  edx, [eax]
.rb_s3:
    add  eax, 12
    dec  ecx
    jnz  .rb_scan

    ; 明文拷到槽的明文缓冲（edx 不受 rep movsb 影响）
    mov  ecx, edi
    cmp  ecx, PLSZ
    ja   .rb_derr
    mov  [ebx + S_PLLEN], ecx
    mov  dword [ebx + S_PLOFF], 0
    lea  edi, [ebx + S_PL]
    cld
    rep  movsb

    ; EXTRA（下一条记录的开头）前移到密文缓冲起点
    test edx, edx
    jz   .rb_noextra
    mov  eax, [ebx + S_INLEN]
    sub  eax, edx
    lea  esi, [ebx + eax + S_IN]
    lea  edi, [ebx + S_IN]
    mov  ecx, edx
    mov  [ebx + S_INLEN], edx
    rep  movsb
    jmp  .rb_loop
.rb_noextra:
    mov  dword [ebx + S_INLEN], 0
    jmp  .rb_loop

.rb_need:
    mov  dword [ebp - 4], 1
    jmp  .rb_loop

.rb_expired:
    mov  dword [ebx + S_EOF], 1
    mov  dword [ebx + S_INLEN], 0
    jmp  .rb_loop

.rb_serve:                                      ; ecx = 剩余明文
    mov  eax, [ebp + 16]
    cmp  ecx, eax
    jbe  .rb_sv1
    mov  ecx, eax
.rb_sv1:
    mov  esi, [ebx + S_PLOFF]
    lea  esi, [ebx + esi + S_PL]
    mov  edi, [ebp + 12]
    mov  eax, ecx                               ; 返回值
    add  [ebx + S_PLOFF], ecx
    sub  [ebx + S_PLLEN], ecx
    cld
    rep  movsb
    mov  dword [tls_last_err], 0
    jmp  .rb_ret

.rb_closed:
    mov  dword [ebx + S_EOF], 1
.rb_eof:
    xor  eax, eax
    jmp  .rb_ret
.rb_derr:
    mov  dword [tls_last_err], 10
    jmp  .rb_err
.rb_ovf:
    mov  dword [tls_last_err], 7
    jmp  .rb_err
.rb_rerr:
    mov  dword [tls_last_err], 6
.rb_err:
    mov  eax, -1
.rb_ret:
    pop  edi
    pop  esi
    pop  ebx
    mov  esp, ebp
    pop  ebp
    ret

; ---------------------------------------------------------------------------
; char* _tls_read(int ctx, int maxlen)
;   最多读 maxlen 字节，返回静态缓冲区中以 0 结尾的字符串（下次调用会被覆盖）
;   EOF 返回空串；失败返回 NULL。数据中含 0x00 时字符串会被截断，
;   二进制数据请改用 _tls_read_buf。
; ---------------------------------------------------------------------------
global _tls_read
_tls_read:
    push ebp
    mov  ebp, esp
    push ebx
    push esi
    push edi
    sub  esp, 16
    mov  eax, [ebp + 12]
    cmp  eax, RDSZ
    jbe  .r_c1
    mov  eax, RDSZ
.r_c1:
    push eax                                    ; maxlen
    push tls_rd
    push dword [ebp + 8]
    call _tls_read_buf
    add  esp, 12
    cmp  eax, 0
    jl   .r_fail
    mov  esi, eax                               ; esi = 读到的字节数

    ; 按 YXG 字符串 ABI 分配：gc_alloc(len+8)，[p]=len，数据在 p+4，返回 p+4
    lea  eax, [esi + 8]
    add  eax, 3
    and  eax, -4
    push eax
    call _gc_alloc
    add  esp, 4
    test eax, eax
    jz   .r_fail
    mov  [eax], esi                             ; 长度头
    lea  edi, [eax + 4]                         ; 数据区
    push edi
    mov  ecx, esi
    mov  ebx, tls_rd
.r_copy:
    test ecx, ecx
    jz   .r_copied
    mov  dl, [ebx]
    mov  [edi], dl
    inc  ebx
    inc  edi
    dec  ecx
    jmp  .r_copy
.r_copied:
    mov  byte [edi], 0                          ; NUL 结尾
    pop  eax                                    ; 数据指针
    add  esp, 16
    pop  edi
    pop  esi
    pop  ebx
    pop  ebp
    ret
.r_fail:
    xor  eax, eax
    add  esp, 16
    pop  edi
    pop  esi
    pop  ebx
    pop  ebp
    ret
; ---------------------------------------------------------------------------
; void _tls_close(int ctx)
;   尽力发送 close_notify，然后 DeleteSecurityContext 并释放槽位
;
; 栈帧：[ebp-4] SCHANNEL_SHUTDOWN 令牌  [ebp-16..-5] SecBuffer  [ebp-28..-17] SecBufferDesc
;       [ebp-40..-29] 输出 SecBuffer  [ebp-52..-41] 输出 SecBufferDesc  [ebp-56] attr
; ---------------------------------------------------------------------------
global _tls_close
_tls_close:
    push ebp
    mov  ebp, esp
    sub  esp, 64
    push ebx

    mov  eax, [ebp + 8]
    call get_slot
    test eax, eax
    jz   .c_done
    mov  ebx, eax

    ; 1) ApplyControlToken(SCHANNEL_SHUTDOWN)
    mov  dword [ebp - 4], SCHANNEL_SHUTDOWN
    mov  dword [ebp - 16], 4
    mov  dword [ebp - 12], SECBUFFER_TOKEN
    lea  eax, [ebp - 4]
    mov  [ebp - 8], eax
    mov  dword [ebp - 28], SECBUFFER_VERSION
    mov  dword [ebp - 24], 1
    lea  eax, [ebp - 16]
    mov  [ebp - 20], eax

    lea  eax, [ebp - 28]
    push eax                                    ; pInput
    lea  eax, [ebx + S_CTX]
    push eax                                    ; phContext
    call _ApplyControlToken@8
    test eax, eax
    jnz  .c_delete

    ; 2) 生成 close_notify
    mov  dword [ebp - 40], OUTSZ
    mov  dword [ebp - 36], SECBUFFER_TOKEN
    mov  dword [ebp - 32], tls_out
    mov  dword [ebp - 52], SECBUFFER_VERSION
    mov  dword [ebp - 48], 1
    lea  eax, [ebp - 40]
    mov  [ebp - 44], eax
    mov  dword [ebp - 56], 0

    push 0                                      ; ptsExpiry
    lea  eax, [ebp - 56]
    push eax                                    ; pfContextAttr
    lea  eax, [ebp - 52]
    push eax                                    ; pOutput
    lea  eax, [ebx + S_CTX]
    push eax                                    ; phNewContext
    push 0                                      ; Reserved2
    push 0                                      ; pInput
    push 0                                      ; TargetDataRep
    push 0                                      ; Reserved1
    push ISC_FLAGS                              ; fContextReq
    push 0                                      ; pszTargetName
    lea  eax, [ebx + S_CTX]
    push eax                                    ; phContext
    push tls_cred                               ; phCredential
    call _InitializeSecurityContextA@48
    test eax, eax
    jnz  .c_delete                              ; 关闭阶段忽略各类状态，直接清理
    mov  ecx, [ebp - 40]
    test ecx, ecx
    jz   .c_delete
    mov  eax, [ebx + S_SOCK]
    mov  edx, tls_out
    call send_all                               ; 尽力而为，失败也不管

.c_delete:
    lea  eax, [ebx + S_CTX]
    push eax
    call _DeleteSecurityContext@4
    mov  dword [ebx + S_INLEN], 0
    mov  dword [ebx + S_PLLEN], 0
    mov  dword [ebx + S_USED], 0
.c_done:
    pop  ebx
    mov  esp, ebp
    pop  ebp
    ret