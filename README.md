# AnimOS™ Experience Operating System Development

This is the official repository for the AnimOS Operating System.

## ⚠️ License Warning
**Copyright (C) 2026 Sipocz Adam - All Rights Reserved.**
This software is PROPRIETARY. The source code is published for educational and review purposes only. 
Unauthorized copying, modification, or redistribution is strictly prohibited. 
See the [LICENSE](LICENSE) and [EULA](EULA) files for details.

## 💻 System Requirements
<table>
  <thead>
    <tr>
      <th align="center">Resource</th>
      <th align="center">Minimum Requirement</th>
      <th align="center">Recommended</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td align="center"><b>Architecture</b></td>
      <td align="center" colspan="2">x86_64 64 bit</td>
    </tr>
    <tr>
      <td align="center"><b>Processor</b></td>
      <td align="center">Intel Core i3 or AMD Ryzen 3</td>
      <td align="center">Intel Core i5 or AMD Ryzen 5 or newer</td>
    </tr>
    <tr>
      <td align="center"><b>RAM</b></td>
      <td align="center">2 GB</td>
      <td align="center">4 GB or more</td>
    </tr>
    <tr>
      <td align="center"><b>Storage</b></td>
      <td align="center">1 GB</td>
      <td align="center">5 GB or more</td>
    </tr>
    <tr>
      <td align="center"><b>Network</b></td>
      <td align="center">Intel E1000 compatible NIC</td>
      <td align="center">Intel E1000 (with active DHCP network/LAN)</td>
    </tr>
    <tr>
      <td align="center"><b>Graphics</b></td>
      <td align="center">Any Integrated GPU</td>
      <td align="center">Any Dedicated GPU</td>
    </tr>
  </tbody>
</table>

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
