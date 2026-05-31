# AnimOS™ Experience Operating System Development

This is the official repository for the AnimOS Operating System.

## ⚠️ License Warning
**Copyright (C) 2026 Sipocz Adam - All Rights Reserved.**
This software is PROPRIETARY. The source code is published for educational and review purposes only. 
Unauthorized copying, modification, or redistribution is strictly prohibited. 
See the [LICENSE](LICENSE) and [EULA](EULA) files for details.

## 💻 System Requirements
| Resource | Minimum Requirement | Recommended |
| :--- | :--- | :--- |
|**Architecture**| x86_64 64 bit |
|**Processor**| Intel Core i3 or AMD Ryzen 3 | Intel Core i5 or AMD Ryzen 5 or newer |
|**RAM**| 2 GB | 4 GB or more |
|**Storage**| 1 GB | 5 GB or more |
|**Network**| Intel E1000 compatible NIC | Intel E1000 (with active DHCP network/LAN) |
|**Graphics**| Any Integrated GPU | Any Dedicated GPU |

## 🛠️ Build and Run
To build the OS using the Docker-based toolchain:
```bash
docker run --rm -v .:/osdev animos-builder make clean all
```

To run the system in QEMU:
```bash
qemu-system-x86_64 -boot d -cdrom animos.iso -drive file=hdd.img,format=raw -m 2G -net nic,model=e1000 -net user
```

To build, prepare images and boot automatically:
```bash
start build-run.bat
```

To run the system in Virtualbox, you need to attach hdd.vdi to Virtualbox

⚠️ Virtualbox have to be added to PATH!

## 📬 Contact
For inquiries or licensing permissions, please contact the author via GitHub.

---
*AnimOS™ is a trademark of Sipocz Adam. All other trademarks are the property of their respective owners.*
