SRCDIR = src
BUILDDIR = build
CONFIGDIR = config
ASSETDIR = assets

all: prep compile link iso

prep:
	mkdir -p $(BUILDDIR)
	mkdir -p isodir/boot/grub/fonts
	mkdir -p isodir/boot/grub

compile:
	nasm -felf32 $(SRCDIR)/boot/boot.asm -o $(BUILDDIR)/boot.o
	gcc -m32 -c $(SRCDIR)/kernel/kernel.c -o $(BUILDDIR)/kernel.o -ffreestanding -O2 -nostdlib

link:
	ld -m elf_i386 -T $(CONFIGDIR)/linker.ld -o $(BUILDDIR)/kernel.bin $(BUILDDIR)/boot.o $(BUILDDIR)/kernel.o

iso:
	cp $(BUILDDIR)/kernel.bin isodir/boot/
	cp $(CONFIGDIR)/grub.cfg isodir/boot/grub/
	mkdir -p isodir/boot/grub/themes/grub-theme
	cp -r $(ASSETDIR)/grub-theme/* isodir/boot/grub/themes/grub-theme/
	grub-mkrescue -o animos.iso isodir

clean:
	rm -rf $(BUILDDIR) isodir animos.iso