; ============================================================
; runtime_music.asm — YXG Music 类运行时
;
; 实例方法调用约定: [ebp+8] = this 对象指针
;                   [this+0] = class_id, [this+4] = vtable
;                   [this+8] = handle 字段
; 内部 C API (cdecl): audio_xxx (music_loader.obj)
; ============================================================

extern _audio_load
extern _audio_play
extern _audio_pause
extern _audio_resume
extern _audio_stop
extern _audio_is_playing
extern _audio_set_volume
extern _audio_get_position
extern _audio_unload

section .text

; int Music_load(string path)
global Music_load_string
Music_load_string:
    push ebp
    mov ebp, esp
    mov ecx, [ebp+8]            ; this
    test ecx, ecx
    jz  .fail
    push dword [ebp+12]         ; path
    call _audio_load
    add esp, 4
    test eax, eax
    jz  .fail
    mov edx, [ebp+8]
    mov [edx+8], eax            ; this->handle = 句柄
    mov eax, 1
    pop ebp
    ret
.fail:
    xor eax, eax
    pop ebp
    ret

; void Music_play(int loop)
global Music_play_int
Music_play_int:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov eax, [eax+8]            ; handle
    test eax, eax
    jz  .ret
    push dword [ebp+12]         ; loop
    push eax
    call _audio_play
    add esp, 8
.ret:
    pop ebp
    ret

; void Music_pause()
global Music_pause
Music_pause:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov eax, [eax+8]
    test eax, eax
    jz  .ret
    push eax
    call _audio_pause
    add esp, 4
.ret:
    pop ebp
    ret

; void Music_resume()
global Music_resume
Music_resume:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov eax, [eax+8]
    test eax, eax
    jz  .ret
    push eax
    call _audio_resume
    add esp, 4
.ret:
    pop ebp
    ret

; void Music_stop()
global Music_stop
Music_stop:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov eax, [eax+8]
    test eax, eax
    jz  .ret
    push eax
    call _audio_stop
    add esp, 4
.ret:
    pop ebp
    ret

; int Music_is_playing()
global Music_is_playing
Music_is_playing:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    test eax, eax
    jz  .zero
    mov eax, [eax+8]
    test eax, eax
    jz  .zero
    push eax
    call _audio_is_playing
    add esp, 4
    pop ebp
    ret
.zero:
    xor eax, eax
    pop ebp
    ret

; void Music_set_volume(int vol)
global Music_set_volume_int
Music_set_volume_int:
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
    call _audio_set_volume
    add esp, 8
.ret:
    pop ebp
    ret

; int Music_get_position()
global Music_get_position
Music_get_position:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    test eax, eax
    jz  .zero
    mov eax, [eax+8]
    test eax, eax
    jz  .zero
    push eax
    call _audio_get_position
    add esp, 4
    pop ebp
    ret
.zero:
    xor eax, eax
    pop ebp
    ret

; void Music_unload()
global Music_unload
Music_unload:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov eax, [eax+8]
    test eax, eax
    jz  .ret
    push eax
    call _audio_unload
    add esp, 4
    mov eax, [ebp+8]            ; 重新取 this
    mov dword [eax+8], 0        ; this->handle = 0
.ret:
    pop ebp
    ret