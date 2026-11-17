; ============================================================
; runtime_video.asm — YXG Video 类运行时
;
; 实例方法约定: [ebp+8] = this
;               [this+0]=class_id, [this+4]=vtable, [this+8]=handle
; ============================================================

extern _video_open
extern _video_close
extern _video_decode_frame
extern _video_get_width
extern _video_get_height
extern _video_get_fps
extern _video_get_duration
extern _video_get_time
extern _video_seek
extern _video_rewind
extern _video_set_loop
extern _video_blit_to
extern get_canvas_offset
extern canvas_table
extern _video_last_error
extern _video_dbg_subtype
extern _video_last_hresult
extern _video_play
extern _video_stop

section .text

; int Video_load(string path)
global Video_load_string
Video_load_string:
    push ebp
    mov ebp, esp
    mov ecx, [ebp+8]
    test ecx, ecx
    jz  .fail
    push dword [ebp+12]
    call _video_open
    add esp, 4
    test eax, eax
    jz  .fail
    mov edx, [ebp+8]
    mov [edx+8], eax            ; this->handle
    mov eax, 1
    pop ebp
    ret
.fail:
    xor eax, eax
    pop ebp
    ret

; void Video_unload()
global Video_unload
Video_unload:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov eax, [eax+8]
    test eax, eax
    jz  .ret
    push eax
    call _video_close
    add esp, 4
    mov eax, [ebp+8]
    mov dword [eax+8], 0
.ret:
    pop ebp
    ret

; int Video_next()  1=新帧 0=结束 -1=错误
global Video_next
Video_next:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    test eax, eax
    jz  .neg
    mov eax, [eax+8]
    test eax, eax
    jz  .neg
    push eax
    call _video_decode_frame
    add esp, 4
    pop ebp
    ret
.neg:
    mov eax, -1
    pop ebp
    ret

; int Video_width()
global Video_width
Video_width:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    test eax, eax
    jz  .zero
    mov eax, [eax+8]
    test eax, eax
    jz  .zero
    push eax
    call _video_get_width
    add esp, 4
    pop ebp
    ret
.zero:
    xor eax, eax
    pop ebp
    ret

; int Video_height()
global Video_height
Video_height:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    test eax, eax
    jz  .zero
    mov eax, [eax+8]
    test eax, eax
    jz  .zero
    push eax
    call _video_get_height
    add esp, 4
    pop ebp
    ret
.zero:
    xor eax, eax
    pop ebp
    ret

; float Video_fps()
global Video_fps
Video_fps:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    test eax, eax
    jz  .fzero
    mov eax, [eax+8]
    test eax, eax
    jz  .fzero
    push eax
    call _video_get_fps
    add esp, 4
    pop ebp
    ret
.fzero:
    fldz
    pop ebp
    ret

; float Video_duration()
global Video_duration
Video_duration:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    test eax, eax
    jz  .fzero
    mov eax, [eax+8]
    test eax, eax
    jz  .fzero
    push eax
    call _video_get_duration
    add esp, 4
    pop ebp
    ret
.fzero:
    fldz
    pop ebp
    ret

; float Video_time()
global Video_time
Video_time:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    test eax, eax
    jz  .fzero
    mov eax, [eax+8]
    test eax, eax
    jz  .fzero
    push eax
    call _video_get_time
    add esp, 4
    pop ebp
    ret
.fzero:
    fldz
    pop ebp
    ret

; void Video_seek(float sec)
global Video_seek_float
Video_seek_float:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov eax, [eax+8]
    test eax, eax
    jz  .ret
    push dword [ebp+12]
    push eax
    call _video_seek
    add esp, 8
.ret:
    pop ebp
    ret

; void Video_rewind()
global Video_rewind
Video_rewind:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov eax, [eax+8]
    test eax, eax
    jz  .ret
    push eax
    call _video_rewind
    add esp, 4
.ret:
    pop ebp
    ret

; void Video_set_loop(int loop)
global Video_set_loop_int
Video_set_loop_int:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov eax, [eax+8]
    test eax, eax
    jz  .ret
    push dword [ebp+12]
    push eax
    call _video_set_loop
    add esp, 8
.ret:
    pop ebp
    ret

; int Video_blit_to(Canvas c)  拷贝当前帧到画布(尺寸须一致), 1=成功
global Video_blit_to_identifier
Video_blit_to_identifier:
    push ebp
    mov ebp, esp
    push esi
    push edi
    mov esi, [ebp+8]
    test esi, esi
    jz  .zero
    mov esi, [esi+8]            ; this->handle
    test esi, esi
    jz  .zero
    mov edi, [ebp+12]
    test edi, edi
    jz  .zero
    mov eax, [edi+8]            ; Canvas 的 id 也在 +8
    call get_canvas_offset
    cmp eax, -1
    je  .zero
    lea edi, [canvas_table]
    add edi, eax
    cmp dword [edi+20], 0
    je  .zero
    push dword [edi+4]
    push dword [edi+0]
    push dword [edi+24]
    push esi
    call _video_blit_to
    add esp, 16
    pop edi
    pop esi
    pop ebp
    ret
.zero:
    xor eax, eax
    pop edi
    pop esi
    pop ebp
    ret

; int Video_last_error()
global Video_last_error
Video_last_error:
    call _video_last_error
    ret

; int Video_dbg_subtype()
global Video_dbg_subtype
Video_dbg_subtype:
    call _video_dbg_subtype
    ret

; int Video_last_hresult()
global Video_last_hresult
Video_last_hresult:
    call _video_last_hresult
    ret

; void Video_play(int loop)
global Video_play_int
Video_play_int:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov eax, [eax+8]
    test eax, eax
    jz  .ret
    push dword [ebp+12]
    push eax
    call _video_play
    add esp, 8
.ret:
    pop ebp
    ret

; void Video_stop()
global Video_stop
Video_stop:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov eax, [eax+8]
    test eax, eax
    jz  .ret
    push eax
    call _video_stop
    add esp, 4
.ret:
    pop ebp
    ret