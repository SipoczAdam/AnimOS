#ifndef E1000_H
#define E1000_H

#include "pci.h"
#include "../kernel/types.h"

#define E1000_REG_CTRL          0x00000
#define E1000_REG_STATUS        0x00008
#define E1000_REG_EECD          0x00010
#define E1000_REG_EERD          0x00014
#define E1000_REG_ICR           0x000C0
#define E1000_REG_IMS           0x000D0
#define E1000_REG_IMC           0x000D8
#define E1000_REG_RCTL          0x00100
#define E1000_REG_TCTL          0x00400
#define E1000_REG_TIPG          0x00410
#define E1000_REG_RDBAL         0x02800
#define E1000_REG_RDBAH         0x02804
#define E1000_REG_RDLEN         0x02808
#define E1000_REG_RDH           0x02810
#define E1000_REG_RDT           0x02818
#define E1000_REG_TDBAL         0x03800
#define E1000_REG_TDBAH         0x03804
#define E1000_REG_TDLEN         0x03808
#define E1000_REG_TDH           0x03810
#define E1000_REG_TDT           0x03818
#define E1000_REG_RAL           0x05400
#define E1000_REG_RAH           0x05404

#define E1000_RCTL_EN           (1 << 1)
#define E1000_RCTL_SBP          (1 << 2)
#define E1000_RCTL_UPE          (1 << 3)
#define E1000_RCTL_MPE          (1 << 4)
#define E1000_RCTL_LPE          (1 << 5)
#define E1000_RCTL_BAM          (1 << 15)
#define E1000_RCTL_BSIZE_2048   (0 << 16)

#define E1000_TCTL_EN           (1 << 1)
#define E1000_TCTL_PSP          (1 << 3)

struct e1000_rx_desc {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} __attribute__((packed));

struct e1000_tx_desc {
    uint64_t addr;
    uint16_t length;
    uint8_t cso;
    uint8_t cmd;
    uint8_t status;
    uint8_t css;
    uint16_t special;
} __attribute__((packed));

int e1000_init(struct pci_device* dev);
int e1000_link_up();
void e1000_send_packet(const void* data, uint16_t length);
int e1000_receive_packet(void* buffer, uint16_t* length);
void e1000_get_mac(uint8_t* mac);
uint64_t e1000_get_bar();
void e1000_get_stats(uint32_t* rdh, uint32_t* rdt, uint32_t* tdh, uint32_t* tdt, uint32_t* status, uint32_t* icr);

#endif
