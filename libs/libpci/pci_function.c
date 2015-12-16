#include <asm/cpu.h>
#include <yaos/types.h>
#include <drivers/pci_function.h>
#include <drivers/pci_device.h>
void pci_bar_init(struct pci_bar *p,pci_device_t pci,u32 pos)
{
    p->_dev=pci;
    p->_pos=pos;
    u32 val = pci_readl(p->_dev, p->_pos);
pci_d("val:%lx\n",val);
    p->_is_mmio = ((val & PCI_BAR_MEMORY_INDICATOR_MASK) == PCI_BAR_MMIO);
    if (p->_is_mmio) {
        p->_is_64 = ((val & PCI_BAR_MEM_ADDR_SPACE_MASK)
                     == PCI_BAR_64BIT_ADDRESS);
        p->_is_prefetchable = ((val & PCI_BAR_PREFETCHABLE_MASK)
                               == PCI_BAR_PREFETCHABLE);
pci_d("_is_mmio:%d,is_64:%d,_is_prefetchable:%d\n",p->_is_mmio,p->_is_64,p->_is_prefetchable);
    }
    p->_addr_size = pci_bar_read_size(p);
pci_d("addr_size:%lx\n",p->_addr_size);
    val = arch_add_pci_bar(val);
    if (p->_is_mmio) {
        p->_addr_lo = val & PCI_BAR_MEM_ADDR_LO_MASK;
        if (p->_is_64) {
            p->_addr_hi = pci_readl(p->_dev, p->_pos + 4);
        }
    }
    else {
        p->_addr_lo = val & PCI_BAR_PIO_ADDR_MASK;
    }
    p->_addr_64 = ((u64) p->_addr_hi << 32) | (u64) (p->_addr_lo);
pci_d("addr_64:%lx\n",p->_addr_64);
}

u64 pci_bar_read_size(struct pci_bar *p)
{
    u32 hi;
    u64 bits;
    u32 lo_orig = pci_readl(p->_dev, p->_pos);
    pci_writel(p->_dev, p->_pos, 0xFFFFFFFF);
    u32 lo = pci_readl(p->_dev, p->_pos);
    pci_writel(p->_dev,p->_pos, lo_orig);
    if (pci_bar_is_pio(p)) {
        lo &= PCI_BAR_PIO_ADDR_MASK;
    }
    else {
        lo &= PCI_BAR_MEM_ADDR_LO_MASK;
    }
    hi = 0xFFFFFFFF;
    if (pci_bar_is_64(p)) {
        u32 hi_orig = pci_readl(p->_dev, p->_pos + 4);

        pci_writel(p->_dev, p->_pos + 4, 0xFFFFFFFF);
        hi = pci_readl(p->_dev, p->_pos + 4);
        pci_writel(p->_dev, p->_pos + 4, hi_orig);

    }
    bits = (u64) hi << 32 | lo;
    return ~bits + 1;
}
