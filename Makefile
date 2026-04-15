# Build Directories 
SRCDIR = src
BUILDDIR = build
CONFIGDIR = config
ASSETDIR = assets

# Cross-Compiler and Linker
CC = gcc
AS = nasm
LD = ld

# 64-bit Flags
CFLAGS = -m64 -nostdlib -nostdinc -fno-builtin -fno-stack-protector -nostartfiles -nodefaultlibs -ffreestanding -O2
LDFLAGS = -m elf_x86_64 -T $(CONFIGDIR)/linker.ld -z max-page-size=0x1000

all: prep compile link iso

# Folders
prep:
	mkdir -p $(BUILDDIR)
	mkdir -p isodir/boot/grub/themes/grub-theme

# Compiler
compile:
	$(AS) -f elf64 $(SRCDIR)/boot/boot.asm -o $(BUILDDIR)/boot.o
	$(CC) $(CFLAGS) -c $(SRCDIR)/kernel/kernel.c -o $(BUILDDIR)/kernel.o

# Linker
link:
	$(LD) $(LDFLAGS) -static -o $(BUILDDIR)/kernel.bin $(BUILDDIR)/boot.o $(BUILDDIR)/kernel.o

# Building ISO
iso:
	cp $(BUILDDIR)/kernel.bin isodir/boot/
	cp $(CONFIGDIR)/grub.cfg isodir/boot/grub/
	rm -rf isodir/boot/grub/themes/grub-theme
	mkdir -p isodir/boot/grub/themes/grub-theme
	cp -r $(ASSETDIR)/grub-theme/* isodir/boot/grub/themes/grub-theme/
	grub-mkrescue -o animos.iso isodir

# Clean
clean:
	rm -rf $(BUILDDIR) isodir animos.iso