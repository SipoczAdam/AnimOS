#include "pci.h"

static inline void outl_pci(uint16_t port, uint32_t val) {
    __asm__ volatile ( "outl %0, %1" : : "a"(val), "Nd"(port) );
}

static inline uint32_t inl_pci(uint16_t port) {
    uint32_t ret;
    __asm__ volatile ( "inl %1, %0" : "=a"(ret) : "Nd"(port) );
    return ret;
}

uint32_t pci_config_read_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)((((uint32_t)bus) << 16) | (((uint32_t)slot) << 11) |
                       (((uint32_t)func) << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));
    outl_pci(0xCF8, address);
    return inl_pci(0xCFC);
}

void pci_config_write_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
    uint32_t address = (uint32_t)((((uint32_t)bus) << 16) | (((uint32_t)slot) << 11) |
                       (((uint32_t)func) << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));
    outl_pci(0xCF8, address);
    outl_pci(0xCFC, val);
}

int pci_find_device(uint16_t vendor_id, uint16_t device_id, struct pci_device* out_dev) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            for (uint8_t func = 0; func < 8; func++) {
                uint32_t reg0 = pci_config_read_dword(bus, slot, func, 0);
                uint16_t vendor = reg0 & 0xFFFF;
                if (vendor == 0xFFFF) continue;
                
                uint32_t reg8 = pci_config_read_dword(bus, slot, func, 0x08);
                uint8_t class_code = (reg8 >> 24) & 0xFF;
                uint8_t sub_class = (reg8 >> 16) & 0xFF;

                uint16_t device = (reg0 >> 16) & 0xFFFF;
                
                // Ha fix ID-t keresünk VAGY ha bármilyen Ethernet vezérlőt (Class 02, Subclass 00)
                int match = 0;
                if (vendor_id != 0xFFFF && vendor == vendor_id && (device_id == 0xFFFF || device == device_id)) match = 1;
                else if (vendor_id == 0xFFFF && class_code == 0x02 && sub_class == 0x00) match = 1;

                if (match) {
                    out_dev->bus = bus;
                    out_dev->slot = slot;
                    out_dev->func = func;
                    out_dev->vendor_id = vendor;
                    out_dev->device_id = device;
                    out_dev->bar0 = pci_config_read_dword(bus, slot, func, 0x10);
                    out_dev->interrupt_line = (pci_config_read_dword(bus, slot, func, 0x3C) & 0xFF);
                    return 1;
                }
            }
        }
    }
    return 0;
}
