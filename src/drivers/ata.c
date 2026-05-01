#include "ata.h"
#include "../kernel/io.h"

static int ata_wait_bsy() {
    uint32_t timeout = 1000000;
    while ((inb(ATA_PRIMARY_STATUS) & ATA_STATUS_BSY) && timeout--);
    return (timeout == 0) ? -1 : 0;
}

static int ata_wait_drq() {
    uint32_t timeout = 1000000;
    while (!(inb(ATA_PRIMARY_STATUS) & ATA_STATUS_DRQ) && timeout--);
    return (timeout == 0) ? -1 : 0;
}

int ata_identify() {
    outb(0x1F6, 0xA0);
    outb(0x1F2, 0);
    outb(0x1F3, 0);
    outb(0x1F4, 0);
    outb(0x1F5, 0);
    outb(0x1F7, 0xEC);

    uint8_t status = inb(0x1F7);
    if (status == 0 || status == 0xFF) return -1; // No drive

    if (ata_wait_bsy() != 0) return -1;

    status = inb(0x1F7);
    if (status & ATA_STATUS_ERR) return -1;

    if (ata_wait_drq() != 0) return -1;

    for (int i = 0; i < 256; i++) {
        inw(ATA_PRIMARY_DATA);
    }
    return 0;
}

int ata_read_sectors(uint32_t lba, uint8_t count, uint8_t* buffer) {
    if (ata_wait_bsy() != 0) return -1;
    outb(0x1F6, 0xE0 | ((lba >> 24) & 0x0F));
    outb(0x1F2, count);
    outb(0x1F3, (uint8_t)lba);
    outb(0x1F4, (uint8_t)(lba >> 8));
    outb(0x1F5, (uint8_t)(lba >> 16));
    outb(0x1F7, 0x20);

    uint16_t* ptr = (uint16_t*)buffer;
    for (int i = 0; i < count; i++) {
        if (ata_wait_bsy() != 0) return -1;
        if (ata_wait_drq() != 0) return -1;
        for (int j = 0; j < 256; j++) {
            *ptr++ = inw(ATA_PRIMARY_DATA);
        }
    }
    return 0;
}
