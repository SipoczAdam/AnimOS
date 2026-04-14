all:
	nasm -felf32 boot.asm -o boot.o
	gcc -m32 -c kernel.c -o kernel.o -ffreestanding -O2 -nostdlib
	ld -m elf_i386 -T linker.ld -o kernel.bin boot.o kernel.o
	mkdir -p isodir/boot/grub
	cp kernel.bin isodir/boot/
	cp grub.cfg isodir/boot/grub/
	grub-mkrescue -o animos.iso isodir