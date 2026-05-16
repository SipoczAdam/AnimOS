#ifndef ATA_H
#define ATA_H

#include "../kernel/types.h"

#define ATA_PRIMARY_DATA         0x1F0
#define ATA_PRIMARY_ERR          0x1F1
#define ATA_PRIMARY_SECCOUNT     0x1F2
#define ATA_PRIMARY_LBA_LO       0x1F3
#define ATA_PRIMARY_LBA_MID      0x1F4
#define ATA_PRIMARY_LBA_HI       0x1F5
#define ATA_PRIMARY_DRIVE_SEL    0x1F6
#define ATA_PRIMARY_COMMAND      0x1F7
#define ATA_PRIMARY_STATUS       0x1F7

#define ATA_CMD_READ_PIO         0x20
#define ATA_CMD_IDENTIFY         0xEC

#define ATA_STATUS_BSY  0x80
#define ATA_STATUS_DRQ  0x08
#define ATA_STATUS_DF   0x20
#define ATA_STATUS_ERR  0x01

int ata_identify(uint8_t drive);
uint32_t ata_get_size_gb(uint8_t drive);
uint32_t ata_get_size_mb(uint8_t drive);
int ata_read_sectors(uint8_t drive, uint32_t lba, uint8_t count, uint8_t* buffer);

#endif
