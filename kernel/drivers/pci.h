/* =============================================================================
 *  AGOS - PCI configuration space, mechanism #1 (pci.h / pci.c)
 *  Port 0xCF8 (address) / 0xCFC (data). Enough to find a device by ID or class
 *  and to read/size its BARs.
 * ========================================================================== */
#ifndef AGOS_PCI_H
#define AGOS_PCI_H

#include <stdbool.h>
#include <stdint.h>

#define PCI_REG_VENDOR   0x00
#define PCI_REG_DEVICE   0x02
#define PCI_REG_COMMAND  0x04
#define PCI_REG_STATUS   0x06
#define PCI_REG_REVISION 0x08
#define PCI_REG_PROGIF   0x09
#define PCI_REG_SUBCLASS 0x0A
#define PCI_REG_CLASS    0x0B
#define PCI_REG_HEADER   0x0E
#define PCI_REG_BAR0     0x10
#define PCI_REG_SUBSYS   0x2C

#define PCI_CMD_IO       0x0001
#define PCI_CMD_MEMORY   0x0002
#define PCI_CMD_MASTER   0x0004

#define PCI_CLASS_DISPLAY 0x03

struct pci_dev {
    uint8_t  bus, dev, fn;
    uint16_t vendor, device;
    uint8_t  class_code, subclass, prog_if, revision;
    uint32_t bar[6];                    /* raw BAR values                      */
    uint16_t subsys_vendor, subsys_device;
};

bool     pci_present(void);             /* is configuration mechanism #1 there */
uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off);
uint16_t pci_read16(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off);
uint8_t  pci_read8(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off);
void     pci_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off, uint32_t v);
void     pci_write16(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off, uint16_t v);

/* Fill *out from configuration space (bus/dev/fn must already be set). */
void     pci_load(struct pci_dev *d);

/* Search all buses for vendor:device / for a class:subclass. */
bool     pci_find(uint16_t vendor, uint16_t device, struct pci_dev *out);
bool     pci_find_class(uint8_t class_code, uint8_t subclass, struct pci_dev *out);

/* Size of memory/IO BAR `index` in bytes (0 if unimplemented). Briefly turns
 * decoding off and interrupts off while probing. */
uint32_t pci_bar_size(const struct pci_dev *d, unsigned index);

void     pci_set_command(const struct pci_dev *d, uint16_t set_bits);

#endif /* AGOS_PCI_H */
