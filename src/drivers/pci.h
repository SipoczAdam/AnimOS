#ifndef PCI_H
#define PCI_H

#include "../kernel/types.h"

uint32_t pci_config_read_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_config_write_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val);

struct pci_device {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t bar0;
    uint8_t interrupt_line;
};

int pci_find_device(uint16_t vendor_id, uint16_t device_id, struct pci_device* out_dev);

#endif
