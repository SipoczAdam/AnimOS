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

int ata_identify(uint8_t drive) {
    outb(0x1F6, (drive == 0) ? 0xA0 : 0xB0);
    io_wait();
    outb(0x1F2, 0);
    outb(0x1F3, 0);
    outb(0x1F4, 0);
    outb(0x1F5, 0);
    outb(0x1F7, 0xEC);

    // 400ns delay
    for(int i = 0; i < 4; i++) inb(ATA_PRIMARY_STATUS);

    uint8_t status = inb(ATA_PRIMARY_STATUS);
    if (status == 0 || status == 0xFF) return -1; // No drive

    if (ata_wait_bsy() != 0) return -1;

    status = inb(ATA_PRIMARY_STATUS);
    if (status & ATA_STATUS_ERR) return -1;

    if (ata_wait_drq() != 0) return -1;

    for (int i = 0; i < 256; i++) {
        inw(ATA_PRIMARY_DATA);
    }
    return 0;
}

static uint64_t ata_get_sectors(uint8_t drive) {
    outb(0x1F6, (drive == 0) ? 0xA0 : 0xB0);
    io_wait();
    outb(0x1F2, 0);
    outb(0x1F3, 0);
    outb(0x1F4, 0);
    outb(0x1F5, 0);
    outb(0x1F7, 0xEC);

    // 400ns delay
    for(int i = 0; i < 4; i++) inb(ATA_PRIMARY_STATUS);

    uint8_t status = inb(ATA_PRIMARY_STATUS);
    if (status == 0 || status == 0xFF) return 0;

    if (ata_wait_bsy() != 0) return 0;
    
    status = inb(ATA_PRIMARY_STATUS);
    if (status & ATA_STATUS_ERR) return 0;

    if (ata_wait_drq() != 0) return 0;

    uint16_t data[256];
    for (int i = 0; i < 256; i++) {
        data[i] = inw(ATA_PRIMARY_DATA);
    }

    uint32_t sectors = *((uint32_t*)&data[60]);
    // Check if LBA48 is supported (word 83, bit 10)
    if (data[83] & (1 << 10)) {
        uint64_t lba48_sectors = *((uint64_t*)&data[100]);
        if (lba48_sectors > 0) return lba48_sectors;
    }

    return (uint64_t)sectors;
}

uint32_t ata_get_size_gb(uint8_t drive) {
    uint64_t sectors = ata_get_sectors(drive);
    return (uint32_t)((sectors * 512) / (1024 * 1024 * 1024));
}

uint32_t ata_get_size_mb(uint8_t drive) {
    uint64_t sectors = ata_get_sectors(drive);
    return (uint32_t)((sectors * 512) / (1024 * 1024));
}

int ata_read_sectors(uint8_t drive, uint32_t lba, uint8_t count, uint8_t* buffer) {
    if (ata_wait_bsy() != 0) return -1;
    outb(0x1F6, ((drive == 0) ? 0xE0 : 0xF0) | ((lba >> 24) & 0x0F));
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
