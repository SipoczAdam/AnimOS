@echo off
echo "Building AnimOS..."
docker run --rm -v .:/osdev animos-builder make clean all

echo "Removing hdd.vdi..."
del hdd.vdi

echo "Converting hdd.img to vdi.img..."
vboxmanage convertfromraw "hdd.img" "hdd.vdi" --format VDI

echo "Running AnimOS..."
qemu-system-x86_64 -boot d -cdrom animos.iso -drive file=hdd.img,format=raw -m 4G -net nic,model=e1000 -net user