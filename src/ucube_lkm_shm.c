/*
 *  uCube kernel driver
 *
 * Copyright (C) 2013 University of Nottingham Ningbo China
 * Author: Filippo Savi <filssavi@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/*
 * Shared memory windows for exchanging data with the RPU cores, one per core.
 *
 * Each buffer is allocated at runtime from the DMA/CMA pool, so no device tree
 * carveout is needed. The size is a module parameter (same for every core),
 * the physical addresses are published in sysfs (shm_addr_<core>) and
 * userspace maps a window through /dev/uscope_shm_<core>.
 *
 * The firmware learns its window through a mailbox in its own TCM (see
 * RPU_SHM_MAILBOX_* in ucube_lkm.h). A core's TCM is only powered while
 * remoteproc runs it, so the mailbox is only touched in answer to a doorbell
 * from that core:
 *   - the firmware empties the mailbox, sets its magic and rings at boot;
 *   - on every doorbell from a core whose mailbox has the magic but not the
 *     current window, ucube_shm_announce() writes size, then addr, and rings
 *     back; that doorbell is not passed on to userspace;
 *   - on load the module rings every core with a doorbell, so a core that is
 *     already running rings back and gets the new window.
 */

#include <linux/log2.h>

#include "ucube_lkm.h"

static unsigned long shm_size = SZ_1M;
module_param(shm_size, ulong, 0444);
MODULE_PARM_DESC(shm_size, "Size in bytes of each core's shared memory window "
                 "(power of two, 16 KiB to 1 MiB; default 1 MiB)");

/*
 * The R5 maps its window with one MPU region, which must be a power of two
 * aligned to its own size. dma_alloc_coherent() only aligns a buffer to its
 * size up to CONFIG_CMA_ALIGNMENT (2^8 pages = 1 MiB with 4K pages), so the
 * window is capped at 1 MiB.
 */
#define SHM_SIZE_MAX    SZ_1M

/*
 * The firmware keeps its log in the last 8 KiB of the window and wants at
 * least as much again for the application (SHM_MIN_SIZE in its shm.h).
 */
#define SHM_SIZE_MIN    SZ_16K

#define UCUBE_SHM_ATTRS(n)                                                              \
static ssize_t shm_addr_##n##_show(struct device *dev, struct device_attribute *attr,  \
                                   char *buf)                                           \
{                                                                                       \
    return sysfs_emit(buf, "0x%llx\n", (unsigned long long)dev_data->rpu[n].shm_phys);  \
}                                                                                       \
static ssize_t shm_size_##n##_show(struct device *dev, struct device_attribute *attr,  \
                                   char *buf)                                           \
{                                                                                       \
    return sysfs_emit(buf, "%zu\n", dev_data->rpu[n].shm_size);                         \
}                                                                                       \
static DEVICE_ATTR_RO(shm_addr_##n);                                                    \
static DEVICE_ATTR_RO(shm_size_##n)

UCUBE_SHM_ATTRS(0);
UCUBE_SHM_ATTRS(1);

static struct attribute *ucube_shm_attrs[] = {
    &dev_attr_shm_addr_0.attr,
    &dev_attr_shm_size_0.attr,
    &dev_attr_shm_addr_1.attr,
    &dev_attr_shm_size_1.attr,
    NULL,
};

static const struct attribute_group ucube_shm_attr_group = {
    .attrs = ucube_shm_attrs,
};

static void ucube_shm_free_windows(struct device *dev)
{
    for (int i = 0; i < N_RPU_CORES; i++) {
        struct ucube_rpu *r = &dev_data->rpu[i];

        r->shm_mailbox = NULL;      /* devm mapping, released with the device */
        if (!r->shm_va)
            continue;
        dma_free_coherent(dev, r->shm_size, r->shm_va, r->shm_phys);
        r->shm_va = NULL;
        r->shm_phys = 0;
        r->shm_size = 0;
    }
}

int ucube_shm_alloc(struct device *dev)
{
    int rc;

    if (!shm_size)
        return 0;   /* shared memory disabled */

    if (shm_size < SHM_SIZE_MIN || shm_size > SHM_SIZE_MAX || !is_power_of_2(shm_size)) {
        unsigned long fixed = clamp_t(unsigned long, roundup_pow_of_two(shm_size),
                                      SHM_SIZE_MIN, SHM_SIZE_MAX);

        dev_warn(dev, "shm_size %lu: must be a power of two from %lu to %lu, using %lu\n",
                 shm_size, (unsigned long)SHM_SIZE_MIN, (unsigned long)SHM_SIZE_MAX, fixed);
        shm_size = fixed;
    }

    /*
     * Keep the buffers below 4 GB. On ZynqMP the only DDR there is DDR_LOW
     * (0 - 2 GB), which is the part the RPU can address.
     */
    rc = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
    if (rc) {
        dev_err(dev, "%s: unable to set 32 bit DMA mask: %d\n", __func__, rc);
        return rc;
    }

    for (int i = 0; i < N_RPU_CORES; i++) {
        struct ucube_rpu *r = &dev_data->rpu[i];

        r->shm_va = dma_alloc_coherent(dev, shm_size, &r->shm_phys, GFP_KERNEL);
        if (!r->shm_va) {
            dev_err(dev, "%s: unable to allocate %lu bytes of shared memory for core %d\n",
                    __func__, shm_size, i);
            ucube_shm_free_windows(dev);
            return -ENOMEM;
        }
        r->shm_size = shm_size;
        dev_info(dev, "shared memory %d: %zu bytes at 0x%llx\n",
                 i, r->shm_size, (unsigned long long)r->shm_phys);
    }
    dev_data->shm_dev = dev;

    rc = sysfs_create_group(&dev->kobj, &ucube_shm_attr_group);
    if (rc) {
        ucube_shm_free_windows(dev);
        dev_data->shm_dev = NULL;
        return rc;
    }

    return 0;
}

void ucube_shm_free(struct device *dev)
{
    if (!dev_data->shm_dev)
        return;

    sysfs_remove_group(&dev->kobj, &ucube_shm_attr_group);
    ucube_shm_free_windows(dev);
    dev_data->shm_dev = NULL;
}

int ucube_shm_mmap(struct ucube_rpu *r, struct vm_area_struct *vma)
{
    if (!r->shm_va)
        return -ENODEV;

    /* dma_mmap_coherent() checks vm_pgoff and the length against the buffer */
    return dma_mmap_coherent(dev_data->shm_dev, vma, r->shm_va,
                             r->shm_phys, r->shm_size);
}

/*
 * Map the mailbox in core r's BTCM. Only maps it: the TCM may be powered off
 * until remoteproc starts the core, so nothing is read or written here.
 */
void ucube_shm_map_mailbox(struct device *dev, struct ucube_rpu *r)
{
    static const unsigned long btcm_base[N_RPU_CORES] = { RPU0_BTCM_BASE, RPU1_BTCM_BASE };

    if (!r->shm_va)
        return;

    /* The R5 can only reach DDR_LOW (below 2 GB): no point announcing it */
    if (r->shm_phys + r->shm_size > SZ_2G) {
        dev_warn(dev, "shared memory %d at 0x%llx is out of the R5's reach, not announced\n",
                 r->core, (unsigned long long)r->shm_phys);
        return;
    }

    r->shm_mailbox = devm_ioremap(dev, btcm_base[r->core] + RPU_SHM_MAILBOX_OFFSET,
                                  RPU_SHM_MAILBOX_LEN);
    if (!r->shm_mailbox)
        dev_warn(dev, "cannot map the shared memory mailbox of core %d\n", r->core);
}

/*
 * Core r just rang: if its firmware has a mailbox that does not hold the
 * current window, write the window there and ring back. The core is running,
 * so its TCM is powered. Called from the IPI receive callback (atomic).
 *
 * Returns true if the doorbell was answered with the window, false if it is
 * an ordinary doorbell (or the firmware has no mailbox).
 */
bool ucube_shm_announce(struct ucube_rpu *r)
{
    void __iomem *mb = r->shm_mailbox;
    u32 addr = lower_32_bits(r->shm_phys);
    u32 size = r->shm_size;

    if (!mb || readl(mb + RPU_SHM_MAILBOX_MAGIC_REG) != RPU_SHM_MAILBOX_MAGIC)
        return false;

    if (readl(mb + RPU_SHM_MAILBOX_ADDR_REG) == addr &&
        readl(mb + RPU_SHM_MAILBOX_SIZE_REG) == size)
        return false;

    /* size first: the firmware takes a non-zero addr to mean "complete" */
    writel(size, mb + RPU_SHM_MAILBOX_SIZE_REG);
    writel(addr, mb + RPU_SHM_MAILBOX_ADDR_REG);   /* writel orders it after size */

    if (ucube_ipi_ring(r))
        dev_warn(dev_data->shm_dev, "cannot ring core %d after announcing its window\n", r->core);
    return true;
}
