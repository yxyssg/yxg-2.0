; ============================================================
; runtime_window.asm ‚Ä? YXG Windows ÂõæÂΩ¢Â∫ìËøêË°åÊó∂Ôºà‰øÆÂ§çÁâàÔº?
;
; ÂÆû‰æãÊñπÊ≥ïÁ∫¶ÂÆö:  [ebp+8] = this
;   [this+0] = class_id  [this+4] = vtable  [this+8] = Á¨¨‰∏Ä‰∏™Â≠óÊÆ?
;   Window Á±ªÁöÑÁ¨¨‰∏Ä‰∏™Â≠óÊÆµÊòØ hwndÔºõCanvas Êò? canvas_id
;   Êéß‰ª∂ÔºàButton/TextBox/Label/‚Ä¶ÔºâÁ¨¨‰∏Ä‰∏™Â≠óÊÆµÊòØ hwnd
; ============================================================

extern _RegisterClassA@4
extern _CreateWindowExA@48
extern _DefWindowProcA@16
extern _GetMessageA@16
extern _PeekMessageA@20
extern _TranslateMessage@4
extern _DispatchMessageA@4
extern _PostQuitMessage@4
extern _DestroyWindow@4
extern _ShowWindow@8
extern _UpdateWindow@4
extern _SetWindowTextA@8
extern _SetWindowLongA@12
extern _GetWindowLongA@8
extern _GetClientRect@8
extern _GetDC@4
extern _ReleaseDC@8
extern _BeginPaint@8
extern _EndPaint@8
extern _GetModuleHandleA@4
extern _LoadCursorA@8
extern _CreateDIBSection@24
extern _CreateCompatibleDC@4
extern _SelectObject@8
extern _DeleteObject@4
extern _DeleteDC@4
extern _BitBlt@36
extern _FillRect@12
extern _FrameRect@12
extern _CreateSolidBrush@4
extern _CreatePen@12
extern _MoveToEx@16
extern _LineTo@12
extern _SetPixelV@16
extern _TextOutA@20
extern _SetTextColor@8
extern _SetBkMode@8
extern _CreateFontA@56
extern _GetStockObject@4
extern _GetAsyncKeyState@4
extern _GetCursorPos@4
extern _ScreenToClient@8
extern _GetTickCount@0
extern _MessageBoxA@16
extern _SetWindowPos@28
extern _lstrlenA@4
extern _Sleep@4
extern _CreateCompatibleBitmap@12
extern _Ellipse@20
extern _Polygon@12
extern _RoundRect@28
extern _GetTextExtentPoint32A@16
extern _GetPixel@12
extern _SetCursor@4
extern _GetSystemMetrics@4
extern _GetWindowRect@8
extern _SetTimer@16
extern _KillTimer@8
extern _IsIconic@4
extern _IsZoomed@4
extern _IsWindowVisible@4
extern _IsWindow@4
extern _SetFocus@4
extern _GetFocus@0
extern _GetOpenFileNameA@4
extern _GetSaveFileNameA@4
extern _MultiByteToWideChar@24
extern _TextOutW@20
extern _GetTextExtentPoint32W@16
extern _CreateFileA@28
extern _ReadFile@20
extern _CloseHandle@4
extern _SetFilePointer@16
extern _StretchBlt@44
extern _gc_alloc
extern _SetWindowTextW@8
extern _GetWindowTextW@12
extern _GetMenuItemCount@4
extern _GetSubMenu@8
extern _SendMessageA@16
extern _SendMessageW@16
extern _WideCharToMultiByte@32
extern _InvalidateRect@12
extern _CreateMenu@0
extern _CreatePopupMenu@0
extern _AppendMenuW@16
extern _SetMenu@8
extern _DrawMenuBar@4
extern _DestroyMenu@4
extern _InitCommonControlsEx@4
extern _MoveWindow@24
extern _RedrawWindow@16
extern _TrackPopupMenu@28
extern _ClientToScreen@8
extern _SetWindowTheme@12
extern _EnableWindow@8
extern _SetForegroundWindow@4
extern _GetActiveWindow@0
extern _GetWindowTextA@12
extern _MessageBoxW@16
extern _IsDialogMessageA@8
extern _GetDlgItem@8
extern _WindowFromPoint@8
extern _CallWindowProcA@20
extern _CreateRoundRectRgn@24
extern _SelectClipRgn@8
extern _ValidateRect@8
extern _image_load
extern _image_free

WM_DESTROY       equ 0x0002
WM_CLOSE         equ 0x0010
WM_PAINT         equ 0x000F
WM_ERASEBKGND    equ 0x0014
WM_QUIT          equ 0x0012
WM_SIZE          equ 0x0005
WM_MOUSEWHEEL    equ 0x020A
WM_LBUTTONDOWN   equ 0x0201
WM_LBUTTONUP     equ 0x0202
WM_RBUTTONDOWN   equ 0x0204
WM_RBUTTONUP     equ 0x0205
GWL_USERDATA     equ -21
WS_OVERLAPPEDWINDOW equ 0x00CF0000
CW_USEDEFAULT    equ 0x80000000
SW_SHOW          equ 5
SW_HIDE          equ 0
DIB_RGB_COLORS   equ 0
SRCCOPY          equ 0x00CC0020
TRANSPARENT      equ 1
PS_SOLID         equ 0
PM_REMOVE        equ 1
IDC_ARROW        equ 32512
WHITE_BRUSH      equ 0
NULL_PEN         equ 8
NULL_BRUSH       equ 5
COLOR_WINDOW     equ 5
VK_LBUTTON       equ 0x01
VK_RBUTTON       equ 0x02
VK_MBUTTON       equ 0x04
WM_CHAR          equ 0x0102
WM_SETCURSOR     equ 0x0020
HTCLIENT         equ 1
IDC_IBEAM        equ 32513
IDC_WAIT         equ 32514
IDC_CROSS        equ 32515
IDC_SIZEWE       equ 32644
IDC_SIZENS       equ 32645
IDC_SIZEALL      equ 32646
IDC_NO           equ 32648
IDC_HAND         equ 32649
MAX_WINDOWS      equ 64
ENTRY_SIZE       equ 160
MAX_CANVAS       equ 256
CANVAS_ENTRY     equ 32
BLACK_BRUSH      equ 4
WM_TIMER         equ 0x0113
SW_MINIMIZE      equ 6
SW_MAXIMIZE      equ 3
SW_RESTORE       equ 9
HWND_TOPMOST     equ -1
HWND_NOTOPMOST   equ -2
OFN_PATHMUSTEXIST    equ 0x00000800
OFN_FILEMUSTEXIST    equ 0x00001000
OFN_OVERWRITEPROMPT  equ 0x00000002
OFN_HIDEREADONLY     equ 0x00000004
WS_CHILD       equ 0x40000000
WS_VISIBLE     equ 0x10000000
WS_POPUP       equ 0x80000000
HWND_TOP       equ 0
WS_BORDER      equ 0x00800000
WS_TABSTOP     equ 0x00010000
BS_PUSHBUTTON  equ 0x00000000
ES_AUTOHSCROLL equ 0x0080
SS_LEFT        equ 0x00000000
WM_COMMAND     equ 0x0111
WM_GETTEXTLENGTH equ 0x000E
BN_CLICKED     equ 0
MAX_CONTROLS   equ 64
CONTROL_ENTRY  equ 20
BS_AUTOCHECKBOX    equ 0x0003
BS_AUTORADIOBUTTON equ 0x0009
BM_GETCHECK        equ 0x00F0
BM_SETCHECK        equ 0x00F1
BST_UNCHECKED      equ 0
BST_CHECKED        equ 1
WS_GROUP           equ 0x00020000
WS_VSCROLL         equ 0x00200000
CBS_DROPDOWNLIST   equ 0x0003
CB_ADDSTRING       equ 0x0143
CB_RESETCONTENT    equ 0x014B
CB_SETCURSEL       equ 0x014E
CB_GETCURSEL       equ 0x0147
CB_GETLBTEXT       equ 0x0148
CB_GETLBTEXTLEN    equ 0x0149
CBN_SELCHANGE      equ 1
LBS_NOTIFY         equ 0x0001
LB_ADDSTRING       equ 0x0180
LB_RESETCONTENT    equ 0x0184
LB_SETCURSEL       equ 0x0186
LB_GETCURSEL       equ 0x0188
LB_GETTEXT         equ 0x0189
LB_GETTEXTLEN      equ 0x018A
LBN_SELCHANGE      equ 1
BS_GROUPBOX        equ 0x0007
MAX_PANELS         equ 64
PANEL_ENTRY        equ 20
MF_STRING          equ 0
MF_OWNERDRAW       equ 0x00000100
MF_SEPARATOR       equ 0x00000800
MF_POPUP           equ 0x00000010
ODT_MENU           equ 1
MAX_MENUS          equ 16
MENU_ENTRY         equ 36
ICC_BARCLASSES     equ 0x00000004
TBSTYLE_FLAT       equ 0x0800
TBSTYLE_LIST       equ 0x1000
TBSTYLE_BUTTON     equ 0x0000
TBSTATE_ENABLED    equ 0x0004
TB_BUTTONSTRUCTSIZE equ 0x041E
TB_ADDBUTTONSW     equ 0x0444
I_IMAGENONE        equ -2
MAX_TOOLBARS       equ 8
TB_ENTRY           equ 24
WS_CLIPCHILDREN    equ 0x02000000
WM_CANCELMODE      equ 0x001F
TB_SETSTATE        equ 0x0411
MAX_STATUSBARS     equ 8
SB_ENTRY           equ 16
SB_SETTEXTW        equ 0x040B
SB_GETTEXTLENGTHW  equ 0x040C
SB_GETTEXTW        equ 0x040D
TPM_RIGHTBUTTON    equ 0x0002
TPM_RETURNCMD      equ 0x0100
ICC_UPDOWN_CLASS   equ 0x00000010
ICC_PROGRESS_CLASS equ 0x00000020
MAX_PROGRESSBARS   equ 16
PB_ENTRY           equ 16
MAX_SPINBOXES      equ 32
SPIN_ENTRY         equ 16
MAX_SLIDERS        equ 16
SLIDER_ENTRY       equ 16
PBM_SETRANGE32     equ 0x0406
PBM_SETPOS         equ 0x0402
PBM_GETPOS         equ 0x0408
PBM_SETSTEP        equ 0x0404
PBM_STEPIT         equ 0x0405
PBM_SETBARCOLOR    equ 0x0409
PBM_SETMARQUEE     equ 0x040A
PBS_MARQUEE        equ 0x0008
UDS_ALIGNRIGHT     equ 0x0004
UDS_ARROWKEYS      equ 0x0020
UDM_SETBUDDY       equ 0x0469
UDM_SETRANGE32     equ 0x046F
UDM_SETACCEL       equ 0x046B
UDM_SETPOS32       equ 0x0471
UDM_GETPOS32       equ 0x0472
TBS_AUTOTICKS      equ 0x0001
TBM_GETPOS         equ 0x0400
TBM_SETPOS         equ 0x0405
TBM_SETRANGE       equ 0x0406
TBM_SETPAGESIZE    equ 0x0415
WM_HSCROLL         equ 0x0114
WM_VSCROLL         equ 0x0115
UDS_SETBUDDYINT    equ 0x0002
WS_CAPTION         equ 0x00C00000
WS_SYSMENU         equ 0x00080000
BS_DEFPUSHBUTTON   equ 0x0001
MB_OK              equ 0x0000
MB_OKCANCEL        equ 0x0001
MB_ABORTRETRYIGNORE equ 0x0002
MB_YESNOCANCEL     equ 0x0003
MB_YESNO           equ 0x0004
MB_RETRYCANCEL     equ 0x0005
MB_ICONERROR       equ 0x0010
MB_ICONQUESTION    equ 0x0020
MB_ICONWARNING     equ 0x0030
MB_ICONINFORMATION equ 0x0040
WM_ACTIVATE        equ 0x0006
WM_SETFOCUS        equ 0x0007
DM_GETDEFID        equ 0x0400
DC_HASDEFID        equ 0x534B
WM_GETTEXT         equ 0x000D
WM_DRAWITEM        equ 0x002B
WM_MEASUREITEM     equ 0x002C
WM_NCPAINT         equ 0x0085
CBS_OWNERDRAWFIXED equ 0x0010
LBS_OWNERDRAWFIXED equ 0x0010
CBS_HASSTRINGS     equ 0x0200
LBS_HASSTRINGS     equ 0x0040
EN_CHANGE          equ 0x0300
BS_OWNERDRAW       equ 0x0000000B
SS_OWNERDRAW       equ 0x0000000D
GWL_STYLE          equ -16
WS_MAXIMIZEBOX     equ 0x00010000
SWP_NOMOVE         equ 0x0002
SWP_NOSIZE         equ 0x0001
SWP_NOZORDER       equ 0x0004
SWP_FRAMECHANGED   equ 0x0020
SWP_SHOWWINDOW     equ 0x0040
WS_THICKFRAME      equ 0x00040000
GENERIC_READ       equ 0x80000000
FILE_SHARE_READ    equ 1
OPEN_EXISTING      equ 3
FILE_ATTRIBUTE_NORMAL equ 0x80
INVALID_HANDLE_VALUE equ -1
FILE_BEGIN         equ 0
GWL_WNDPROC        equ -4
WM_NCDESTROY       equ 0x0082
MAX_SUBCLASS       equ 64
SUBCLASS_ENTRY     equ 16

section .data
    class_registered dd 0
    window_class_name db "YXGWindowClass", 0
    font_size dd 18
    ofn_filter  db "All Files (*.*)",0,"*.*",0,0
    ofn_def_ext db "txt",0
    btn_class_name    db "BUTTON", 0
    edit_class_name   db "EDIT", 0
    static_class_name db "STATIC", 0
    empty_title       db 0, 0
    combo_class_name  db "COMBOBOX", 0
    listbox_class_name db "LISTBOX", 0
    panel_class_name   db "YXGPanelClass", 0
    toolbar_class_name db "ToolbarWindow32", 0
    empty_wide         dw 0
    cmd_id_base        dd 100
    statusbar_class_name db "msctls_statusbar32", 0
    progress_class_name db "msctls_progress32", 0
    updown_class_name   db "msctls_updown32", 0
    slider_class_name   db "msctls_trackbar32", 0
    inputbox_class_name db "YXGInputBoxClass", 0
    ok_button_text      db "OK", 0
    cancel_button_text  db "Cancel", 0

section .bss
    window_table   resd MAX_WINDOWS * (ENTRY_SIZE / 4)
    msg_struct     resd 7
    pt_struct      resd 2
    rect_struct    resd 4
    paint_struct   resd 16
    bmi_buffer     resd 16
    dib_bits       resd 1
    font_handle    resd 1
    prev_keys      resb 256
    key_pressed_latch resb 256
    prev_mouse     resd 3
    size_struct    resd 2
    poly_points    resd 6
    char_buffer    resb 2
    window_rect    resd 4
    global canvas_table
    canvas_table   resd MAX_CANVAS * (CANVAS_ENTRY / 4)
    ofn_struct    resd 19
    ofn_buffer    resb 260
    ofn_filetitle resb 260
    utf16_buffer  resb 1024
    button_table resd MAX_CONTROLS * (CONTROL_ENTRY / 4)
    edit_table   resd MAX_CONTROLS * (CONTROL_ENTRY / 4)
    label_table  resd MAX_CONTROLS * (CONTROL_ENTRY / 4)
    checkbox_table    resd MAX_CONTROLS * (CONTROL_ENTRY / 4)
    radiobutton_table resd MAX_CONTROLS * (CONTROL_ENTRY / 4)
    combo_table    resd MAX_CONTROLS * (CONTROL_ENTRY / 4)
    listbox_table  resd MAX_CONTROLS * (CONTROL_ENTRY / 4)
    layout_active   resd 1
    layout_origin_x resd 1
    layout_origin_y resd 1
    layout_gap      resd 1
    layout_max_w    resd 1
    layout_cur_x    resd 1
    layout_cur_y    resd 1
    layout_line_h   resd 1
    panel_table    resd MAX_PANELS * (PANEL_ENTRY / 4)
    panel_class_registered resd 1
    menu_table     resd MAX_MENUS * (MENU_ENTRY / 4)
    toolbar_table  resd MAX_TOOLBARS * (TB_ENTRY / 4)
    icc_struct     resd 2
    toolbar_comctl_init resd 1
    tb_btn_struct  resd 5
    tb_id_save     resd 1
    tb_iString_save resd 1
    tb_string_pool resb 2048
    tb_pool_off    resd 1
    new_dc_save       resd 1
    new_bitmap_save   resd 1
    new_oldbitmap_save resd 1
    old_width_save    resd 1
    old_height_save   resd 1
    statusbar_table resd MAX_STATUSBARS * (SB_ENTRY / 4)
    sb_width_save   resd 1
    sb_height_save  resd 1
    progress_table  resd MAX_PROGRESSBARS * (PB_ENTRY / 4)
    spinbox_table   resd MAX_SPINBOXES * (SPIN_ENTRY / 4)
    slider_table    resd MAX_SLIDERS * (SLIDER_ENTRY / 4)
    comctl_ext_init resd 1
    udm_err_flag    resd 1
    udaccel_struct  resd 2
    inputbox_class_registered resd 1
    inputbox_edit_hwnd        resd 1
    inputbox_done             resd 1
    inputbox_prompt           resd 1
    inputbox_title            resd 1
    inputbox_textbuf          resb 512
    popup_text_utf16          resb 1024
    popup_title_utf16         resb 1024
    win_title_utf16           resb 1024
    paint_rect      resd 4
    paint_text_utf16 resb 1024
    button_cb_table       resd MAX_CONTROLS
    checkbox_cb_table     resd MAX_CONTROLS
    radiobutton_cb_table  resd MAX_CONTROLS
    label_cb_table        resd MAX_CONTROLS
    combo_cb_table        resd MAX_CONTROLS
    listbox_cb_table      resd MAX_CONTROLS
    img_width       resd 1
    img_height      resd 1
    img_bpp         resd 1
    img_pixels      resd 1
    img_y           resd 1
    subclass_table  resd MAX_SUBCLASS * (SUBCLASS_ENTRY / 4)

section .text

; ============================================================
; ÂÜÖÈÉ®ËæÖÂä©ÔºàÊó† thisÔºå‰∏ç‰øÆÊîπÔº?
; ============================================================
bgr_convert:
    push ebx
    mov ebx, eax
    mov ecx, eax
    and ecx, 0xFF
    shl ecx, 16
    mov edx, eax
    and edx, 0x00FF00
    or  ecx, edx
    and eax, 0xFF0000
    shr eax, 16
    or  eax, ecx
    pop ebx
    ret

ensure_font:
    push ebx
    cmp dword [font_handle], 0
    jne .ret
    push 0
    push 0
    push 0
    push 0
    push 0
    push 1
    push 0
    push 0
    push 0
    push 400
    push 0
    push 0
    push 0
    push dword [font_size]
    call _CreateFontA@56
    mov [font_handle], eax
.ret:
    pop ebx
    ret

refresh_input_state:
    push eax
    push ebx
    push esi
    push edi
    xor edi, edi
.key_loop:
    push edi
    call _GetAsyncKeyState@4
    and eax, 0x8000
    jz .key_up
    mov eax, 1
.key_up:
    mov ebx, eax
    movzx edx, byte [prev_keys+edi]
    mov [prev_keys+edi], al
    test ebx, ebx
    jz  .key_next
    test edx, edx
    jnz .key_next
    mov byte [key_pressed_latch+edi], 1
.key_next:
    inc edi
    cmp edi, 256
    jl .key_loop

    push VK_LBUTTON
    call _GetAsyncKeyState@4
    and eax, 0x8000
    jz .m0_up
    mov eax, 1
.m0_up:
    mov [prev_mouse+0], eax

    push VK_RBUTTON
    call _GetAsyncKeyState@4
    and eax, 0x8000
    jz .m1_up
    mov eax, 1
.m1_up:
    mov [prev_mouse+4], eax

    push VK_MBUTTON
    call _GetAsyncKeyState@4
    and eax, 0x8000
    jz .m2_up
    mov eax, 1
.m2_up:
    mov [prev_mouse+8], eax

    pop edi
    pop esi
    pop ebx
    pop eax
    ret

get_slot_offset:
    push ebx
    mov ebx, eax
    push GWL_USERDATA
    push ebx
    call _GetWindowLongA@8
    cmp eax, 1
    jl  .fail
    cmp eax, MAX_WINDOWS
    jg  .fail
    dec eax
    imul eax, ENTRY_SIZE
    jmp .done
.fail:
    mov eax, -1
.done:
    pop ebx
    ret

find_free_slot:
    xor ecx, ecx
    lea edx, [window_table]
.loop:
    cmp dword [edx+24], 0
    je  .found
    add edx, ENTRY_SIZE
    inc ecx
    cmp ecx, MAX_WINDOWS
    jl  .loop
    mov eax, -1
    ret
.found:
    mov eax, ecx
    ret

register_class:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    sub esp, 40
    push 0
    call _GetModuleHandleA@4
    mov esi, eax
    push IDC_ARROW
    push 0
    call _LoadCursorA@8
    mov edi, eax
    push WHITE_BRUSH
    call _GetStockObject@4
    mov ebx, eax
    lea edx, [ebp-40]
    mov dword [edx+0], 3
    mov dword [edx+4], WndProc
    mov dword [edx+8], 0
    mov dword [edx+12], 0
    mov [edx+16], esi
    mov dword [edx+20], 0
    mov [edx+24], edi
    mov [edx+28], ebx
    mov dword [edx+32], 0
    mov dword [edx+36], window_class_name
    push edx
    call _RegisterClassA@4
    test eax, eax
    jz .fail
    mov dword [class_registered], 1
    mov eax, 1
    jmp .done
.fail:
    xor eax, eax
.done:
    add esp, 40
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

setup_buffer:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    lea eax, [rect_struct]
    push eax
    push ebx
    call _GetClientRect@8
    mov eax, [rect_struct+8]
    mov ecx, [rect_struct+12]
    test eax, eax
    jnz .w_ok
    mov eax, 1
.w_ok:
    test ecx, ecx
    jnz .h_ok
    mov ecx, 1
.h_ok:
    imul esi, ENTRY_SIZE
    lea edi, [window_table]
    add edi, esi
    push edi
    push eax
    push ecx
    xor eax, eax
    mov ecx, ENTRY_SIZE / 4
    rep stosd
    pop ecx
    pop eax
    pop edi
    mov [edi+0], ebx
    mov [edi+4], eax
    mov [edi+8], ecx
    mov dword [edi+24], 1

    mov dword [bmi_buffer+0], 40
    mov [bmi_buffer+4], eax
    mov eax, ecx
    neg eax
    mov [bmi_buffer+8], eax
    mov word  [bmi_buffer+12], 1
    mov word  [bmi_buffer+14], 32
    mov dword [bmi_buffer+16], 0
    mov dword [bmi_buffer+20], 0
    mov dword [bmi_buffer+24], 0
    mov dword [bmi_buffer+28], 0
    mov dword [bmi_buffer+32], 0
    mov dword [bmi_buffer+36], 0

    push ebx
    call _GetDC@4
    mov [edi+28], eax
    push eax
    call _CreateCompatibleDC@4
    mov [edi+12], eax
    push 0
    push 0
    lea eax, [dib_bits]
    push eax
    push DIB_RGB_COLORS
    lea eax, [bmi_buffer]
    push eax
    push dword [edi+28]
    call _CreateDIBSection@24
    mov [edi+16], eax
    push dword [edi+28]
    push ebx
    call _ReleaseDC@8
    push dword [edi+16]
    push dword [edi+12]
    call _SelectObject@8
    mov [edi+20], eax

    mov eax, [edi+4]
    mov [rect_struct+8], eax
    mov eax, [edi+8]
    mov [rect_struct+12], eax
    mov dword [rect_struct+0], 0
    mov dword [rect_struct+4], 0
    push WHITE_BRUSH
    call _GetStockObject@4
    push eax
    lea eax, [rect_struct]
    push eax
    push dword [edi+12]
    call _FillRect@12

    mov dword [edi+28], 0
    mov eax, [dib_bits]
    mov [edi+120], eax
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

recreate_buffer:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov ebx, [ebp+8]
    mov eax, ebx
    call get_slot_offset
    cmp eax, -1
    je  .ret
    lea esi, [window_table]
    add esi, eax
    cmp dword [esi+24], 0
    je  .ret

    mov eax, [esi+4]
    mov [old_width_save], eax
    mov eax, [esi+8]
    mov [old_height_save], eax

    lea eax, [rect_struct]
    push eax
    push ebx
    call _GetClientRect@8
    mov eax, [rect_struct+8]
    mov ecx, [rect_struct+12]
    test eax, eax
    jnz .w_ok
    mov eax, 1
.w_ok:
    test ecx, ecx
    jnz .h_ok
    mov ecx, 1
.h_ok:
    mov [esi+4], eax
    mov [esi+8], ecx

    mov dword [bmi_buffer+0], 40
    mov [bmi_buffer+4], eax
    mov eax, ecx
    neg eax
    mov [bmi_buffer+8], eax
    mov word  [bmi_buffer+12], 1
    mov word  [bmi_buffer+14], 32
    mov dword [bmi_buffer+16], 0
    mov dword [bmi_buffer+20], 0
    mov dword [bmi_buffer+24], 0
    mov dword [bmi_buffer+28], 0
    mov dword [bmi_buffer+32], 0
    mov dword [bmi_buffer+36], 0

    push ebx
    call _GetDC@4
    mov edi, eax
    push eax
    call _CreateCompatibleDC@4
    mov [new_dc_save], eax
    push 0
    push 0
    lea eax, [dib_bits]
    push eax
    push DIB_RGB_COLORS
    lea eax, [bmi_buffer]
    push eax
    push edi
    call _CreateDIBSection@24
    mov [new_bitmap_save], eax
    push edi
    push ebx
    call _ReleaseDC@8

    push dword [new_bitmap_save]
    push dword [new_dc_save]
    call _SelectObject@8
    mov [new_oldbitmap_save], eax

    mov eax, [esi+4]
    mov [rect_struct+8], eax
    mov eax, [esi+8]
    mov [rect_struct+12], eax
    mov dword [rect_struct+0], 0
    mov dword [rect_struct+4], 0
    mov eax, [esi+128]
    cmp eax, 0
    jne .have_bg
    mov eax, 0xFFFFFF
.have_bg:
    call bgr_convert
    push eax
    call _CreateSolidBrush@4
    mov ebx, eax
    push ebx
    lea eax, [rect_struct]
    push eax
    push dword [new_dc_save]
    call _FillRect@12
    push ebx
    call _DeleteObject@4

    push SRCCOPY
    push 0
    push 0
    push dword [esi+12]
    push dword [old_height_save]
    push dword [old_width_save]
    push 0
    push 0
    push dword [new_dc_save]
    call _BitBlt@36

    push dword [esi+20]
    push dword [esi+12]
    call _SelectObject@8
    push dword [esi+16]
    call _DeleteObject@4
    push dword [esi+12]
    call _DeleteDC@4

    mov eax, [new_dc_save]
    mov [esi+12], eax
    mov eax, [new_bitmap_save]
    mov [esi+16], eax
    mov eax, [new_oldbitmap_save]
    mov [esi+20], eax
    mov eax, [dib_bits]
    mov [esi+120], eax
.ret:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

paint_window:
    push ebp
    mov ebp, esp
    push esi
    push edi
    mov eax, [ebp+8]
    call get_slot_offset
    cmp eax, -1
    je  .ret
    lea esi, [window_table]
    add esi, eax
    cmp dword [esi+24], 0
    je  .ret
    lea eax, [paint_struct]
    push eax
    push dword [ebp+8]
    call _BeginPaint@8
    mov eax, [paint_struct]
    mov edi, eax
    push SRCCOPY
    push 0
    push 0
    push dword [esi+12]
    push dword [esi+8]
    push dword [esi+4]
    push 0
    push 0
    push edi
    call _BitBlt@36

    mov eax, [esi+124]
    test eax, eax
    jz  .no_virtual
    mov ecx, [eax+4]
    test ecx, ecx
    jz  .no_virtual
    mov ecx, [ecx]
    test ecx, ecx
    jz  .no_virtual
    push edi
    push eax
    call ecx
    add esp, 8
.no_virtual:
    lea eax, [paint_struct]
    push eax
    push dword [ebp+8]
    call _EndPaint@8
.ret:
    pop edi
    pop esi
    pop ebp
    ret

destroy_window_cleanup:
    push ebp
    mov ebp, esp
    push esi
    mov eax, [ebp+8]
    call get_slot_offset
    cmp eax, -1
    je  .count
    lea esi, [window_table]
    add esi, eax
    cmp dword [esi+24], 0
    je  .count
    mov dword [esi+24], 0
    push dword [esi+20]
    push dword [esi+12]
    call _SelectObject@8
    push dword [esi+16]
    call _DeleteObject@4
    push dword [esi+12]
    call _DeleteDC@4
    mov dword [esi+0], 0
    mov dword [esi+120], 0
.count:
    xor ecx, ecx
    xor eax, eax
    lea edx, [window_table]
.count_loop:
    cmp dword [edx+24], 1
    jne .count_next
    inc eax
.count_next:
    add edx, ENTRY_SIZE
    inc ecx
    cmp ecx, MAX_WINDOWS
    jl  .count_loop
    test eax, eax
    jnz .ret
    push 0
    call _PostQuitMessage@4
.ret:
    pop esi
    pop ebp
    ret

WndProc:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov eax, [ebp+12]
    cmp eax, WM_ERASEBKGND
    je  .erase
    cmp eax, WM_PAINT
    je  .paint
    cmp eax, WM_CLOSE
    je  .close
    cmp eax, WM_DESTROY
    je  .destroy
    cmp eax, WM_SIZE
    je  .size_msg
    cmp eax, WM_MOUSEWHEEL
    je  .mousewheel
    cmp eax, WM_CHAR
    je  .char_msg
    cmp eax, WM_TIMER
    je  .timer_msg
    cmp eax, WM_SETCURSOR
    je  .setcursor
    cmp eax, WM_LBUTTONDOWN
    je  .mouse_msg
    cmp eax, WM_LBUTTONUP
    je  .mouse_msg
    cmp eax, WM_RBUTTONDOWN
    je  .mouse_msg
    cmp eax, WM_RBUTTONUP
    je  .mouse_msg
    cmp eax, WM_HSCROLL
    je  .scroll_msg
    cmp eax, WM_VSCROLL
    je  .scroll_msg
    cmp eax, WM_COMMAND
    je  .command
    cmp eax, WM_DRAWITEM
    je  .drawitem
    cmp eax, WM_MEASUREITEM
    je  .measure
.default:
    push dword [ebp+20]
    push dword [ebp+16]
    push dword [ebp+12]
    push dword [ebp+8]
    call _DefWindowProcA@16
    jmp .done
.measure:
    mov edi, [ebp+20]
    test edi, edi
    jz  .default
    cmp dword [edi+0], ODT_MENU
    jne .measure_non_menu
    mov dword [edi+12], 120
    mov dword [edi+16], 22
    mov eax, 1
    jmp .done
.measure_non_menu:
    mov dword [edi+16], 22
    mov eax, 1
    jmp .done
.erase:
    mov eax, 1
    jmp .done
.paint:
    push dword [ebp+8]
    call paint_window
    add esp, 4
    xor eax, eax
    jmp .done
.close:
    mov eax, [ebp+8]
    call get_slot_offset
    cmp eax, -1
    je  .close_destroy
    lea edx, [window_table]
    add edx, eax
    cmp dword [edx+24], 0
    je  .close_destroy
    cmp dword [edx+116], 0
    jne .close_blocked
.close_destroy:
    push dword [ebp+8]
    call _DestroyWindow@4
    xor eax, eax
    jmp .done
.close_blocked:
    mov dword [edx+108], 1
    xor eax, eax
    jmp .done
.destroy:
    push dword [ebp+8]
    call destroy_window_cleanup
    add esp, 4
    xor eax, eax
    jmp .done
.size_msg:
    push dword [ebp+8]
    call recreate_buffer
    add esp, 4
    movzx ebx, word [ebp+20]
    test ebx, ebx
    jz  .size_done
    movzx eax, word [ebp+22]
    cmp eax, 22
    jle .size_done
    lea esi, [toolbar_table]
    xor edi, edi
.size_tb_loop:
    cmp dword [esi+0], 0
    je  .size_tb_next
    push 1
    push 34
    push ebx
    push 0
    push 0
    push dword [esi+0]
    call _MoveWindow@24
.size_tb_next:
    add esi, TB_ENTRY
    inc edi
    cmp edi, MAX_TOOLBARS
    jl  .size_tb_loop
    movzx eax, word [ebp+22]
    sub eax, 22
    mov [sb_height_save], eax
    mov [sb_width_save], ebx
    lea esi, [statusbar_table]
    xor edi, edi
.size_sb_loop:
    cmp dword [esi+0], 0
    je  .size_sb_next
    push 1
    push 22
    push dword [sb_width_save]
    push dword [sb_height_save]
    push 0
    push dword [esi+0]
    call _MoveWindow@24
.size_sb_next:
    add esi, SB_ENTRY
    inc edi
    cmp edi, MAX_STATUSBARS
    jl  .size_sb_loop
.size_done:
    xor eax, eax
    jmp .done
.mousewheel:
    mov ecx, [ebp+16]
    sar ecx, 16
    mov eax, [ebp+8]
    call get_slot_offset
    cmp eax, -1
    je  .wheel_done
    lea edx, [window_table]
    add edx, eax
    cmp dword [edx+24], 0
    je  .wheel_done
    add [edx+28], ecx
.wheel_done:
    xor eax, eax
    jmp .done
.char_msg:
    mov eax, [ebp+8]
    call get_slot_offset
    cmp eax, -1
    je  .char_done
    lea edx, [window_table]
    add edx, eax
    mov ecx, [edx+32]
    mov esi, [edx+36]
    mov edi, esi
    inc edi
    and edi, 63
    cmp edi, ecx
    je  .char_done
    movzx eax, word [ebp+16]
    and eax, 0xFF
    mov [edx+40+esi], al
    mov [edx+36], edi
.char_done:
    xor eax, eax
    jmp .done
.setcursor:
    mov eax, [ebp+20]
    and eax, 0xFFFF
    cmp eax, HTCLIENT
    jne .cursor_default
    mov eax, [ebp+8]
    call get_slot_offset
    cmp eax, -1
    je  .cursor_default
    lea edx, [window_table]
    add edx, eax
    mov ecx, [edx+104]
    test ecx, ecx
    jz  .cursor_default
    push ecx
    call _SetCursor@4
    xor eax, eax
    jmp .done
.cursor_default:
    push dword [ebp+20]
    push dword [ebp+16]
    push dword [ebp+12]
    push dword [ebp+8]
    call _DefWindowProcA@16
    jmp .done
.timer_msg:
    mov eax, [ebp+8]
    call get_slot_offset
    cmp eax, -1
    je  .timer_done
    lea edx, [window_table]
    add edx, eax
    inc dword [edx+112]
.timer_done:
    xor eax, eax
    jmp .done
.mouse_msg:
    push dword [ebp+20]
    push dword [ebp+16]
    push dword [ebp+12]
    push dword [ebp+8]
    call _DefWindowProcA@16
    jmp .done
.drawitem:
    call drawitem_dispatch
    test eax, eax
    jz  .default
    xor eax, eax
    jmp .done
.command:
    mov edx, [ebp+20]
    test edx, edx
    jz  .menu_cmd
    call toolbar_dispatch
    call command_dispatch
    xor eax, eax
    jmp .done
.menu_cmd:
    call menu_dispatch
    xor eax, eax
    jmp .done
.scroll_msg:
    call scroll_notify
    xor eax, eax
    jmp .done
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret 16

; ============================================================
; ÂÆû‰æãÊñπÊ≥ï
; ============================================================

global Window_create_string_int_int
Window_create_string_int_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    cmp dword [class_registered], 0
    jne .registered
    call register_class
    test eax, eax
    jz  .fail
.registered:
    push 0
    call _GetModuleHandleA@4
    mov edi, eax
    push 0
    push edi
    push 0
    push 0
    push dword [ebp+20]
    push dword [ebp+16]
    push CW_USEDEFAULT
    push CW_USEDEFAULT
    push WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN
    push empty_title
    push window_class_name
    push 0
    call _CreateWindowExA@48
    test eax, eax
    jz  .fail
    mov ebx, eax
    mov eax, [ebp+12]
    lea edx, [win_title_utf16]
    mov ecx, 1024
    call utf8_convert_to
    test eax, eax
    jz  .no_title
    push eax
    push ebx
    call _SetWindowTextW@8
.no_title:
    call find_free_slot
    cmp eax, -1
    je  .fail_no_slot
    mov esi, eax
    call setup_buffer
    inc esi
    push esi
    push GWL_USERDATA
    push ebx
    call _SetWindowLongA@12
    dec esi
    imul esi, ENTRY_SIZE
    lea edi, [window_table]
    add edi, esi
    mov eax, [ebp+8]
    mov [edi+124], eax
    push SW_SHOW
    push ebx
    call _ShowWindow@8
    push ebx
    call _UpdateWindow@4
    mov eax, ebx
    mov edx, [ebp+8]
    mov [edx+8], eax            ; this->hwndÔºàÂéü [edx] Âà†Èô§Ôº?
    jmp .done
.fail_no_slot:
    push ebx
    call _DestroyWindow@4
    xor eax, eax
    jmp .done
.fail:
    xor eax, eax
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

global Window_close
Window_close:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    mov eax, [eax+8]
    push eax
    call _DestroyWindow@4
    pop ebp
    ret

global Window_show
Window_show:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    mov eax, [eax+8]
    push SW_SHOW
    push eax
    call _ShowWindow@8
    pop ebp
    ret

global Window_hide
Window_hide:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    mov eax, [eax+8]
    push SW_HIDE
    push eax
    call _ShowWindow@8
    pop ebp
    ret

global Window_set_title_string
Window_set_title_string:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    mov esi, [ebp+8]
    test esi, esi
    jz  .ret
    mov ebx, [esi+8]
    test ebx, ebx
    jz  .ret
    mov eax, [ebp+12]
    lea edx, [win_title_utf16]
    mov ecx, 1024
    call utf8_convert_to
    test eax, eax
    jz  .ret
    push eax
    push ebx
    call _SetWindowTextW@8
.ret:
    pop esi
    pop ebx
    pop ebp
    ret

global Window_move_int_int
Window_move_int_int:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    mov eax, [eax+8]
    push 5
    push 0
    push 0
    push dword [ebp+16]
    push dword [ebp+12]
    push 0
    push eax
    call _SetWindowPos@28
    pop ebp
    ret

global Window_resize_int_int
Window_resize_int_int:
    push ebp
    mov ebp, esp
    push ebx
    mov ebx, [ebp+8]
    mov ebx, [ebx+8]
    push 6
    push dword [ebp+16]
    push dword [ebp+12]
    push 0
    push 0
    push 0
    push ebx
    call _SetWindowPos@28
    push ebx
    call recreate_buffer
    add esp, 4
    pop ebx
    pop ebp
    ret

global Window_get_width
Window_get_width:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    mov eax, [eax+8]
    call get_slot_offset
    cmp eax, -1
    je  .err
    lea edx, [window_table]
    add edx, eax
    mov eax, [edx+4]
    jmp .done
.err:
    xor eax, eax
.done:
    pop ebp
    ret

global Window_get_height
Window_get_height:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    mov eax, [eax+8]
    call get_slot_offset
    cmp eax, -1
    je  .err
    lea edx, [window_table]
    add edx, eax
    mov eax, [edx+8]
    jmp .done
.err:
    xor eax, eax
.done:
    pop ebp
    ret

global Window_update
Window_update:
    push ebp
    mov ebp, esp
.loop:
    push PM_REMOVE
    push 0
    push 0
    push 0
    lea eax, [msg_struct]
    push eax
    call _PeekMessageA@20
    test eax, eax
    jz  .none
    mov eax, [msg_struct+4]
    cmp eax, WM_QUIT
    je  .quit
    lea eax, [msg_struct]
    push eax
    call _TranslateMessage@4
    lea eax, [msg_struct]
    push eax
    call _DispatchMessageA@4
    jmp .loop
.none:
    push 1
    call _Sleep@4
    xor eax, eax
    jmp .done
.quit:
    mov eax, 1
.done:
    call refresh_input_state
    pop ebp
    ret

global Window_wait
Window_wait:
    push ebp
    mov ebp, esp
.loop:
    push 0
    push 0
    push 0
    lea eax, [msg_struct]
    push eax
    call _GetMessageA@16
    test eax, eax
    jz  .quit
    cmp eax, -1
    je  .quit
    lea eax, [msg_struct]
    push eax
    call _TranslateMessage@4
    lea eax, [msg_struct]
    push eax
    call _DispatchMessageA@4
    call refresh_input_state
    jmp .loop
.quit:
    mov eax, 1
    pop ebp
    ret

global Window_flush
Window_flush:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    mov eax, [ebp+8]
    mov eax, [eax+8]
    call get_slot_offset
    cmp eax, -1
    je  .ret
    lea esi, [window_table]
    add esi, eax
    cmp dword [esi+24], 0
    je  .ret
    push dword [esi+0]
    call _GetDC@4
    mov ebx, eax
    push SRCCOPY
    push 0
    push 0
    push dword [esi+12]
    push dword [esi+8]
    push dword [esi+4]
    push 0
    push 0
    push ebx
    call _BitBlt@36
    push ebx
    push dword [esi+0]
    call _ReleaseDC@8
.ret:
    pop esi
    pop ebx
    pop ebp
    ret

global Window_clear_int
Window_clear_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    mov eax, [ebp+8]
    mov eax, [eax+8]
    call get_slot_offset
    cmp eax, -1
    je  .ret
    lea esi, [window_table]
    add esi, eax
    cmp dword [esi+24], 0
    je  .ret
    mov eax, [ebp+12]
    mov [esi+128], eax
    mov eax, [esi+4]
    mov [rect_struct+8], eax
    mov eax, [esi+8]
    mov [rect_struct+12], eax
    mov dword [rect_struct+0], 0
    mov dword [rect_struct+4], 0
    mov eax, [ebp+12]
    call bgr_convert
    push eax
    call _CreateSolidBrush@4
    mov ebx, eax
    push ebx
    lea eax, [rect_struct]
    push eax
    push dword [esi+12]
    call _FillRect@12
    push ebx
    call _DeleteObject@4
.ret:
    pop esi
    pop ebx
    pop ebp
    ret

global Window_set_pixel_int_int_int
Window_set_pixel_int_int_int:
    push ebp
    mov ebp, esp
    push esi
    mov eax, [ebp+8]
    mov eax, [eax+8]
    call get_slot_offset
    cmp eax, -1
    je  .ret
    lea esi, [window_table]
    add esi, eax
    cmp dword [esi+24], 0
    je  .ret
    mov eax, [ebp+20]
    call bgr_convert
    push eax
    push dword [ebp+16]
    push dword [ebp+12]
    push dword [esi+12]
    call _SetPixelV@16
.ret:
    pop esi
    pop ebp
    ret

global Window_draw_line_int_int_int_int_int
Window_draw_line_int_int_int_int_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    mov eax, [ebp+8]
    mov eax, [eax+8]
    call get_slot_offset
    cmp eax, -1
    je  .ret
    lea esi, [window_table]
    add esi, eax
    cmp dword [esi+24], 0
    je  .ret
    mov eax, [ebp+28]
    call bgr_convert
    push eax
    push 1
    push PS_SOLID
    call _CreatePen@12
    mov ebx, eax
    push ebx
    push dword [esi+12]
    call _SelectObject@8
    push eax
    push 0
    push dword [ebp+16]
    push dword [ebp+12]
    push dword [esi+12]
    call _MoveToEx@16
    push dword [ebp+24]
    push dword [ebp+20]
    push dword [esi+12]
    call _LineTo@12
    pop eax
    push eax
    push dword [esi+12]
    call _SelectObject@8
    push ebx
    call _DeleteObject@4
.ret:
    pop esi
    pop ebx
    pop ebp
    ret

global Window_draw_line_thick_int_int_int_int_int_int
Window_draw_line_thick_int_int_int_int_int_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    mov eax, [ebp+8]
    mov eax, [eax+8]
    call get_slot_offset
    cmp eax, -1
    je .ret
    lea esi, [window_table]
    add esi, eax
    cmp dword [esi+24], 0
    je .ret
    mov eax, [ebp+28]
    call bgr_convert
    push eax
    push dword [ebp+32]
    push PS_SOLID
    call _CreatePen@12
    mov ebx, eax
    push ebx
    push dword [esi+12]
    call _SelectObject@8
    push eax
    push 0
    push dword [ebp+16]
    push dword [ebp+12]
    push dword [esi+12]
    call _MoveToEx@16
    push dword [ebp+24]
    push dword [ebp+20]
    push dword [esi+12]
    call _LineTo@12
    pop eax
    push eax
    push dword [esi+12]
    call _SelectObject@8
    push ebx
    call _DeleteObject@4
.ret:
    pop esi
    pop ebx
    pop ebp
    ret

global Window_fill_rect_int_int_int_int_int
Window_fill_rect_int_int_int_int_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    mov eax, [ebp+8]
    mov eax, [eax+8]
    call get_slot_offset
    cmp eax, -1
    je  .ret
    lea esi, [window_table]
    add esi, eax
    cmp dword [esi+24], 0
    je  .ret
    mov eax, [ebp+12]
    mov [rect_struct+0], eax
    mov eax, [ebp+16]
    mov [rect_struct+4], eax
    mov eax, [ebp+12]
    add eax, [ebp+20]
    mov [rect_struct+8], eax
    mov eax, [ebp+16]
    add eax, [ebp+24]
    mov [rect_struct+12], eax
    mov eax, [ebp+28]
    call bgr_convert
    push eax
    call _CreateSolidBrush@4
    mov ebx, eax
    push ebx
    lea eax, [rect_struct]
    push eax
    push dword [esi+12]
    call _FillRect@12
    push ebx
    call _DeleteObject@4
.ret:
    pop esi
    pop ebx
    pop ebp
    ret

global Window_draw_rect_int_int_int_int_int
Window_draw_rect_int_int_int_int_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    mov eax, [ebp+8]
    mov eax, [eax+8]
    call get_slot_offset
    cmp eax, -1
    je  .ret
    lea esi, [window_table]
    add esi, eax
    cmp dword [esi+24], 0
    je  .ret
    mov eax, [ebp+12]
    mov [rect_struct+0], eax
    mov eax, [ebp+16]
    mov [rect_struct+4], eax
    mov eax, [ebp+12]
    add eax, [ebp+20]
    mov [rect_struct+8], eax
    mov eax, [ebp+16]
    add eax, [ebp+24]
    mov [rect_struct+12], eax
    mov eax, [ebp+28]
    call bgr_convert
    push eax
    call _CreateSolidBrush@4
    mov ebx, eax
    push ebx
    lea eax, [rect_struct]
    push eax
    push dword [esi+12]
    call _FrameRect@12
    push ebx
    call _DeleteObject@4
.ret:
    pop esi
    pop ebx
    pop ebp
    ret

global Window_fill_round_rect_int_int_int_int_int_int
Window_fill_round_rect_int_int_int_int_int_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    mov eax, [ebp+8]
    mov eax, [eax+8]
    call get_slot_offset
    cmp eax, -1
    je .ret
    lea esi, [window_table]
    add esi, eax
    cmp dword [esi+24], 0
    je .ret
    mov eax, [ebp+32]
    call bgr_convert
    push eax
    call _CreateSolidBrush@4
    mov ebx, eax
    push ebx
    push dword [esi+12]
    call _SelectObject@8
    push eax
    push NULL_PEN
    call _GetStockObject@4
    push eax
    push dword [esi+12]
    call _SelectObject@8
    push eax
    push dword [ebp+28]
    push dword [ebp+28]
    mov eax, [ebp+16]
    add eax, [ebp+24]
    push eax
    mov eax, [ebp+12]
    add eax, [ebp+20]
    push eax
    push dword [ebp+16]
    push dword [ebp+12]
    push dword [esi+12]
    call _RoundRect@28
    pop eax
    push eax
    push dword [esi+12]
    call _SelectObject@8
    pop eax
    push eax
    push dword [esi+12]
    call _SelectObject@8
    push ebx
    call _DeleteObject@4
.ret:
    pop esi
    pop ebx
    pop ebp
    ret

global Window_draw_round_rect_int_int_int_int_int_int
Window_draw_round_rect_int_int_int_int_int_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    mov eax, [ebp+8]
    mov eax, [eax+8]
    call get_slot_offset
    cmp eax, -1
    je .ret
    lea esi, [window_table]
    add esi, eax
    cmp dword [esi+24], 0
    je .ret
    mov eax, [ebp+32]
    call bgr_convert
    push eax
    push 1
    push PS_SOLID
    call _CreatePen@12
    mov ebx, eax
    push ebx
    push dword [esi+12]
    call _SelectObject@8
    push eax
    push NULL_BRUSH
    call _GetStockObject@4
    push eax
    push dword [esi+12]
    call _SelectObject@8
    push eax
    push dword [ebp+28]
    push dword [ebp+28]
    mov eax, [ebp+16]
    add eax, [ebp+24]
    push eax
    mov eax, [ebp+12]
    add eax, [ebp+20]
    push eax
    push dword [ebp+16]
    push dword [ebp+12]
    push dword [esi+12]
    call _RoundRect@28
    pop eax
    push eax
    push dword [esi+12]
    call _SelectObject@8
    pop eax
    push eax
    push dword [esi+12]
    call _SelectObject@8
    push ebx
    call _DeleteObject@4
.ret:
    pop esi
    pop ebx
    pop ebp
    ret

global Window_fill_circle_int_int_int_int
Window_fill_circle_int_int_int_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    mov eax, [ebp+8]
    mov eax, [eax+8]
    call get_slot_offset
    cmp eax, -1
    je .ret
    lea esi, [window_table]
    add esi, eax
    cmp dword [esi+24], 0
    je .ret
    mov eax, [ebp+24]
    call bgr_convert
    push eax
    call _CreateSolidBrush@4
    mov ebx, eax
    push ebx
    push dword [esi+12]
    call _SelectObject@8
    push eax
    push NULL_PEN
    call _GetStockObject@4
    push eax
    push dword [esi+12]
    call _SelectObject@8
    push eax
    mov eax, [ebp+16]
    add eax, [ebp+20]
    push eax
    mov eax, [ebp+12]
    add eax, [ebp+20]
    push eax
    mov eax, [ebp+16]
    sub eax, [ebp+20]
    push eax
    mov eax, [ebp+12]
    sub eax, [ebp+20]
    push eax
    push dword [esi+12]
    call _Ellipse@20
    pop eax
    push eax
    push dword [esi+12]
    call _SelectObject@8
    pop eax
    push eax
    push dword [esi+12]
    call _SelectObject@8
    push ebx
    call _DeleteObject@4
.ret:
    pop esi
    pop ebx
    pop ebp
    ret

global Window_draw_circle_int_int_int_int
Window_draw_circle_int_int_int_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    mov eax, [ebp+8]
    mov eax, [eax+8]
    call get_slot_offset
    cmp eax, -1
    je .ret
    lea esi, [window_table]
    add esi, eax
    cmp dword [esi+24], 0
    je .ret
    mov eax, [ebp+24]
    call bgr_convert
    push eax
    push 1
    push PS_SOLID
    call _CreatePen@12
    mov ebx, eax
    push ebx
    push dword [esi+12]
    call _SelectObject@8
    push eax
    push NULL_BRUSH
    call _GetStockObject@4
    push eax
    push dword [esi+12]
    call _SelectObject@8
    push eax
    mov eax, [ebp+16]
    add eax, [ebp+20]
    push eax
    mov eax, [ebp+12]
    add eax, [ebp+20]
    push eax
    mov eax, [ebp+16]
    sub eax, [ebp+20]
    push eax
    mov eax, [ebp+12]
    sub eax, [ebp+20]
    push eax
    push dword [esi+12]
    call _Ellipse@20
    pop eax
    push eax
    push dword [esi+12]
    call _SelectObject@8
    pop eax
    push eax
    push dword [esi+12]
    call _SelectObject@8
    push ebx
    call _DeleteObject@4
.ret:
    pop esi
    pop ebx
    pop ebp
    ret

global Window_fill_triangle_int_int_int_int_int_int_int
Window_fill_triangle_int_int_int_int_int_int_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    mov eax, [ebp+8]
    mov eax, [eax+8]
    call get_slot_offset
    cmp eax, -1
    je .ret
    lea esi, [window_table]
    add esi, eax
    cmp dword [esi+24], 0
    je .ret
    mov eax, [ebp+12]
    mov [poly_points+0], eax
    mov eax, [ebp+16]
    mov [poly_points+4], eax
    mov eax, [ebp+20]
    mov [poly_points+8], eax
    mov eax, [ebp+24]
    mov [poly_points+12], eax
    mov eax, [ebp+28]
    mov [poly_points+16], eax
    mov eax, [ebp+32]
    mov [poly_points+20], eax
    mov eax, [ebp+36]
    call bgr_convert
    push eax
    call _CreateSolidBrush@4
    mov ebx, eax
    push ebx
    push dword [esi+12]
    call _SelectObject@8
    push eax
    push NULL_PEN
    call _GetStockObject@4
    push eax
    push dword [esi+12]
    call _SelectObject@8
    push eax
    push 3
    lea eax, [poly_points]
    push eax
    push dword [esi+12]
    call _Polygon@12
    pop eax
    push eax
    push dword [esi+12]
    call _SelectObject@8
    pop eax
    push eax
    push dword [esi+12]
    call _SelectObject@8
    push ebx
    call _DeleteObject@4
.ret:
    pop esi
    pop ebx
    pop ebp
    ret

global Window_draw_triangle_int_int_int_int_int_int_int
Window_draw_triangle_int_int_int_int_int_int_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    mov eax, [ebp+8]
    mov eax, [eax+8]
    call get_slot_offset
    cmp eax, -1
    je .ret
    lea esi, [window_table]
    add esi, eax
    cmp dword [esi+24], 0
    je .ret
    mov eax, [ebp+12]
    mov [poly_points+0], eax
    mov eax, [ebp+16]
    mov [poly_points+4], eax
    mov eax, [ebp+20]
    mov [poly_points+8], eax
    mov eax, [ebp+24]
    mov [poly_points+12], eax
    mov eax, [ebp+28]
    mov [poly_points+16], eax
    mov eax, [ebp+32]
    mov [poly_points+20], eax
    mov eax, [ebp+36]
    call bgr_convert
    push eax
    push 1
    push PS_SOLID
    call _CreatePen@12
    mov ebx, eax
    push ebx
    push dword [esi+12]
    call _SelectObject@8
    push eax
    push NULL_BRUSH
    call _GetStockObject@4
    push eax
    push dword [esi+12]
    call _SelectObject@8
    push eax
    push 3
    lea eax, [poly_points]
    push eax
    push dword [esi+12]
    call _Polygon@12
    pop eax
    push eax
    push dword [esi+12]
    call _SelectObject@8
    pop eax
    push eax
    push dword [esi+12]
    call _SelectObject@8
    push ebx
    call _DeleteObject@4
.ret:
    pop esi
    pop ebx
    pop ebp
    ret

global Window_draw_text_int_int_string_int
Window_draw_text_int_int_string_int:
    push ebp
    mov ebp, esp
    push esi
    mov eax, [ebp+8]
    mov eax, [eax+8]
    call get_slot_offset
    cmp eax, -1
    je  .ret
    lea esi, [window_table]
    add esi, eax
    cmp dword [esi+24], 0
    je  .ret
    mov eax, [ebp+24]
    call bgr_convert
    push eax
    push dword [esi+12]
    call _SetTextColor@8
    push TRANSPARENT
    push dword [esi+12]
    call _SetBkMode@8
    call ensure_font
    push dword [font_handle]
    push dword [esi+12]
    call _SelectObject@8
    push eax
    mov eax, [ebp+20]
    call utf8_convert
    test eax, eax
    jz  .skip_text
    push ecx
    push eax
    push dword [ebp+16]
    push dword [ebp+12]
    push dword [esi+12]
    call _TextOutW@20
.skip_text:
    pop eax
    push eax
    push dword [esi+12]
    call _SelectObject@8
.ret:
    pop esi
    pop ebp
    ret

global Window_draw_text_center_int_string_int
Window_draw_text_center_int_string_int:
    push ebp
    mov ebp, esp
    push esi
    mov eax, [ebp+8]
    mov eax, [eax+8]
    call get_slot_offset
    cmp eax, -1
    je .ret
    lea esi, [window_table]
    add esi, eax
    cmp dword [esi+24], 0
    je .ret
    mov eax, [ebp+20]
    call bgr_convert
    push eax
    push dword [esi+12]
    call _SetTextColor@8
    push TRANSPARENT
    push dword [esi+12]
    call _SetBkMode@8
    call ensure_font
    push dword [font_handle]
    push dword [esi+12]
    call _SelectObject@8
    push eax
    mov eax, [ebp+16]
    call utf8_convert
    test eax, eax
    jz  .skip_text
    push ecx
    lea edx, [size_struct]
    push edx
    push ecx
    push eax
    push dword [esi+12]
    call _GetTextExtentPoint32W@16
    mov eax, [esi+4]
    sub eax, [size_struct+0]
    sar eax, 1
    pop ecx
    push ecx
    lea edx, [utf16_buffer]
    push edx
    push dword [ebp+12]
    push eax
    push dword [esi+12]
    call _TextOutW@20
.skip_text:
    pop eax
    push eax
    push dword [esi+12]
    call _SelectObject@8
.ret:
    pop esi
    pop ebp
    ret

global Window_is_key_down_int
Window_is_key_down_int:
    push ebp
    mov ebp, esp
    push dword [ebp+12]
    call _GetAsyncKeyState@4
    and eax, 0x8000
    neg eax
    sbb eax, eax
    neg eax
    pop ebp
    ret

global Window_is_key_pressed_int
Window_is_key_pressed_int:
    push ebp
    mov ebp, esp
    push ebx
    mov ebx, [ebp+12]
    and ebx, 0xFF
    movzx eax, byte [key_pressed_latch+ebx]
    mov byte [key_pressed_latch+ebx], 0
    test eax, eax
    jz .no
    mov eax, 1
    jmp .done
.no:
    xor eax, eax
.done:
    pop ebx
    pop ebp
    ret

global Window_is_mouse_down_int
Window_is_mouse_down_int:
    push ebp
    mov ebp, esp
    mov eax, [ebp+12]
    cmp eax, 0
    jne .try_right
    mov eax, VK_LBUTTON
    jmp .check
.try_right:
    cmp eax, 1
    jne .middle
    mov eax, VK_RBUTTON
    jmp .check
.middle:
    mov eax, VK_MBUTTON
.check:
    push eax
    call _GetAsyncKeyState@4
    and eax, 0x8000
    neg eax
    sbb eax, eax
    neg eax
    pop ebp
    ret

global Window_is_mouse_clicked_int
Window_is_mouse_clicked_int:
    push ebp
    mov ebp, esp
    push esi
    mov eax, [ebp+12]
    xor esi, esi
    cmp eax, 0
    je .vk_left
    mov esi, 1
    cmp eax, 1
    je .vk_right
    mov esi, 2
    mov eax, VK_MBUTTON
    jmp .check
.vk_left:
    mov eax, VK_LBUTTON
    jmp .check
.vk_right:
    mov eax, VK_RBUTTON
.check:
    push eax
    call _GetAsyncKeyState@4
    and eax, 0x8000
    jz .no
    cmp dword [prev_mouse+esi*4], 0
    jne .no
    mov eax, 1
    jmp .done
.no:
    xor eax, eax
.done:
    pop esi
    pop ebp
    ret

global Window_get_mouse_x
Window_get_mouse_x:
    push ebp
    mov ebp, esp
    push ebx
    mov ebx, [ebp+8]
    mov ebx, [ebx+8]
    lea edx, [pt_struct]
    push edx
    call _GetCursorPos@4
    lea edx, [pt_struct]
    push edx
    push ebx
    call _ScreenToClient@8
    mov eax, [pt_struct+0]
    pop ebx
    pop ebp
    ret

global Window_get_mouse_y
Window_get_mouse_y:
    push ebp
    mov ebp, esp
    push ebx
    mov ebx, [ebp+8]
    mov ebx, [ebx+8]
    lea edx, [pt_struct]
    push edx
    call _GetCursorPos@4
    lea edx, [pt_struct]
    push edx
    push ebx
    call _ScreenToClient@8
    mov eax, [pt_struct+4]
    pop ebx
    pop ebp
    ret

global Window_get_mouse_wheel
Window_get_mouse_wheel:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    mov eax, [eax+8]
    call get_slot_offset
    cmp eax, -1
    je .zero
    lea edx, [window_table]
    add edx, eax
    cmp dword [edx+24], 0
    je .zero
    mov eax, [edx+28]
    mov dword [edx+28], 0
    jmp .done
.zero:
    xor eax, eax
.done:
    pop ebp
    ret

; ‚î?‚î? ÈùôÊ?ÅÊñπÊ≥? ‚î?‚î?

global Window_set_font_size_int
Window_set_font_size_int:
    push ebp
    mov ebp, esp
    mov eax, [ebp+12]
    cmp eax, 4
    jge .min_ok
    mov eax, 4
.min_ok:
    cmp eax, 256
    jle .max_ok
    mov eax, 256
.max_ok:
    mov [font_size], eax
    cmp dword [font_handle], 0
    je .ret
    push dword [font_handle]
    call _DeleteObject@4
    mov dword [font_handle], 0
.ret:
    pop ebp
    ret

global Window_get_ticks
Window_get_ticks:
    call _GetTickCount@0
    ret

global Window_sleep_int
Window_sleep_int:
    push ebp
    mov ebp, esp
    push dword [ebp+12]
    call _Sleep@4
    pop ebp
    ret

global Window_message_box_string_string
Window_message_box_string_string:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov eax, [ebp+12]
    lea edx, [popup_text_utf16]
    mov ecx, 1024
    call utf8_convert_to
    test eax, eax
    jz  .no_text
    mov esi, eax
.have_text:
    mov eax, [ebp+16]
    lea edx, [popup_title_utf16]
    mov ecx, 1024
    call utf8_convert_to
    test eax, eax
    jz  .no_title
    mov edi, eax
.have_title:
    push 0
    push edi
    push esi
    push 0
    call _MessageBoxW@16
    jmp .done
.no_text:
    mov esi, empty_wide
    jmp .have_text
.no_title:
    mov edi, empty_wide
    jmp .have_title
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

find_free_canvas_slot:
    xor ecx, ecx
    lea edx, [canvas_table]
.loop:
    cmp dword [edx+20], 0
    je  .found
    add edx, CANVAS_ENTRY
    inc ecx
    cmp ecx, MAX_CANVAS
    jl  .loop
    mov eax, -1
    ret
.found:
    mov eax, ecx
    ret

global get_canvas_offset
get_canvas_offset:
    push ebx
    mov ebx, eax
    cmp ebx, 1
    jl  .fail
    cmp ebx, MAX_CANVAS
    jg  .fail
    dec ebx
    shl ebx, 5
    mov eax, ebx
    jmp .done
.fail:
    mov eax, -1
.done:
    pop ebx
    ret

rgb_convert:
    push ebx
    mov ebx, eax
    mov ecx, eax
    and ecx, 0xFF0000
    shr ecx, 16
    mov edx, eax
    and edx, 0x00FF00
    and eax, 0xFF
    shl eax, 16
    or  ecx, edx
    or  eax, ecx
    pop ebx
    ret

; ============================================================
; Canvas ‚Ä? Canvas_create_int_int ÈáåÊää canvas_id ÂÜôÂÖ• this+8
; ============================================================

global Canvas_create_int_int
Canvas_create_int_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov ecx, [ebp+8]
    test ecx, ecx
    jz  .fail
    call find_free_canvas_slot
    cmp eax, -1
    je  .fail
    mov esi, eax
    mov eax, [ebp+12]
    mov ecx, [ebp+16]
    test eax, eax
    jnz .w_ok
    mov eax, 1
.w_ok:
    test ecx, ecx
    jnz .h_ok
    mov ecx, 1
.h_ok:
    shl esi, 5
    lea edi, [canvas_table]
    add edi, esi
    mov [edi+0], eax
    mov [edi+4], ecx
    mov dword [edi+20], 1
    mov dword [bmi_buffer+0], 40
    mov [bmi_buffer+4], eax
    mov eax, ecx
    neg eax
    mov [bmi_buffer+8], eax
    mov word  [bmi_buffer+12], 1
    mov word  [bmi_buffer+14], 32
    mov dword [bmi_buffer+16], 0
    mov dword [bmi_buffer+20], 0
    mov dword [bmi_buffer+24], 0
    mov dword [bmi_buffer+28], 0
    mov dword [bmi_buffer+32], 0
    mov dword [bmi_buffer+36], 0
    push 0
    call _GetDC@4
    mov ebx, eax
    push eax
    call _CreateCompatibleDC@4
    mov [edi+8], eax
    push 0
    push 0
    lea eax, [dib_bits]
    push eax
    push DIB_RGB_COLORS
    lea eax, [bmi_buffer]
    push eax
    push ebx
    call _CreateDIBSection@24
    mov [edi+12], eax
    push ebx
    push 0
    call _ReleaseDC@8
    push dword [edi+12]
    push dword [edi+8]
    call _SelectObject@8
    mov [edi+16], eax
    mov eax, [edi+0]
    mov [rect_struct+8], eax
    mov eax, [edi+4]
    mov [rect_struct+12], eax
    mov dword [rect_struct+0], 0
    mov dword [rect_struct+4], 0
    push BLACK_BRUSH
    call _GetStockObject@4
    push eax
    lea eax, [rect_struct]
    push eax
    push dword [edi+8]
    call _FillRect@12
    mov eax, [dib_bits]
    mov [edi+24], eax
    mov edx, edi
    sub edx, canvas_table
    sar edx, 5
    inc edx
    mov eax, [ebp+8]
    mov [eax+8], edx            ; this->canvas_idÔºàÂéü [eax] Âà†Èô§Ôº?
    mov eax, edx
    jmp .done
.fail:
    xor eax, eax
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

global Canvas_destroy
Canvas_destroy:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov esi, [ebp+8]
    test esi, esi
    jz  .ret
    mov eax, [esi+8]
    call get_canvas_offset
    cmp eax, -1
    je  .ret
    lea edi, [canvas_table]
    add edi, eax
    cmp dword [edi+20], 0
    je  .ret
    mov dword [edi+20], 0
    push dword [edi+16]
    push dword [edi+8]
    call _SelectObject@8
    push dword [edi+12]
    call _DeleteObject@4
    push dword [edi+8]
    call _DeleteDC@4
    mov dword [edi+24], 0
    mov dword [esi+8], 0
.ret:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

global Canvas_get_width
Canvas_get_width:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    mov eax, [eax+8]
    call get_canvas_offset
    cmp eax, -1
    je  .err
    lea edx, [canvas_table]
    add edx, eax
    mov eax, [edx+0]
    jmp .done
.err:
    xor eax, eax
.done:
    pop ebp
    ret

global Canvas_get_height
Canvas_get_height:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    mov eax, [eax+8]
    call get_canvas_offset
    cmp eax, -1
    je  .err
    lea edx, [canvas_table]
    add edx, eax
    mov eax, [edx+4]
    jmp .done
.err:
    xor eax, eax
.done:
    pop ebp
    ret

global Canvas_clear_int
Canvas_clear_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    mov eax, [ebp+8]
    mov eax, [eax+8]
    call get_canvas_offset
    cmp eax, -1
    je  .ret
    lea esi, [canvas_table]
    add esi, eax
    cmp dword [esi+20], 0
    je  .ret
    mov eax, [esi+0]
    mov [rect_struct+8], eax
    mov eax, [esi+4]
    mov [rect_struct+12], eax
    mov dword [rect_struct+0], 0
    mov dword [rect_struct+4], 0
    mov eax, [ebp+12]
    call bgr_convert
    push eax
    call _CreateSolidBrush@4
    mov ebx, eax
    push ebx
    lea eax, [rect_struct]
    push eax
    push dword [esi+8]
    call _FillRect@12
    push ebx
    call _DeleteObject@4
.ret:
    pop esi
    pop ebx
    pop ebp
    ret

global Canvas_set_pixel_int_int_int
Canvas_set_pixel_int_int_int:
    push ebp
    mov ebp, esp
    push esi
    mov eax, [ebp+8]
    mov eax, [eax+8]
    call get_canvas_offset
    cmp eax, -1
    je  .ret
    lea esi, [canvas_table]
    add esi, eax
    cmp dword [esi+20], 0
    je  .ret
    mov eax, [ebp+20]
    call bgr_convert
    push eax
    push dword [ebp+16]
    push dword [ebp+12]
    push dword [esi+8]
    call _SetPixelV@16
.ret:
    pop esi
    pop ebp
    ret

global Canvas_fill_rect_int_int_int_int_int
Canvas_fill_rect_int_int_int_int_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    mov eax, [ebp+8]
    mov eax, [eax+8]
    call get_canvas_offset
    cmp eax, -1
    je  .ret
    lea esi, [canvas_table]
    add esi, eax
    cmp dword [esi+20], 0
    je  .ret
    mov eax, [ebp+12]
    mov [rect_struct+0], eax
    mov eax, [ebp+16]
    mov [rect_struct+4], eax
    mov eax, [ebp+12]
    add eax, [ebp+20]
    mov [rect_struct+8], eax
    mov eax, [ebp+16]
    add eax, [ebp+24]
    mov [rect_struct+12], eax
    mov eax, [ebp+28]
    call bgr_convert
    push eax
    call _CreateSolidBrush@4
    mov ebx, eax
    push ebx
    lea eax, [rect_struct]
    push eax
    push dword [esi+8]
    call _FillRect@12
    push ebx
    call _DeleteObject@4
.ret:
    pop esi
    pop ebx
    pop ebp
    ret

global Canvas_draw_rect_int_int_int_int_int
Canvas_draw_rect_int_int_int_int_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    mov eax, [ebp+8]
    mov eax, [eax+8]
    call get_canvas_offset
    cmp eax, -1
    je  .ret
    lea esi, [canvas_table]
    add esi, eax
    cmp dword [esi+20], 0
    je  .ret
    mov eax, [ebp+12]
    mov [rect_struct+0], eax
    mov eax, [ebp+16]
    mov [rect_struct+4], eax
    mov eax, [ebp+12]
    add eax, [ebp+20]
    mov [rect_struct+8], eax
    mov eax, [ebp+16]
    add eax, [ebp+24]
    mov [rect_struct+12], eax
    mov eax, [ebp+28]
    call bgr_convert
    push eax
    call _CreateSolidBrush@4
    mov ebx, eax
    push ebx
    lea eax, [rect_struct]
    push eax
    push dword [esi+8]
    call _FrameRect@12
    push ebx
    call _DeleteObject@4
.ret:
    pop esi
    pop ebx
    pop ebp
    ret

global Canvas_draw_line_int_int_int_int_int
Canvas_draw_line_int_int_int_int_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    mov eax, [ebp+8]
    mov eax, [eax+8]
    call get_canvas_offset
    cmp eax, -1
    je  .ret
    lea esi, [canvas_table]
    add esi, eax
    cmp dword [esi+20], 0
    je  .ret
    mov eax, [ebp+28]
    call bgr_convert
    push eax
    push 1
    push PS_SOLID
    call _CreatePen@12
    mov ebx, eax
    push ebx
    push dword [esi+8]
    call _SelectObject@8
    push eax
    push 0
    push dword [ebp+16]
    push dword [ebp+12]
    push dword [esi+8]
    call _MoveToEx@16
    push dword [ebp+24]
    push dword [ebp+20]
    push dword [esi+8]
    call _LineTo@12
    pop eax
    push eax
    push dword [esi+8]
    call _SelectObject@8
    push ebx
    call _DeleteObject@4
.ret:
    pop esi
    pop ebx
    pop ebp
    ret

global Canvas_fill_circle_int_int_int_int
Canvas_fill_circle_int_int_int_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    mov eax, [ebp+8]
    mov eax, [eax+8]
    call get_canvas_offset
    cmp eax, -1
    je  .ret
    lea esi, [canvas_table]
    add esi, eax
    cmp dword [esi+20], 0
    je  .ret
    mov eax, [ebp+24]
    call bgr_convert
    push eax
    call _CreateSolidBrush@4
    mov ebx, eax
    push ebx
    push dword [esi+8]
    call _SelectObject@8
    push eax
    push NULL_PEN
    call _GetStockObject@4
    push eax
    push dword [esi+8]
    call _SelectObject@8
    push eax
    mov eax, [ebp+16]
    add eax, [ebp+20]
    push eax
    mov eax, [ebp+12]
    add eax, [ebp+20]
    push eax
    mov eax, [ebp+16]
    sub eax, [ebp+20]
    push eax
    mov eax, [ebp+12]
    sub eax, [ebp+20]
    push eax
    push dword [esi+8]
    call _Ellipse@20
    pop eax
    push eax
    push dword [esi+8]
    call _SelectObject@8
    pop eax
    push eax
    push dword [esi+8]
    call _SelectObject@8
    push ebx
    call _DeleteObject@4
.ret:
    pop esi
    pop ebx
    pop ebp
    ret

global Canvas_draw_circle_int_int_int_int
Canvas_draw_circle_int_int_int_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    mov eax, [ebp+8]
    mov eax, [eax+8]
    call get_canvas_offset
    cmp eax, -1
    je  .ret
    lea esi, [canvas_table]
    add esi, eax
    cmp dword [esi+20], 0
    je  .ret
    mov eax, [ebp+24]
    call bgr_convert
    push eax
    push 1
    push PS_SOLID
    call _CreatePen@12
    mov ebx, eax
    push ebx
    push dword [esi+8]
    call _SelectObject@8
    push eax
    push NULL_BRUSH
    call _GetStockObject@4
    push eax
    push dword [esi+8]
    call _SelectObject@8
    push eax
    mov eax, [ebp+16]
    add eax, [ebp+20]
    push eax
    mov eax, [ebp+12]
    add eax, [ebp+20]
    push eax
    mov eax, [ebp+16]
    sub eax, [ebp+20]
    push eax
    mov eax, [ebp+12]
    sub eax, [ebp+20]
    push eax
    push dword [esi+8]
    call _Ellipse@20
    pop eax
    push eax
    push dword [esi+8]
    call _SelectObject@8
    pop eax
    push eax
    push dword [esi+8]
    call _SelectObject@8
    push ebx
    call _DeleteObject@4
.ret:
    pop esi
    pop ebx
    pop ebp
    ret

global Canvas_draw_text_int_int_string_int
Canvas_draw_text_int_int_string_int:
    push ebp
    mov ebp, esp
    push esi
    mov eax, [ebp+8]
    mov eax, [eax+8]
    call get_canvas_offset
    cmp eax, -1
    je  .ret
    lea esi, [canvas_table]
    add esi, eax
    cmp dword [esi+20], 0
    je  .ret
    mov eax, [ebp+24]
    call bgr_convert
    push eax
    push dword [esi+8]
    call _SetTextColor@8
    push TRANSPARENT
    push dword [esi+8]
    call _SetBkMode@8
    call ensure_font
    push dword [font_handle]
    push dword [esi+8]
    call _SelectObject@8
    push eax
    mov eax, [ebp+20]
    call utf8_convert
    test eax, eax
    jz  .skip_text
    push ecx
    push eax
    push dword [ebp+16]
    push dword [ebp+12]
    push dword [esi+8]
    call _TextOutW@20
.skip_text:
    pop eax
    push eax
    push dword [esi+8]
    call _SelectObject@8
.ret:
    pop esi
    pop ebp
    ret

global Canvas_get_pixel_int_int
Canvas_get_pixel_int_int:
    push ebp
    mov ebp, esp
    push esi
    mov eax, [ebp+8]
    mov eax, [eax+8]
    call get_canvas_offset
    cmp eax, -1
    je  .zero
    lea esi, [canvas_table]
    add esi, eax
    cmp dword [esi+20], 0
    je  .zero
    push dword [ebp+16]
    push dword [ebp+12]
    push dword [esi+8]
    call _GetPixel@12
    call rgb_convert
    jmp .done
.zero:
    xor eax, eax
.done:
    pop esi
    pop ebp
    ret

; ============================================================
; Window.draw_canvas
; ============================================================

global Window_draw_canvas_identifier_int_int
Window_draw_canvas_identifier_int_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov eax, [ebp+8]
    mov eax, [eax+8]
    call get_slot_offset
    cmp eax, -1
    je  .ret
    lea esi, [window_table]
    add esi, eax
    cmp dword [esi+24], 0
    je  .ret
    mov eax, [ebp+12]
    test eax, eax
    jz  .ret
    mov eax, [eax+8]            ; canvas identifier ÁöÑÂ?º‰πüÂú? +8
    call get_canvas_offset
    cmp eax, -1
    je  .ret
    lea edi, [canvas_table]
    add edi, eax
    cmp dword [edi+20], 0
    je  .ret
    push SRCCOPY
    push 0
    push 0
    push dword [edi+8]
    push dword [edi+4]
    push dword [edi+0]
    push dword [ebp+20]
    push dword [ebp+16]
    push dword [esi+12]
    call _BitBlt@36
.ret:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

; ============================================================
; ÊâπÊ¨° C: ÂÖâÊ†á / ÊñáÂ≠óËæìÂÖ• / Â±Ö‰∏≠
; ============================================================

global Window_set_cursor_int
Window_set_cursor_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    mov eax, [ebp+8]
    mov eax, [eax+8]
    call get_slot_offset
    cmp eax, -1
    je  .ret
    lea esi, [window_table]
    add esi, eax
    mov eax, [ebp+12]
    cmp eax, 1
    je  .hand
    cmp eax, 2
    je  .cross
    cmp eax, 3
    je  .ibeam
    cmp eax, 4
    je  .waitc
    cmp eax, 5
    je  .sizeall
    cmp eax, 6
    je  .sizens
    cmp eax, 7
    je  .sizewe
    cmp eax, 8
    je  .no
    mov eax, IDC_ARROW
    jmp .load
.hand:
    mov eax, IDC_HAND
    jmp .load
.cross:
    mov eax, IDC_CROSS
    jmp .load
.ibeam:
    mov eax, IDC_IBEAM
    jmp .load
.waitc:
    mov eax, IDC_WAIT
    jmp .load
.sizeall:
    mov eax, IDC_SIZEALL
    jmp .load
.sizens:
    mov eax, IDC_SIZENS
    jmp .load
.sizewe:
    mov eax, IDC_SIZEWE
    jmp .load
.no:
    mov eax, IDC_NO
.load:
    push eax
    push 0
    call _LoadCursorA@8
    mov [esi+104], eax
.ret:
    pop esi
    pop ebx
    pop ebp
    ret

global Window_get_char
Window_get_char:
    push ebp
    mov ebp, esp
    push esi
    mov eax, [ebp+8]
    mov eax, [eax+8]
    call get_slot_offset
    cmp eax, -1
    je  .empty
    lea esi, [window_table]
    add esi, eax
    mov eax, [esi+32]
    cmp eax, [esi+36]
    je  .empty
    mov ecx, eax
    movzx eax, byte [esi+40+ecx]
    inc ecx
    and ecx, 63
    mov [esi+32], ecx
    jmp .done
.empty:
    xor eax, eax
.done:
    pop esi
    pop ebp
    ret

global Window_center
Window_center:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov eax, [ebp+8]
    mov eax, [eax+8]
    call get_slot_offset
    cmp eax, -1
    je  .ret
    lea esi, [window_table]
    add esi, eax
    push 0
    call _GetSystemMetrics@4
    mov ebx, eax
    push 1
    call _GetSystemMetrics@4
    mov edi, eax
    lea eax, [window_rect]
    push eax
    push dword [esi+0]
    call _GetWindowRect@8
    mov eax, [window_rect+8]
    sub eax, [window_rect+0]
    mov ecx, eax
    mov eax, [window_rect+12]
    sub eax, [window_rect+4]
    mov edx, ebx
    sub edx, ecx
    sar edx, 1
    mov ecx, edi
    sub ecx, eax
    sar ecx, 1
    push 5
    push 0
    push 0
    push ecx
    push edx
    push 0
    push dword [esi+0]
    call _SetWindowPos@28
.ret:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

global Window_draw_char_int_int_int_int
Window_draw_char_int_int_int_int:
    push ebp
    mov ebp, esp
    push esi
    mov eax, [ebp+8]
    mov eax, [eax+8]
    call get_slot_offset
    cmp eax, -1
    je  .ret
    lea esi, [window_table]
    add esi, eax
    cmp dword [esi+24], 0
    je  .ret
    mov eax, [ebp+24]
    call bgr_convert
    push eax
    push dword [esi+12]
    call _SetTextColor@8
    push TRANSPARENT
    push dword [esi+12]
    call _SetBkMode@8
    call ensure_font
    push dword [font_handle]
    push dword [esi+12]
    call _SelectObject@8
    push eax
    mov eax, [ebp+20]
    and eax, 0xFF
    mov [char_buffer], al
    mov byte [char_buffer+1], 0
    push 1
    push char_buffer
    push dword [ebp+16]
    push dword [ebp+12]
    push dword [esi+12]
    call _TextOutA@20
    pop eax
    push eax
    push dword [esi+12]
    call _SelectObject@8
.ret:
    pop esi
    pop ebp
    ret

; ============================================================
; ÊâπÊ¨° D: ÂÆöÊó∂Âô?
; ============================================================

global Window_start_timer_int
Window_start_timer_int:
    push ebp
    mov ebp, esp
    push esi
    mov eax, [ebp+8]
    mov eax, [eax+8]
    call get_slot_offset
    cmp eax, -1
    je  .ret
    lea esi, [window_table]
    add esi, eax
    mov eax, [ebp+12]
    test eax, eax
    jle .ret
    push 0
    push eax
    push 1
    push dword [esi+0]
    call _SetTimer@16
.ret:
    pop esi
    pop ebp
    ret

global Window_stop_timer
Window_stop_timer:
    push ebp
    mov ebp, esp
    push esi
    mov eax, [ebp+8]
    mov eax, [eax+8]
    call get_slot_offset
    cmp eax, -1
    je  .ret
    lea esi, [window_table]
    add esi, eax
    push 1
    push dword [esi+0]
    call _KillTimer@8
.ret:
    pop esi
    pop ebp
    ret

global Window_get_timer_hits
Window_get_timer_hits:
    push ebp
    mov ebp, esp
    push esi
    mov eax, [ebp+8]
    mov eax, [eax+8]
    call get_slot_offset
    cmp eax, -1
    je  .zero
    lea esi, [window_table]
    add esi, eax
    mov eax, [esi+112]
    mov dword [esi+112], 0
    jmp .done
.zero:
    xor eax, eax
.done:
    pop esi
    pop ebp
    ret

; ============================================================
; ÊâπÊ¨° E: Â§öÁ™óÂè£ÁÆ°Áê?
; ============================================================

global Window_is_valid
Window_is_valid:
    push ebp
    mov ebp, esp
    push esi
    mov eax, [ebp+8]
    mov eax, [eax+8]
    test eax, eax
    jz  .invalid
    call get_slot_offset
    cmp eax, -1
    je  .invalid
    lea esi, [window_table]
    add esi, eax
    cmp dword [esi+24], 0
    je  .invalid
    push dword [esi+0]
    call _IsWindow@4
    test eax, eax
    jz  .invalid
    mov eax, 1
    jmp .done
.invalid:
    xor eax, eax
.done:
    pop esi
    pop ebp
    ret

global Window_is_minimized
Window_is_minimized:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    mov eax, [eax+8]
    push eax
    call _IsIconic@4
    neg eax
    sbb eax, eax
    neg eax
    pop ebp
    ret

global Window_is_maximized
Window_is_maximized:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    mov eax, [eax+8]
    push eax
    call _IsZoomed@4
    neg eax
    sbb eax, eax
    neg eax
    pop ebp
    ret

global Window_is_visible
Window_is_visible:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    mov eax, [eax+8]
    push eax
    call _IsWindowVisible@4
    neg eax
    sbb eax, eax
    neg eax
    pop ebp
    ret

global Window_set_focus
Window_set_focus:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    mov eax, [eax+8]
    push eax
    call _SetFocus@4
    pop ebp
    ret

global Window_has_focus
Window_has_focus:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    mov eax, [eax+8]
    push eax
    call _GetFocus@0
    pop edx
    cmp eax, edx
    sete al
    movzx eax, al
    pop ebp
    ret

global Window_set_topmost_int
Window_set_topmost_int:
    push ebp
    mov ebp, esp
    push esi
    mov eax, [ebp+8]
    mov eax, [eax+8]
    call get_slot_offset
    cmp eax, -1
    je  .ret
    lea esi, [window_table]
    add esi, eax
    mov ecx, [ebp+12]
    mov edx, HWND_NOTOPMOST
    test ecx, ecx
    jz  .not_top
    mov edx, HWND_TOPMOST
.not_top:
    push 3
    push 0
    push 0
    push 0
    push 0
    push edx
    push dword [esi+0]
    call _SetWindowPos@28
.ret:
    pop esi
    pop ebp
    ret

global Window_minimize
Window_minimize:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    mov eax, [eax+8]
    push SW_MINIMIZE
    push eax
    call _ShowWindow@8
    pop ebp
    ret

global Window_maximize
Window_maximize:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    mov eax, [eax+8]
    push SW_MAXIMIZE
    push eax
    call _ShowWindow@8
    pop ebp
    ret

global Window_restore
Window_restore:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    mov eax, [eax+8]
    push SW_RESTORE
    push eax
    call _ShowWindow@8
    pop ebp
    ret

global Window_set_close_blocked_int
Window_set_close_blocked_int:
    push ebp
    mov ebp, esp
    push esi
    mov eax, [ebp+8]
    mov eax, [eax+8]
    call get_slot_offset
    cmp eax, -1
    je  .ret
    lea esi, [window_table]
    add esi, eax
    mov eax, [ebp+12]
    mov [esi+116], eax
.ret:
    pop esi
    pop ebp
    ret

global Window_is_close_requested
Window_is_close_requested:
    push ebp
    mov ebp, esp
    push esi
    mov eax, [ebp+8]
    mov eax, [eax+8]
    call get_slot_offset
    cmp eax, -1
    je  .zero
    lea esi, [window_table]
    add esi, eax
    mov eax, [esi+108]
    jmp .done
.zero:
    xor eax, eax
.done:
    pop esi
    pop ebp
    ret

global Window_clear_close_request
Window_clear_close_request:
    push ebp
    mov ebp, esp
    push esi
    mov eax, [ebp+8]
    mov eax, [eax+8]
    call get_slot_offset
    cmp eax, -1
    je  .ret
    lea esi, [window_table]
    add esi, eax
    mov dword [esi+108], 0
.ret:
    pop esi
    pop ebp
    ret

global Window_get_count
Window_get_count:
    xor ecx, ecx
    xor eax, eax
    lea edx, [window_table]
.loop:
    cmp dword [edx+24], 0
    je  .next
    inc eax
.next:
    add edx, ENTRY_SIZE
    inc ecx
    cmp ecx, MAX_WINDOWS
    jl  .loop
    ret

global Window_get_at_int
Window_get_at_int:
    push ebp
    mov ebp, esp
    push esi
    mov ecx, [ebp+8]
    test ecx, ecx
    jl  .zero
    xor edx, edx
    xor eax, eax
    lea esi, [window_table]
.loop:
    cmp eax, MAX_WINDOWS
    jge .zero
    cmp dword [esi+24], 0
    je  .skip
    cmp edx, ecx
    je  .found
    inc edx
.skip:
    add esi, ENTRY_SIZE
    inc eax
    jmp .loop
.found:
    mov eax, [esi+0]
    jmp .done
.zero:
    xor eax, eax
.done:
    pop esi
    pop ebp
    ret

; ============================================================
; ÊâπÊ¨° F: Êñá‰ª∂ÂØπËØùÊ°ÜÔºàÈùôÊ?ÅÊñπÊ≥ïÔºåofn_fill_common ‰ª? this+8 ËØ? hwndÔº?
; ============================================================

ofn_fill_common:
    push esi
    push edi
    xor eax, eax
    lea edi, [ofn_struct]
    mov ecx, 19
    rep stosd
    xor eax, eax
    lea edi, [ofn_buffer]
    mov ecx, 65
    rep stosd
    xor eax, eax
    lea edi, [ofn_filetitle]
    mov ecx, 65
    rep stosd
    mov eax, [ebp+8]
    mov eax, [eax+8]            ; this->hwndÔºàÂéü‰∏? [eax]Ôº?
    mov dword [ofn_struct+0], 76
    mov [ofn_struct+4], eax
    mov dword [ofn_struct+8], 0
    mov dword [ofn_struct+12], ofn_filter
    mov dword [ofn_struct+16], 0
    mov dword [ofn_struct+20], 0
    mov dword [ofn_struct+24], 1
    mov dword [ofn_struct+28], ofn_buffer
    mov dword [ofn_struct+32], 260
    mov dword [ofn_struct+36], ofn_filetitle
    mov dword [ofn_struct+40], 260
    mov dword [ofn_struct+44], 0
    mov eax, [ebp+12]
    mov [ofn_struct+48], eax
    mov dword [ofn_struct+56], 0
    mov dword [ofn_struct+60], 0
    mov dword [ofn_struct+64], 0
    mov dword [ofn_struct+68], 0
    mov dword [ofn_struct+72], 0
    pop edi
    pop esi
    ret

ofn_copy_result:
    push ofn_buffer
    call _lstrlenA@4
    mov ecx, eax
    inc ecx
    push ecx
    call _gc_alloc
    add esp, 4
    test eax, eax
    jz  .done
    mov edi, eax
    lea esi, [ofn_buffer]
.copy_loop:
    mov dl, byte [esi]
    mov byte [edi], dl
    inc esi
    inc edi
    test dl, dl
    jnz .copy_loop
.done:
    ret

global Window_open_file_string
Window_open_file_string:
    push ebp
    mov ebp, esp
    call ofn_fill_common
    mov dword [ofn_struct+52],\
        OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY
    lea eax, [ofn_struct]
    push eax
    call _GetOpenFileNameA@4
    test eax, eax
    jz  .empty
    call ofn_copy_result
    jmp .done
.empty:
    push 1
    call _gc_alloc
    add esp, 4
    test eax, eax
    jz  .done
    mov byte [eax], 0
.done:
    pop ebp
    ret

global Window_save_file_string
Window_save_file_string:
    push ebp
    mov ebp, esp
    call ofn_fill_common
    mov dword [ofn_struct+60], ofn_def_ext
    mov dword [ofn_struct+52],\
        OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY
    lea eax, [ofn_struct]
    push eax
    call _GetSaveFileNameA@4
    test eax, eax
    jz  .empty
    call ofn_copy_result
    jmp .done
.empty:
    push 1
    call _gc_alloc
    add esp, 4
    test eax, eax
    jz  .done
    mov byte [eax], 0
.done:
    pop ebp
    ret

; ============================================================
; UTF-8 -> UTF-16
; ============================================================
utf8_convert_to:
    push ebx
    push esi
    push edi
    mov esi, eax
    mov edi, edx
    mov ebx, ecx
    test esi, esi
    jz  .fail
    push 0
    push 0
    push -1
    push esi
    push 0
    push 65001
    call _MultiByteToWideChar@24
    test eax, eax
    jz  .fail
    shl eax, 1
    cmp eax, ebx
    jge .fail
    shr eax, 1
    mov ebx, eax
    push ebx
    push edi
    push -1
    push esi
    push 0
    push 65001
    call _MultiByteToWideChar@24
    test eax, eax
    jz  .fail
    mov eax, edi
    mov ecx, ebx
    dec ecx
    jmp .done
.fail:
    xor eax, eax
    xor ecx, ecx
.done:
    pop edi
    pop esi
    pop ebx
    ret

utf8_convert:
    lea edx, [utf16_buffer]
    mov ecx, 1024
    jmp utf8_convert_to

layout_place:
    push ebx
    mov ebx, eax
    mov ecx, edx
    cmp dword [layout_active], 0
    je  .passive
    mov eax, [layout_cur_x]
    mov edx, [layout_cur_y]
    sub eax, [layout_origin_x]
    add eax, ebx
    cmp eax, [layout_max_w]
    jle .place
    mov eax, [layout_cur_x]
    cmp eax, [layout_origin_x]
    je  .place
    mov eax, [layout_origin_x]
    mov edx, [layout_cur_y]
    add edx, [layout_line_h]
    add edx, [layout_gap]
    mov [layout_line_h], ecx
.place:
    push eax
    add eax, ebx
    add eax, [layout_gap]
    mov [layout_cur_x], eax
    pop eax
    mov [layout_cur_y], edx
    cmp ecx, [layout_line_h]
    jle .out
    mov [layout_line_h], ecx
.out:
    mov ecx, edx
    pop ebx
    ret
.passive:
    xor eax, eax
    xor ecx, ecx
    pop ebx
    ret

; ============================================================
; ÁªÑ‰ª∂Â∫?
; ============================================================

; control_create: ebx = identifier(parent)ÔºåÁà∂Á™óÂè£ hwnd ‰ª? [ebx+8] ËØ?
control_create:
    push esi
    push edi
    mov esi, eax
    mov edi, [ebx+8]            ; Áà∂Á™óÂè? hwndÔºàÂéü‰∏? [ebx]Ôº?
    mov eax, [ebp+20]
    mov ecx, [ebp+24]
    cmp eax, -1
    jne .no_auto
    cmp ecx, -1
    jne .no_auto
    push edx
    mov eax, [ebp+28]
    mov edx, [ebp+32]
    call layout_place
    mov [ebp+20], eax
    mov [ebp+24], ecx
    pop edx
.no_auto:
    push 0
    push 0
    push 0
    push edi
    push dword [ebp+32]
    push dword [ebp+28]
    push dword [ebp+24]
    push dword [ebp+20]
    push edx
    push empty_title
    push esi
    push 0
    call _CreateWindowExA@48
    test eax, eax
    jz  .done
    mov edi, eax
    mov eax, [ebp+16]
    call utf8_convert
    test eax, eax
    jz  .no_text
    push eax
    push edi
    call _SetWindowTextW@8
.no_text:
    mov eax, edi
.done:
    pop edi
    pop esi
    ret

find_free_control:
    push ebx
    push esi
    mov esi, eax
    xor ebx, ebx
.loop:
    cmp dword [esi+8], 0
    je  .found
    add esi, ecx
    inc ebx
    cmp ebx, edx
    jl  .loop
    mov eax, -1
    jmp .done
.found:
    mov eax, ebx
.done:
    pop esi
    pop ebx
    ret

; ---------------- Button ----------------

global Button_create_identifier_string_int_int_int_int
Button_create_identifier_string_int_int_int_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov eax, [ebp+12]
    test eax, eax
    jz  .fail
    mov ebx, eax
    mov edx, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON
    lea eax, [btn_class_name]
    call control_create
    test eax, eax
    jz  .fail
    mov edi, eax
    lea eax, [button_table]
    mov ecx, CONTROL_ENTRY
    mov edx, MAX_CONTROLS
    call find_free_control
    cmp eax, -1
    je  .fail_destroy
    mov esi, eax
    imul esi, CONTROL_ENTRY
    lea edx, [button_table]
    add edx, esi
    mov [edx+0], edi
    mov dword [edx+4], 0
    mov dword [edx+8], 1
    mov eax, [ebp+12]
    mov eax, [eax]
    mov [edx+12], eax
    mov eax, [ebp+8]
    mov [edx+16], eax
    mov eax, [ebp+8]
    mov [eax+8], edi            ; this->hwndÔºàÂéü [eax] Âà†Èô§Ôº?
    mov eax, edi
    jmp .done
.fail_destroy:
    push edi
    call _DestroyWindow@4
.fail:
    xor eax, eax
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

global Button_set_text_string
Button_set_text_string:
    push ebp
    mov ebp, esp
    push ebx
    mov ebx, [ebp+8]
    test ebx, ebx
    jz  .ret
    mov ebx, [ebx+8]
    test ebx, ebx
    jz  .ret
    mov eax, [ebp+12]
    call utf8_convert
    test eax, eax
    jz  .ret
    push eax
    push ebx
    call _SetWindowTextW@8
.ret:
    pop ebx
    pop ebp
    ret

global Button_is_clicked
Button_is_clicked:
    push ebp
    mov ebp, esp
    push esi
    mov eax, [ebp+8]
    test eax, eax
    jz  .zero
    mov eax, [eax+8]
    test eax, eax
    jz  .zero
    lea esi, [button_table]
    xor ecx, ecx
.loop:
    cmp dword [esi+0], eax
    je  .found
    add esi, CONTROL_ENTRY
    inc ecx
    cmp ecx, MAX_CONTROLS
    jl  .loop
.zero:
    xor eax, eax
    jmp .done
.found:
    mov eax, [esi+4]
    mov dword [esi+4], 0
.done:
    pop esi
    pop ebp
    ret

global Button_destroy
Button_destroy:
    push ebp
    mov ebp, esp
    push esi
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov esi, [eax+8]
    mov dword [eax+8], 0
    test esi, esi
    jz  .ret
    lea edx, [button_table]
    xor ecx, ecx
.loop:
    cmp dword [edx+0], esi
    je  .found
    add edx, CONTROL_ENTRY
    inc ecx
    cmp ecx, MAX_CONTROLS
    jl  .loop
    jmp .destroy
.found:
    mov dword [edx+0], 0
    mov dword [edx+4], 0
    mov dword [edx+8], 0
    mov dword [edx+12], 0
    mov dword [edx+16], 0
.destroy:
    push esi
    call _DestroyWindow@4
.ret:
    pop esi
    pop ebp
    ret

; ---------------- TextBox ----------------

global TextBox_create_identifier_string_int_int_int_int
TextBox_create_identifier_string_int_int_int_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov eax, [ebp+12]
    test eax, eax
    jz  .fail
    mov ebx, eax
    mov edx, WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL
    lea eax, [edit_class_name]
    call control_create
    test eax, eax
    jz  .fail
    mov edi, eax
    lea eax, [edit_table]
    mov ecx, CONTROL_ENTRY
    mov edx, MAX_CONTROLS
    call find_free_control
    cmp eax, -1
    je  .fail_destroy
    mov esi, eax
    imul esi, CONTROL_ENTRY
    lea edx, [edit_table]
    add edx, esi
    mov [edx+0], edi
    mov dword [edx+4], 0
    mov dword [edx+8], 1
    mov eax, [ebp+12]
    mov eax, [eax]
    mov [edx+12], eax
    mov eax, [ebp+8]
    mov [eax+8], edi
    mov eax, edi
    jmp .done
.fail_destroy:
    push edi
    call _DestroyWindow@4
.fail:
    xor eax, eax
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

global TextBox_set_text_string
TextBox_set_text_string:
    push ebp
    mov ebp, esp
    push ebx
    mov ebx, [ebp+8]
    test ebx, ebx
    jz  .ret
    mov ebx, [ebx+8]
    test ebx, ebx
    jz  .ret
    mov eax, [ebp+12]
    call utf8_convert
    test eax, eax
    jz  .ret
    push eax
    push ebx
    call _SetWindowTextW@8
.ret:
    pop ebx
    pop ebp
    ret

global TextBox_clear
TextBox_clear:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov eax, [eax+8]
    test eax, eax
    jz  .ret
    push empty_title
    push eax
    call _SetWindowTextW@8
.ret:
    pop ebp
    ret

global TextBox_get_text
TextBox_get_text:
    push ebp
    mov ebp, esp
    push esi
    mov esi, [ebp+8]
    test esi, esi
    jz  .empty
    mov esi, [esi+8]
    test esi, esi
    jz  .empty
    push 0
    push 0
    push WM_GETTEXTLENGTH
    push esi
    call _SendMessageA@16
    inc eax
    cmp eax, 512
    jle .len_ok
    mov eax, 512
.len_ok:
    push eax
    lea eax, [utf16_buffer]
    push eax
    push esi
    call _GetWindowTextW@12
    test eax, eax
    jz  .empty
    mov ecx, eax
    call utf16_to_utf8
    jmp .done
.empty:
    push 1
    call _gc_alloc
    add esp, 4
    test eax, eax
    jz  .done
    mov byte [eax], 0
.done:
    pop esi
    pop ebp
    ret

global TextBox_destroy
TextBox_destroy:
    push ebp
    mov ebp, esp
    push esi
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov esi, [eax+8]
    mov dword [eax+8], 0
    test esi, esi
    jz  .ret
    lea edx, [edit_table]
    xor ecx, ecx
.loop:
    cmp dword [edx+0], esi
    je  .found
    add edx, CONTROL_ENTRY
    inc ecx
    cmp ecx, MAX_CONTROLS
    jl  .loop
    jmp .destroy
.found:
    mov dword [edx+0], 0
    mov dword [edx+4], 0
    mov dword [edx+8], 0
    mov dword [edx+12], 0
    mov dword [edx+16], 0
.destroy:
    push esi
    call _DestroyWindow@4
.ret:
    pop esi
    pop ebp
    ret

; ---------------- Label ----------------

global Label_create_identifier_string_int_int_int_int
Label_create_identifier_string_int_int_int_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov eax, [ebp+12]
    test eax, eax
    jz  .fail
    mov ebx, eax
    mov edx, WS_CHILD | WS_VISIBLE | SS_LEFT
    lea eax, [static_class_name]
    call control_create
    test eax, eax
    jz  .fail
    mov edi, eax
    lea eax, [label_table]
    mov ecx, CONTROL_ENTRY
    mov edx, MAX_CONTROLS
    call find_free_control
    cmp eax, -1
    je  .fail_destroy
    mov esi, eax
    imul esi, CONTROL_ENTRY
    lea edx, [label_table]
    add edx, esi
    mov [edx+0], edi
    mov dword [edx+4], 0
    mov dword [edx+8], 1
    mov eax, [ebp+12]
    mov eax, [eax]
    mov [edx+12], eax
    mov eax, [ebp+8]
    mov [edx+16], eax
    mov eax, [ebp+8]
    mov [eax+8], edi
    mov eax, edi
    jmp .done
.fail_destroy:
    push edi
    call _DestroyWindow@4
.fail:
    xor eax, eax
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

global Label_set_text_string
Label_set_text_string:
    push ebp
    mov ebp, esp
    push ebx
    mov ebx, [ebp+8]
    test ebx, ebx
    jz  .ret
    mov ebx, [ebx+8]
    test ebx, ebx
    jz  .ret
    mov eax, [ebp+12]
    call utf8_convert
    test eax, eax
    jz  .ret
    push eax
    push ebx
    call _SetWindowTextW@8
.ret:
    pop ebx
    pop ebp
    ret

global Label_destroy
Label_destroy:
    push ebp
    mov ebp, esp
    push esi
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov esi, [eax+8]
    mov dword [eax+8], 0
    test esi, esi
    jz  .ret
    lea edx, [label_table]
    xor ecx, ecx
.loop:
    cmp dword [edx+0], esi
    je  .found
    add edx, CONTROL_ENTRY
    inc ecx
    cmp ecx, MAX_CONTROLS
    jl  .loop
    jmp .destroy
.found:
    mov dword [edx+0], 0
    mov dword [edx+4], 0
    mov dword [edx+8], 0
    mov dword [edx+12], 0
    mov dword [edx+16], 0
.destroy:
    push esi
    call _DestroyWindow@4
.ret:
    pop esi
    pop ebp
    ret

; ============================================================
; UTF-16 -> UTF-8
; ============================================================
utf16_to_utf8:
    push ebx
    push esi
    push edi
    mov ebx, ecx
    push 0
    push 0
    push 0
    push 0
    push ebx
    lea eax, [utf16_buffer]
    push eax
    push 0
    push 65001
    call _WideCharToMultiByte@32
    test eax, eax
    jz  .empty
    inc eax
    mov edi, eax
    push eax
    call _gc_alloc
    add esp, 4
    test eax, eax
    jz  .empty
    mov esi, eax
    push 0
    push 0
    push edi
    push esi
    push ebx
    lea eax, [utf16_buffer]
    push eax
    push 0
    push 65001
    call _WideCharToMultiByte@32
    test eax, eax
    jz  .empty
    mov byte [esi+eax], 0
    mov eax, esi
    jmp .done
.empty:
    push 1
    call _gc_alloc
    add esp, 4
    test eax, eax
    jz  .done
    mov byte [eax], 0
.done:
    pop edi
    pop esi
    pop ebx
    ret

scroll_notify:
    mov eax, [ebp+20]
    test eax, eax
    jz  .done
    lea edx, [slider_table]
    xor ecx, ecx
.sl:
    cmp dword [edx+0], eax
    je  .sl_hit
    add edx, SLIDER_ENTRY
    inc ecx
    cmp ecx, MAX_SLIDERS
    jl  .sl
    lea edx, [spinbox_table]
    xor ecx, ecx
.sp:
    cmp dword [edx+4], eax
    je  .sp_hit
    add edx, SPIN_ENTRY
    inc ecx
    cmp ecx, MAX_SPINBOXES
    jl  .sp
.done:
    ret
.sl_hit:
    inc dword [edx+12]
    ret
.sp_hit:
    inc dword [edx+12]
    ret

command_dispatch:
    mov eax, [ebp+16]
    shr eax, 16
    cmp eax, BN_CLICKED
    je  .btn
    cmp eax, CBN_SELCHANGE
    je  .sel
    cmp eax, EN_CHANGE
    je  .enchg
    ret
.btn:
    lea eax, [button_table]
    call notify_search
    cmp eax, 1
    je  .done
    lea eax, [checkbox_table]
    call notify_search
    cmp eax, 1
    je  .done
    lea eax, [radiobutton_table]
    call notify_search
    ret
.sel:
    lea eax, [combo_table]
    call notify_search
    cmp eax, 1
    je  .done
    lea eax, [listbox_table]
    call notify_search
.done:
    ret
.enchg:
    lea eax, [edit_table]
    call notify_search
    ret

notify_search:
    push ebx
    push ecx
    push edx
    mov ebx, [ebp+20]
    xor ecx, ecx
.loop:
    cmp dword [eax+0], ebx
    je  .found
    add eax, CONTROL_ENTRY
    inc ecx
    cmp ecx, MAX_CONTROLS
    jl  .loop
    xor eax, eax
    jmp .ret
.found:
    cmp dword [eax+8], 0
    je  .no_count
    inc dword [eax+4]
.no_count:
    mov eax, 1
.ret:
    pop edx
    pop ecx
    pop ebx
    ret

toggle_create:
    push ebx
    push esi
    push edi
    mov esi, eax
    mov edi, edx
    mov eax, [ebp+12]
    test eax, eax
    jz  .fail
    mov ebx, eax
    mov edx, esi
    lea eax, [btn_class_name]
    call control_create
    test eax, eax
    jz  .fail
    mov esi, eax
    mov eax, edi
    mov ecx, CONTROL_ENTRY
    mov edx, MAX_CONTROLS
    call find_free_control
    cmp eax, -1
    je  .fail_destroy
    mov ecx, eax
    imul ecx, CONTROL_ENTRY
    add edi, ecx
    mov [edi+0], esi
    mov dword [edi+4], 0
    mov dword [edi+8], 1
    mov eax, [ebp+12]
    mov eax, [eax]
    mov [edi+12], eax
    mov eax, [ebp+8]
    mov [edi+16], eax
    mov eax, [ebp+8]
    mov [eax+8], esi
    mov eax, esi
    jmp .done
.fail_destroy:
    push esi
    call _DestroyWindow@4
.fail:
    xor eax, eax
.done:
    pop edi
    pop esi
    pop ebx
    ret

control_destroy:
    push ebp
    mov ebp, esp
    push esi
    mov edx, [ebp+8]
    test edx, edx
    jz  .ret
    mov esi, [edx+8]
    mov dword [edx+8], 0
    test esi, esi
    jz  .ret
    xor ecx, ecx
.loop:
    cmp dword [eax+0], esi
    je  .found
    add eax, CONTROL_ENTRY
    inc ecx
    cmp ecx, MAX_CONTROLS
    jl  .loop
    jmp .destroy
.found:
    mov dword [eax+0], 0
    mov dword [eax+4], 0
    mov dword [eax+8], 0
    mov dword [eax+12], 0
    mov dword [eax+16], 0
.destroy:
    push esi
    call _DestroyWindow@4
.ret:
    pop esi
    pop ebp
    ret

control_is_clicked:
    push ebp
    mov ebp, esp
    push esi
    push edi
    mov edi, [ebp+8]
    test edi, edi
    jz  .zero
    mov edi, [edi+8]
    test edi, edi
    jz  .zero
    xor ecx, ecx
.loop:
    cmp dword [eax+0], edi
    je  .found
    add eax, CONTROL_ENTRY
    inc ecx
    cmp ecx, MAX_CONTROLS
    jl  .loop
.zero:
    xor eax, eax
    jmp .done
.found:
    mov esi, eax
    mov eax, [esi+4]
    mov dword [esi+4], 0
.done:
    pop edi
    pop esi
    pop ebp
    ret

; ---------------- CheckBox ----------------

global CheckBox_create_identifier_string_int_int_int_int
CheckBox_create_identifier_string_int_int_int_int:
    push ebp
    mov ebp, esp
    mov eax, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX
    lea edx, [checkbox_table]
    call toggle_create
    pop ebp
    ret

global CheckBox_set_text_string
CheckBox_set_text_string:
    jmp Button_set_text_string

global CheckBox_is_checked
CheckBox_is_checked:
global RadioButton_is_checked
RadioButton_is_checked:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    test eax, eax
    jz  .zero
    mov eax, [eax+8]
    test eax, eax
    jz  .zero
    push 0
    push 0
    push BM_GETCHECK
    push eax
    call _SendMessageA@16
    cmp eax, BST_CHECKED
    je  .one
.zero:
    xor eax, eax
    jmp .done
.one:
    mov eax, 1
.done:
    pop ebp
    ret

global CheckBox_set_checked_int
CheckBox_set_checked_int:
global RadioButton_set_checked_int
RadioButton_set_checked_int:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov eax, [eax+8]
    test eax, eax
    jz  .ret
    mov ecx, BST_UNCHECKED
    cmp dword [ebp+12], 0
    je  .send
    mov ecx, BST_CHECKED
.send:
    push 0
    push ecx
    push BM_SETCHECK
    push eax
    call _SendMessageA@16
.ret:
    pop ebp
    ret

global CheckBox_is_clicked
CheckBox_is_clicked:
    lea eax, [checkbox_table]
    jmp control_is_clicked

global CheckBox_destroy
CheckBox_destroy:
    lea eax, [checkbox_table]
    jmp control_destroy

; ---------------- RadioButton ----------------

global RadioButton_create_identifier_string_int_int_int_int
RadioButton_create_identifier_string_int_int_int_int:
    push ebp
    mov ebp, esp
    mov eax, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON
    lea edx, [radiobutton_table]
    call toggle_create
    pop ebp
    ret

global RadioButton_set_text_string
RadioButton_set_text_string:
    jmp Button_set_text_string

global RadioButton_is_clicked
RadioButton_is_clicked:
    lea eax, [radiobutton_table]
    jmp control_is_clicked

global RadioButton_destroy
RadioButton_destroy:
    lea eax, [radiobutton_table]
    jmp control_destroy

; ============================================================
; ComboBox / ListBox
; ============================================================

global ComboBox_create_identifier_int_int_int_int
ComboBox_create_identifier_int_int_int_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov ebx, [ebp+12]
    test ebx, ebx
    jz  .fail
    mov esi, [ebx+8]            ; Áà∂Á™óÂè? hwnd
    mov edx, WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS
    mov eax, [ebp+16]
    mov ecx, [ebp+20]
    cmp eax, -1
    jne .no_auto
    cmp ecx, -1
    jne .no_auto
    push edx
    mov eax, [ebp+24]
    mov edx, [ebp+28]
    call layout_place
    mov [ebp+16], eax
    mov [ebp+20], ecx
    pop edx
.no_auto:
    push 0
    push 0
    push 0
    push esi
    push dword [ebp+28]
    push dword [ebp+24]
    push dword [ebp+20]
    push dword [ebp+16]
    push edx
    push empty_title
    lea eax, [combo_class_name]
    push eax
    push 0
    call _CreateWindowExA@48
    test eax, eax
    jz  .fail
    mov edi, eax
    lea eax, [combo_table]
    mov ecx, CONTROL_ENTRY
    mov edx, MAX_CONTROLS
    call find_free_control
    cmp eax, -1
    je  .fail_destroy
    mov esi, eax
    imul esi, CONTROL_ENTRY
    lea edx, [combo_table]
    add edx, esi
    mov [edx+0], edi
    mov dword [edx+4], 0
    mov dword [edx+8], 1
    mov eax, [ebp+12]
    mov eax, [eax]
    mov [edx+12], eax
    mov eax, [ebp+8]
    mov [edx+16], eax
    mov eax, [ebp+8]
    mov [eax+8], edi
    mov eax, edi
    jmp .done
.fail_destroy:
    push edi
    call _DestroyWindow@4
.fail:
    xor eax, eax
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

global ComboBox_add_item_string
ComboBox_add_item_string:
    push ebp
    mov ebp, esp
    push ebx
    mov ebx, [ebp+8]
    test ebx, ebx
    jz  .ret
    mov ebx, [ebx+8]
    test ebx, ebx
    jz  .ret
    push dword [ebp+12]
    push 0
    push CB_ADDSTRING
    push ebx
    call _SendMessageA@16
.ret:
    pop ebx
    pop ebp
    ret

global ComboBox_clear_items
ComboBox_clear_items:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov eax, [eax+8]
    test eax, eax
    jz  .ret
    push 0
    push 0
    push CB_RESETCONTENT
    push eax
    call _SendMessageA@16
.ret:
    pop ebp
    ret

global ComboBox_get_selected
ComboBox_get_selected:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    test eax, eax
    jz  .none
    mov eax, [eax+8]
    test eax, eax
    jz  .none
    push 0
    push 0
    push CB_GETCURSEL
    push eax
    call _SendMessageA@16
    jmp .done
.none:
    mov eax, -1
.done:
    pop ebp
    ret

global ComboBox_set_selected_int
ComboBox_set_selected_int:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov eax, [eax+8]
    test eax, eax
    jz  .ret
    push 0
    push dword [ebp+12]
    push CB_SETCURSEL
    push eax
    call _SendMessageA@16
.ret:
    pop ebp
    ret

global ComboBox_get_selected_text
ComboBox_get_selected_text:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov eax, [ebp+8]
    test eax, eax
    jz  .empty
    mov eax, [eax+8]
    test eax, eax
    jz  .empty
    mov edi, eax
    push 0
    push 0
    push CB_GETCURSEL
    push edi
    call _SendMessageA@16
    test eax, eax
    js  .empty
    push 0
    push eax
    push CB_GETLBTEXTLEN
    push edi
    call _SendMessageA@16
    test eax, eax
    js  .empty
    cmp eax, 500
    jle .len_ok
    mov eax, 500
.len_ok:
    inc eax
    mov ebx, eax
    push ebx
    call _gc_alloc
    add esp, 4
    test eax, eax
    jz  .empty
    mov esi, eax
    push 0
    push 0
    push CB_GETCURSEL
    push edi
    call _SendMessageA@16
    push esi
    push eax
    push CB_GETLBTEXT
    push edi
    call _SendMessageA@16
    mov eax, esi
    jmp .done
.empty:
    push 1
    call _gc_alloc
    add esp, 4
    test eax, eax
    jz  .done
    mov byte [eax], 0
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

global ComboBox_get_item_text_int
ComboBox_get_item_text_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov eax, [ebp+8]
    test eax, eax
    jz  .empty
    mov eax, [eax+8]
    test eax, eax
    jz  .empty
    mov edi, eax
    push 0
    push dword [ebp+12]
    push CB_GETLBTEXTLEN
    push edi
    call _SendMessageA@16
    test eax, eax
    js  .empty
    cmp eax, 500
    jle .len_ok
    mov eax, 500
.len_ok:
    inc eax
    mov ebx, eax
    push ebx
    call _gc_alloc
    add esp, 4
    test eax, eax
    jz  .empty
    mov esi, eax
    push esi
    push dword [ebp+12]
    push CB_GETLBTEXT
    push edi
    call _SendMessageA@16
    mov eax, esi
    jmp .done
.empty:
    push 1
    call _gc_alloc
    add esp, 4
    test eax, eax
    jz  .done
    mov byte [eax], 0
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

global ComboBox_is_changed
ComboBox_is_changed:
    lea eax, [combo_table]
    jmp control_is_clicked

global ComboBox_destroy
ComboBox_destroy:
    lea eax, [combo_table]
    jmp control_destroy

; ---------------- ListBox ----------------

global ListBox_create_identifier_int_int_int_int
ListBox_create_identifier_int_int_int_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov ebx, [ebp+12]
    test ebx, ebx
    jz  .fail
    mov esi, [ebx+8]            ; Áà∂Á™óÂè? hwnd
    mov edx, WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_BORDER | WS_TABSTOP | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS
    mov eax, [ebp+16]
    mov ecx, [ebp+20]
    cmp eax, -1
    jne .no_auto
    cmp ecx, -1
    jne .no_auto
    push edx
    mov eax, [ebp+24]
    mov edx, [ebp+28]
    call layout_place
    mov [ebp+16], eax
    mov [ebp+20], ecx
    pop edx
.no_auto:
    push 0
    push 0
    push 0
    push esi
    push dword [ebp+28]
    push dword [ebp+24]
    push dword [ebp+20]
    push dword [ebp+16]
    push edx
    push empty_title
    lea eax, [listbox_class_name]
    push eax
    push 0
    call _CreateWindowExA@48
    test eax, eax
    jz  .fail
    mov edi, eax
    lea eax, [listbox_table]
    mov ecx, CONTROL_ENTRY
    mov edx, MAX_CONTROLS
    call find_free_control
    cmp eax, -1
    je  .fail_destroy
    mov esi, eax
    imul esi, CONTROL_ENTRY
    lea edx, [listbox_table]
    add edx, esi
    mov [edx+0], edi
    mov dword [edx+4], 0
    mov dword [edx+8], 1
    mov eax, [ebp+12]
    mov eax, [eax]
    mov [edx+12], eax
    mov eax, [ebp+8]
    mov [edx+16], eax
    mov eax, [ebp+8]
    mov [eax+8], edi
    mov eax, edi
    jmp .done
.fail_destroy:
    push edi
    call _DestroyWindow@4
.fail:
    xor eax, eax
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

global ListBox_add_item_string
ListBox_add_item_string:
    push ebp
    mov ebp, esp
    push ebx
    mov ebx, [ebp+8]
    test ebx, ebx
    jz  .ret
    mov ebx, [ebx+8]
    test ebx, ebx
    jz  .ret
    push dword [ebp+12]
    push 0
    push LB_ADDSTRING
    push ebx
    call _SendMessageA@16
.ret:
    pop ebx
    pop ebp
    ret

global ListBox_clear_items
ListBox_clear_items:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov eax, [eax+8]
    test eax, eax
    jz  .ret
    push 0
    push 0
    push LB_RESETCONTENT
    push eax
    call _SendMessageA@16
.ret:
    pop ebp
    ret

global ListBox_get_selected
ListBox_get_selected:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    test eax, eax
    jz  .none
    mov eax, [eax+8]
    test eax, eax
    jz  .none
    push 0
    push 0
    push LB_GETCURSEL
    push eax
    call _SendMessageA@16
    jmp .done
.none:
    mov eax, -1
.done:
    pop ebp
    ret

global ListBox_set_selected_int
ListBox_set_selected_int:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov eax, [eax+8]
    test eax, eax
    jz  .ret
    push 0
    push dword [ebp+12]
    push LB_SETCURSEL
    push eax
    call _SendMessageA@16
.ret:
    pop ebp
    ret

global ListBox_get_selected_text
ListBox_get_selected_text:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov eax, [ebp+8]
    test eax, eax
    jz  .empty
    mov eax, [eax+8]
    test eax, eax
    jz  .empty
    mov edi, eax
    push 0
    push 0
    push LB_GETCURSEL
    push edi
    call _SendMessageA@16
    test eax, eax
    js  .empty
    push 0
    push eax
    push LB_GETTEXTLEN
    push edi
    call _SendMessageA@16
    test eax, eax
    js  .empty
    cmp eax, 500
    jle .len_ok
    mov eax, 500
.len_ok:
    inc eax
    mov ebx, eax
    push ebx
    call _gc_alloc
    add esp, 4
    test eax, eax
    jz  .empty
    mov esi, eax
    push 0
    push 0
    push LB_GETCURSEL
    push edi
    call _SendMessageA@16
    push esi
    push eax
    push LB_GETTEXT
    push edi
    call _SendMessageA@16
    mov eax, esi
    jmp .done
.empty:
    push 1
    call _gc_alloc
    add esp, 4
    test eax, eax
    jz  .done
    mov byte [eax], 0
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

global ListBox_get_item_text_int
ListBox_get_item_text_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov eax, [ebp+8]
    test eax, eax
    jz  .empty
    mov eax, [eax+8]
    test eax, eax
    jz  .empty
    mov edi, eax
    push 0
    push dword [ebp+12]
    push LB_GETTEXTLEN
    push edi
    call _SendMessageA@16
    test eax, eax
    js  .empty
    cmp eax, 500
    jle .len_ok
    mov eax, 500
.len_ok:
    inc eax
    mov ebx, eax
    push ebx
    call _gc_alloc
    add esp, 4
    test eax, eax
    jz  .empty
    mov esi, eax
    push esi
    push dword [ebp+12]
    push LB_GETTEXT
    push edi
    call _SendMessageA@16
    mov eax, esi
    jmp .done
.empty:
    push 1
    call _gc_alloc
    add esp, 4
    test eax, eax
    jz  .done
    mov byte [eax], 0
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

global ListBox_is_changed
ListBox_is_changed:
    lea eax, [listbox_table]
    jmp control_is_clicked

global ListBox_destroy
ListBox_destroy:
    lea eax, [listbox_table]
    jmp control_destroy

; ---- Ëá™Âä®Â∏ÉÂ±Ä ----

global Window_layout_begin_int_int_int_int
Window_layout_begin_int_int_int_int:
    push ebp
    mov ebp, esp
    mov eax, [ebp+12]
    mov [layout_origin_x], eax
    mov [layout_cur_x], eax
    mov eax, [ebp+16]
    mov [layout_origin_y], eax
    mov [layout_cur_y], eax
    mov eax, [ebp+20]
    mov [layout_gap], eax
    mov eax, [ebp+24]
    mov [layout_max_w], eax
    mov dword [layout_line_h], 0
    mov dword [layout_active], 1
    pop ebp
    ret

global Window_layout_end
Window_layout_end:
    mov dword [layout_active], 0
    ret

global Window_layout_space_int
Window_layout_space_int:
    push ebp
    mov ebp, esp
    mov eax, [ebp+12]
    add [layout_cur_x], eax
    pop ebp
    ret

; ============================================================
; GroupBox
; ============================================================

global GroupBox_create_identifier_string_int_int_int_int
GroupBox_create_identifier_string_int_int_int_int:
    push ebp
    mov ebp, esp
    push ebx
    mov eax, [ebp+12]
    test eax, eax
    jz  .fail
    mov ebx, eax
    mov edx, WS_CHILD | WS_VISIBLE | BS_GROUPBOX
    lea eax, [btn_class_name]
    call control_create
    test eax, eax
    jz  .fail
    mov edx, [ebp+8]
    mov [edx+8], eax            ; this->hwndÔºàÂéü [edx] Âà†Èô§Ôº?
    jmp .done
.fail:
    xor eax, eax
.done:
    pop ebx
    pop ebp
    ret

global GroupBox_set_text_string
GroupBox_set_text_string:
    jmp Button_set_text_string

global GroupBox_destroy
GroupBox_destroy:
    push ebp
    mov ebp, esp
    push esi
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov esi, [eax+8]
    mov dword [eax+8], 0
    test esi, esi
    jz  .ret
    push esi
    call _DestroyWindow@4
.ret:
    pop esi
    pop ebp
    ret

; ============================================================
; Panel
; ============================================================

register_panel_class:
    push ebx
    push esi
    push edi
    push 0
    call _GetModuleHandleA@4
    mov  esi, eax
    push IDC_ARROW
    push 0
    call _LoadCursorA@8
    mov  edi, eax
    push WHITE_BRUSH
    call _GetStockObject@4
    mov  ebx, eax
    sub  esp, 40
    mov  edx, esp
    mov dword [edx+0], 3
    mov dword [edx+4], PanelWndProc
    mov dword [edx+8], 0
    mov dword [edx+12], 0
    mov [edx+16], esi
    mov dword [edx+20], 0
    mov [edx+24], edi
    mov [edx+28], ebx
    mov dword [edx+32], 0
    mov dword [edx+36], panel_class_name
    push edx
    call _RegisterClassA@4
    add  esp, 40
    test eax, eax
    jz   .fail
    mov dword [panel_class_registered], 1
    mov eax, 1
    jmp  .done
.fail:
    xor eax, eax
.done:
    pop edi
    pop esi
    pop ebx
    ret

PanelWndProc:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov eax, [ebp+12]
    cmp eax, WM_PAINT
    je  .paint
    cmp eax, WM_ERASEBKGND
    je  .erase
    cmp eax, WM_HSCROLL
    je  .scroll_msg
    cmp eax, WM_VSCROLL
    je  .scroll_msg
    cmp eax, WM_COMMAND
    je  .command
    cmp eax, WM_DRAWITEM
    je  .drawitem
    cmp eax, WM_MEASUREITEM
    je  .measure
.default:
    push dword [ebp+20]
    push dword [ebp+16]
    push dword [ebp+12]
    push dword [ebp+8]
    call _DefWindowProcA@16
    jmp .done
.measure:
    mov edi, [ebp+20]
    test edi, edi
    jz  .default
    cmp dword [edi+0], ODT_MENU
    jne .measure_non_menu
    mov dword [edi+12], 120
    mov dword [edi+16], 22
    mov eax, 1
    jmp .done
.measure_non_menu:
    mov dword [edi+16], 22
    mov eax, 1
    jmp .done
.paint:
    mov eax, [ebp+8]
    call panel_find_this
    test eax, eax
    jz  .default
    mov esi, eax
    push dword [ebp+8]
    call _GetDC@4
    mov edi, eax
    lea eax, [rect_struct]
    push eax
    push dword [ebp+8]
    call _GetClientRect@8
    mov ecx, [esi+4]
    test ecx, ecx
    jz  .paint_end
    mov ecx, [ecx]
    test ecx, ecx
    jz  .paint_end
    push 0
    push dword [rect_struct+12]
    push dword [rect_struct+8]
    push 0
    push 0
    push edi
    push esi
    call ecx
    add esp, 28
.paint_end:
    push 0
    push dword [ebp+8]
    call _ValidateRect@8
    push edi
    push dword [ebp+8]
    call _ReleaseDC@8
    xor eax, eax
    jmp .done
.erase:
    mov eax, [ebp+8]
    call panel_find_color
    test eax, eax
    jz  .default
    call bgr_convert
    push eax
    call _CreateSolidBrush@4
    mov ebx, eax
    push ebx
    lea eax, [rect_struct]
    push eax
    push dword [ebp+8]
    call _GetClientRect@8
    lea eax, [rect_struct]
    push eax
    push dword [ebp+16]
    call _FillRect@12
    push ebx
    call _DeleteObject@4
    mov eax, 1
    jmp .done
.drawitem:
    call drawitem_dispatch
    test eax, eax
    jz  .default
    xor eax, eax
    jmp .done
.command:
    call toolbar_dispatch
    call command_dispatch
    xor eax, eax
    jmp .done
.scroll_msg:
    call scroll_notify
    xor eax, eax
    jmp .done
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret 16

panel_find_color:
    push ebx
    push ecx
    mov ebx, eax
    lea eax, [panel_table]
    xor ecx, ecx
.loop:
    cmp dword [eax+0], ebx
    je  .found
    add eax, PANEL_ENTRY
    inc ecx
    cmp ecx, MAX_PANELS
    jl  .loop
    xor eax, eax
    jmp .done
.found:
    mov eax, [eax+4]
.done:
    pop ecx
    pop ebx
    ret

panel_find_this:
    push ecx
    push edx
    mov edx, eax
    lea eax, [panel_table]
    xor ecx, ecx
.loop:
    cmp dword [eax+0], edx
    je  .found
    add eax, PANEL_ENTRY
    inc ecx
    cmp ecx, MAX_PANELS
    jl  .loop
    xor eax, eax
    jmp .done
.found:
    mov eax, [eax+16]
.done:
    pop edx
    pop ecx
    ret

clear_children_of_panel:
    push ecx
    xor ecx, ecx
.loop:
    mov edx, [eax+12]
    cmp edx, esi
    jne .next
    mov dword [eax+0], 0
    mov dword [eax+4], 0
    mov dword [eax+8], 0
    mov dword [eax+12], 0
    mov dword [eax+16], 0
.next:
    add eax, CONTROL_ENTRY
    inc ecx
    cmp ecx, MAX_CONTROLS
    jl  .loop
    pop ecx
    ret

global Panel_create_identifier_int_int_int_int
Panel_create_identifier_int_int_int_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    cmp dword [panel_class_registered], 0
    jne .registered
    call register_panel_class
    test eax, eax
    jz  .fail
.registered:
    mov ebx, [ebp+12]
    test ebx, ebx
    jz  .fail
    mov esi, [ebx+8]            ; Áà∂Á™óÂè? hwnd
    mov edx, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN
    mov eax, [ebp+16]
    mov ecx, [ebp+20]
    cmp eax, -1
    jne .no_auto
    cmp ecx, -1
    jne .no_auto
    push edx
    mov eax, [ebp+24]
    mov edx, [ebp+28]
    call layout_place
    mov [ebp+16], eax
    mov [ebp+20], ecx
    pop edx
.no_auto:
    push 0
    push 0
    push 0
    push esi
    push dword [ebp+28]
    push dword [ebp+24]
    push dword [ebp+20]
    push dword [ebp+16]
    push edx
    push empty_title
    lea eax, [panel_class_name]
    push eax
    push 0
    call _CreateWindowExA@48
    test eax, eax
    jz  .fail
    mov edi, eax
    lea eax, [panel_table]
    mov ecx, PANEL_ENTRY
    mov edx, MAX_PANELS
    call find_free_control
    cmp eax, -1
    je  .fail_destroy
    mov esi, eax
    imul esi, PANEL_ENTRY
    lea edx, [panel_table]
    add edx, esi
    mov [edx+0], edi
    mov dword [edx+4], 0
    mov dword [edx+8], 1
    mov eax, [ebp+12]
    mov eax, [eax]
    mov [edx+12], eax
    mov eax, [ebp+8]
    mov [edx+16], eax
    mov eax, [ebp+8]
    mov [eax+8], edi
    mov eax, edi
    jmp .done
.fail_destroy:
    push edi
    call _DestroyWindow@4
.fail:
    xor eax, eax
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

global Panel_set_background_int
Panel_set_background_int:
    push ebp
    mov ebp, esp
    push esi
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov esi, [eax+8]
    test esi, esi
    jz  .ret
    lea eax, [panel_table]
    xor ecx, ecx
.loop:
    cmp dword [eax+0], esi
    je  .found
    add eax, PANEL_ENTRY
    inc ecx
    cmp ecx, MAX_PANELS
    jl  .loop
    jmp .ret
.found:
    mov edx, [ebp+12]
    mov [eax+4], edx
    push 1
    push 0
    push esi
    call _InvalidateRect@12
.ret:
    pop esi
    pop ebp
    ret

global Panel_destroy
Panel_destroy:
    push ebp
    mov ebp, esp
    push esi
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov esi, [eax+8]
    mov dword [eax+8], 0
    test esi, esi
    jz  .ret
    lea eax, [button_table]
    call clear_children_of_panel
    lea eax, [edit_table]
    call clear_children_of_panel
    lea eax, [label_table]
    call clear_children_of_panel
    lea eax, [checkbox_table]
    call clear_children_of_panel
    lea eax, [radiobutton_table]
    call clear_children_of_panel
    lea eax, [combo_table]
    call clear_children_of_panel
    lea eax, [listbox_table]
    call clear_children_of_panel
    lea eax, [panel_table]
    xor ecx, ecx
.cloop:
    cmp dword [eax+0], esi
    je  .cfound
    add eax, PANEL_ENTRY
    inc ecx
    cmp ecx, MAX_PANELS
    jl  .cloop
    jmp .destroy
.cfound:
    mov dword [eax+0], 0
    mov dword [eax+4], 0
    mov dword [eax+8], 0
    mov dword [eax+12], 0
    mov dword [eax+16], 0
.destroy:
    push esi
    call _DestroyWindow@4
.ret:
    pop esi
    pop ebp
    ret

; ============================================================
; ÈÄöÁî®Êéß‰ª∂Êìç‰Ωú
; ============================================================

control_move_int_int:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov eax, [eax+8]
    test eax, eax
    jz  .ret
    push 5
    push 0
    push 0
    push dword [ebp+16]
    push dword [ebp+12]
    push 0
    push eax
    call _SetWindowPos@28
.ret:
    pop ebp
    ret

control_set_size_int_int:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov eax, [eax+8]
    test eax, eax
    jz  .ret
    push 6
    push dword [ebp+16]
    push dword [ebp+12]
    push 0
    push 0
    push 0
    push eax
    call _SetWindowPos@28
.ret:
    pop ebp
    ret

control_show:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov eax, [eax+8]
    test eax, eax
    jz  .ret
    push SW_SHOW
    push eax
    call _ShowWindow@8
.ret:
    pop ebp
    ret

control_hide:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov eax, [eax+8]
    test eax, eax
    jz  .ret
    push SW_HIDE
    push eax
    call _ShowWindow@8
.ret:
    pop ebp
    ret

global Button_move_int_int
Button_move_int_int:
    jmp control_move_int_int
global Button_set_size_int_int
Button_set_size_int_int:
    jmp control_set_size_int_int
global Button_show
Button_show:
    jmp control_show
global Button_hide
Button_hide:
    jmp control_hide

global TextBox_move_int_int
TextBox_move_int_int:
    jmp control_move_int_int
global TextBox_set_size_int_int
TextBox_set_size_int_int:
    jmp control_set_size_int_int
global TextBox_show
TextBox_show:
    jmp control_show
global TextBox_hide
TextBox_hide:
    jmp control_hide

global Label_move_int_int
Label_move_int_int:
    jmp control_move_int_int
global Label_set_size_int_int
Label_set_size_int_int:
    jmp control_set_size_int_int
global Label_show
Label_show:
    jmp control_show
global Label_hide
Label_hide:
    jmp control_hide

global CheckBox_move_int_int
CheckBox_move_int_int:
    jmp control_move_int_int
global CheckBox_set_size_int_int
CheckBox_set_size_int_int:
    jmp control_set_size_int_int
global CheckBox_show
CheckBox_show:
    jmp control_show
global CheckBox_hide
CheckBox_hide:
    jmp control_hide

global RadioButton_move_int_int
RadioButton_move_int_int:
    jmp control_move_int_int
global RadioButton_set_size_int_int
RadioButton_set_size_int_int:
    jmp control_set_size_int_int
global RadioButton_show
RadioButton_show:
    jmp control_show
global RadioButton_hide
RadioButton_hide:
    jmp control_hide

global ComboBox_move_int_int
ComboBox_move_int_int:
    jmp control_move_int_int
global ComboBox_set_size_int_int
ComboBox_set_size_int_int:
    jmp control_set_size_int_int
global ComboBox_show
ComboBox_show:
    jmp control_show
global ComboBox_hide
ComboBox_hide:
    jmp control_hide

global ListBox_move_int_int
ListBox_move_int_int:
    jmp control_move_int_int
global ListBox_set_size_int_int
ListBox_set_size_int_int:
    jmp control_set_size_int_int
global ListBox_show
ListBox_show:
    jmp control_show
global ListBox_hide
ListBox_hide:
    jmp control_hide

global GroupBox_move_int_int
GroupBox_move_int_int:
    jmp control_move_int_int
global GroupBox_set_size_int_int
GroupBox_set_size_int_int:
    jmp control_set_size_int_int
global GroupBox_show
GroupBox_show:
    jmp control_show
global GroupBox_hide
GroupBox_hide:
    jmp control_hide

global Panel_move_int_int
Panel_move_int_int:
    jmp control_move_int_int
global Panel_set_size_int_int
Panel_set_size_int_int:
    jmp control_set_size_int_int
global Panel_show
Panel_show:
    jmp control_show
global Panel_hide
Panel_hide:
    jmp control_hide

; ============================================================
; Menu
; ============================================================
menu_entry_by_handle:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    sub esp, 8
    mov esi, eax
    mov dword [ebp-16], 0
    lea ebx, [menu_table]
.outer:
    cmp dword [ebp-16], MAX_MENUS
    jge .not_found
    cmp dword [ebx+8], 0
    je  .outer_next
    cmp dword [ebx+0], esi
    je  .found
    push dword [ebx+0]
    call _GetMenuItemCount@4
    mov [ebp-20], eax
    xor edi, edi
.inner:
    cmp edi, [ebp-20]
    jge .outer_next
    push edi
    push dword [ebx+0]
    call _GetSubMenu@8
    cmp eax, esi
    je  .found
    inc edi
    jmp .inner
.outer_next:
    add ebx, MENU_ENTRY
    inc dword [ebp-16]
    jmp .outer
.not_found:
    xor eax, eax
    jmp .done
.found:
    mov eax, ebx
.done:
    add esp, 8
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

menu_dispatch:
    mov eax, [ebp+16]
    and eax, 0xFFFF
    lea edx, [menu_table]
    xor ecx, ecx
.loop:
    cmp dword [edx+8], 0
    je  .next
    cmp eax, [edx+12]
    jl  .next
    cmp eax, [edx+16]
    jge .next
    mov [edx+20], eax
    ret
.next:
    add edx, MENU_ENTRY
    inc ecx
    cmp ecx, MAX_MENUS
    jl  .loop
    ret

menu_dispatch_id:
    lea edx, [menu_table]
    xor ecx, ecx
.loop:
    cmp dword [edx+8], 0
    je  .next
    cmp eax, [edx+12]
    jl  .next
    cmp eax, [edx+16]
    jge .next
    mov [edx+20], eax
    ret
.next:
    add edx, MENU_ENTRY
    inc ecx
    cmp ecx, MAX_MENUS
    jl  .loop
    ret

global Menu_create_identifier
Menu_create_identifier:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov ebx, [ebp+12]
    test ebx, ebx
    jz  .fail
    mov esi, [ebx+8]            ; Áà∂Á™óÂè? hwnd
    test esi, esi
    jz  .fail
    call _CreateMenu@0
    test eax, eax
    jz  .fail
    mov edi, eax
    lea eax, [menu_table]
    mov ecx, MENU_ENTRY
    mov edx, MAX_MENUS
    call find_free_control
    cmp eax, -1
    je  .fail_menu
    mov ecx, eax
    mov eax, MENU_ENTRY
    mul ecx
    lea edx, [menu_table]
    add edx, eax
    mov [edx+0], edi
    mov dword [edx+4], 0
    mov dword [edx+8], 1
    mov eax, [cmd_id_base]
    mov [edx+12], eax
    mov [edx+16], eax
    mov dword [edx+20], -1
    mov [edx+24], esi
    mov eax, [ebp+8]
    mov [edx+28], eax
    mov dword [edx+32], 0
    push edi
    push esi
    call _SetMenu@8
    push esi
    call _DrawMenuBar@4
    mov edx, [ebp+8]
    mov [edx+8], edi            ; this->hmenuÔºàÂéü [edx] Âà†Èô§Ôº?
    mov eax, edi
    jmp .done
.fail_menu:
    push edi
    call _DestroyMenu@4
.fail:
    xor eax, eax
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

global Menu_add_menu_string
Menu_add_menu_string:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov ebx, [ebp+8]
    test ebx, ebx
    jz  .fail
    mov ebx, [ebx+8]
    test ebx, ebx
    jz  .fail
    call _CreatePopupMenu@0
    test eax, eax
    jz  .fail
    mov edi, eax
    mov eax, [ebp+12]
    call utf8_convert
    test eax, eax
    jz  .append_only
    push eax
    push edi
    push MF_POPUP
    push ebx
    call _AppendMenuW@16
    jmp .appended
.append_only:
    push 0
    push edi
    push MF_POPUP
    push ebx
    call _AppendMenuW@16
.appended:
    mov eax, ebx
    call menu_entry_by_handle
    test eax, eax
    jz  .done_popup
    mov [eax+4], edi
    mov esi, [eax+24]
    push esi
    call _DrawMenuBar@4
.done_popup:
    mov eax, edi
    jmp .done
.fail:
    xor eax, eax
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

global Menu_add_item_string
Menu_add_item_string:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov ebx, [ebp+8]
    test ebx, ebx
    jz  .fail
    mov ebx, [ebx+8]
    test ebx, ebx
    jz  .fail
    mov eax, ebx
    call menu_entry_by_handle
    test eax, eax
    jz  .fail
    mov esi, eax
    mov edi, [esi+4]
    test edi, edi
    jnz .have_target
    mov edi, ebx
.have_target:
    mov edx, [esi+16]
    inc edx
    mov [esi+16], edx
    cmp edx, [cmd_id_base]
    jle .base_ok
    mov [cmd_id_base], edx
.base_ok:
    dec edx
    mov ebx, edx
    mov eax, [ebp+12]
    call utf8_convert
    mov edx, MF_STRING
    cmp dword [esi+32], 0
    je  .flags_ok
    mov edx, MF_OWNERDRAW
.flags_ok:
    test eax, eax
    jz  .no_text
    push eax
    push ebx
    push edx
    push edi
    call _AppendMenuW@16
    jmp .added
.no_text:
    push 0
    push ebx
    push edx
    push edi
    call _AppendMenuW@16
.added:
    mov eax, ebx
    mov ebx, [esi+24]
    push ebx
    call _DrawMenuBar@4
    jmp .done
.fail:
    xor eax, eax
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

global Menu_add_separator
Menu_add_separator:
    push ebp
    mov ebp, esp
    push ebx
    mov ebx, [ebp+8]
    test ebx, ebx
    jz  .ret
    mov ebx, [ebx+8]
    test ebx, ebx
    jz  .ret
    mov eax, ebx
    call menu_entry_by_handle
    test eax, eax
    jz  .ret
    mov ebx, eax
    mov eax, [ebx+4]
    test eax, eax
    jnz .tgt
    mov eax, [ebx+0]
.tgt:
    push 0
    push MF_SEPARATOR
    push 0
    push eax
    call _AppendMenuW@16
    mov eax, [ebx+24]
    push eax
    call _DrawMenuBar@4
.ret:
    pop ebx
    pop ebp
    ret

global Menu_get_clicked
Menu_get_clicked:
    push ebp
    mov ebp, esp
    push esi
    mov eax, [ebp+8]
    test eax, eax
    jz  .none
    mov eax, [eax+8]
    test eax, eax
    jz  .none
    call menu_entry_by_handle
    test eax, eax
    jz  .none
    mov esi, eax
    mov eax, [esi+20]
    mov dword [esi+20], -1
    jmp .done
.none:
    mov eax, -1
.done:
    pop esi
    pop ebp
    ret

global Menu_owner_draw
Menu_owner_draw:
    push ebp
    mov ebp, esp
    push esi
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov eax, [eax+8]
    test eax, eax
    jz  .ret
    call menu_entry_by_handle
    test eax, eax
    jz  .ret
    mov dword [eax+32], 1
.ret:
    pop esi
    pop ebp
    ret

global Menu_destroy
Menu_destroy:
    push ebp
    mov ebp, esp
    push esi
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov esi, [eax+8]
    mov dword [eax+8], 0
    test esi, esi
    jz  .ret
    mov eax, esi
    call menu_entry_by_handle
    test eax, eax
    jz  .destroy_only
    mov edx, [eax+24]
    push 0
    push edx
    call _SetMenu@8
    mov dword [eax+0], 0
    mov dword [eax+4], 0
    mov dword [eax+8], 0
.destroy_only:
    push esi
    call _DestroyMenu@4
.ret:
    pop esi
    pop ebp
    ret

; ============================================================
; Toolbar
; ============================================================
toolbar_dispatch:
    mov eax, [ebp+20]
    test eax, eax
    jz  .not_toolbar
    lea edx, [toolbar_table]
    xor ecx, ecx
.loop:
    cmp dword [edx+0], eax
    je  .found
    add edx, TB_ENTRY
    inc ecx
    cmp ecx, MAX_TOOLBARS
    jl  .loop
.not_toolbar:
    ret
.found:
    mov eax, [ebp+16]
    and eax, 0xFFFF
    mov [edx+16], eax
    ret

global Toolbar_create_identifier_int
Toolbar_create_identifier_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    cmp dword [toolbar_comctl_init], 0
    jne .initialized
    mov dword [icc_struct+0], 8
    mov dword [icc_struct+4], ICC_BARCLASSES
    lea eax, [icc_struct]
    push eax
    call _InitCommonControlsEx@4
    mov dword [toolbar_comctl_init], 1
.initialized:
    mov ebx, [ebp+12]
    test ebx, ebx
    jz  .fail
    mov esi, [ebx+8]            ; Áà∂Á™óÂè? hwnd
    test esi, esi
    jz  .fail
    lea eax, [rect_struct]
    push eax
    push esi
    call _GetClientRect@8
    mov edx, [rect_struct+8]
    push 0
    push 0
    push 0
    push esi
    push dword [ebp+16]
    push edx
    push 0
    push 0
    push WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_LIST
    push empty_title
    lea eax, [toolbar_class_name]
    push eax
    push 0
    call _CreateWindowExA@48
    test eax, eax
    jz  .fail
    mov edi, eax
    lea eax, [toolbar_table]
    mov ecx, TB_ENTRY
    mov edx, MAX_TOOLBARS
    call find_free_control
    cmp eax, -1
    je  .fail_destroy
    mov ecx, eax
    mov eax, TB_ENTRY
    mul ecx
    lea edx, [toolbar_table]
    add edx, eax
    mov [edx+0], edi
    mov eax, [cmd_id_base]
    mov [edx+4], eax
    mov dword [edx+8], 1
    mov [edx+12], eax
    mov dword [edx+16], -1
    mov eax, [ebp+8]
    mov [edx+20], eax
    push 0
    push 20
    push TB_BUTTONSTRUCTSIZE
    push edi
    call _SendMessageA@16
    mov ecx, [ebp+8]
    mov [ecx+8], edi            ; this->hwndÔºàÂéü [ecx] Âà†Èô§Ôº?
    mov eax, edi
    jmp .done
.fail_destroy:
    push edi
    call _DestroyWindow@4
.fail:
    xor eax, eax
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

global Toolbar_add_button_string
Toolbar_add_button_string:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov ebx, [ebp+8]
    test ebx, ebx
    jz  .fail
    mov ebx, [ebx+8]
    test ebx, ebx
    jz  .fail
    lea esi, [toolbar_table]
    xor ecx, ecx
.loop:
    cmp dword [esi+0], ebx
    je  .found
    add esi, TB_ENTRY
    inc ecx
    cmp ecx, MAX_TOOLBARS
    jl  .loop
.fail:
    xor eax, eax
    jmp .done
.found:
    mov eax, [esi+12]
    mov [tb_id_save], eax
    inc dword [esi+12]
    mov edx, [cmd_id_base]
    cmp dword [esi+12], edx
    jle .base_ok
    mov edx, [esi+12]
    mov [cmd_id_base], edx
.base_ok:
    mov eax, [ebp+12]
    call utf8_convert
    test eax, eax
    jz  .no_text
    mov edx, eax
    mov edi, [tb_pool_off]
    add edi, tb_string_pool
    mov [tb_iString_save], edi
    lea esi, [ecx*2+2]
.copy:
    mov al, byte [edx]
    mov byte [edi], al
    inc edx
    inc edi
    dec esi
    jnz .copy
    mov eax, edi
    sub eax, tb_string_pool
    mov [tb_pool_off], eax
    jmp .build
.no_text:
    mov dword [tb_iString_save], empty_wide
.build:
    mov dword [tb_btn_struct+0], I_IMAGENONE
    mov eax, [tb_id_save]
    mov [tb_btn_struct+4], eax
    mov byte [tb_btn_struct+8], TBSTATE_ENABLED
    mov byte [tb_btn_struct+9], TBSTYLE_BUTTON
    mov word [tb_btn_struct+10], 0
    mov dword [tb_btn_struct+12], 0
    mov eax, [tb_iString_save]
    mov [tb_btn_struct+16], eax
    lea eax, [tb_btn_struct]
    push eax
    push 1
    push TB_ADDBUTTONSW
    push ebx
    call _SendMessageW@16
    mov eax, [tb_id_save]
    jmp .done
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

global Toolbar_get_clicked
Toolbar_get_clicked:
    push ebp
    mov ebp, esp
    push esi
    mov eax, [ebp+8]
    test eax, eax
    jz  .none
    mov eax, [eax+8]
    test eax, eax
    jz  .none
    lea esi, [toolbar_table]
    xor ecx, ecx
.loop:
    cmp dword [esi+0], eax
    je  .found
    add esi, TB_ENTRY
    inc ecx
    cmp ecx, MAX_TOOLBARS
    jl  .loop
.none:
    mov eax, -1
    jmp .done
.found:
    mov eax, [esi+16]
    mov dword [esi+16], -1
.done:
    pop esi
    pop ebp
    ret

global Toolbar_destroy
Toolbar_destroy:
    push ebp
    mov ebp, esp
    push esi
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov esi, [eax+8]
    mov dword [eax+8], 0
    test esi, esi
    jz  .ret
    lea edx, [toolbar_table]
    xor ecx, ecx
.loop:
    cmp dword [edx+0], esi
    je  .found
    add edx, TB_ENTRY
    inc ecx
    cmp ecx, MAX_TOOLBARS
    jl  .loop
    jmp .destroy
.found:
    mov dword [edx+0], 0
    mov dword [edx+4], 0
    mov dword [edx+8], 0
    mov dword [edx+12], 0
    mov dword [edx+16], -1
    mov dword [edx+20], 0
.destroy:
    push esi
    call _DestroyWindow@4
.ret:
    pop esi
    pop ebp
    ret

; ============================================================
; ButtonBar
; ============================================================
global ButtonBar_create_identifier_int
ButtonBar_create_identifier_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov ebx, [ebp+8]
    mov esi, [ebp+12]
    test ebx, ebx
    jz  .fail
    test esi, esi
    jz  .fail
    mov eax, [esi+8]            ; Áà∂Á™óÂè? hwnd
    test eax, eax
    jz  .fail
    lea edi, [rect_struct]
    push edi
    push eax
    call _GetClientRect@8
    mov edi, [rect_struct+8]
    test edi, edi
    jnz .w_ok
    mov edi, 400
.w_ok:
    push 40
    push edi
    push dword [ebp+16]
    push 0
    push esi
    push ebx
    call Panel_create_identifier_int_int_int_int
    add esp, 24
    test eax, eax
    jz  .fail
    push 12632256
    push ebx
    call Panel_set_background_int
    add esp, 8
    mov eax, [ebx+8]
    jmp .done
.fail:
    xor eax, eax
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

global ButtonBar_add_identifier_string
ButtonBar_add_identifier_string:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov ebx, [ebp+8]
    mov esi, [ebp+12]
    test ebx, ebx
    jz  .fail
    test esi, esi
    jz  .fail
    mov eax, [ebx+8]
    test eax, eax
    jz  .fail
    push 0
    mov edi, esp
    mov [edi], eax
    push 30
    push dword [ebx+20]
    push 5
    push dword [ebx+12]
    push dword [ebp+16]
    push edi
    push esi
    call Button_create_identifier_string_int_int_int_int
    add esp, 28
    add esp, 4
    test eax, eax
    jz  .fail
    mov eax, [ebx+12]
    add eax, [ebx+20]
    add eax, [ebx+16]
    mov [ebx+12], eax
    mov eax, 1
    jmp .done
.fail:
    xor eax, eax
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

; ============================================================
; StatusBar
; ============================================================
global StatusBar_create_identifier
StatusBar_create_identifier:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov ebx, [ebp+8]
    mov esi, [ebp+12]
    test ebx, ebx
    jz  .fail
    test esi, esi
    jz  .fail
    mov esi, [esi+8]            ; Áà∂Á™óÂè? hwnd
    test esi, esi
    jz  .fail
    cmp dword [toolbar_comctl_init], 0
    jne .initialized
    mov dword [icc_struct+0], 8
    mov dword [icc_struct+4], ICC_BARCLASSES
    lea eax, [icc_struct]
    push eax
    call _InitCommonControlsEx@4
    mov dword [toolbar_comctl_init], 1
.initialized:
    push 0
    push 0
    push 0
    push esi
    push 0
    push 0
    push 0
    push 0
    push WS_CHILD | WS_VISIBLE
    push empty_title
    lea eax, [statusbar_class_name]
    push eax
    push 0
    call _CreateWindowExA@48
    test eax, eax
    jz  .fail
    mov edi, eax
    lea ecx, [statusbar_table]
    xor edx, edx
.sb_find:
    cmp dword [ecx+8], 0
    je  .sb_slot
    add ecx, SB_ENTRY
    inc edx
    cmp edx, MAX_STATUSBARS
    jl  .sb_find
    push edi
    call _DestroyWindow@4
    jmp .fail
.sb_slot:
    mov [ecx+0], edi
    mov [ecx+4], esi
    mov dword [ecx+8], 1
    lea edx, [rect_struct]
    push edx
    push esi
    call _GetClientRect@8
    mov eax, [rect_struct+8]
    mov edx, [rect_struct+12]
    test eax, eax
    jnz .w_ok
    mov eax, 400
.w_ok:
    test edx, edx
    jnz .h_ok
    mov edx, 240
.h_ok:
    sub edx, 22
    push 1
    push 22
    push eax
    push edx
    push 0
    push edi
    call _MoveWindow@24
    mov [ebx+8], edi            ; this->hwndÔºàÂéü [ebx] Âà†Èô§Ôº?
    mov eax, edi
    jmp .done
.fail:
    xor eax, eax
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

global StatusBar_set_text_string
StatusBar_set_text_string:
    push ebp
    mov ebp, esp
    push ebx
    mov ebx, [ebp+8]
    test ebx, ebx
    jz  .ret
    mov ebx, [ebx+8]
    test ebx, ebx
    jz  .ret
    mov eax, [ebp+12]
    call utf8_convert
    test eax, eax
    jz  .ret
    push eax
    push 0
    push SB_SETTEXTW
    push ebx
    call _SendMessageW@16
.ret:
    pop ebx
    pop ebp
    ret

global StatusBar_get_text
StatusBar_get_text:
    push ebp
    mov ebp, esp
    push esi
    mov esi, [ebp+8]
    test esi, esi
    jz .empty
    mov esi, [esi+8]
    test esi, esi
    jz .empty
    push 0
    push 0
    push SB_GETTEXTLENGTHW
    push esi
    call _SendMessageW@16
    test eax, eax
    jz .empty
    inc eax
    cmp eax, 512
    jle .len_ok
    mov eax, 512
.len_ok:
    push eax
    lea eax, [utf16_buffer]
    push eax
    push 0
    push esi
    call _SendMessageW@16
    test eax, eax
    jz .empty
    mov ecx, eax
    call utf16_to_utf8
    jmp .done
.empty:
    push 1
    call _gc_alloc
    add esp, 4
    test eax, eax
    jz .done
    mov byte [eax], 0
.done:
    pop esi
    pop ebp
    ret

global StatusBar_destroy
StatusBar_destroy:
    push ebp
    mov ebp, esp
    push esi
    mov eax, [ebp+8]
    test eax, eax
    jz .ret
    mov esi, [eax+8]
    mov dword [eax+8], 0
    test esi, esi
    jz .ret
    lea edx, [statusbar_table]
    xor ecx, ecx
.loop:
    cmp dword [edx+0], esi
    je  .found
    add edx, SB_ENTRY
    inc ecx
    cmp ecx, MAX_STATUSBARS
    jl .loop
    jmp .destroy
.found:
    mov dword [edx+0], 0
    mov dword [edx+4], 0
    mov dword [edx+8], 0
.destroy:
    push esi
    call _DestroyWindow@4
.ret:
    pop esi
    pop ebp
    ret

global Menu_popup_int_int
Menu_popup_int_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov ebx, [ebp+8]
    test ebx, ebx
    jz .ret
    mov ebx, [ebx+8]
    test ebx, ebx
    jz .ret
    mov eax, ebx
    call menu_entry_by_handle
    test eax, eax
    jz .ret
    mov esi, eax
    mov eax, [esi+4]
    test eax, eax
    jz .ret
    mov ebx, eax
    mov eax, [ebp+12]
    mov [pt_struct+0], eax
    mov eax, [ebp+16]
    mov [pt_struct+4], eax
    lea edx, [pt_struct]
    push edx
    push dword [esi+24]
    call _ClientToScreen@8
    push 0
    push dword [esi+24]
    push 0
    push dword [pt_struct+4]
    push dword [pt_struct+0]
    push TPM_RIGHTBUTTON | TPM_RETURNCMD
    push ebx
    call _TrackPopupMenu@28
    test eax, eax
    jz .ret
    call menu_dispatch_id
.ret:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

; ============================================================
; ProgressBar / SpinBox / Slider
; ============================================================

subclass_find:
    push ecx
    push edx
    mov edx, eax
    lea eax, [subclass_table]
    xor ecx, ecx
.loop:
    cmp dword [eax+0], edx
    je  .found
    add eax, SUBCLASS_ENTRY
    inc ecx
    cmp ecx, MAX_SUBCLASS
    jl  .loop
    xor eax, eax
    jmp .done
.found:
.done:
    pop edx
    pop ecx
    ret

subclass_install:
    push ebx
    push esi
    push edi
    test eax, eax
    jz  .out
    mov esi, eax
    mov edi, edx
    lea ebx, [subclass_table]
    xor ecx, ecx
.find:
    cmp dword [ebx+0], 0
    je  .slot
    add ebx, SUBCLASS_ENTRY
    inc ecx
    cmp ecx, MAX_SUBCLASS
    jl  .find
    jmp .out
.slot:
    mov [ebx+0], esi
    mov [ebx+4], edi
    push GWL_WNDPROC
    push esi
    call _GetWindowLongA@8
    mov [ebx+8], eax
    push SubclassProc
    push GWL_WNDPROC
    push esi
    call _SetWindowLongA@12
    push 0
    push 0
    push esi
    call _InvalidateRect@12
.out:
    pop edi
    pop esi
    pop ebx
    ret

SubclassProc:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov eax, [ebp+8]
    call subclass_find
    test eax, eax
    jz  .default
    mov esi, eax
    mov eax, [ebp+12]
    cmp eax, WM_PAINT
    je  .paint
    cmp eax, WM_ERASEBKGND
    je  .erase
    cmp eax, WM_NCPAINT
    je  .ncpaint
    cmp eax, WM_NCDESTROY
    je  .ncdestroy
    jmp .forward
.paint:
    push dword [ebp+8]
    call _GetDC@4
    mov edi, eax
    lea eax, [rect_struct]
    push eax
    push dword [ebp+8]
    call _GetClientRect@8
    mov eax, [esi+4]
    test eax, eax
    jz  .paint_end
    mov ecx, [eax+4]
    test ecx, ecx
    jz  .paint_end
    mov ecx, [ecx]
    test ecx, ecx
    jz  .paint_end
    cmp ecx, 0x00010000
    jb  .paint_end
    push 0
    push dword [rect_struct+12]
    push dword [rect_struct+8]
    push 0
    push 0
    push edi
    push dword [esi+4]
    call ecx
    add esp, 28
.paint_end:
    push 0
    push dword [ebp+8]
    call _ValidateRect@8
    push edi
    push dword [ebp+8]
    call _ReleaseDC@8
    xor eax, eax
    jmp .done
.erase:
    push 0
    push 0
    push dword [ebp+8]
    call _InvalidateRect@12
    mov eax, 1
    jmp .done
.ncpaint:
    xor eax, eax
    jmp .done
.ncdestroy:
    mov eax, [esi+8]
    mov dword [esi+0], 0
    mov dword [esi+4], 0
    mov dword [esi+8], 0
    test eax, eax
    jz  .default
    push dword [ebp+20]
    push dword [ebp+16]
    push dword [ebp+12]
    push dword [ebp+8]
    push eax
    call _CallWindowProcA@20
    jmp .done
.forward:
    mov eax, [esi+8]
    test eax, eax
    jz  .default
    push dword [ebp+20]
    push dword [ebp+16]
    push dword [ebp+12]
    push dword [ebp+8]
    push eax
    call _CallWindowProcA@20
    mov ebx, eax
    push 0
    push 0
    push dword [ebp+8]
    call _InvalidateRect@12
    mov eax, ebx
    jmp .done
.default:
    push dword [ebp+20]
    push dword [ebp+16]
    push dword [ebp+12]
    push dword [ebp+8]
    call _DefWindowProcA@16
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret 16

init_comctl_ext:
    cmp dword [comctl_ext_init], 0
    jne .done
    mov dword [icc_struct+0], 8
    mov dword [icc_struct+4], ICC_BARCLASSES | ICC_UPDOWN_CLASS | ICC_PROGRESS_CLASS
    lea eax, [icc_struct]
    push eax
    call _InitCommonControlsEx@4
    mov dword [comctl_ext_init], 1
.done:
    ret

; ---------------- ProgressBar ----------------

global ProgressBar_create_identifier_int_int_int_int
ProgressBar_create_identifier_int_int_int_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov ebx, [ebp+8]
    mov esi, [ebp+12]
    test ebx, ebx
    jz  .fail
    test esi, esi
    jz  .fail
    mov esi, [esi+8]            ; Áà∂Á™óÂè? hwnd
    test esi, esi
    jz  .fail
    call init_comctl_ext
    mov eax, [ebp+16]
    mov ecx, [ebp+20]
    cmp eax, -1
    jne .no_auto
    cmp ecx, -1
    jne .no_auto
    push esi
    mov eax, [ebp+24]
    mov edx, [ebp+28]
    call layout_place
    mov [ebp+16], eax
    mov [ebp+20], ecx
    pop esi
.no_auto:
    push 0
    push 0
    push 0
    push esi
    push dword [ebp+28]
    push dword [ebp+24]
    push dword [ebp+20]
    push dword [ebp+16]
    push WS_CHILD | WS_VISIBLE
    push empty_title
    lea eax, [progress_class_name]
    push eax
    push 0
    call _CreateWindowExA@48
    test eax, eax
    jz  .fail
    mov edi, eax
    push empty_wide
    push empty_wide
    push edi
    call _SetWindowTheme@12
    lea eax, [progress_table]
    mov ecx, PB_ENTRY
    mov edx, MAX_PROGRESSBARS
    call find_free_control
    cmp eax, -1
    je  .fail_destroy
    mov ecx, eax
    shl ecx, 4
    lea edx, [progress_table]
    add edx, ecx
    mov [edx+0], edi
    mov [edx+4], esi
    mov dword [edx+8], 1
    mov dword [edx+12], 0
    mov eax, [ebp+8]
    mov [eax+8], edi            ; this->hwndÔºàÂéü [eax] Âà†Èô§Ôº?
    mov eax, edi
    jmp .done
.fail_destroy:
    push edi
    call _DestroyWindow@4
.fail:
    xor eax, eax
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

global ProgressBar_set_range_int_int
ProgressBar_set_range_int_int:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov eax, [eax+8]
    test eax, eax
    jz  .ret
    push dword [ebp+16]
    push dword [ebp+12]
    push PBM_SETRANGE32
    push eax
    call _SendMessageA@16
.ret:
    pop ebp
    ret

global ProgressBar_set_value_int
ProgressBar_set_value_int:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov eax, [eax+8]
    test eax, eax
    jz  .ret
    push 0
    push dword [ebp+12]
    push PBM_SETPOS
    push eax
    call _SendMessageA@16
.ret:
    pop ebp
    ret

global ProgressBar_get_value
ProgressBar_get_value:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    test eax, eax
    jz  .zero
    mov eax, [eax+8]
    test eax, eax
    jz  .zero
    push 0
    push 0
    push PBM_GETPOS
    push eax
    call _SendMessageA@16
    jmp .done
.zero:
    xor eax, eax
.done:
    pop ebp
    ret

global ProgressBar_step_int
ProgressBar_step_int:
    push ebp
    mov ebp, esp
    push esi
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov esi, [eax+8]
    test esi, esi
    jz  .ret
    push 0
    push dword [ebp+12]
    push PBM_SETSTEP
    push esi
    call _SendMessageA@16
    push 0
    push 0
    push PBM_STEPIT
    push esi
    call _SendMessageA@16
.ret:
    pop esi
    pop ebp
    ret

global ProgressBar_set_bar_color_int
ProgressBar_set_bar_color_int:
    push ebp
    mov ebp, esp
    push esi
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov esi, [eax+8]
    test esi, esi
    jz  .ret
    mov eax, [ebp+12]
    call bgr_convert
    push eax
    push 0
    push PBM_SETBARCOLOR
    push esi
    call _SendMessageA@16
.ret:
    pop esi
    pop ebp
    ret

global ProgressBar_set_marquee_int_int
ProgressBar_set_marquee_int_int:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov eax, [eax+8]
    test eax, eax
    jz  .ret
    push dword [ebp+16]
    push dword [ebp+12]
    push PBM_SETMARQUEE
    push eax
    call _SendMessageA@16
.ret:
    pop ebp
    ret

global ProgressBar_destroy
ProgressBar_destroy:
    push ebp
    mov ebp, esp
    push esi
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov esi, [eax+8]
    mov dword [eax+8], 0
    test esi, esi
    jz  .ret
    lea edx, [progress_table]
    xor ecx, ecx
.loop:
    cmp dword [edx+0], esi
    je  .found
    add edx, PB_ENTRY
    inc ecx
    cmp ecx, MAX_PROGRESSBARS
    jl  .loop
    jmp .destroy
.found:
    mov dword [edx+0], 0
    mov dword [edx+4], 0
    mov dword [edx+8], 0
.destroy:
    push esi
    call _DestroyWindow@4
.ret:
    pop esi
    pop ebp
    ret

global ProgressBar_move_int_int
ProgressBar_move_int_int:
    jmp control_move_int_int
global ProgressBar_set_size_int_int
ProgressBar_set_size_int_int:
    jmp control_set_size_int_int
global ProgressBar_show
ProgressBar_show:
    jmp control_show
global ProgressBar_hide
ProgressBar_hide:
    jmp control_hide

; ---------------- SpinBox ----------------

global SpinBox_create_identifier_int_int_int_int
SpinBox_create_identifier_int_int_int_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    sub esp, 4
    mov ebx, [ebp+8]
    mov esi, [ebp+12]
    test ebx, ebx
    jz  .fail
    test esi, esi
    jz  .fail
    mov esi, [esi+8]            ; Áà∂Á™óÂè? hwnd
    test esi, esi
    jz  .fail
    call init_comctl_ext
    mov eax, [ebp+16]
    mov ecx, [ebp+20]
    cmp eax, -1
    jne .no_auto
    cmp ecx, -1
    jne .no_auto
    push esi
    mov eax, [ebp+24]
    mov edx, [ebp+28]
    call layout_place
    mov [ebp+16], eax
    mov [ebp+20], ecx
    pop esi
.no_auto:
    mov edi, [ebp+24]
    sub edi, 20
    jge .w_ok
    mov edi, 40
.w_ok:
    push 0
    push 0
    push 0
    push esi
    push dword [ebp+28]
    push edi
    push dword [ebp+20]
    push dword [ebp+16]
    push WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL
    push empty_title
    lea eax, [edit_class_name]
    push eax
    push 0
    call _CreateWindowExA@48
    test eax, eax
    jz  .fail
    mov [ebp-4], eax
    mov edi, [ebp+16]
    add edi, [ebp+24]
    sub edi, 20
    push 0
    push 0
    push 0
    push esi
    push dword [ebp+28]
    push 20
    push dword [ebp+20]
    push edi
    push WS_CHILD | WS_VISIBLE | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_SETBUDDYINT
    push empty_title
    lea eax, [updown_class_name]
    push eax
    push 0
    call _CreateWindowExA@48
    test eax, eax
    jz  .fail_edit
    mov edi, eax
    push 0
    push dword [ebp-4]
    push UDM_SETBUDDY
    push edi
    call _SendMessageA@16
    lea eax, [spinbox_table]
    mov ecx, SPIN_ENTRY
    mov edx, MAX_SPINBOXES
    call find_free_control
    cmp eax, -1
    je  .fail_both
    mov ecx, eax
    shl ecx, 4
    lea edx, [spinbox_table]
    add edx, ecx
    mov eax, [ebp-4]
    mov [edx+0], eax
    mov [edx+4], edi
    mov dword [edx+8], 1
    mov dword [edx+12], 0
    mov eax, [ebp-4]
    mov ecx, [ebp+8]
    mov [ecx+8], eax            ; this->hwndÔºàÂéü [ecx] Âà†Èô§Ôº?
    mov eax, eax
    jmp .done
.fail_both:
    push edi
    call _DestroyWindow@4
.fail_edit:
    push dword [ebp-4]
    call _DestroyWindow@4
.fail:
    xor eax, eax
.done:
    add esp, 4
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

spinbox_find_updown:
    lea edx, [spinbox_table]
    xor ecx, ecx
.loop:
    cmp dword [edx+0], esi
    je  .found
    add edx, SPIN_ENTRY
    inc ecx
    cmp ecx, MAX_SPINBOXES
    jl  .loop
    xor esi, esi
    ret
.found:
    mov esi, [edx+4]
    ret

global SpinBox_set_range_int_int
SpinBox_set_range_int_int:
    push ebp
    mov ebp, esp
    push esi
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov esi, [eax+8]
    test esi, esi
    jz  .ret
    call spinbox_find_updown
    test esi, esi
    jz  .ret
    push dword [ebp+16]
    push dword [ebp+12]
    push UDM_SETRANGE32
    push esi
    call _SendMessageA@16
.ret:
    pop esi
    pop ebp
    ret

global SpinBox_set_step_int
SpinBox_set_step_int:
    push ebp
    mov ebp, esp
    push esi
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov esi, [eax+8]
    test esi, esi
    jz  .ret
    call spinbox_find_updown
    test esi, esi
    jz  .ret
    mov eax, [ebp+12]
    test eax, eax
    jg  .step_ok
    mov eax, 1
.step_ok:
    mov dword [udaccel_struct+0], 0
    mov [udaccel_struct+4], eax
    lea eax, [udaccel_struct]
    push eax
    push 1
    push UDM_SETACCEL
    push esi
    call _SendMessageA@16
.ret:
    pop esi
    pop ebp
    ret

global SpinBox_set_value_int
SpinBox_set_value_int:
    push ebp
    mov ebp, esp
    push esi
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov esi, [eax+8]
    test esi, esi
    jz  .ret
    call spinbox_find_updown
    test esi, esi
    jz  .ret
    push 0
    push dword [ebp+12]
    push UDM_SETPOS32
    push esi
    call _SendMessageA@16
.ret:
    pop esi
    pop ebp
    ret

global SpinBox_get_value
SpinBox_get_value:
    push ebp
    mov ebp, esp
    push esi
    mov eax, [ebp+8]
    test eax, eax
    jz  .zero
    mov esi, [eax+8]
    test esi, esi
    jz  .zero
    call spinbox_find_updown
    test esi, esi
    jz  .zero
    push udm_err_flag
    push 0
    push UDM_GETPOS32
    push esi
    call _SendMessageA@16
    jmp .done
.zero:
    xor eax, eax
.done:
    pop esi
    pop ebp
    ret

global SpinBox_is_changed
SpinBox_is_changed:
    push ebp
    mov ebp, esp
    push esi
    mov eax, [ebp+8]
    test eax, eax
    jz  .zero
    mov esi, [eax+8]
    test esi, esi
    jz  .zero
    lea edx, [spinbox_table]
    xor ecx, ecx
.loop:
    cmp dword [edx+0], esi
    je  .found
    add edx, SPIN_ENTRY
    inc ecx
    cmp ecx, MAX_SPINBOXES
    jl  .loop
.zero:
    xor eax, eax
    jmp .done
.found:
    mov eax, [edx+12]
    mov dword [edx+12], 0
.done:
    pop esi
    pop ebp
    ret

global SpinBox_set_text_string
SpinBox_set_text_string:
    jmp TextBox_set_text_string

global SpinBox_get_text
SpinBox_get_text:
    jmp TextBox_get_text

global SpinBox_destroy
SpinBox_destroy:
    push ebp
    mov ebp, esp
    push esi
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov esi, [eax+8]
    mov dword [eax+8], 0
    test esi, esi
    jz  .ret
    lea edx, [spinbox_table]
    xor ecx, ecx
.loop:
    cmp dword [edx+0], esi
    je  .found
    add edx, SPIN_ENTRY
    inc ecx
    cmp ecx, MAX_SPINBOXES
    jl  .loop
    jmp .destroy
.found:
    mov eax, [edx+4]
    mov dword [edx+0], 0
    mov dword [edx+4], 0
    mov dword [edx+8], 0
    mov dword [edx+12], 0
    push eax
    call _DestroyWindow@4
.destroy:
    push esi
    call _DestroyWindow@4
.ret:
    pop esi
    pop ebp
    ret

; ---------------- Slider ----------------

global Slider_create_identifier_int_int_int_int
Slider_create_identifier_int_int_int_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov ebx, [ebp+8]
    mov esi, [ebp+12]
    test ebx, ebx
    jz  .fail
    test esi, esi
    jz  .fail
    mov esi, [esi+8]            ; Áà∂Á™óÂè? hwnd
    test esi, esi
    jz  .fail
    call init_comctl_ext
    mov eax, [ebp+16]
    mov ecx, [ebp+20]
    cmp eax, -1
    jne .no_auto
    cmp ecx, -1
    jne .no_auto
    push esi
    mov eax, [ebp+24]
    mov edx, [ebp+28]
    call layout_place
    mov [ebp+16], eax
    mov [ebp+20], ecx
    pop esi
.no_auto:
    push 0
    push 0
    push 0
    push esi
    push dword [ebp+28]
    push dword [ebp+24]
    push dword [ebp+20]
    push dword [ebp+16]
    push WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS
    push empty_title
    lea eax, [slider_class_name]
    push eax
    push 0
    call _CreateWindowExA@48
    test eax, eax
    jz  .fail
    mov edi, eax
    lea eax, [slider_table]
    mov ecx, SLIDER_ENTRY
    mov edx, MAX_SLIDERS
    call find_free_control
    cmp eax, -1
    je  .fail_destroy
    mov ecx, eax
    shl ecx, 4
    lea edx, [slider_table]
    add edx, ecx
    mov [edx+0], edi
    mov [edx+4], esi
    mov dword [edx+8], 1
    mov dword [edx+12], 0
    mov eax, [ebp+8]
    mov [eax+8], edi            ; this->hwndÔºàÂéü [eax] Âà†Èô§Ôº?
    mov eax, edi
    jmp .done
.fail_destroy:
    push edi
    call _DestroyWindow@4
.fail:
    xor eax, eax
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

global Slider_set_range_int_int
Slider_set_range_int_int:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov eax, [eax+8]
    test eax, eax
    jz  .ret
    mov ecx, [ebp+16]
    and ecx, 0xFFFF
    shl ecx, 16
    mov edx, [ebp+12]
    and edx, 0xFFFF
    or  ecx, edx
    push ecx
    push 1
    push TBM_SETRANGE
    push eax
    call _SendMessageA@16
.ret:
    pop ebp
    ret

global Slider_set_value_int
Slider_set_value_int:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov eax, [eax+8]
    test eax, eax
    jz  .ret
    push dword [ebp+12]
    push 1
    push TBM_SETPOS
    push eax
    call _SendMessageA@16
.ret:
    pop ebp
    ret

global Slider_get_value
Slider_get_value:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    test eax, eax
    jz  .zero
    mov eax, [eax+8]
    test eax, eax
    jz  .zero
    push 0
    push 0
    push TBM_GETPOS
    push eax
    call _SendMessageA@16
    jmp .done
.zero:
    xor eax, eax
.done:
    pop ebp
    ret

global Slider_set_page_int
Slider_set_page_int:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov eax, [eax+8]
    test eax, eax
    jz  .ret
    push dword [ebp+12]
    push 0
    push TBM_SETPAGESIZE
    push eax
    call _SendMessageA@16
.ret:
    pop ebp
    ret

global Slider_is_changed
Slider_is_changed:
    push ebp
    mov ebp, esp
    push esi
    mov eax, [ebp+8]
    test eax, eax
    jz  .zero
    mov esi, [eax+8]
    test esi, esi
    jz  .zero
    lea edx, [slider_table]
    xor ecx, ecx
.loop:
    cmp dword [edx+0], esi
    je  .found
    add edx, SLIDER_ENTRY
    inc ecx
    cmp ecx, MAX_SLIDERS
    jl  .loop
.zero:
    xor eax, eax
    jmp .done
.found:
    mov eax, [edx+12]
    mov dword [edx+12], 0
.done:
    pop esi
    pop ebp
    ret

global Slider_destroy
Slider_destroy:
    push ebp
    mov ebp, esp
    push esi
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov esi, [eax+8]
    mov dword [eax+8], 0
    test esi, esi
    jz  .ret
    lea edx, [slider_table]
    xor ecx, ecx
.loop:
    cmp dword [edx+0], esi
    je  .found
    add edx, SLIDER_ENTRY
    inc ecx
    cmp ecx, MAX_SLIDERS
    jl  .loop
    jmp .destroy
.found:
    mov dword [edx+0], 0
    mov dword [edx+4], 0
    mov dword [edx+8], 0
    mov dword [edx+12], 0
.destroy:
    push esi
    call _DestroyWindow@4
.ret:
    pop esi
    pop ebp
    ret

global Slider_move_int_int
Slider_move_int_int:
    jmp control_move_int_int
global Slider_set_size_int_int
Slider_set_size_int_int:
    jmp control_set_size_int_int
global Slider_show
Slider_show:
    jmp control_show
global Slider_hide
Slider_hide:
    jmp control_hide

; ============================================================
; Â≠êÁ±ªÂå? owner-draw ÂÖ•Âè£
; ============================================================
global ProgressBar_owner_draw
ProgressBar_owner_draw:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov eax, [eax+8]
    test eax, eax
    jz  .ret
    mov edx, [ebp+8]
    call subclass_install
.ret:
    pop ebp
    ret

global Slider_owner_draw
Slider_owner_draw:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov eax, [eax+8]
    test eax, eax
    jz  .ret
    mov edx, [ebp+8]
    call subclass_install
.ret:
    pop ebp
    ret

global SpinBox_owner_draw
SpinBox_owner_draw:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov eax, [eax+8]
    test eax, eax
    jz  .ret
    mov edx, [ebp+8]
    call subclass_install
.ret:
    pop ebp
    ret

global StatusBar_owner_draw
StatusBar_owner_draw:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov eax, [eax+8]
    test eax, eax
    jz  .ret
    mov edx, [ebp+8]
    call subclass_install
.ret:
    pop ebp
    ret

global GroupBox_owner_draw
GroupBox_owner_draw:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov eax, [eax+8]
    test eax, eax
    jz  .ret
    mov edx, [ebp+8]
    call subclass_install
.ret:
    pop ebp
    ret

; ============================================================
; ËæìÂÖ•Áä∂Ê??
; ============================================================
control_is_hover:
    push ebp
    mov ebp, esp
    push ebx
    mov eax, [ebp+8]
    test eax, eax
    jz  .zero
    mov ebx, [eax+8]
    test ebx, ebx
    jz  .zero
    lea eax, [pt_struct]
    push eax
    call _GetCursorPos@4
    push dword [pt_struct+4]
    push dword [pt_struct+0]
    call _WindowFromPoint@8
    cmp eax, ebx
    je  .one
.zero:
    xor eax, eax
    jmp .done
.one:
    mov eax, 1
.done:
    pop ebx
    pop ebp
    ret

control_is_down:
    push ebp
    mov ebp, esp
    push ebx
    mov eax, [ebp+8]
    test eax, eax
    jz  .zero
    mov ebx, [eax+8]
    test ebx, ebx
    jz  .zero
    lea eax, [pt_struct]
    push eax
    call _GetCursorPos@4
    push dword [pt_struct+4]
    push dword [pt_struct+0]
    call _WindowFromPoint@8
    cmp eax, ebx
    jne .zero
    push VK_LBUTTON
    call _GetAsyncKeyState@4
    and eax, 0x8000
    neg eax
    sbb eax, eax
    neg eax
    jmp .done
.zero:
    xor eax, eax
.done:
    pop ebx
    pop ebp
    ret

control_has_focus:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    test eax, eax
    jz  .zero
    mov eax, [eax+8]
    test eax, eax
    jz  .zero
    push eax
    call _GetFocus@0
    pop edx
    cmp eax, edx
    sete al
    movzx eax, al
    jmp .done
.zero:
    xor eax, eax
.done:
    pop ebp
    ret

control_refresh:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov eax, [eax+8]
    test eax, eax
    jz  .ret
    push 1
    push 0
    push eax
    call _InvalidateRect@12
.ret:
    pop ebp
    ret

; ============================================================
; Ëá™ÂÆö‰πâÁªòÂà?
; ============================================================
set_paint_impl:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov esi, eax
    push edx
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret_p
    mov ebx, [eax+8]
    test ebx, ebx
    jz  .ret_p
    xor ecx, ecx
.find:
    cmp dword [edi], ebx
    je  .found
    add edi, CONTROL_ENTRY
    inc ecx
    cmp ecx, MAX_CONTROLS
    jl  .find
    jmp .ret_p
.found:
    mov eax, [ebp+12]
    mov edx, [ebp-16]
    mov [edx+ecx*4], eax
    push GWL_STYLE
    push ebx
    call _GetWindowLongA@8
    mov edx, eax
    cmp dword [ebp+12], 0
    je  .remove
    or  edx, esi
    jmp .write
.remove:
    not esi
    and edx, esi
.write:
    push edx
    push GWL_STYLE
    push ebx
    call _SetWindowLongA@12
    push 1
    push 0
    push ebx
    call _InvalidateRect@12
.ret_p:
    add esp, 4
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

owner_draw_impl:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov esi, eax
    mov eax, [ebp+8]
    test eax, eax
    jz  .fail
    mov ebx, [eax+8]
    test ebx, ebx
    jz  .fail
    xor ecx, ecx
.find:
    cmp dword [edi], ebx
    je  .found
    add edi, CONTROL_ENTRY
    inc ecx
    cmp ecx, MAX_CONTROLS
    jl  .find
.fail:
    xor eax, eax
    jmp .done
.found:
    push GWL_STYLE
    push ebx
    call _GetWindowLongA@8
    or  eax, esi
    push eax
    push GWL_STYLE
    push ebx
    call _SetWindowLongA@12
    push 1
    push 0
    push ebx
    call _InvalidateRect@12
    mov eax, 1
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

global Button_owner_draw
Button_owner_draw:
    mov eax, BS_OWNERDRAW
    lea edi, [button_table]
    jmp owner_draw_impl

global CheckBox_owner_draw
CheckBox_owner_draw:
    mov eax, BS_OWNERDRAW
    lea edi, [checkbox_table]
    jmp owner_draw_impl

global RadioButton_owner_draw
RadioButton_owner_draw:
    mov eax, BS_OWNERDRAW
    lea edi, [radiobutton_table]
    jmp owner_draw_impl

global Label_owner_draw
Label_owner_draw:
    mov eax, SS_OWNERDRAW
    lea edi, [label_table]
    jmp owner_draw_impl

drawitem_dispatch_one:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    sub esp, 20
    mov [ebp-16], edx
    xor ecx, ecx
.loop:
    cmp dword [esi+0], ebx
    je  .found
    add esi, CONTROL_ENTRY
    inc ecx
    cmp ecx, MAX_CONTROLS
    jl  .loop
    xor eax, eax
    jmp .done
.found:
    mov eax, [edi+36]
    sub eax, [edi+28]
    mov [ebp-20], eax
    mov eax, [edi+40]
    sub eax, [edi+32]
    mov [ebp-24], eax
    mov edx, [ebp-16]
    mov eax, [edx + ecx*4]
    test eax, eax
    jz  .try_vtable
    push dword [edi+16]
    push dword [ebp-24]
    push dword [ebp-20]
    push dword [edi+32]
    push dword [edi+28]
    push dword [edi+24]
    call eax
    add esp, 24
    mov eax, 1
    jmp .done
.try_vtable:
    mov eax, [esi+16]
    test eax, eax
    jz  .fail
    mov ecx, [eax+4]
    test ecx, ecx
    jz  .fail
    mov ecx, [ecx]
    test ecx, ecx
    jz  .fail
    mov edx, [edi+16]
    mov eax, [edi+0]
    cmp eax, 3
    je  .encode_item
    cmp eax, 2
    je  .encode_item
    jmp .do_call
.encode_item:
    mov eax, [edi+8]
    and eax, 0xFFFF
    shl eax, 16
    and edx, 0xFFFF
    or  edx, eax
.do_call:
    push edx
    push dword [ebp-24]
    push dword [ebp-20]
    push dword [edi+32]
    push dword [edi+28]
    push dword [edi+24]
    push dword [esi+16]
    call ecx
    add esp, 28
    mov eax, 1
    jmp .done
.fail:
    xor eax, eax
.done:
    add esp, 20
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

drawitem_dispatch:
    push ebx
    push esi
    push edi
    mov edi, [ebp+20]
    test edi, edi
    jz  .not_found
    mov eax, [edi+0]
    cmp eax, ODT_MENU
    je  .menu
    mov ebx, [edi+20]
    test ebx, ebx
    jz  .not_found
    lea esi, [button_table]
    lea edx, [button_cb_table]
    call drawitem_dispatch_one
    test eax, eax
    jnz .found
    lea esi, [checkbox_table]
    lea edx, [checkbox_cb_table]
    call drawitem_dispatch_one
    test eax, eax
    jnz .found
    lea esi, [radiobutton_table]
    lea edx, [radiobutton_cb_table]
    call drawitem_dispatch_one
    test eax, eax
    jnz .found
    lea esi, [label_table]
    lea edx, [label_cb_table]
    call drawitem_dispatch_one
    test eax, eax
    jnz .found
    lea esi, [combo_table]
    lea edx, [combo_cb_table]
    call drawitem_dispatch_one
    test eax, eax
    jnz .found
    lea esi, [listbox_table]
    lea edx, [listbox_cb_table]
    call drawitem_dispatch_one
    test eax, eax
    jnz .found
    jmp .not_found
.menu:
    mov eax, [edi+20]
    call menu_entry_by_handle
    test eax, eax
    jz  .not_found
    mov esi, eax
    mov eax, [esi+28]
    test eax, eax
    jz  .not_found
    mov ecx, [eax+4]
    test ecx, ecx
    jz  .not_found
    mov ecx, [ecx]
    test ecx, ecx
    jz  .not_found
    cmp ecx, 0x00010000
    jb  .not_found
    mov edx, [edi+16]
    mov eax, [edi+8]
    and eax, 0xFFFF
    shl eax, 16
    and edx, 0xFFFF
    or  edx, eax
    push edx
    mov eax, [edi+40]
    sub eax, [edi+32]
    push eax
    mov eax, [edi+36]
    sub eax, [edi+28]
    push eax
    push dword [edi+32]
    push dword [edi+28]
    push dword [edi+24]
    push dword [esi+28]
    call ecx
    add esp, 28
    jmp .found
.not_found:
    xor eax, eax
    jmp .done
.found:
    mov eax, 1
.done:
    pop edi
    pop esi
    pop ebx
    ret

; ============================================================
; Ëá™ÁªòÁî? DC ÁªòÂõæ
; ============================================================
global Window_dc_fill_rect_int_int_int_int_int_int
Window_dc_fill_rect_int_int_int_int_int_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    mov esi, [ebp+8]
    mov eax, [ebp+12]
    mov [paint_rect+0], eax
    mov eax, [ebp+16]
    mov [paint_rect+4], eax
    mov eax, [ebp+12]
    add eax, [ebp+20]
    mov [paint_rect+8], eax
    mov eax, [ebp+16]
    add eax, [ebp+24]
    mov [paint_rect+12], eax
    mov eax, [ebp+28]
    call bgr_convert
    push eax
    call _CreateSolidBrush@4
    mov ebx, eax
    push ebx
    lea eax, [paint_rect]
    push eax
    push esi
    call _FillRect@12
    push ebx
    call _DeleteObject@4
    pop esi
    pop ebx
    pop ebp
    ret

global Window_dc_draw_rect_int_int_int_int_int_int
Window_dc_draw_rect_int_int_int_int_int_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    mov esi, [ebp+8]
    mov eax, [ebp+12]
    mov [paint_rect+0], eax
    mov eax, [ebp+16]
    mov [paint_rect+4], eax
    mov eax, [ebp+12]
    add eax, [ebp+20]
    mov [paint_rect+8], eax
    mov eax, [ebp+16]
    add eax, [ebp+24]
    mov [paint_rect+12], eax
    mov eax, [ebp+28]
    call bgr_convert
    push eax
    call _CreateSolidBrush@4
    mov ebx, eax
    push ebx
    lea eax, [paint_rect]
    push eax
    push esi
    call _FrameRect@12
    push ebx
    call _DeleteObject@4
    pop esi
    pop ebx
    pop ebp
    ret

global Window_dc_set_pixel_int_int_int_int
Window_dc_set_pixel_int_int_int_int:
    push ebp
    mov ebp, esp
    push esi
    mov esi, [ebp+8]
    mov eax, [ebp+20]
    call bgr_convert
    push eax
    push dword [ebp+16]
    push dword [ebp+12]
    push esi
    call _SetPixelV@16
    pop esi
    pop ebp
    ret

global Window_dc_draw_line_int_int_int_int_int_int
Window_dc_draw_line_int_int_int_int_int_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    mov esi, [ebp+8]
    mov eax, [ebp+28]
    call bgr_convert
    push eax
    push 1
    push PS_SOLID
    call _CreatePen@12
    mov ebx, eax
    push ebx
    push esi
    call _SelectObject@8
    push eax
    push 0
    push dword [ebp+16]
    push dword [ebp+12]
    push esi
    call _MoveToEx@16
    push dword [ebp+24]
    push dword [ebp+20]
    push esi
    call _LineTo@12
    pop eax
    push eax
    push esi
    call _SelectObject@8
    push ebx
    call _DeleteObject@4
    pop esi
    pop ebx
    pop ebp
    ret

global Window_dc_fill_circle_int_int_int_int_int
Window_dc_fill_circle_int_int_int_int_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    mov esi, [ebp+8]
    mov eax, [ebp+24]
    call bgr_convert
    push eax
    call _CreateSolidBrush@4
    mov ebx, eax
    push ebx
    push esi
    call _SelectObject@8
    push eax
    push NULL_PEN
    call _GetStockObject@4
    push eax
    push esi
    call _SelectObject@8
    push eax
    mov eax, [ebp+16]
    add eax, [ebp+20]
    push eax
    mov eax, [ebp+12]
    add eax, [ebp+20]
    push eax
    mov eax, [ebp+16]
    sub eax, [ebp+20]
    push eax
    mov eax, [ebp+12]
    sub eax, [ebp+20]
    push eax
    push esi
    call _Ellipse@20
    pop eax
    push eax
    push esi
    call _SelectObject@8
    pop eax
    push eax
    push esi
    call _SelectObject@8
    push ebx
    call _DeleteObject@4
    pop esi
    pop ebx
    pop ebp
    ret

global Window_dc_draw_circle_int_int_int_int_int
Window_dc_draw_circle_int_int_int_int_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    mov esi, [ebp+8]
    mov eax, [ebp+24]
    call bgr_convert
    push eax
    push 1
    push PS_SOLID
    call _CreatePen@12
    mov ebx, eax
    push ebx
    push esi
    call _SelectObject@8
    push eax
    push NULL_BRUSH
    call _GetStockObject@4
    push eax
    push esi
    call _SelectObject@8
    push eax
    mov eax, [ebp+16]
    add eax, [ebp+20]
    push eax
    mov eax, [ebp+12]
    add eax, [ebp+20]
    push eax
    mov eax, [ebp+16]
    sub eax, [ebp+20]
    push eax
    mov eax, [ebp+12]
    sub eax, [ebp+20]
    push eax
    push esi
    call _Ellipse@20
    pop eax
    push eax
    push esi
    call _SelectObject@8
    pop eax
    push eax
    push esi
    call _SelectObject@8
    push ebx
    call _DeleteObject@4
    pop esi
    pop ebx
    pop ebp
    ret

global Window_dc_draw_text_int_int_int_string_int
Window_dc_draw_text_int_int_int_string_int:
    push ebp
    mov ebp, esp
    push esi
    mov esi, [ebp+8]
    mov eax, [ebp+24]
    call bgr_convert
    push eax
    push esi
    call _SetTextColor@8
    push TRANSPARENT
    push esi
    call _SetBkMode@8
    call ensure_font
    push dword [font_handle]
    push esi
    call _SelectObject@8
    push eax
    mov eax, [ebp+20]
    lea edx, [paint_text_utf16]
    mov ecx, 1024
    call utf8_convert_to
    test eax, eax
    jz  .skip
    push ecx
    push eax
    push dword [ebp+16]
    push dword [ebp+12]
    push esi
    call _TextOutW@20
.skip:
    pop eax
    push eax
    push esi
    call _SelectObject@8
    pop esi
    pop ebp
    ret

global Window_dc_get_text_width_int_string
Window_dc_get_text_width_int_string:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    mov esi, [ebp+8]
    call ensure_font
    push dword [font_handle]
    push esi
    call _SelectObject@8
    push eax
    mov eax, [ebp+12]
    lea edx, [paint_text_utf16]
    mov ecx, 1024
    call utf8_convert_to
    test eax, eax
    jz  .zero
    lea edx, [size_struct]
    push edx
    push ecx
    push eax
    push esi
    call _GetTextExtentPoint32W@16
    mov ebx, [size_struct+0]
    jmp .restore
.zero:
    xor ebx, ebx
.restore:
    pop eax
    push eax
    push esi
    call _SelectObject@8
    mov eax, ebx
    pop esi
    pop ebx
    pop ebp
    ret

; ============================================================
; set_paint ÂÖ•Âè£
; ============================================================
global Button_set_paint_int
Button_set_paint_int:
    mov eax, BS_OWNERDRAW
    lea edi, [button_table]
    lea edx, [button_cb_table]
    jmp set_paint_impl
global CheckBox_set_paint_int
CheckBox_set_paint_int:
    mov eax, BS_OWNERDRAW
    lea edi, [checkbox_table]
    lea edx, [checkbox_cb_table]
    jmp set_paint_impl
global RadioButton_set_paint_int
RadioButton_set_paint_int:
    mov eax, BS_OWNERDRAW
    lea edi, [radiobutton_table]
    lea edx, [radiobutton_cb_table]
    jmp set_paint_impl
global Label_set_paint_int
Label_set_paint_int:
    mov eax, SS_OWNERDRAW
    lea edi, [label_table]
    lea edx, [label_cb_table]
    jmp set_paint_impl

; ============================================================
; get_text / Áä∂Ê?? / refresh ÂÖ•Âè£
; ============================================================
global Button_get_text
Button_get_text:
    jmp TextBox_get_text
global CheckBox_get_text
CheckBox_get_text:
    jmp TextBox_get_text
global RadioButton_get_text
RadioButton_get_text:
    jmp TextBox_get_text
global Label_get_text
Label_get_text:
    jmp TextBox_get_text

global Button_is_down
Button_is_down:
    jmp control_is_down
global CheckBox_is_down
CheckBox_is_down:
    jmp control_is_down
global RadioButton_is_down
RadioButton_is_down:
    jmp control_is_down

global Button_is_hover
Button_is_hover:
    jmp control_is_hover
global CheckBox_is_hover
CheckBox_is_hover:
    jmp control_is_hover
global RadioButton_is_hover
RadioButton_is_hover:
    jmp control_is_hover

global Button_has_focus
Button_has_focus:
    jmp control_has_focus
global CheckBox_has_focus
CheckBox_has_focus:
    jmp control_has_focus
global RadioButton_has_focus
RadioButton_has_focus:
    jmp control_has_focus
global TextBox_is_focused
TextBox_is_focused:
    jmp control_has_focus

global Button_refresh
Button_refresh:
    jmp control_refresh
global CheckBox_refresh
CheckBox_refresh:
    jmp control_refresh
global RadioButton_refresh
RadioButton_refresh:
    jmp control_refresh
global Label_refresh
Label_refresh:
    jmp control_refresh

global TextBox_is_changed
TextBox_is_changed:
    lea eax, [edit_table]
    jmp control_is_clicked

; ============================================================
; ÂºπÁ™óÁ≥ªÂàó
; ============================================================
popup_exec:
    push ebx
    push esi
    push edi
    mov ebx, eax
    mov eax, [ebp+8]
    lea edx, [popup_text_utf16]
    mov ecx, 1024
    call utf8_convert_to
    test eax, eax
    jz  .no_text
    mov esi, eax
.have_text:
    mov eax, [ebp+12]
    lea edx, [popup_title_utf16]
    mov ecx, 1024
    call utf8_convert_to
    test eax, eax
    jz  .no_title
    mov edi, eax
.have_title:
    push ebx
    push edi
    push esi
    push 0
    call _MessageBoxW@16
    jmp .done
.no_text:
    mov esi, empty_wide
    jmp .have_text
.no_title:
    mov edi, empty_wide
    jmp .have_title
.done:
    pop edi
    pop esi
    pop ebx
    ret

global Window_popup_info_string_string
Window_popup_info_string_string:
    push ebp
    mov ebp, esp
    mov eax, MB_ICONINFORMATION | MB_OK
    call popup_exec
    pop ebp
    ret

global Window_popup_warning_string_string
Window_popup_warning_string_string:
    push ebp
    mov ebp, esp
    mov eax, MB_ICONWARNING | MB_OK
    call popup_exec
    pop ebp
    ret

global Window_popup_error_string_string
Window_popup_error_string_string:
    push ebp
    mov ebp, esp
    mov eax, MB_ICONERROR | MB_OK
    call popup_exec
    pop ebp
    ret

global Window_popup_question_string_string
Window_popup_question_string_string:
    push ebp
    mov ebp, esp
    mov eax, MB_ICONQUESTION | MB_OK
    call popup_exec
    pop ebp
    ret

global Window_popup_yes_no_string_string
Window_popup_yes_no_string_string:
    push ebp
    mov ebp, esp
    mov eax, MB_ICONQUESTION | MB_YESNO
    call popup_exec
    pop ebp
    ret

global Window_popup_yes_no_cancel_string_string
Window_popup_yes_no_cancel_string_string:
    push ebp
    mov ebp, esp
    mov eax, MB_ICONQUESTION | MB_YESNOCANCEL
    call popup_exec
    pop ebp
    ret

global Window_popup_ok_cancel_string_string
Window_popup_ok_cancel_string_string:
    push ebp
    mov ebp, esp
    mov eax, MB_ICONINFORMATION | MB_OKCANCEL
    call popup_exec
    pop ebp
    ret

global Window_popup_retry_cancel_string_string
Window_popup_retry_cancel_string_string:
    push ebp
    mov ebp, esp
    mov eax, MB_ICONWARNING | MB_RETRYCANCEL
    call popup_exec
    pop ebp
    ret

; ============================================================
; InputBox
; ============================================================
register_inputbox_class:
    push ebx
    push esi
    push edi
    push 0
    call _GetModuleHandleA@4
    mov esi, eax
    push IDC_ARROW
    push 0
    call _LoadCursorA@8
    mov edi, eax
    push WHITE_BRUSH
    call _GetStockObject@4
    mov ebx, eax
    sub esp, 40
    mov edx, esp
    mov dword [edx+0], 3
    mov dword [edx+4], InputBoxWndProc
    mov dword [edx+8], 0
    mov dword [edx+12], 0
    mov [edx+16], esi
    mov dword [edx+20], 0
    mov [edx+24], edi
    mov [edx+28], ebx
    mov dword [edx+32], 0
    mov dword [edx+36], inputbox_class_name
    push edx
    call _RegisterClassA@4
    add esp, 40
    test eax, eax
    jz .fail
    mov dword [inputbox_class_registered], 1
    mov eax, 1
    jmp .done
.fail:
    xor eax, eax
.done:
    pop edi
    pop esi
    pop ebx
    ret

InputBoxWndProc:
    push ebp
    mov ebp, esp
    mov eax, [ebp+12]
    cmp eax, WM_COMMAND
    je  .command
    cmp eax, WM_CLOSE
    je  .cancel
    cmp eax, WM_DESTROY
    je  .destroy
    cmp eax, WM_SETFOCUS
    je  .setfocus
    cmp eax, WM_ACTIVATE
    je  .activate
    cmp eax, DM_GETDEFID
    je  .getdefid
.default:
    push dword [ebp+20]
    push dword [ebp+16]
    push dword [ebp+12]
    push dword [ebp+8]
    call _DefWindowProcA@16
    jmp .done
.command:
    mov eax, [ebp+16]
    and eax, 0xFFFF
    cmp eax, 1
    je  .ok
    cmp eax, 2
    je  .cancel
    jmp .default
.ok:
    mov dword [inputbox_done], 1
    xor eax, eax
    jmp .done
.cancel:
    mov dword [inputbox_done], -1
    xor eax, eax
    jmp .done
.destroy:
    xor eax, eax
    jmp .done
.setfocus:
    push 0x200
    push dword [ebp+8]
    call _GetDlgItem@8
    test eax, eax
    jz  .default
    push eax
    call _SetFocus@4
    xor eax, eax
    jmp .done
.activate:
    mov eax, [ebp+16]
    test eax, eax
    jz  .default
    push 0x200
    push dword [ebp+8]
    call _GetDlgItem@8
    test eax, eax
    jz  .default
    push eax
    call _SetFocus@4
    jmp .default
.getdefid:
    mov eax, DC_HASDEFID
    shl eax, 16
    or  eax, 1
    jmp .done
.done:
    pop ebp
    ret 16

global Window_popup_input_string_string
Window_popup_input_string_string:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    sub esp, 24
    mov eax, [ebp+8]
    mov [inputbox_prompt], eax
    mov eax, [ebp+12]
    mov [inputbox_title], eax
    cmp dword [inputbox_class_registered], 0
    jne .registered
    call register_inputbox_class
    test eax, eax
    jz  .fail
.registered:
    call _GetActiveWindow@0
    mov [ebp-20], eax
    push 0
    call _GetModuleHandleA@4
    mov esi, eax
    push 0
    call _GetSystemMetrics@4
    mov ebx, eax
    push 1
    call _GetSystemMetrics@4
    mov edi, eax
    mov eax, ebx
    sub eax, 340
    sar eax, 1
    mov ebx, eax
    mov eax, edi
    sub eax, 150
    sar eax, 1
    mov edi, eax
    push 0
    push esi
    push 0
    push dword [ebp-20]
    push 150
    push 340
    push edi
    push ebx
    push WS_CAPTION | WS_SYSMENU
    push empty_title
    push inputbox_class_name
    push 1
    call _CreateWindowExA@48
    test eax, eax
    jz  .fail
    mov [ebp-16], eax
    mov eax, [inputbox_title]
    lea edx, [popup_title_utf16]
    mov ecx, 1024
    call utf8_convert_to
    test eax, eax
    jz  .title_done
    push eax
    push dword [ebp-16]
    call _SetWindowTextW@8
.title_done:
    mov eax, [ebp-16]
    push 0
    push esi
    push -1
    push eax
    push 18
    push 320
    push 12
    push 10
    push WS_CHILD | WS_VISIBLE
    push empty_title
    push static_class_name
    push 0
    call _CreateWindowExA@48
    mov [ebp-24], eax
    mov eax, [inputbox_prompt]
    lea edx, [popup_text_utf16]
    mov ecx, 1024
    call utf8_convert_to
    test eax, eax
    jz  .prompt_done
    push eax
    push dword [ebp-24]
    call _SetWindowTextW@8
.prompt_done:
    mov eax, [ebp-16]
    push 0
    push esi
    push 0x200
    push eax
    push 24
    push 320
    push 38
    push 10
    push WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL
    push empty_title
    push edit_class_name
    push 0x200
    call _CreateWindowExA@48
    mov [inputbox_edit_hwnd], eax
    mov eax, [ebp-16]
    push 0
    push esi
    push 1
    push eax
    push 26
    push 80
    push 78
    push 150
    push WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON
    push ok_button_text
    push btn_class_name
    push 0
    call _CreateWindowExA@48
    mov eax, [ebp-16]
    push 0
    push esi
    push 2
    push eax
    push 26
    push 80
    push 78
    push 240
    push WS_CHILD | WS_VISIBLE | WS_TABSTOP
    push cancel_button_text
    push btn_class_name
    push 0
    call _CreateWindowExA@48
    mov dword [inputbox_done], 0
    push 0
    push dword [ebp-20]
    call _EnableWindow@8
    push SW_SHOW
    push dword [ebp-16]
    call _ShowWindow@8
    push dword [ebp-16]
    call _UpdateWindow@4
    push dword [ebp-16]
    call _SetForegroundWindow@4
    push dword [inputbox_edit_hwnd]
    call _SetFocus@4
.loop:
    push PM_REMOVE
    push 0
    push 0
    push 0
    lea eax, [msg_struct]
    push eax
    call _PeekMessageA@20
    test eax, eax
    jz  .check
    mov eax, [msg_struct+4]
    cmp eax, WM_QUIT
    je  .quit
    lea eax, [msg_struct]
    push eax
    push dword [ebp-16]
    call _IsDialogMessageA@8
    test eax, eax
    jnz .check
    lea eax, [msg_struct]
    push eax
    call _TranslateMessage@4
    lea eax, [msg_struct]
    push eax
    call _DispatchMessageA@4
.check:
    cmp dword [inputbox_done], 0
    je  .loop
    jmp .finished
.quit:
    mov dword [inputbox_done], -1
.finished:
    cmp dword [inputbox_done], 1
    jne .done_cancel
    push 0x200
    push dword [ebp-16]
    call _GetDlgItem@8
    test eax, eax
    jz  .no_dlgitem
    mov esi, eax
    lea eax, [inputbox_textbuf]
    push eax
    push 512
    push WM_GETTEXT
    push esi
    call _SendMessageA@16
    test eax, eax
    jg  .have_text
.no_text:
    mov byte [inputbox_textbuf], 0
    jmp .have_text
.no_dlgitem:
    mov byte [inputbox_textbuf], 0
    jmp .have_text
.done_cancel:
    push dword [ebp-16]
    call _DestroyWindow@4
    push 1
    push dword [ebp-20]
    call _EnableWindow@8
    push dword [ebp-20]
    call _SetForegroundWindow@4
    jmp .empty
.have_text:
    push dword [ebp-16]
    call _DestroyWindow@4
    push 1
    push dword [ebp-20]
    call _EnableWindow@8
    push dword [ebp-20]
    call _SetForegroundWindow@4
    push inputbox_textbuf
    call _lstrlenA@4
    test eax, eax
    jle .empty
    mov esi, eax
    inc esi
    push esi
    call _gc_alloc
    add esp, 4
    test eax, eax
    jz  .empty
    mov edi, eax
    lea esi, [inputbox_textbuf]
.copy:
    mov dl, byte [esi]
    mov byte [edi], dl
    inc esi
    inc edi
    test dl, dl
    jnz .copy
    jmp .done
.empty:
    push 1
    call _gc_alloc
    add esp, 4
    test eax, eax
    jz  .done
    mov byte [eax], 0
    jmp .done
.fail:
    push 1
    call _gc_alloc
    add esp, 4
    test eax, eax
    jz  .done
    mov byte [eax], 0
.done:
    add esp, 24
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

; ============================================================
; Canvas_load_string
; ============================================================
global Canvas_load_string
Canvas_load_string:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    sub esp, 24
    mov dword [ebp-24], 0
    mov esi, [ebp+8]
    test esi, esi
    jz  .fail
    mov eax, [esi+8]            ; Â∑≤Áªè +8
    test eax, eax
    jnz .have_handle
    call find_free_canvas_slot
    cmp eax, -1
    je  .fail
    mov dword [ebp-24], 1
    inc eax
    mov [esi+8], eax
.have_handle:
    mov eax, [esi+8]
    call get_canvas_offset
    cmp eax, -1
    je  .fail_auto
    lea edi, [canvas_table]
    add edi, eax
    cmp dword [ebp-24], 0
    jne .slot_ok
    cmp dword [edi+20], 0
    je  .fail_auto
.slot_ok:
    mov [ebp-16], edi
    lea eax, [img_pixels]
    push eax
    lea eax, [img_bpp]
    push eax
    lea eax, [img_height]
    push eax
    lea eax, [img_width]
    push eax
    push dword [ebp+12]
    call _image_load
    add esp, 20
    test eax, eax
    jz  .fail_load
    mov eax, [img_pixels]
    test eax, eax
    jz  .fail_load
    mov [ebp-20], eax
    cmp dword [img_width], 1
    jl  .fail_img
    cmp dword [img_height], 1
    jl  .fail_img
    mov dword [bmi_buffer+0], 40
    mov eax, [img_width]
    mov [bmi_buffer+4], eax
    mov eax, [img_height]
    neg eax
    mov [bmi_buffer+8], eax
    mov word  [bmi_buffer+12], 1
    mov word  [bmi_buffer+14], 32
    mov dword [bmi_buffer+16], 0
    mov dword [bmi_buffer+20], 0
    mov dword [bmi_buffer+24], 0
    mov dword [bmi_buffer+28], 0
    mov dword [bmi_buffer+32], 0
    mov dword [bmi_buffer+36], 0
    push 0
    call _GetDC@4
    mov ebx, eax
    push eax
    call _CreateCompatibleDC@4
    mov [new_dc_save], eax
    push 0
    push 0
    lea eax, [dib_bits]
    push eax
    push DIB_RGB_COLORS
    lea eax, [bmi_buffer]
    push eax
    push ebx
    call _CreateDIBSection@24
    test eax, eax
    jz  .fail_dib
    mov [new_bitmap_save], eax
    push ebx
    push 0
    call _ReleaseDC@8
    push dword [new_bitmap_save]
    push dword [new_dc_save]
    call _SelectObject@8
    mov [new_oldbitmap_save], eax
    mov edi, [ebp-16]
    push dword [edi+16]
    push dword [edi+8]
    call _SelectObject@8
    push dword [edi+12]
    call _DeleteObject@4
    push dword [edi+8]
    call _DeleteDC@4
    mov eax, [new_dc_save]
    mov [edi+8], eax
    mov eax, [new_bitmap_save]
    mov [edi+12], eax
    mov eax, [new_oldbitmap_save]
    mov [edi+16], eax
    mov eax, [dib_bits]
    mov [edi+24], eax
    mov dword [edi+20], 1
    mov eax, [img_width]
    mov [edi+0], eax
    mov eax, [img_height]
    mov [edi+4], eax
    mov edi, [ebp-16]
    mov edi, [edi+24]
    mov esi, [ebp-20]
    mov dword [img_y], 0
.row_loop:
    mov eax, [img_y]
    cmp eax, [img_height]
    jge .copy_done
    xor ecx, ecx
.pix_loop:
    movzx eax, byte [esi+0]
    shl eax, 16
    movzx edx, byte [esi+1]
    shl edx, 8
    or  eax, edx
    movzx edx, byte [esi+2]
    or  eax, edx
    movzx edx, byte [esi+3]
    shl edx, 24
    or  eax, edx
    mov [edi], eax
    add esi, 4
    add edi, 4
    inc ecx
    cmp ecx, [img_width]
    jl  .pix_loop
.next_row:
    inc dword [img_y]
    jmp .row_loop
.copy_done:
    push dword [ebp-20]
    call _image_free
    add esp, 4
    mov eax, 1
    jmp .done
.fail_dib:
    push dword [new_dc_save]
    call _DeleteDC@4
    push dword [ebp-20]
    call _image_free
    add esp, 4
    call release_auto_slot
    mov eax, 3
    jmp .done
.fail_img:
    push dword [ebp-20]
    call _image_free
    add esp, 4
    call release_auto_slot
    mov eax, 3
    jmp .done
.fail_load:
    call release_auto_slot
    mov eax, 2
    jmp .done
.fail_auto:
    call release_auto_slot
.fail:
    xor eax, eax
.done:
    add esp, 24
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

release_auto_slot:
    cmp dword [ebp-24], 0
    je  .ras_ret
    mov dword [esi+8], 0
.ras_ret:
    ret

; ============================================================
; ÂÉèÁ¥†Á∫ßË¥¥Âõ?
; ============================================================

blit_fetch_targets:
    mov eax, [ebp+8]
    test eax, eax
    jz  .bad
    mov eax, [eax+8]            ; this->hwndÔºàÂéü‰∏? [eax]Ôº?
    call get_slot_offset
    cmp eax, -1
    je  .bad
    lea edx, [window_table]
    add edx, eax
    cmp dword [edx+24], 0
    je  .bad
    mov eax, [edx+120]
    test eax, eax
    jz  .bad
    mov [ebp-16], eax
    mov eax, [edx+4]
    mov [ebp-24], eax
    mov eax, [edx+8]
    mov [ebp-28], eax
    mov eax, [ebp+12]
    test eax, eax
    jz  .bad
    mov eax, [eax+8]            ; Canvas Áö? id ‰πüÂú® +8
    call get_canvas_offset
    cmp eax, -1
    je  .bad
    lea edx, [canvas_table]
    add edx, eax
    cmp dword [edx+20], 0
    je  .bad
    mov eax, [edx+24]
    test eax, eax
    jz  .bad
    mov [ebp-20], eax
    mov eax, [edx+0]
    mov [ebp-32], eax
    mov eax, [edx+4]
    mov [ebp-36], eax
    mov eax, 1
    ret
.bad:
    xor eax, eax
    ret

blit_core_setup:
    cmp dword [ebp-48], 0
    jle .no_draw
    cmp dword [ebp-52], 0
    jle .no_draw
    cmp dword [ebp-40], 0
    jge .sy
    mov eax, [ebp-40]
    add [ebp-48], eax
    mov dword [ebp-40], 0
.sy:
    cmp dword [ebp-44], 0
    jge .dim
    mov eax, [ebp-44]
    add [ebp-52], eax
    mov dword [ebp-44], 0
.dim:
    mov eax, [ebp-40]
    add eax, [ebp-48]
    cmp eax, [ebp-32]
    jle .dim2
    mov eax, [ebp-32]
    sub eax, [ebp-40]
    mov [ebp-48], eax
.dim2:
    mov eax, [ebp-44]
    add eax, [ebp-52]
    cmp eax, [ebp-36]
    jle .dclamp
    mov eax, [ebp-36]
    sub eax, [ebp-44]
    mov [ebp-52], eax
.dclamp:
    cmp dword [ebp-56], 0
    jge .dy
    mov eax, [ebp-56]
    add [ebp-48], eax
    sub [ebp-40], eax
    mov dword [ebp-56], 0
.dy:
    cmp dword [ebp-60], 0
    jge .ddim
    mov eax, [ebp-60]
    add [ebp-52], eax
    sub [ebp-44], eax
    mov dword [ebp-60], 0
.ddim:
    mov eax, [ebp-56]
    add eax, [ebp-48]
    cmp eax, [ebp-24]
    jle .ddim2
    mov eax, [ebp-24]
    sub eax, [ebp-56]
    mov [ebp-48], eax
.ddim2:
    mov eax, [ebp-60]
    add eax, [ebp-52]
    cmp eax, [ebp-28]
    jle .start
    mov eax, [ebp-28]
    sub eax, [ebp-60]
    mov [ebp-52], eax
.start:
    cmp dword [ebp-48], 0
    jle .no_draw
    cmp dword [ebp-52], 0
    jle .no_draw
    mov eax, [ebp-40]
    shl eax, 2
    mov ebx, [ebp-44]
    imul ebx, [ebp-32]
    shl ebx, 2
    add ebx, eax
    mov esi, [ebp-20]
    add esi, ebx
    mov eax, [ebp-56]
    shl eax, 2
    mov ebx, [ebp-60]
    imul ebx, [ebp-24]
    shl ebx, 2
    add ebx, eax
    mov edi, [ebp-16]
    add edi, ebx
    mov ebx, [ebp-52]
    mov eax, 1
    ret
.no_draw:
    xor eax, eax
    ret

blit_part_core:
    call blit_core_setup
    test eax, eax
    jz  .done_b
.row_b:
    mov ecx, [ebp-48]
    cld
    rep movsd
    mov eax, [ebp-32]
    sub eax, [ebp-48]
    shl eax, 2
    add esi, eax
    mov eax, [ebp-24]
    sub eax, [ebp-48]
    shl eax, 2
    add edi, eax
    dec ebx
    jnz .row_b
.done_b:
    add esp, 60
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

blit_part_core_t:
    call blit_core_setup
    test eax, eax
    jz  .done_t
.row_t:
    mov ecx, [ebp-48]
.px_t:
    mov eax, [esi]
    mov edx, eax
    and edx, 0x00FFFFFF
    cmp edx, 0x00FF00FF
    je  .skip_t
    mov [edi], eax
.skip_t:
    add esi, 4
    add edi, 4
    dec ecx
    jnz .px_t
    mov eax, [ebp-32]
    sub eax, [ebp-48]
    shl eax, 2
    add esi, eax
    mov eax, [ebp-24]
    sub eax, [ebp-48]
    shl eax, 2
    add edi, eax
    dec ebx
    jnz .row_t
.done_t:
    add esp, 60
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

global Window_draw_canvas_part_identifier_int_int_int_int_int_int
Window_draw_canvas_part_identifier_int_int_int_int_int_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    sub esp, 60
    call blit_fetch_targets
    test eax, eax
    jz  .ret_x
    mov eax, [ebp+16]
    mov [ebp-40], eax
    mov eax, [ebp+20]
    mov [ebp-44], eax
    mov eax, [ebp+24]
    mov [ebp-48], eax
    mov eax, [ebp+28]
    mov [ebp-52], eax
    mov eax, [ebp+32]
    mov [ebp-56], eax
    mov eax, [ebp+36]
    mov [ebp-60], eax
    jmp blit_part_core
.ret_x:
    add esp, 60
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

global Window_draw_canvas_part_transparent_identifier_int_int_int_int_int_int
Window_draw_canvas_part_transparent_identifier_int_int_int_int_int_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    sub esp, 60
    call blit_fetch_targets
    test eax, eax
    jz  .ret_x
    mov eax, [ebp+16]
    mov [ebp-40], eax
    mov eax, [ebp+20]
    mov [ebp-44], eax
    mov eax, [ebp+24]
    mov [ebp-48], eax
    mov eax, [ebp+28]
    mov [ebp-52], eax
    mov eax, [ebp+32]
    mov [ebp-56], eax
    mov eax, [ebp+36]
    mov [ebp-60], eax
    jmp blit_part_core_t
.ret_x:
    add esp, 60
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

global Window_draw_canvas_transparent_identifier_int_int
Window_draw_canvas_transparent_identifier_int_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    sub esp, 60
    call blit_fetch_targets
    test eax, eax
    jz  .ret_x
    mov dword [ebp-40], 0
    mov dword [ebp-44], 0
    mov eax, [ebp-32]
    mov [ebp-48], eax
    mov eax, [ebp-36]
    mov [ebp-52], eax
    mov eax, [ebp+16]
    mov [ebp-56], eax
    mov eax, [ebp+20]
    mov [ebp-60], eax
    jmp blit_part_core_t
.ret_x:
    add esp, 60
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

global Window_draw_canvas_scaled_identifier_int_int_int_int
Window_draw_canvas_scaled_identifier_int_int_int_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov eax, [ebp+8]
    test eax, eax
    jz  .ret
    mov eax, [eax+8]
    call get_slot_offset
    cmp eax, -1
    je  .ret
    lea esi, [window_table]
    add esi, eax
    cmp dword [esi+24], 0
    je  .ret
    mov eax, [ebp+12]
    test eax, eax
    jz  .ret
    mov eax, [eax+8]            ; Canvas id ‰πüÂú® +8
    call get_canvas_offset
    cmp eax, -1
    je  .ret
    lea edi, [canvas_table]
    add edi, eax
    cmp dword [edi+20], 0
    je  .ret
    push SRCCOPY
    push dword [edi+4]
    push dword [edi+0]
    push 0
    push 0
    push dword [edi+8]
    push dword [ebp+28]
    push dword [ebp+24]
    push dword [ebp+20]
    push dword [ebp+16]
    push dword [esi+12]
    call _StretchBlt@44
.ret:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

; ============================================================
; Window_set_maximizable / Window_set_resizable
; ============================================================
global Window_set_maximizable_int
Window_set_maximizable_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov esi, [ebp+8]
    test esi, esi
    jz  .ret
    mov eax, [esi+8]
    test eax, eax
    jz  .ret
    call get_slot_offset
    cmp eax, -1
    je  .ret
    lea edi, [window_table]
    add edi, eax
    mov ebx, [edi+0]
    push GWL_STYLE
    push ebx
    call _GetWindowLongA@8
    mov esi, eax
    cmp dword [ebp+12], 0
    je  .disable
    or  esi, WS_MAXIMIZEBOX
    jmp .write
.disable:
    and esi, ~WS_MAXIMIZEBOX
.write:
    push esi
    push GWL_STYLE
    push ebx
    call _SetWindowLongA@12
    push SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED
    push 0
    push 0
    push 0
    push 0
    push 0
    push ebx
    call _SetWindowPos@28
.ret:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

global Window_set_resizable_int
Window_set_resizable_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov esi, [ebp+8]
    test esi, esi
    jz  .ret
    mov eax, [esi+8]
    test eax, eax
    jz  .ret
    call get_slot_offset
    cmp eax, -1
    je  .ret
    lea edi, [window_table]
    add edi, eax
    mov ebx, [edi+0]
    push GWL_STYLE
    push ebx
    call _GetWindowLongA@8
    mov esi, eax
    cmp dword [ebp+12], 0
    je  .disable
    or  esi, WS_THICKFRAME
    jmp .write
.disable:
    and esi, ~WS_THICKFRAME
.write:
    push esi
    push GWL_STYLE
    push ebx
    call _SetWindowLongA@12
    push SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED
    push 0
    push 0
    push 0
    push 0
    push 0
    push ebx
    call _SetWindowPos@28
.ret:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

; ============================================================
; Window_set_fullscreen : «–ªªŒﬁ±ﬂøÚ»´∆¡
;   this, enable(1=Ω¯»Î, 0=ÕÀ≥ˆ)
;   ≤€Œª +132 ¥ÊΩ¯»Î«∞µƒ—˘ Ω£®∑« 0 ±Ì æµ±«∞¥¶”⁄»´∆¡£©
;   ≤€Œª +136..151 ¥ÊΩ¯»Î«∞µƒ¥∞ø⁄æÿ–Œ
; ============================================================
global Window_set_fullscreen_int
Window_set_fullscreen_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov esi, [ebp+8]
    test esi, esi
    jz  .fs_ret
    mov eax, [esi+8]
    test eax, eax
    jz  .fs_ret
    call get_slot_offset
    cmp eax, -1
    je  .fs_ret
    lea edi, [window_table]
    add edi, eax
    mov ebx, [edi+0]
    mov eax, [ebp+12]
    test ebx, ebx
    jz  .fs_ret

    cmp dword [ebp+12], 0
    je  .fs_exit

    ; ---- Ω¯»Î»´∆¡ ----
    cmp dword [edi+132], 0
    jne .fs_ret
    push GWL_STYLE
    push ebx
    call _GetWindowLongA@8
    mov [edi+132], eax
    lea eax, [edi+136]
    push eax
    push ebx
    call _GetWindowRect@8

    ; œ»»•±ﬂøÚ
    push GWL_STYLE
    push ebx
    call _GetWindowLongA@8
    and eax, ~(WS_OVERLAPPEDWINDOW)
    or  eax, WS_POPUP | WS_VISIBLE
    push eax
    push GWL_STYLE
    push ebx
    call _SetWindowLongA@12

    ; »°∆¡ƒªøÌ∏ﬂ£®GetSystemMetrics ª·∆∆ªµ edx/ecx£¨±ÿ–Îº∞ ±—π’ª£©
    push 0
    call _GetSystemMetrics@4
    push eax                      ; ∆¡ƒªøÌ
    push 1
    call _GetSystemMetrics@4
    push eax                      ; ∆¡ƒª∏ﬂ

    ; SetWindowPos(hwnd, HWND_TOP, 0, 0, cx, cy, flags)
    push SWP_FRAMECHANGED | SWP_NOZORDER | SWP_SHOWWINDOW
    push dword [esp+4]            ; cy = ∆¡ƒª∏ﬂ
    push dword [esp+12]           ; cx = ∆¡ƒªøÌ£®◊¢“‚ esp “—“Ú…œ“ªÃı push “∆∂Ø£©
    push 0                        ; y
    push 0                        ; x
    push HWND_TOP
    push ebx
    call _SetWindowPos@28
    add  esp, 8                   ; ∂™µÙ¡Ω∏ˆ∆¡ƒª≥ﬂ¥Á
    jmp  .fs_ret

.fs_exit:
    cmp dword [edi+132], 0
    je  .fs_ret
    push dword [edi+132]
    push GWL_STYLE
    push ebx
    call _SetWindowLongA@12
    mov dword [edi+132], 0
    lea eax, [edi+136]
    mov ecx, [eax+0]          ; left
    mov edx, [eax+4]          ; top
    mov esi, [eax+8]          ; right
    mov edi, [eax+12]         ; bottom
    sub esi, ecx              ; cx = right - left
    sub edi, edx              ; cy = bottom - top
    push SWP_NOZORDER | SWP_FRAMECHANGED
    push edi                  ; cy
    push esi                  ; cx
    push edx                  ; y
    push ecx                  ; x
    push HWND_TOP
    push ebx
    call _SetWindowPos@28
.fs_ret:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret


; ============================================================
; ÂûÇÁõ¥Ê∏êÂèòÂ°´ÂÖÖÔºàËá™Áª? DC Áî®Ôºâ
; ============================================================
global Window_dc_fill_gradient_int_int_int_int_int_int_int
Window_dc_fill_gradient_int_int_int_int_int_int_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    sub esp, 48
    mov esi, [ebp+8]
    mov eax, [ebp+24]
    test eax, eax
    jle .done
    mov [ebp-16], eax

    mov eax, [ebp+12]
    mov [ebp-32], eax
    mov eax, [ebp+12]
    add eax, [ebp+20]
    mov [ebp-24], eax

    mov eax, [ebp+28]
    mov ebx, eax
    shr ebx, 16
    and ebx, 0xFF
    mov [ebp-36], ebx
    mov ebx, eax
    shr ebx, 8
    and ebx, 0xFF
    mov [ebp-40], ebx
    mov ebx, eax
    and ebx, 0xFF
    mov [ebp-44], ebx

    mov eax, [ebp+32]
    mov ebx, eax
    shr ebx, 16
    and ebx, 0xFF
    sub ebx, [ebp-36]
    mov [ebp-48], ebx
    mov ebx, eax
    shr ebx, 8
    and ebx, 0xFF
    sub ebx, [ebp-40]
    mov [ebp-52], ebx
    mov ebx, eax
    and ebx, 0xFF
    sub ebx, [ebp-44]
    mov [ebp-56], ebx

    xor edi, edi
.row_loop:
    mov eax, edi
    shl eax, 8
    xor edx, edx
    div dword [ebp-16]
    mov ecx, eax

    mov eax, [ebp-48]
    imul ecx
    sar eax, 8
    add eax, [ebp-36]
    and eax, 0xFF
    mov ebx, eax
    shl ebx, 16

    mov eax, [ebp-52]
    imul ecx
    sar eax, 8
    add eax, [ebp-40]
    and eax, 0xFF
    shl eax, 8
    or ebx, eax

    mov eax, [ebp-56]
    imul ecx
    sar eax, 8
    add eax, [ebp-44]
    and eax, 0xFF
    or ebx, eax

    mov eax, ebx
    call bgr_convert

    mov ebx, [ebp+16]
    add ebx, edi
    mov [ebp-28], ebx
    inc ebx
    mov [ebp-20], ebx

    push eax
    call _CreateSolidBrush@4
    mov ebx, eax

    push ebx
    lea eax, [ebp-32]
    push eax
    push esi
    call _FillRect@12

    push ebx
    call _DeleteObject@4

    inc edi
    cmp edi, [ebp-16]
    jl .row_loop

.done:
    add esp, 48
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

; ============================================================
; ÂúÜËßíÊ∏êÂèòÂ°´ÂÖÖ
; ============================================================
global Window_dc_fill_gradient_round_int_int_int_int_int_int_int_int
Window_dc_fill_gradient_round_int_int_int_int_int_int_int_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi

    mov esi, [ebp+8]

    mov eax, [ebp+28]
    shl eax, 1
    push eax
    push eax
    mov eax, [ebp+16]
    add eax, [ebp+24]
    push eax
    mov eax, [ebp+12]
    add eax, [ebp+20]
    push eax
    push dword [ebp+16]
    push dword [ebp+12]
    call _CreateRoundRectRgn@24
    mov ebx, eax

    push ebx
    push esi
    call _SelectClipRgn@8

    push dword [ebp+36]
    push dword [ebp+32]
    push dword [ebp+24]
    push dword [ebp+20]
    push dword [ebp+16]
    push dword [ebp+12]
    push dword [ebp+8]
    call Window_dc_fill_gradient_int_int_int_int_int_int_int
    add esp, 28

    push 0
    push esi
    call _SelectClipRgn@8

    push ebx
    call _DeleteObject@4

    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

; ============================================================
; Áú? Alpha Ê∑∑ÂêàÊ†∏ÂøÉ
; ============================================================
blit_part_core_alpha:
    call blit_core_setup
    test eax, eax
    jz  .done_a
    sub esp, 16
    mov [ebp-76], ebx
.row_a:
    mov ecx, [ebp-48]
.px_a:
    mov eax, [esi]
    mov edx, eax
    shr edx, 24
    jz  .next_a
    cmp edx, 0xFF
    je  .opaque_a

    mov [ebp-64], edx
    mov ebx, 255
    sub ebx, edx
    mov [ebp-68], ebx

    xor ebx, ebx

    mov eax, [esi]
    shr eax, 16
    and eax, 0xFF
    imul dword [ebp-64]
    mov [ebp-72], eax
    mov eax, [edi]
    shr eax, 16
    and eax, 0xFF
    imul dword [ebp-68]
    add eax, [ebp-72]
    shr eax, 8
    and eax, 0xFF
    shl eax, 16
    or ebx, eax

    mov eax, [esi]
    shr eax, 8
    and eax, 0xFF
    imul dword [ebp-64]
    mov [ebp-72], eax
    mov eax, [edi]
    shr eax, 8
    and eax, 0xFF
    imul dword [ebp-68]
    add eax, [ebp-72]
    shr eax, 8
    and eax, 0xFF
    shl eax, 8
    or ebx, eax

    mov eax, [esi]
    and eax, 0xFF
    imul dword [ebp-64]
    mov [ebp-72], eax
    mov eax, [edi]
    and eax, 0xFF
    imul dword [ebp-68]
    add eax, [ebp-72]
    shr eax, 8
    and eax, 0xFF
    or ebx, eax

    or ebx, 0xFF000000
    mov [edi], ebx
    jmp .next_a

.opaque_a:
    mov eax, [esi]
    mov [edi], eax

.next_a:
    add esi, 4
    add edi, 4
    dec ecx
    jnz .px_a

    mov eax, [ebp-32]
    sub eax, [ebp-48]
    shl eax, 2
    add esi, eax
    mov eax, [ebp-24]
    sub eax, [ebp-48]
    shl eax, 2
    add edi, eax
    dec dword [ebp-76]
    jnz .row_a

    add esp, 16
.done_a:
    add esp, 60
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

global Window_draw_canvas_alpha_identifier_int_int
Window_draw_canvas_alpha_identifier_int_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    sub esp, 60
    call blit_fetch_targets
    test eax, eax
    jz  .ret_x
    mov dword [ebp-40], 0
    mov dword [ebp-44], 0
    mov eax, [ebp-32]
    mov [ebp-48], eax
    mov eax, [ebp-36]
    mov [ebp-52], eax
    mov eax, [ebp+16]
    mov [ebp-56], eax
    mov eax, [ebp+20]
    mov [ebp-60], eax
    jmp blit_part_core_alpha
.ret_x:
    add esp, 60
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

global Window_draw_canvas_part_alpha_identifier_int_int_int_int_int_int
Window_draw_canvas_part_alpha_identifier_int_int_int_int_int_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    sub esp, 60
    call blit_fetch_targets
    test eax, eax
    jz  .ret_x
    mov eax, [ebp+16]
    mov [ebp-40], eax
    mov eax, [ebp+20]
    mov [ebp-44], eax
    mov eax, [ebp+24]
    mov [ebp-48], eax
    mov eax, [ebp+28]
    mov [ebp-52], eax
    mov eax, [ebp+32]
    mov [ebp-56], eax
    mov eax, [ebp+36]
    mov [ebp-60], eax
    jmp blit_part_core_alpha
.ret_x:
    add esp, 60
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

global ComboBox_owner_draw
ComboBox_owner_draw:
    mov eax, CBS_OWNERDRAWFIXED
    lea edi, [combo_table]
    jmp owner_draw_impl

global ListBox_owner_draw
ListBox_owner_draw:
    mov eax, LBS_OWNERDRAWFIXED
    lea edi, [listbox_table]
    jmp owner_draw_impl

global Toolbar_owner_draw
Toolbar_owner_draw:
    push ebp
    mov ebp, esp
    push esi
    push edi
    mov esi, [ebp+8]
    test esi, esi
    jz  .ret
    mov eax, [esi+8]
    test eax, eax
    jz  .ret
    lea edi, [toolbar_table]
    xor ecx, ecx
.find:
    cmp dword [edi+0], eax
    je  .found
    add edi, TB_ENTRY
    inc ecx
    cmp ecx, MAX_TOOLBARS
    jl  .find
    jmp .ret
.found:
    mov edx, [edi+20]
    test edx, edx
    jz  .use_this
    jmp .install
.use_this:
    mov edx, esi
.install:
    call subclass_install
.ret:
    pop edi
    pop esi
    pop ebp
    ret
