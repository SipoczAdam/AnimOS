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
    dd 24                        ; size
    dd 8                         ; request framebuffer
    dd 4                         ; request basic meminfo
    dd 6                         ; request mmap
    dd 0                         ; padding to 8 bytes

    ; Alignment tag (VirtualBox és néhány GRUB verzió szereti)
    align 8
    dw 6                         ; type 6
    dw 0                         ; flags
    dd 8                         ; size

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
    ; Multiboot pointer mentése (esi-be, mert a .bss törlés felülírná a memóriában)
    mov esi, ebx

    ; .bss nullázása (fontos a statikus változók miatt)
    ; Ezt MÉG a stack beállítása előtt kell megtenni!
    extern bss_start
    extern bss_end
    mov edi, bss_start
    mov ecx, bss_end
    sub ecx, edi
    shr ecx, 2
    xor eax, eax
    rep stosd

    ; Most már beállíthatjuk a stacket és a multiboot pointert a tiszta .bss-be
    mov esp, stack_top
    mov [multiboot_ptr], esi

    call check_cpuid
    call check_long_mode
    call setup_page_tables
    call enable_paging

    ; SSE engedélyezése (GCC optimalizációk miatt kritikus!)
    mov eax, cr0
    and ax, 0xFFFB      ; emuláció kikapcsolása
    or ax, 0x0002       ; monitor coprocessor bekapcsolása
    mov cr0, eax
    mov eax, cr4
    or ax, 3 << 9       ; OSFXSR és OSXMMEXCPT bekapcsolása
    mov cr4, eax

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
    ; PML4 (1) + PDPT (1) + PDs (128) = 130 oldalt nullázunk (128GB mapping)
    mov edi, pml4_table
    xor eax, eax
    mov ecx, 130 * 1024
    rep stosd

    ; PML4 -> PDPT
    mov eax, pdpt_table
    or eax, 0b11
    mov [pml4_table], eax

    ; PDPT -> 128 PD tábla (0-128GB)
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
    cmp ecx, 128
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
    cmp ecx, 128 * 512
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
extern keyboard_handler_main
global isr_keyboard_stub
isr_keyboard_stub:
    push rax
    push rcx
    push rdx
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    call keyboard_handler_main
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

global exception_handler_stub
extern exception_handler_main

; Egyedi stubok a hibákhoz, hogy tudjuk melyik történt
%macro EXCEPTION_NO_ERR 1
global exception_stub_%1
exception_stub_%1:
    push qword 0 ; Dummy error code
    push qword %1 ; Vector number
    jmp exception_common
%endmacro

%macro EXCEPTION_ERR 1
global exception_stub_%1
exception_stub_%1:
    push qword %1 ; Vector number
    jmp exception_common
%endmacro

EXCEPTION_NO_ERR 0
EXCEPTION_NO_ERR 1
EXCEPTION_NO_ERR 2
EXCEPTION_NO_ERR 3
EXCEPTION_NO_ERR 4
EXCEPTION_NO_ERR 5
EXCEPTION_NO_ERR 6
EXCEPTION_NO_ERR 7
EXCEPTION_ERR    8
EXCEPTION_NO_ERR 9
EXCEPTION_ERR    10
EXCEPTION_ERR    11
EXCEPTION_ERR    12
EXCEPTION_ERR    13
EXCEPTION_ERR    14
EXCEPTION_NO_ERR 15
EXCEPTION_NO_ERR 16
EXCEPTION_ERR    17
EXCEPTION_NO_ERR 18
EXCEPTION_NO_ERR 19
EXCEPTION_NO_ERR 20
EXCEPTION_ERR    21
EXCEPTION_NO_ERR 22
EXCEPTION_NO_ERR 23
EXCEPTION_NO_ERR 24
EXCEPTION_NO_ERR 25
EXCEPTION_NO_ERR 26
EXCEPTION_NO_ERR 27
EXCEPTION_NO_ERR 28
EXCEPTION_ERR    29
EXCEPTION_ERR    30
EXCEPTION_NO_ERR 31

exception_common:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, [rsp + 15*8] ; Vector number
    mov rsi, [rsp + 16*8] ; Error code
    mov rdx, [rsp + 17*8] ; RIP (Instruction Pointer)
    
    ; Stack igazítás (16 bájtos) a C hívás előtt
    mov rbp, rsp
    and rsp, ~0xF
    call exception_handler_main
    mov rsp, rbp

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16 ; Vector és Error code eldobása
    iretq

global irq_common_stub
irq_common_stub:
    push rax
    mov al, 0x20
    out 0x20, al ; Master PIC EOI
    out 0xA0, al ; Slave PIC EOI
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
    
    ; Stack igazítás (ABI előírás)
    and rsp, ~0xF
    
    ; FPU inicializálása
    fninit

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
    resb 4096 * 128
align 16
multiboot_ptr:
    resd 1
    resb 12

; A stack-et a .bss végére tesszük és megnöveljük 1MB-ra
stack_bottom:
    resb 1048576 ; 1MB stack
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

