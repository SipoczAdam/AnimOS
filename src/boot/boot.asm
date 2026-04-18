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
    dw 5                         ; type 5 (framebuffer)
    dw 0                         ; flags
    dd 20                        ; size
    dd 1024                      ; width
    dd 768                       ; height
    dd 32                        ; depth

    ; Module alignment
    align 8
    dw 6
    dw 0
    dd 8

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
    ; Stack beállítása
    mov esp, stack_top
    
    ; Multiboot info mentése (ebx-ben jön)
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
    mov al, "1"
    jmp error

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
    mov al, "2"
    jmp error

setup_page_tables:
    ; PML4 + PDPT + 32 PD tábla nullázása
    mov edi, pml4_table
    xor eax, eax
    mov ecx, (2 + 32) * 1024
    rep stosd

    ; PML4 -> PDPT
    mov eax, pdpt_table
    or eax, 0b11
    mov [pml4_table], eax

    ; PDPT -> 32 darab PD tábla (0-16GB lefedése)
    mov ecx, 0
.map_pdpt:
    mov eax, 4096
    mul ecx
    add eax, pd_tables
    or eax, 0b11
    mov [pdpt_table + ecx * 8], eax
    mov dword [pdpt_table + ecx * 8 + 4], 0 ; Felső 32 bit 0
    inc ecx
    cmp ecx, 32
    jne .map_pdpt

    ; PD-k feltöltése (Identity mapping 2MB lapokkal)
    mov ecx, 0
.map_pds:
    mov eax, 0x200000
    mul ecx
    or eax, 0b10000011 ; huge page + write + present
    mov [pd_tables + ecx * 8], eax
    mov [pd_tables + ecx * 8 + 4], edx ; EDX-ben van a cím felső része a mul után!
    inc ecx
    cmp ecx, 16384 ; 32 tábla * 512 bejegyzés
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

error:
    mov dword [0xb8000], 0x4f524f45 ; "ER"
    mov [0xb8008], al
    hlt

[BITS 64]
long_mode_start:
    mov ax, gdt64.data
    mov ss, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ; Stack igazítása 16 bájtra (ABI elvárás)
    mov rsp, stack_top
    
    ; Debug jelzés: Zöld '!' a sarokba
    mov rax, 0x2F212F212F212F21
    mov [0xB8000], rax

    ; Multiboot pointer átadása
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
    resb 4096 * 32
multiboot_ptr:
    resd 1
stack_bottom:
    resb 16384
stack_top:

section .rodata
align 8
gdt64:
    dq 0 ; null descriptor
.code: equ $ - gdt64
    dq (1<<43) | (1<<44) | (1<<47) | (1<<53) ; code descriptor
.data: equ $ - gdt64
    dq (1<<44) | (1<<47) | (1<<41)           ; data descriptor
.pointer:
    dw $ - gdt64 - 1
    dq gdt64

align 8
global wallpaper_data
wallpaper_data:
    incbin "sysroot/AnimOS/assets/wallpapers/bubble.bmp"
wallpaper_end:
