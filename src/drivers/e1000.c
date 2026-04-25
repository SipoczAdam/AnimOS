#include "e1000.h"

#define NUM_RX_DESCRIPTORS 256
#define NUM_TX_DESCRIPTORS 256

static uint8_t rx_descriptors_raw[NUM_RX_DESCRIPTORS * sizeof(struct e1000_rx_desc) + 4096] __attribute__((aligned(4096)));
static uint8_t tx_descriptors_raw[NUM_TX_DESCRIPTORS * sizeof(struct e1000_tx_desc) + 4096] __attribute__((aligned(4096)));

static volatile struct e1000_rx_desc* rx_descs;
static volatile struct e1000_tx_desc* tx_descs;

static uint8_t rx_buffers[NUM_RX_DESCRIPTORS][2048] __attribute__((aligned(4096)));
static uint8_t tx_buffers[NUM_TX_DESCRIPTORS][2048] __attribute__((aligned(4096)));

static uint32_t rx_cur = 0;
static uint32_t tx_cur = 0;

static uint64_t mmio_base = 0;
static uint8_t mac_addr[6];

static inline void write_reg(uint32_t reg, uint32_t val) {
    *(volatile uint32_t*)(mmio_base + reg) = val;
}

static inline uint32_t read_reg(uint32_t reg) {
    return *(volatile uint32_t*)(mmio_base + reg);
}

void e1000_get_mac(uint8_t* mac) {
    for(int i = 0; i < 6; i++) mac[i] = mac_addr[i];
}

uint64_t e1000_get_bar() { return mmio_base; }

void e1000_get_stats(uint32_t* rdh, uint32_t* rdt, uint32_t* tdh, uint32_t* tdt, uint32_t* status, uint32_t* icr) {
    *rdh = read_reg(E1000_REG_RDH);
    *rdt = read_reg(E1000_REG_RDT);
    *tdh = read_reg(E1000_REG_TDH);
    *tdt = read_reg(E1000_REG_TDT);
    *status = read_reg(E1000_REG_STATUS);
    *icr = read_reg(E1000_REG_ICR);
}

int e1000_link_up() {
    return (read_reg(E1000_REG_STATUS) & (1 << 1));
}

uint16_t e1000_eeprom_read(uint8_t addr) {
    write_reg(E1000_REG_EERD, (1) | ((uint32_t)(addr) << 8));
    uint32_t tmp = 0;
    while (!((tmp = read_reg(E1000_REG_EERD)) & (1 << 4))) __asm__ volatile("pause");
    return (uint16_t)((tmp >> 16) & 0xFFFF);
}

int e1000_init(struct pci_device* dev) {
    pci_config_write_dword(dev->bus, dev->slot, dev->func, 0x04, 0x0007);
    mmio_base = dev->bar0 & ~0xF;
    
    // 1. Full Reset
    write_reg(E1000_REG_IMC, 0xFFFFFFFF);
    write_reg(E1000_REG_CTRL, read_reg(E1000_REG_CTRL) | (1 << 26)); // RST
    uint32_t timeout = 10000;
    while((read_reg(E1000_REG_CTRL) & (1 << 26)) && timeout--) __asm__ volatile("pause");

    // 2. Link setup
    write_reg(E1000_REG_CTRL, (1 << 6) | (1 << 5) | (1 << 31)); // SLU, ASDE, PHY_RST
    write_reg(E1000_REG_IMC, 0xFFFFFFFF);
    read_reg(E1000_REG_ICR); 

    for(int i = 0; i < 128; i++) write_reg(0x05200 + (i * 4), 0);

    // 3. MAC cím
    uint16_t val;
    val = e1000_eeprom_read(0); mac_addr[0] = val & 0xFF; mac_addr[1] = val >> 8;
    val = e1000_eeprom_read(1); mac_addr[2] = val & 0xFF; mac_addr[3] = val >> 8;
    val = e1000_eeprom_read(2); mac_addr[4] = val & 0xFF; mac_addr[5] = val >> 8;

    write_reg(E1000_REG_RAL, *(uint32_t*)(mac_addr));
    write_reg(E1000_REG_RAH, (*(uint16_t*)(mac_addr + 4)) | (1 << 31));

    // 4. RX Ring
    rx_descs = (volatile struct e1000_rx_desc*)rx_descriptors_raw;
    for(int i = 0; i < NUM_RX_DESCRIPTORS; i++) {
        rx_descs[i].addr = (uint64_t)rx_buffers[i];
        rx_descs[i].status = 0;
    }
    write_reg(E1000_REG_RDBAL, (uint32_t)(uint64_t)rx_descs);
    write_reg(E1000_REG_RDBAH, (uint32_t)((uint64_t)rx_descs >> 32));
    write_reg(E1000_REG_RDLEN, NUM_RX_DESCRIPTORS * 16);
    write_reg(E1000_REG_RDH, 0);
    write_reg(E1000_REG_RDT, NUM_RX_DESCRIPTORS - 1);
    
    // 5. TX Ring
    tx_descs = (volatile struct e1000_tx_desc*)tx_descriptors_raw;
    for(int i = 0; i < NUM_TX_DESCRIPTORS; i++) {
        tx_descs[i].addr = (uint64_t)tx_buffers[i];
        tx_descs[i].status = 1; 
        tx_descs[i].cmd = 0;
    }
    write_reg(E1000_REG_TDBAL, (uint32_t)(uint64_t)tx_descs);
    write_reg(E1000_REG_TDBAH, (uint32_t)((uint64_t)tx_descs >> 32));
    write_reg(E1000_REG_TDLEN, NUM_TX_DESCRIPTORS * 16);
    write_reg(E1000_REG_TDH, 0);
    write_reg(E1000_REG_TDT, 0);

    // 6. Enable
    write_reg(E1000_REG_RCTL, (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 15) | (1 << 26));
    write_reg(E1000_REG_RDT, NUM_RX_DESCRIPTORS - 1); // Extra kick
    write_reg(E1000_REG_TCTL, (1 << 1) | (1 << 3) | (0xF << 4) | (0x40 << 12));
    write_reg(E1000_REG_TIPG, 0x0060200A);

    return 0;
}

void e1000_send_packet(const void* data, uint16_t length) {
    uint8_t* dst = (uint8_t*)tx_descs[tx_cur].addr;
    for(uint16_t i = 0; i < length; i++) dst[i] = ((uint8_t*)data)[i];
    tx_descs[tx_cur].length = length;
    tx_descs[tx_cur].status = 0;
    tx_descs[tx_cur].cmd = (1 << 0) | (1 << 1) | (1 << 3); // EOP, IFCS, RS
    
    uint32_t old_cur = tx_cur;
    tx_cur = (tx_cur + 1) % NUM_TX_DESCRIPTORS;
    write_reg(E1000_REG_TDT, tx_cur);
    
    uint32_t timeout = 1000000;
    while(!(tx_descs[old_cur].status & 0x01) && timeout--) __asm__ volatile("pause");
}

int e1000_receive_packet(void* buffer, uint16_t* length) {
    read_reg(E1000_REG_ICR); // Minden olvasáskor töröljük a függőben lévő állapotokat
    if(!(rx_descs[rx_cur].status & 0x01)) return 0;
    *length = rx_descs[rx_cur].length;
    uint8_t* src = (uint8_t*)rx_descs[rx_cur].addr;
    for(uint16_t i = 0; i < *length; i++) ((uint8_t*)buffer)[i] = src[i];
    rx_descs[rx_cur].status = 0;
    write_reg(E1000_REG_RDT, rx_cur);
    rx_cur = (rx_cur + 1) % NUM_RX_DESCRIPTORS;
    return 1;
}
