@echo off
echo "Removing hdd.vdi..."
del hdd.vdi

echo "Converting hdd.img to vdi.img..."
vboxmanage convertfromraw "hdd.img" "hdd.vdi" --format VDI