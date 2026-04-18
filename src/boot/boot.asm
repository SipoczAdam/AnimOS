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
    mov esp, stack_top
    
    ; Megőrizzük az EBX-et (Multiboot info)
    push ebx

    call check_cpuid
    call check_long_mode

    call setup_page_tables
    call enable_paging

    lgdt [gdt64.pointer]
    pop ebx ; Visszaszerezzük az EBX-et
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
    ; PML4 nullázása
    mov edi, pml4_table
    xor eax, eax
    mov ecx, 1024
    rep stosd

    ; PDPT nullázása
    mov edi, pdpt_table
    mov ecx, 1024
    rep stosd

    ; PD nullázása
    mov edi, pd_table
    mov ecx, 1024
    rep stosd

    ; PML4 -> PDPT
    mov eax, pdpt_table
    or eax, 0b11 ; present + writable
    mov [pml4_table], eax

    ; PDPT -> PD (Több GB-ot is lefedünk)
    ; 1. GB
    mov eax, pd_table
    or eax, 0b11
    mov [pdpt_table], eax
    
    ; 2. GB (Opcionális, ha a framebuffer magasabban van)
    mov eax, pd_table_2
    or eax, 0b11
    mov [pdpt_table + 8], eax

    ; 3. GB
    mov eax, pd_table_3
    or eax, 0b11
    mov [pdpt_table + 16], eax

    ; 4. GB
    mov eax, pd_table_4
    or eax, 0b11
    mov [pdpt_table + 24], eax

    ; PD feltöltése (Identity mapping)
    ; 0-1 GB
    mov ecx, 0
.map_p2_table_0:
    mov eax, 0x200000 ; 2MB
    mul ecx
    or eax, 0b10000011 ; present + writable + huge page
    mov [pd_table + ecx * 8], eax
    inc ecx
    cmp ecx, 512
    jne .map_p2_table_0

    ; 1-2 GB
    mov ecx, 0
.map_p2_table_2:
    mov eax, 0x200000
    mul ecx
    add eax, 0x40000000 ; +1GB
    or eax, 0b10000011
    mov [pd_table_2 + ecx * 8], eax
    inc ecx
    cmp ecx, 512
    jne .map_p2_table_2

    ; 2-3 GB
    mov ecx, 0
.map_p2_table_3:
    mov eax, 0x200000
    mul ecx
    add eax, 0x80000000 ; +2GB
    or eax, 0b10000011
    mov [pd_table_3 + ecx * 8], eax
    inc ecx
    cmp ecx, 512
    jne .map_p2_table_3

    ; 3-4 GB
    mov ecx, 0
.map_p2_table_4:
    mov eax, 0x200000
    mul ecx
    add eax, 0xC0000000 ; +3GB
    or eax, 0b10000011
    mov [pd_table_4 + ecx * 8], eax
    inc ecx
    cmp ecx, 512
    jne .map_p2_table_4
    
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

    ; EBX tartalmazza a Multiboot info címet (32-biten raktuk oda)
    mov edi, ebx 
    
    call kernel_main
    hlt

section .bss
align 4096
pml4_table:
    resb 4096
pdpt_table:
    resb 4096
pd_table:
    resb 4096
pd_table_2:
    resb 4096
pd_table_3:
    resb 4096
pd_table_4:
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
