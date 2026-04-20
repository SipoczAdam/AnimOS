; --- Multiboot 2 Header ---
section .multiboot_header
align 8
header_start:
    dd 0xe85250d6                ; magic number
    dd 0                         ; architecture 0 (i386)
    dd header_end - header_start ; header length
    ; Checksum
    dd 0x100000000 - (0xe85250d6 + 0 + (header_end - header_start))

    ; Framebuffer tag
    align 8
    dw 5                         ; type 5
    dw 0                         ; flags
    dd 20                        ; size
    dd 1024                      ; width
    dd 768                       ; height
    dd 32                        ; depth

    ; Information Request Tag (Biztos ami biztos)
    align 8
    dw 1                         ; type 1
    dw 0                         ; flags
    dd 12                        ; size
    dd 8                         ; request framebuffer
    dd 0                         ; padding to 8 bytes

    ; End tag
    align 8
    dw 0
    dw 0
    dd 8
header_end:

section .text
[BITS 32]
global _start
extern kernel_main

_start:
    cli
    mov esp, stack_top
    mov [multiboot_ptr], ebx

    call check_cpuid
    call check_long_mode
    call setup_page_tables
    call enable_paging

    lgdt [gdt64.pointer]
    jmp gdt64.code:long_mode_start

check_cpuid:
    pushfd
    pop eax
    mov ecx, eax
    xor eax, 1 << 21
    push eax
    popfd
    pushfd
    pop eax
    push ecx
    popfd
    cmp eax, ecx
    je .no_cpuid
    ret
.no_cpuid:
    hlt

check_long_mode:
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .no_long_mode
    mov eax, 0x80000001
    cpuid
    test edx, 1 << 29
    jz .no_long_mode
    ret
.no_long_mode:
    hlt

setup_page_tables:
    ; PML4 (1) + PDPT (1) + PDs (512) = 514 oldalt nullázunk
    mov edi, pml4_table
    xor eax, eax
    mov ecx, 514 * 1024
    rep stosd

    ; PML4 -> PDPT
    mov eax, pdpt_table
    or eax, 0b11
    mov [pml4_table], eax

    ; PDPT -> 512 PD tábla (0-512GB)
    mov ecx, 0
.map_pdpt:
    mov eax, 4096
    mul ecx
    add eax, pd_tables
    adc edx, 0
    or eax, 0b11
    mov [pdpt_table + ecx * 8], eax
    mov [pdpt_table + ecx * 8 + 4], edx
    inc ecx
    cmp ecx, 512
    jne .map_pdpt

    ; PD-k feltöltése (Identity mapping 2MB lapokkal)
    mov ecx, 0
.map_pds:
    mov eax, 0x200000
    mul ecx
    or eax, 0b10000011
    mov [pd_tables + ecx * 8], eax
    mov [pd_tables + ecx * 8 + 4], edx
    inc ecx
    cmp ecx, 512 * 512
    jne .map_pds
    ret

enable_paging:
    mov eax, pml4_table
    mov cr3, eax
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax
    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr
    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax
    ret

[BITS 64]
extern mouse_handler_main
global isr_mouse_stub
isr_mouse_stub:
    push rax
    push rcx
    push rdx
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    call mouse_handler_main
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rax
    iretq

global idt_load
idt_load:
    lidt [rdi]
    ret

long_mode_start:
    mov ax, gdt64.data
    mov ss, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov rsp, stack_top

    xor rdi, rdi
    mov edi, [multiboot_ptr]
    call kernel_main
    hlt

section .bss
align 4096
pml4_table:
    resb 4096
pdpt_table:
    resb 4096
pd_tables:
    resb 4096 * 512
align 16
multiboot_ptr:
    resd 1
    resb 12
stack_bottom:
    resb 16384
stack_top:

section .rodata
align 8
gdt64:
    dq 0
.code: equ $ - gdt64
    dq (1<<43) | (1<<44) | (1<<47) | (1<<53)
.data: equ $ - gdt64
    dq (1<<44) | (1<<47) | (1<<41)
.pointer:
    dw $ - gdt64 - 1
    dq gdt64

align 8
global cursor_data
cursor_data:
    incbin "sysroot/AnimOS/assets/cursor/Default/Normal Select.cur"
cursor_end:

align 8
global wallpaper_data
wallpaper_data:
    incbin "sysroot/AnimOS/assets/wallpapers/bubble.bmp"
wallpaper_end:
