# AnimOS Operating System Development

This is the official repository for the AnimOS Operating System.

## ⚠️ License Warning
**Copyright (C) 2026 Sipocz Adam - All Rights Reserved.**
This software is PROPRIETARY. The source code is published for educational and review purposes only. 
Unauthorized copying, modification, or redistribution is strictly prohibited. 
See the [LICENSE](LICENSE) and [EULA](EULA) files for details.

## 🛠️ Build and Run
To build the OS using the Docker-based toolchain:
```bash
docker run --rm -v .:/osdev animos-builder make clean all
```

To run the system in QEMU:
```bash
qemu-system-x86_64 -boot d -cdrom animos.iso -drive file=hdd.img,format=raw -m 1G -net nic,model=e1000 -net user
```

## 📬 Contact
For inquiries or licensing permissions, please contact the author via GitHub.