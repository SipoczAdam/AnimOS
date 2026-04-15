; --- Multiboot 2 Header ---
section .multiboot_header
align 8
header_start:
    dd 0xe85250d6                ; magic number
    dd 0                         ; architecture 0 (i386)
    dd header_end - header_start ; header length
    ; Checksum
    dd 0x100000000 - (0xe85250d6 + 0 + (header_end - header_start))

    ; End tag
    dw 0
    dw 0
    dd 8
header_end:

section .text
[BITS 32]
global _start
extern kernel_main

_start:
    mov esp, stack_top

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
    ; 1. PML4 nullázása (Kritikus!)
    mov edi, pml4_table
    xor eax, eax
    mov ecx, 1024
    rep stosd

    ; 2. PDPT nullázása
    mov edi, pdpt_table
    mov ecx, 1024
    rep stosd

    ; 3. PD nullázása
    mov edi, pd_table
    mov ecx, 1024
    rep stosd

    ; PML4 -> PDPT
    mov eax, pdpt_table
    or eax, 0b11 ; present + writable
    mov [pml4_table], eax

    ; PDPT -> PD
    mov eax, pd_table
    or eax, 0b11 ; present + writable
    mov [pdpt_table], eax

    ; PD feltöltése (Identity mapping 1GB-ig, 2MB-os lapokkal)
    mov ecx, 0
.map_p2_table:
    mov eax, 0x200000 ; 2MB
    mul ecx
    or eax, 0b10000011 ; present + writable + huge page
    mov [pd_table + ecx * 8], eax

    inc ecx
    cmp ecx, 512
    jne .map_p2_table
    ret

enable_paging:
    mov eax, pml4_table
    mov cr3, eax

    mov eax, cr4
    or eax, 1 << 5 ; PAE bit
    mov cr4, eax

    mov ecx, 0xC0000080 ; EFER MSR
    rdmsr
    or eax, 1 << 8 ; LME (Long Mode Enable)
    wrmsr

    mov eax, cr0
    or eax, 1 << 31 ; Paging bit
    mov cr0, eax
    ret

error:
    mov dword [0xb8000], 0x4f524f45
    mov dword [0xb8004], 0x4f3a4f52
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

    call kernel_main
    hlt

section .bss
align 4096 ; A laptábláknak kötelező a 4KB-os igazítás!
pml4_table:
    resb 4096
pdpt_table:
    resb 4096
pd_table:
    resb 4096
stack_bottom:
    resb 16384
stack_top:

section .rodata
gdt64:
    dq 0 ; null descriptor
.code: equ $ - gdt64
    dq (1<<43) | (1<<44) | (1<<47) | (1<<53) ; code descriptor
.data: equ $ - gdt64
    dq (1<<44) | (1<<47) | (1<<41)           ; data descriptor
.pointer:
    dw $ - gdt64 - 1
    dq gdt64 ; 64 bites cím