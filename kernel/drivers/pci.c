/* =============================================================================
 *  AGOS - PCI configuration space, mechanism #1 (pci.c)
 * ========================================================================== */
#include "pci.h"
#include "io.h"

#define CFG_ADDR 0xCF8
#define CFG_DATA 0xCFC

static inline uint32_t cfg_addr(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off)
{
    return 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)(dev & 31) << 11) |
           ((uint32_t)(fn & 7) << 8) | (off & 0xFC);
}

bool pci_present(void)
{
    uint32_t saved = inl(CFG_ADDR);
    bool ok;

    outl(CFG_ADDR, 0x80000000u);
    ok = inl(CFG_ADDR) == 0x80000000u;
    outl(CFG_ADDR, saved);
    return ok;
}

uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off)
{
    outl(CFG_ADDR, cfg_addr(bus, dev, fn, off));
    return inl(CFG_DATA);
}

uint16_t pci_read16(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off)
{
    return (uint16_t)(pci_read32(bus, dev, fn, off) >> ((off & 2) * 8));
}

uint8_t pci_read8(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off)
{
    return (uint8_t)(pci_read32(bus, dev, fn, off) >> ((off & 3) * 8));
}

void pci_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off, uint32_t v)
{
    outl(CFG_ADDR, cfg_addr(bus, dev, fn, off));
    outl(CFG_DATA, v);
}

void pci_write16(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off, uint16_t v)
{
    uint32_t old = pci_read32(bus, dev, fn, off);
    unsigned sh = (off & 2) * 8;

    old = (old & ~(0xFFFFu << sh)) | ((uint32_t)v << sh);
    pci_write32(bus, dev, fn, off, old);
}

void pci_load(struct pci_dev *d)
{
    uint32_t id  = pci_read32(d->bus, d->dev, d->fn, PCI_REG_VENDOR);
    uint32_t cls = pci_read32(d->bus, d->dev, d->fn, PCI_REG_REVISION);
    uint32_t ss  = pci_read32(d->bus, d->dev, d->fn, PCI_REG_SUBSYS);

    d->vendor     = (uint16_t)id;
    d->device     = (uint16_t)(id >> 16);
    d->revision   = (uint8_t)cls;
    d->prog_if    = (uint8_t)(cls >> 8);
    d->subclass   = (uint8_t)(cls >> 16);
    d->class_code = (uint8_t)(cls >> 24);
    d->subsys_vendor = (uint16_t)ss;
    d->subsys_device = (uint16_t)(ss >> 16);
    for (unsigned i = 0; i < 6; i++)
        d->bar[i] = pci_read32(d->bus, d->dev, d->fn, (uint8_t)(PCI_REG_BAR0 + i * 4));
}

/* Visit every function; stop when `match` returns true. */
static bool scan(bool (*match)(const struct pci_dev *, const void *), const void *arg,
                 struct pci_dev *out)
{
    for (unsigned bus = 0; bus < 256; bus++) {
        for (unsigned dev = 0; dev < 32; dev++) {
            uint8_t hdr;

            if ((uint16_t)pci_read32((uint8_t)bus, (uint8_t)dev, 0, PCI_REG_VENDOR) == 0xFFFF)
                continue;
            hdr = pci_read8((uint8_t)bus, (uint8_t)dev, 0, PCI_REG_HEADER);

            for (unsigned fn = 0; fn < ((hdr & 0x80) ? 8u : 1u); fn++) {
                struct pci_dev d;

                d.bus = (uint8_t)bus;
                d.dev = (uint8_t)dev;
                d.fn = (uint8_t)fn;
                if ((uint16_t)pci_read32(d.bus, d.dev, d.fn, PCI_REG_VENDOR) == 0xFFFF)
                    continue;
                pci_load(&d);
                if (match(&d, arg)) {
                    *out = d;
                    return true;
                }
            }
        }
        /* Buses are normally numbered contiguously: stop at the first empty
         * one beyond bus 0 to keep boot fast on machines without bridges. */
        if (bus >= 1 && (uint16_t)pci_read32((uint8_t)bus, 0, 0, PCI_REG_VENDOR) == 0xFFFF &&
            bus >= 3)
            break;
    }
    return false;
}

static bool match_id(const struct pci_dev *d, const void *arg)
{
    const uint16_t *ids = arg;
    return d->vendor == ids[0] && d->device == ids[1];
}

static bool match_class(const struct pci_dev *d, const void *arg)
{
    const uint8_t *c = arg;
    return d->class_code == c[0] && d->subclass == c[1];
}

bool pci_find(uint16_t vendor, uint16_t device, struct pci_dev *out)
{
    uint16_t ids[2] = { vendor, device };

    if (!pci_present())
        return false;
    return scan(match_id, ids, out);
}

bool pci_find_class(uint8_t class_code, uint8_t subclass, struct pci_dev *out)
{
    uint8_t c[2] = { class_code, subclass };

    if (!pci_present())
        return false;
    return scan(match_class, c, out);
}

uint32_t pci_bar_size(const struct pci_dev *d, unsigned index)
{
    uint8_t off = (uint8_t)(PCI_REG_BAR0 + index * 4);
    uint32_t orig, probe, flags = cpu_irq_save();
    uint16_t cmd = pci_read16(d->bus, d->dev, d->fn, PCI_REG_COMMAND);
    uint32_t size = 0;

    if (index < 6) {
        pci_write16(d->bus, d->dev, d->fn, PCI_REG_COMMAND,
                    (uint16_t)(cmd & ~(PCI_CMD_IO | PCI_CMD_MEMORY)));
        orig = pci_read32(d->bus, d->dev, d->fn, off);
        pci_write32(d->bus, d->dev, d->fn, off, 0xFFFFFFFFu);
        probe = pci_read32(d->bus, d->dev, d->fn, off);
        pci_write32(d->bus, d->dev, d->fn, off, orig);
        pci_write16(d->bus, d->dev, d->fn, PCI_REG_COMMAND, cmd);

        probe &= (orig & 1) ? 0xFFFFFFFCu : 0xFFFFFFF0u;
        if (probe)
            size = ~probe + 1u;
    }
    cpu_irq_restore(flags);
    return size;
}

void pci_set_command(const struct pci_dev *d, uint16_t set_bits)
{
    uint16_t cmd = pci_read16(d->bus, d->dev, d->fn, PCI_REG_COMMAND);

    if ((cmd & set_bits) != set_bits)
        pci_write16(d->bus, d->dev, d->fn, PCI_REG_COMMAND, (uint16_t)(cmd | set_bits));
}
