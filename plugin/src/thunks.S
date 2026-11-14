# Register-preserving vtable thunks. Each saves the four integer argument
# registers, rax and xmm0-3, calls cm_on_slot(index, this), restores everything
# and tail-jumps to the original slot target, so the game's function sees
# exactly the registers and stack it would have seen without the plugin.
.intel_syntax noprefix
.text
.macro THUNK idx
.globl cm_thunk\idx
.def cm_thunk\idx; .scl 2; .type 32; .endef
.seh_proc cm_thunk\idx
cm_thunk\idx:
    push rcx
    .seh_pushreg rcx
    push rdx
    .seh_pushreg rdx
    push r8
    .seh_pushreg r8
    push r9
    .seh_pushreg r9
    push rax
    .seh_pushreg rax
    sub rsp, 0x60
    .seh_stackalloc 0x60
    .seh_endprologue
    movdqu [rsp+0x20], xmm0
    movdqu [rsp+0x30], xmm1
    movdqu [rsp+0x40], xmm2
    movdqu [rsp+0x50], xmm3
    mov rdx, rcx
    mov ecx, \idx
    call cm_on_slot
    movdqu xmm0, [rsp+0x20]
    movdqu xmm1, [rsp+0x30]
    movdqu xmm2, [rsp+0x40]
    movdqu xmm3, [rsp+0x50]
    add rsp, 0x60
    pop rax
    pop r9
    pop r8
    pop rdx
    pop rcx
    jmp qword ptr [rip + cm_orig + 8*\idx]
.seh_endproc
.endm
THUNK 0
THUNK 1
THUNK 2
THUNK 3
THUNK 4
