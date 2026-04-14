# AnimOS Operating System Development
```bash
docker run --rm -v ${PWD}:/osdev animos-builder make

qemu-system-i386 -cdrom animos.iso -m 1G
```