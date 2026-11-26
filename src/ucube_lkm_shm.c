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
 */

#include "ucube_lkm.h"

static unsigned long shm_size = SZ_1M;
module_param(shm_size, ulong, 0444);
MODULE_PARM_DESC(shm_size, "Size in bytes of each core's shared memory window (default 1 MiB)");

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

    shm_size = PAGE_ALIGN(shm_size);

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
