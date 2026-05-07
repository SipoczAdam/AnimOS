# AnimOS Operating System Development
```bash
docker run --rm -v .:/osdev animos-builder make clean all

qemu-system-x86_64 -boot d -cdrom animos.iso -drive file=hdd.img,format=raw -m 1G -net nic,model=e1000 -net user
```

# Copyright (C)
Copyright (C) 2026 SipoczAdam - AnimOS Project. All rights reserved.