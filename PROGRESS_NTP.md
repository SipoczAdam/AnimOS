# AnimOS - NTP és Hálózati Fejlesztés Állapota

## Megvalósított komponensek

1.  **Típusdefiníciók:** `src/kernel/types.h` létrehozva a standard típusokhoz (`uint32_t`, stb.) `nostdinc` környezetben.
2.  **PCI Driver:** `src/drivers/pci.c/h` - Képes az Ethernet vezérlők (Class 02) és specifikusan az Intel kártyák keresésére.
3.  **E1000 Driver:** `src/drivers/e1000.c/h` - Intel PRO/1000 (8254x) inicializálás, EEPROM-ból MAC cím olvasás, DMA-alapú küldés és fogadás.
4.  **Network Stack:** `src/net/net.c/h` - Alapszintű Ethernet, ARP (kérés és válasz), IPv4, UDP és NTP kliens implementáció.
5.  **Kernel Integráció:** `src/kernel/kernel.c` - Inicializálja a kártyát, periodikusan küldi az NTP kéréseket, és debug információkat jelenít meg a tálcán.

## Jelenlegi Állapot (2026. április 25. - Frissítve)

*   **Gateway ARP:** Implementálva. A rendszer most már lekéri a `10.0.2.2` (QEMU gateway) MAC címét.
*   **Routing:** Az NTP csomagok most már a Gateway MAC címére mennek a korábbi Broadcast helyett, ami elengedhetetlen a hálózaton kívüli kommunikációhoz.
*   **Retry Logic:** Az NTP újrapróbálkozási idő megnövelve, hogy elkerüljük a flood-ot és hagyjunk időt az ARP válasz megérkezésére.
*   **QEMU:** A `Makefile` frissítve a `-net nic,model=e1000 -net user` paraméterekkel a könnyebb teszteléshez.

## Következő lépések (ha még mindig nincs NTP válasz)

1.  **ARP Debug:** Ellenőrizni, hogy a `gateway_mac` sikeresen kitöltődik-e (debug kiíratással).
2.  **UDP Checksum:** Bár opcionális, néhány szigorúbb NTP szerver vagy router eldobhatja a 0 checksumú csomagot.
3.  **Interrupts vs Polling:** Ha a polling nem elég gyors a beérkező csomagok elkapásához a sok rajzolás mellett, át kell térni megszakítás alapú kezelésre.

## Fájlok listája

*   `src/kernel/types.h`
*   `src/drivers/pci.h` / `src/drivers/pci.c`
*   `src/drivers/e1000.h` / `src/drivers/e1000.c`
*   `src/net/net.h` / `src/net/net.c`
*   `Makefile` (Frissítve az új objektumokkal)
*   `src/kernel/kernel.c` (Frissítve a hálózati pollinggal és debug UI-val)

## Következő lépések

1.  **TX Ellenőrzés:** Megnézni, hogy a `TX H` (Transmit Head) értéke változik-e a küldésekkor.
2.  **DMA Nyomozás:** Ha a `TX H` sem mozog, akkor a kártya nem fér hozzá a memóriához (lehet 64 bites címzési hiba vagy paging probléma).
3.  **Link Debug:** Bár a szoftver szerint a Link fent van, ellenőrizni kell az emulátor hálózati beállításait (QEMU user-net vs bridge).
4.  **Interrupt Polling:** Megbizonyosodni róla, hogy az `ICR` regiszter olvasása nélkül is hajlandó-e a kártya dolgozni tisztán polling módban.

---
*Mentve: 2026. április 25.*
