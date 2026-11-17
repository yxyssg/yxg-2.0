section .data
    bounds_msg db `Error: Array index out of bounds\n`, 0
    null_msg db `Error: null pointer dereference\n`, 0

section .text
extern _printf
extern _ExitProcess@4
global _array_bounds_error

_array_bounds_error:
    push bounds_msg
    call _printf
    add esp, 4
    push 1
    call _ExitProcess@4
global _null_ptr_error
_null_ptr_error:
    push null_msg
    call _printf
    add esp, 4
    push 1
    call _ExitProcess@4