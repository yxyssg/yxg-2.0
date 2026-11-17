; ============================================================
; runtime_socket.asm
; YXG 编译器网络运行时（Winsock2 封装，cdecl 调用约定）
;
; 参数排布：[ebp+8]=第1参数 ... [ebp+24]=第5参数；返回值在 eax。
;
; 注意：_socket_recv_str 返回 8 字节 str，编译器按"返回大结构体"
; ABI 传入隐藏返回缓冲区指针作为第一个参数：
;       str _socket_recv_str(void* hidden_ret_buf, int fd, int len)
; ============================================================

section .text
    extern _WSAStartup@8
    extern _socket@12
    extern _connect@12
    extern _bind@12
    extern _listen@8
    extern _accept@12
    extern _send@16
    extern _recv@16
    extern _sendto@24
    extern _recvfrom@24
    extern _setsockopt@20
    extern _getsockopt@20
    extern _getpeername@12
    extern _getsockname@12
    extern _select@20
    extern _ioctlsocket@12
    extern _closesocket@4
    extern _htons@4
    extern _gethostbyname@4
    extern _inet_addr@4
    extern _inet_ntoa@4
    extern _WSAGetLastError@0
    extern _GetProcessHeap@0
    extern _HeapAlloc@12
    extern _gc_alloc

    global _socket_create
    global _socket_connect
    global _socket_bind
    global _socket_listen
    global _socket_accept
    global _socket_send
    global _socket_recv
    global _socket_close
    global _socket_last_error
    global _socket_sendto
    global _socket_recvfrom
    global _socket_sender_addr
    global _socket_sender_port
    global _socket_sender_ip
    global _socket_set_timeout
    global _socket_set_nonblock
    global _socket_set_reuseaddr
    global _socket_set_nodelay
    global _socket_get_error
    global _socket_peer_ip
    global _socket_peer_port
    global _socket_local_port
    global _socket_send_str
    global _socket_recv_str
    global _socket_send_all
    global _socket_recv_line
    global _socket_recv_exact
    global _socket_select_read
    global _socket_select_write
    global _socket_htonl
    global _socket_htons
    global _socket_ntohl
    global _socket_ntohs
    global _socket_bind_addr
    global _socket_inet_addr
    global _socket_inet_ntoa
    global _socket_error_string

; ------------------------------------------------------------
; 内部辅助：_ws_alloc(int bytes)，进程默认堆分配并清零
; ------------------------------------------------------------
_ws_alloc:
    push ebp
    mov ebp, esp
    push ebx

    call _GetProcessHeap@0
    test eax, eax
    jz .fail

    push dword [ebp+8]
    push 8                      ; HEAP_ZERO_MEMORY
    push eax
    call _HeapAlloc@12
    test eax, eax
    jz .fail

    pop ebx
    pop ebp
    ret

.fail:
    xor eax, eax
    pop ebx
    pop ebp
    ret

; ------------------------------------------------------------
; 内部辅助：惰性初始化 Winsock
; ------------------------------------------------------------
_ws_ensure:
    cmp dword [ws_started], 0
    jne .ok

    push wsadata
    push 0x0202                 ; MAKEWORD(2,2)
    call _WSAStartup@8
    test eax, eax
    jnz .fail

    mov dword [ws_started], 1
.ok:
    xor eax, eax
    ret
.fail:
    mov [ws_last_error], eax
    mov eax, -1
    ret

; ------------------------------------------------------------
; 内部辅助：保存 WSAGetLastError 并返回 -1
; ------------------------------------------------------------
_ws_fail:
    call _WSAGetLastError@0
    mov [ws_last_error], eax
    mov eax, -1
    ret

; ------------------------------------------------------------
; int _socket_create(int af, int type, int proto)
; ------------------------------------------------------------
_socket_create:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi

    call _ws_ensure
    test eax, eax
    jnz .err

    push dword [ebp+16]
    push dword [ebp+12]
    push dword [ebp+8]
    call _socket@12

    cmp eax, 0xFFFFFFFF
    je .err
    jmp .done

.err:
    call _ws_fail
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

; ------------------------------------------------------------
; int _socket_connect(int fd, string host, int port)
; ------------------------------------------------------------
_socket_connect:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi

    call _ws_ensure
    test eax, eax
    jnz .err

    push dword [ebp+12]
    call _gethostbyname@4
    test eax, eax
    jz .err

    mov esi, [eax + 12]         ; hostent.h_addr_list（偏移 12）
    test esi, esi
    jz .err
    mov esi, [esi]
    test esi, esi
    jz .err
    mov esi, [esi]              ; 4 字节地址（网络字节序）

    push dword [ebp+16]
    call _htons@4               ; ax = 网络字节序端口

    mov word  [ws_sa], 2        ; AF_INET
    mov word  [ws_sa + 2], ax
    mov dword [ws_sa + 4], esi
    mov dword [ws_sa + 8], 0
    mov dword [ws_sa + 12], 0

    push 16
    push ws_sa
    push dword [ebp+8]
    call _connect@12

    test eax, eax
    jnz .err

    xor eax, eax
    jmp .done

.err:
    call _ws_fail
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

; ------------------------------------------------------------
; int _socket_bind(int fd, int port)
; ------------------------------------------------------------
_socket_bind:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi

    call _ws_ensure
    test eax, eax
    jnz .err

    push dword [ebp+12]
    call _htons@4

    mov word  [ws_sa], 2        ; AF_INET
    mov word  [ws_sa + 2], ax
    mov dword [ws_sa + 4], 0    ; INADDR_ANY
    mov dword [ws_sa + 8], 0
    mov dword [ws_sa + 12], 0

    push 16
    push ws_sa
    push dword [ebp+8]
    call _bind@12

    test eax, eax
    jnz .err

    xor eax, eax
    jmp .done

.err:
    call _ws_fail
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

; ------------------------------------------------------------
; int _socket_listen(int fd, int backlog)
; ------------------------------------------------------------
_socket_listen:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi

    call _ws_ensure
    test eax, eax
    jnz .err

    push dword [ebp+12]
    push dword [ebp+8]
    call _listen@8

    test eax, eax
    jnz .err

    xor eax, eax
    jmp .done

.err:
    call _ws_fail
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

; ------------------------------------------------------------
; int _socket_accept(int fd)
; ------------------------------------------------------------
_socket_accept:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi

    call _ws_ensure
    test eax, eax
    jnz .err

    push 0
    push 0
    push dword [ebp+8]
    call _accept@12

    cmp eax, 0xFFFFFFFF
    je .err
    jmp .done

.err:
    call _ws_fail
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

; ------------------------------------------------------------
; int _socket_send(int fd, string data, int len)
; ------------------------------------------------------------
_socket_send:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi

    call _ws_ensure
    test eax, eax
    jnz .err

    push 0
    push dword [ebp+16]
    push dword [ebp+12]
    push dword [ebp+8]
    call _send@16

    cmp eax, 0xFFFFFFFF
    je .err
    jmp .done

.err:
    call _ws_fail
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

; ------------------------------------------------------------
; string _socket_recv(int fd, int len)
; ------------------------------------------------------------
_socket_recv:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi

    call _ws_ensure
    test eax, eax
    jnz .null

    mov dword [ws_last_error], 0

    mov eax, [ebp+12]
    add eax, 5
    push eax
    call _gc_alloc
    add esp, 4
    test eax, eax
    jz .null
    mov ebx, eax

    lea edx, [ebx + 4]
    push 0
    push dword [ebp+12]
    push edx
    push dword [ebp+8]
    call _recv@16

    cmp eax, 0xFFFFFFFF
    je .err_recv
    test eax, eax
    js .err_recv

    mov dword [ebx], eax
    mov byte [ebx + eax + 4], 0
    lea eax, [ebx + 4]
    jmp .done

.err_recv:
    call _ws_fail
    mov dword [ebx], 0
    mov byte [ebx + 4], 0
    lea eax, [ebx + 4]
    jmp .done

.null:
    mov eax, _ws_empty_str
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

; ------------------------------------------------------------
; int _socket_close(int fd)
; ------------------------------------------------------------
_socket_close:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi

    push dword [ebp+8]
    call _closesocket@4

    test eax, eax
    jnz .err

    xor eax, eax
    jmp .done

.err:
    call _ws_fail
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

; ------------------------------------------------------------
; int _socket_last_error()
; ------------------------------------------------------------
_socket_last_error:
    mov eax, [ws_last_error]
    ret

; ------------------------------------------------------------
; int _socket_sendto(int fd, string data, int len, string host, int port)
; ------------------------------------------------------------
_socket_sendto:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi

    call _ws_ensure
    test eax, eax
    jnz .err

    push dword [ebp+20]         ; host
    call _gethostbyname@4
    test eax, eax
    jz .err
    mov esi, [eax + 12]
    test esi, esi
    jz .err
    mov esi, [esi]
    test esi, esi
    jz .err
    mov esi, [esi]

    push dword [ebp+24]         ; port
    call _htons@4

    mov word  [ws_sa], 2
    mov word  [ws_sa + 2], ax
    mov dword [ws_sa + 4], esi
    mov dword [ws_sa + 8], 0
    mov dword [ws_sa + 12], 0

    push 16                     ; tolen
    push ws_sa                  ; to
    push 0                      ; flags
    push dword [ebp+16]         ; len
    push dword [ebp+12]         ; data
    push dword [ebp+8]          ; fd
    call _sendto@24

    cmp eax, 0xFFFFFFFF
    je .err
    jmp .done

.err:
    call _ws_fail
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

; ------------------------------------------------------------
; string _socket_recvfrom(int fd, int len)
; ------------------------------------------------------------
_socket_recvfrom:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi

    call _ws_ensure
    test eax, eax
    jnz .null

    mov dword [ws_last_error], 0

    mov eax, [ebp+12]
    add eax, 1
    push eax
    call _ws_alloc
    add esp, 4
    test eax, eax
    jz .null
    mov ebx, eax

    mov dword [ws_from_len], 16

    push ws_from_len
    push ws_from_sa
    push 0
    push dword [ebp+12]
    push ebx
    push dword [ebp+8]
    call _recvfrom@24

    movzx ecx, word [ws_from_sa + 2]
    mov [ws_last_from_port], ecx
    mov ecx, dword [ws_from_sa + 4]
    mov [ws_last_from_addr], ecx

    test eax, eax
    js .err_recv
    mov byte [ebx + eax], 0
    mov eax, ebx
    jmp .done

.err_recv:
    call _ws_fail
    mov byte [ebx], 0
    mov eax, ebx
    jmp .done

.null:
    mov eax, _ws_empty_str
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

; ------------------------------------------------------------
; int _socket_sender_addr()
; ------------------------------------------------------------
_socket_sender_addr:
    mov eax, [ws_last_from_addr]
    ret

; ------------------------------------------------------------
; int _socket_sender_port()
; ------------------------------------------------------------
_socket_sender_port:
    movzx eax, word [ws_last_from_port]
    xchg al, ah
    ret

; ------------------------------------------------------------
; string _socket_sender_ip()
; 静态缓冲区，返回值须立即使用
; ------------------------------------------------------------
_socket_sender_ip:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi

    mov edi, ws_ip_buf
    mov al, byte [ws_last_from_addr]
    call _write_byte_decimal
    mov byte [edi], '.'
    inc edi
    mov al, byte [ws_last_from_addr + 1]
    call _write_byte_decimal
    mov byte [edi], '.'
    inc edi
    mov al, byte [ws_last_from_addr + 2]
    call _write_byte_decimal
    mov byte [edi], '.'
    inc edi
    mov al, byte [ws_last_from_addr + 3]
    call _write_byte_decimal
    mov byte [edi], 0

    mov eax, ws_ip_buf
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

; 辅助：al = 0..255，十进制写入 [edi]，edi 前进；破坏 eax/edx/ecx
_write_byte_decimal:
    movzx eax, al
    xor edx, edx
    mov ecx, 100
    div ecx
    test al, al
    jz .skip_h
    add al, '0'
    mov [edi], al
    inc edi
.skip_h:
    mov eax, edx
    xor edx, edx
    mov ecx, 10
    div ecx
    test al, al
    jz .skip_t
    add al, '0'
    mov [edi], al
    inc edi
.skip_t:
    add dl, '0'
    mov [edi], dl
    inc edi
    ret

; ------------------------------------------------------------
; int _socket_set_timeout(int fd, int ms)
; ------------------------------------------------------------
_socket_set_timeout:
    push ebp
    mov ebp, esp
    push ebx

    mov eax, [ebp+12]
    mov [ws_opt_int], eax

    push 4
    push ws_opt_int
    push 0x1006
    push 0xFFFF
    push dword [ebp+8]
    call _setsockopt@20
    test eax, eax
    jnz .err

    push 4
    push ws_opt_int
    push 0x1005
    push 0xFFFF
    push dword [ebp+8]
    call _setsockopt@20
    test eax, eax
    jnz .err

    xor eax, eax
    jmp .done

.err:
    call _ws_fail
.done:
    pop ebx
    pop ebp
    ret

; ------------------------------------------------------------
; int _socket_set_nonblock(int fd, int on)
; ------------------------------------------------------------
_socket_set_nonblock:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi

    sub esp, 4
    mov eax, [ebp+12]
    mov [esp], eax

    push esp
    push 0x8004667E             ; FIONBIO
    push dword [ebp+8]
    call _ioctlsocket@12
    add esp, 4

    test eax, eax
    jnz .err
    xor eax, eax
    jmp .done

.err:
    call _ws_fail
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

; ------------------------------------------------------------
; int _socket_set_reuseaddr(int fd, int on)
; 端口快速复用，服务器重启不再报 10048
; ------------------------------------------------------------
_socket_set_reuseaddr:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi

    mov eax, [ebp+12]
    mov [ws_opt_int], eax
    mov dword [ws_opt_len], 4

    push ws_opt_len
    push ws_opt_int
    push 0x0004                 ; SO_REUSEADDR
    push 0xFFFF                 ; SOL_SOCKET
    push dword [ebp+8]
    call _setsockopt@20

    test eax, eax
    jnz .err
    xor eax, eax
    jmp .done

.err:
    call _ws_fail
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

; ------------------------------------------------------------
; int _socket_set_nodelay(int fd, int on)
; 禁用 Nagle 算法，低延迟场景（游戏/交互协议）
; ------------------------------------------------------------
_socket_set_nodelay:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi

    mov eax, [ebp+12]
    mov [ws_opt_int], eax
    mov dword [ws_opt_len], 4

    push ws_opt_len
    push ws_opt_int
    push 0x0001                 ; TCP_NODELAY
    push 6                      ; IPPROTO_TCP
    push dword [ebp+8]
    call _setsockopt@20

    test eax, eax
    jnz .err
    xor eax, eax
    jmp .done

.err:
    call _ws_fail
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

; ------------------------------------------------------------
; int _socket_get_error(int fd)
; 读取并清空 SO_ERROR，无错误返回 0
; ------------------------------------------------------------
_socket_get_error:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi

    mov dword [ws_opt_int], 0
    mov dword [ws_opt_len], 4

    push ws_opt_len
    push ws_opt_int
    push 0x1007                 ; SO_ERROR
    push 0xFFFF
    push dword [ebp+8]
    call _getsockopt@20

    test eax, eax
    jnz .err
    mov eax, [ws_opt_int]
    jmp .done

.err:
    call _ws_fail
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

; ------------------------------------------------------------
; string _socket_peer_ip(int fd)
; TCP 对端 IP；静态缓冲区，返回值须立即使用
; ------------------------------------------------------------
_socket_peer_ip:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi

    mov dword [ws_name_len], 16

    push ws_name_len
    push ws_sa
    push dword [ebp+8]
    call _getpeername@12

    test eax, eax
    jnz .err

    mov edi, ws_ip_buf
    mov al, byte [ws_sa + 4]
    call _write_byte_decimal
    mov byte [edi], '.'
    inc edi
    mov al, byte [ws_sa + 5]
    call _write_byte_decimal
    mov byte [edi], '.'
    inc edi
    mov al, byte [ws_sa + 6]
    call _write_byte_decimal
    mov byte [edi], '.'
    inc edi
    mov al, byte [ws_sa + 7]
    call _write_byte_decimal
    mov byte [edi], 0

    mov eax, ws_ip_buf
    jmp .done

.err:
    call _ws_fail
    mov eax, _ws_empty_str
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

; ------------------------------------------------------------
; int _socket_peer_port(int fd)
; TCP 对端端口（主机字节序）
; ------------------------------------------------------------
_socket_peer_port:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi

    mov dword [ws_name_len], 16

    push ws_name_len
    push ws_sa
    push dword [ebp+8]
    call _getpeername@12

    test eax, eax
    jnz .err

    movzx eax, word [ws_sa + 2]
    xchg al, ah
    jmp .done

.err:
    call _ws_fail
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

; ------------------------------------------------------------
; int _socket_local_port(int fd)
; 本端端口（主机字节序）；配合 bind(0) 可查出系统分配的端口
; ------------------------------------------------------------
_socket_local_port:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi

    mov dword [ws_name_len], 16

    push ws_name_len
    push ws_sa
    push dword [ebp+8]
    call _getsockname@12

    test eax, eax
    jnz .err

    movzx eax, word [ws_sa + 2]
    xchg al, ah
    jmp .done

.err:
    call _ws_fail
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

; ------------------------------------------------------------
; int _socket_send_str(int fd, str data)
; str 按值压栈 8 字节：[ebp+12]=数据指针 [ebp+16]=长度
; ------------------------------------------------------------
_socket_send_str:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi

    call _ws_ensure
    test eax, eax
    jnz .err

    push 0
    push dword [ebp+16]
    push dword [ebp+12]
    push dword [ebp+8]
    call _send@16

    cmp eax, 0xFFFFFFFF
    je .err
    jmp .done

.err:
    call _ws_fail
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

; ------------------------------------------------------------
; str _socket_recv_str(void* hidden_ret_buf, int fd, int len)
; [ebp+8]=hidden_ret_buf [ebp+12]=fd [ebp+16]=len
; ------------------------------------------------------------
_socket_recv_str:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi

    call _ws_ensure
    test eax, eax
    jnz .null_empty

    mov dword [ws_last_error], 0

    mov ebx, [ebp+8]            ; hidden_ret_buf
    mov dword [ebx], 0
    mov dword [ebx + 4], 0

    mov eax, [ebp+16]
    add eax, 1
    push eax
    call _ws_alloc
    add esp, 4
    test eax, eax
    jz .empty_fat
    mov esi, eax
    mov [ebx], esi

    push 0
    push dword [ebp+16]
    push esi
    push dword [ebp+12]
    call _recv@16

    test eax, eax
    js .err_recv
    mov [ebx + 4], eax
    mov byte [esi + eax], 0
    mov eax, ebx
    jmp .done

.err_recv:
    call _ws_fail
    mov byte [esi], 0
    mov eax, ebx
    jmp .done

.empty_fat:
    mov eax, ebx
    jmp .done

.null_empty:
    mov ebx, [ebp+8]
    mov dword [ebx], _ws_empty_str
    mov dword [ebx + 4], 0
    mov eax, ebx
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

; ------------------------------------------------------------
; int _socket_send_all(int fd, string data, int len)
; 循环 send 直到全部发出；返回累计字节数，失败返回 -1
; ------------------------------------------------------------
_socket_send_all:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi

    call _ws_ensure
    test eax, eax
    jnz .err

    mov esi, [ebp+12]           ; data
    xor edi, edi                ; total = 0

    cmp dword [ebp+16], 0
    je .all_done

.loop:
    mov ecx, [ebp+16]
    sub ecx, edi                ; remaining
    jz .all_done

    push 0
    push ecx
    push esi
    push dword [ebp+8]
    call _send@16

    cmp eax, 0xFFFFFFFF
    je .err
    test eax, eax
    jle .err

    add esi, eax
    add edi, eax
    jmp .loop

.all_done:
    mov eax, edi
    jmp .done

.err:
    call _ws_fail
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

; ------------------------------------------------------------
; string _socket_recv_line(int fd, int max_len)
; 逐字节读到 '\n' 或满 max_len 或对端关闭。
; 去除行尾的 '\r' 和 '\n'。失败返回空串（配合 lastError）。
; ------------------------------------------------------------
_socket_recv_line:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi

    call _ws_ensure
    test eax, eax
    jnz .null

    mov dword [ws_last_error], 0

    mov ecx, [ebp+12]           ; max_len
    test ecx, ecx
    jle .null

    inc ecx                     ; +1 NUL
    push ecx
    call _ws_alloc
    add esp, 4
    test eax, eax
    jz .null
    mov ebx, eax                ; 缓冲区
    xor esi, esi                ; idx

.read_loop:
    cmp esi, [ebp+12]
    jae .done_read

    push 0
    push 1
    push ws_tmp_byte
    push dword [ebp+8]
    call _recv@16

    cmp eax, 0xFFFFFFFF
    je .err_recv
    test eax, eax
    jz .eof
    js .err_recv

    mov al, [ws_tmp_byte]
    mov [ebx + esi], al
    inc esi
    cmp al, 10                  ; '\n'
    je .done_read
    jmp .read_loop

.eof:
    test esi, esi
    jnz .done_read
    mov byte [ebx], 0
    mov eax, ebx
    jmp .done                   ; 对端正常关闭，返回空串

.err_recv:
    call _ws_fail
    mov byte [ebx], 0
    mov eax, ebx
    jmp .done

.done_read:
    mov byte [ebx + esi], 0
    mov edi, esi
.strip:
    cmp edi, 0
    je .ret_buf
    movzx eax, byte [ebx + edi - 1]
    cmp al, 10
    je .cut
    cmp al, 13
    jne .ret_buf
.cut:
    dec edi
    mov byte [ebx + edi], 0
    jmp .strip
.ret_buf:
    mov eax, ebx
    jmp .done

.null:
    mov eax, _ws_empty_str
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

; ------------------------------------------------------------
; string _socket_recv_exact(int fd, int len)
; 循环 recv 直到读满 len 字节或对端提前关闭。
; 提前关闭返回较短字符串（用 _str_length 判断）。
; ------------------------------------------------------------
_socket_recv_exact:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi

    call _ws_ensure
    test eax, eax
    jnz .null

    mov dword [ws_last_error], 0

    mov ecx, [ebp+12]
    inc ecx
    push ecx
    call _ws_alloc
    add esp, 4
    test eax, eax
    jz .null
    mov ebx, eax
    xor esi, esi                ; total

.loop:
    mov ecx, [ebp+12]
    cmp esi, ecx
    jae .done_read
    sub ecx, esi

    lea eax, [ebx + esi]
    push 0
    push ecx
    push eax
    push dword [ebp+8]
    call _recv@16

    test eax, eax
    js .err_recv
    jz .done_read               ; 提前关闭
    add esi, eax
    jmp .loop

.done_read:
    mov byte [ebx + esi], 0
    mov eax, ebx
    jmp .done

.err_recv:
    call _ws_fail
    mov byte [ebx], 0
    mov eax, ebx
    jmp .done

.null:
    mov eax, _ws_empty_str
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

; ------------------------------------------------------------
; int _socket_select_read(int fd, int ms)
; 返回 1=可读，0=超时，-1=错误
; ------------------------------------------------------------
_socket_select_read:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi

    mov dword [ws_fdset], 1
    mov eax, [ebp+8]
    mov [ws_fdset + 4], eax

    mov eax, [ebp+12]
    xor edx, edx
    mov ecx, 1000
    div ecx
    mov [ws_timeval], eax        ; tv_sec
    mov eax, edx
    mov ecx, 1000
    mul ecx
    mov [ws_timeval + 4], eax    ; tv_usec

    push ws_timeval
    push 0                       ; exceptfds
    push 0                       ; writefds
    push ws_fdset                ; readfds
    push 0                       ; nfds（Winsock 忽略）
    call _select@20

    cmp eax, 0xFFFFFFFF
    je .err
    test eax, eax
    jz .timeout
    mov eax, 1
    jmp .done
.timeout:
    xor eax, eax
    jmp .done
.err:
    call _ws_fail
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

; ------------------------------------------------------------
; int _socket_select_write(int fd, int ms)
; 返回 1=可写，0=超时，-1=错误；用于非阻塞 connect 完成检测
; ------------------------------------------------------------
_socket_select_write:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi

    mov dword [ws_fdset], 1
    mov eax, [ebp+8]
    mov [ws_fdset + 4], eax

    mov eax, [ebp+12]
    xor edx, edx
    mov ecx, 1000
    div ecx
    mov [ws_timeval], eax
    mov eax, edx
    mov ecx, 1000
    mul ecx
    mov [ws_timeval + 4], eax

    push ws_timeval
    push 0                       ; exceptfds
    push ws_fdset                ; writefds
    push 0                       ; readfds
    push 0
    call _select@20

    cmp eax, 0xFFFFFFFF
    je .err
    test eax, eax
    jz .timeout
    mov eax, 1
    jmp .done
.timeout:
    xor eax, eax
    jmp .done
.err:
    call _ws_fail
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

; ------------------------------------------------------------
; int _socket_bind_addr(int fd, string host, int port)
; ------------------------------------------------------------
_socket_bind_addr:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi

    call _ws_ensure
    test eax, eax
    jnz .err

    push dword [ebp+12]
    call _inet_addr@4
    cmp eax, 0xFFFFFFFF
    je .err
    mov esi, eax                ; addr（网络字节序）

    push dword [ebp+16]
    call _htons@4               ; ax = port（网络字节序）

    mov word  [ws_sa], 2        ; AF_INET
    mov word  [ws_sa + 2], ax
    mov dword [ws_sa + 4], esi
    mov dword [ws_sa + 8], 0
    mov dword [ws_sa + 12], 0

    push 16
    push ws_sa
    push dword [ebp+8]
    call _bind@12

    test eax, eax
    jnz .err

    xor eax, eax
    jmp .done

.err:
    call _ws_fail
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

; ------------------------------------------------------------
; 字节序转换（bswap，无系统调用）
; ------------------------------------------------------------
_socket_htonl:
    mov eax, [esp + 4]
    bswap eax
    ret

_socket_ntohl:
    jmp _socket_htonl

_socket_htons:
    movzx eax, word [esp + 4]
    xchg al, ah
    ret

_socket_ntohs:
    jmp _socket_htons

; ------------------------------------------------------------
; int _socket_inet_addr(string ip)
; 点分十进制 → 32 位网络字节序；非法返回 0xFFFFFFFF（-1）
; ------------------------------------------------------------
_socket_inet_addr:
    mov eax, [esp + 4]
    push eax
    call _inet_addr@4
    ret

; ------------------------------------------------------------
; string _socket_inet_ntoa(int addr)
; 32 位网络字节序 → 点分十进制；静态缓冲区，返回值须立即使用
; ------------------------------------------------------------
_socket_inet_ntoa:
    mov eax, [esp + 4]
    push eax
    call _inet_ntoa@4
    ret

; ------------------------------------------------------------
; string _socket_error_string(int code)
; ------------------------------------------------------------
_socket_error_string:
    mov eax, [esp + 4]
    mov ecx, _ws_err_table
.loop:
    cmp dword [ecx], -1
    je .unknown
    cmp eax, [ecx]
    je .found
    add ecx, 8
    jmp .loop
.found:
    mov eax, [ecx + 4]
    ret
.unknown:
    mov eax, _ws_err_unknown
    ret

section .data
    dd 0
    _ws_empty_str       db 0
    _ws_err_unknown     db "unknown error",0

    _err_ok            db "success",0
    _err_10013         db "permission denied",0
    _err_10022         db "invalid argument",0
    _err_10035         db "operation would block",0
    _err_10036         db "operation in progress",0
    _err_10047         db "address family not supported",0
    _err_10048         db "address already in use",0
    _err_10049         db "address not available",0
    _err_10051         db "network unreachable",0
    _err_10053         db "connection aborted",0
    _err_10054         db "connection reset by peer",0
    _err_10055         db "no buffer space available",0
    _err_10056         db "socket already connected",0
    _err_10057         db "socket not connected",0
    _err_10060         db "connection timed out",0
    _err_10061         db "connection refused",0
    _err_10064         db "host is down",0
    _err_10065         db "no route to host",0
    _err_10093         db "winsock not initialised",0
    _err_11001         db "host not found",0
    _err_11004         db "no data record of requested type",0

    _ws_err_table:
        dd 0,      _err_ok
        dd 10013,  _err_10013
        dd 10022,  _err_10022
        dd 10035,  _err_10035
        dd 10036,  _err_10036
        dd 10047,  _err_10047
        dd 10048,  _err_10048
        dd 10049,  _err_10049
        dd 10051,  _err_10051
        dd 10053,  _err_10053
        dd 10054,  _err_10054
        dd 10055,  _err_10055
        dd 10056,  _err_10056
        dd 10057,  _err_10057
        dd 10060,  _err_10060
        dd 10061,  _err_10061
        dd 10064,  _err_10064
        dd 10065,  _err_10065
        dd 10093,  _err_10093
        dd 11001,  _err_11001
        dd 11004,  _err_11004
        dd -1,     0

section .bss
    ws_started        resd 1
    ws_last_error     resd 1
    ws_last_from_addr resd 1
    ws_last_from_port resd 1
    ws_sa             resb 16
    ws_from_sa        resb 16
    ws_from_len       resd 1
    ws_ip_buf         resb 16
    ws_opt_int        resd 1
    ws_opt_len        resd 1
    ws_name_len       resd 1
    ws_fdset          resb 260     ; fd_count + fd_array[64]
    ws_timeval        resb 8
    ws_tmp_byte       resb 1
    wsadata           resb 400