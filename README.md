# AnimOS Operating System Development
```bash
docker run --rm -v .:/osdev animos-builder make clean all

qemu-system-x86_64 -cdrom animos.iso -m 1G
```