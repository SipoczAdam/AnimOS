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
CFLAGS = -m64 -nostdlib -nostdinc -fno-builtin -fno-stack-protector -nostartfiles -nodefaultlibs -ffreestanding -O2 -mno-red-zone -mno-mmx -mno-sse -mno-sse2
LDFLAGS = -m elf_x86_64 -T $(CONFIGDIR)/linker.ld -z max-page-size=0x1000

all: prep compile link iso hdd.img

# Folders
prep:
	mkdir -p $(BUILDDIR)
	mkdir -p $(BUILDDIR)/apps
	mkdir -p isodir/boot/grub/themes/grub-theme
	mkdir -p sysroot/AnimOS/apps

# Compiler
compile:
	$(AS) -f elf64 $(SRCDIR)/boot/boot.asm -o $(BUILDDIR)/boot.o
	$(CC) $(CFLAGS) -c $(SRCDIR)/kernel/kernel.c -o $(BUILDDIR)/kernel.o
	$(CC) $(CFLAGS) -c $(SRCDIR)/drivers/pci.c -o $(BUILDDIR)/pci.o
	$(CC) $(CFLAGS) -c $(SRCDIR)/drivers/e1000.c -o $(BUILDDIR)/e1000.o
	$(CC) $(CFLAGS) -c $(SRCDIR)/drivers/ata.c -o $(BUILDDIR)/ata.o
	$(CC) $(CFLAGS) -c $(SRCDIR)/fs/fat32.c -o $(BUILDDIR)/fat32.o
	$(CC) $(CFLAGS) -c $(SRCDIR)/fs/vfs.c -o $(BUILDDIR)/vfs.o
	$(CC) $(CFLAGS) -c $(SRCDIR)/net/net.c -o $(BUILDDIR)/net.o
	# Build Applications
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections -c $(SRCDIR)/applications/preferences/main.c -o $(BUILDDIR)/apps/preferences.o
	$(LD) -m elf_x86_64 -T $(SRCDIR)/applications/linker.ld --oformat binary -o sysroot/AnimOS/apps/preferences.bin $(BUILDDIR)/apps/preferences.o
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections -c $(SRCDIR)/applications/file_explorer/main.c -o $(BUILDDIR)/apps/file_explorer.o
	$(LD) -m elf_x86_64 -T $(SRCDIR)/applications/linker.ld --oformat binary -o sysroot/AnimOS/apps/file_explorer.bin $(BUILDDIR)/apps/file_explorer.o

# Linker
link:
	$(LD) $(LDFLAGS) -static -o $(BUILDDIR)/kernel.bin $(BUILDDIR)/boot.o $(BUILDDIR)/kernel.o $(BUILDDIR)/pci.o $(BUILDDIR)/e1000.o $(BUILDDIR)/ata.o $(BUILDDIR)/fat32.o $(BUILDDIR)/vfs.o $(BUILDDIR)/net.o

# Building ISO
iso:
	cp $(BUILDDIR)/kernel.bin isodir/boot/
	cp $(CONFIGDIR)/grub.cfg isodir/boot/grub/
	rm -rf isodir/boot/grub/themes/grub-theme
	mkdir -p isodir/boot/grub/themes/grub-theme
	cp -r $(ASSETDIR)/grub-theme/* isodir/boot/grub/themes/grub-theme/
	grub-mkrescue -o animos.iso isodir

# Disk Image
hdd.img:
	dd if=/dev/zero of=hdd.img bs=1M count=128
	mkfs.fat -F 32 -n "SYSROOT" hdd.img
	mmd -i hdd.img ::/AnimOS
	mmd -i hdd.img ::/Users
	mcopy -i hdd.img -s sysroot/AnimOS/* ::/AnimOS/
	mcopy -i hdd.img -s sysroot/Users/* ::/Users/

# Clean
clean:
	rm -rf $(BUILDDIR) isodir animos.iso hdd.img

# Run
run:
	qemu-system-x86_64 -boot d -cdrom animos.iso -drive file=hdd.img,format=raw -m 1G -net nic,model=e1000 -net user