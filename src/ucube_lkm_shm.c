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
 * Shared memory window for exchanging data with other cores (e.g. the RPU).
 *
 * The buffer is allocated at runtime from the DMA/CMA pool, so no device tree
 * carveout is needed. Its size is a module parameter, its physical address is
 * published in sysfs (shm_addr) and userspace maps it through /dev/uscope_shm.
 */

#include "ucube_lkm.h"

static unsigned long shm_size = SZ_1M;
module_param(shm_size, ulong, 0444);
MODULE_PARM_DESC(shm_size, "Size in bytes of the shared memory window (default 1 MiB)");

static ssize_t shm_addr_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sysfs_emit(buf, "0x%llx\n", (unsigned long long)dev_data->shm_phys);
}

static ssize_t shm_size_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sysfs_emit(buf, "%zu\n", dev_data->shm_size);
}

static DEVICE_ATTR_RO(shm_addr);
static DEVICE_ATTR_RO(shm_size);

static struct attribute *ucube_shm_attrs[] = {
    &dev_attr_shm_addr.attr,
    &dev_attr_shm_size.attr,
    NULL,
};

static const struct attribute_group ucube_shm_attr_group = {
    .attrs = ucube_shm_attrs,
};

int ucube_shm_alloc(struct device *dev)
{
    int rc;

    if (!shm_size)
        return 0;   /* shared memory disabled */

    shm_size = PAGE_ALIGN(shm_size);

    /*
     * Keep the buffer below 4 GB. On ZynqMP the only DDR there is DDR_LOW
     * (0 - 2 GB), which is the part the RPU can address.
     */
    rc = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
    if (rc) {
        dev_err(dev, "%s: unable to set 32 bit DMA mask: %d\n", __func__, rc);
        return rc;
    }

    dev_data->shm_va = dma_alloc_coherent(dev, shm_size, &dev_data->shm_phys, GFP_KERNEL);
    if (!dev_data->shm_va) {
        dev_err(dev, "%s: unable to allocate %lu bytes of shared memory\n", __func__, shm_size);
        return -ENOMEM;
    }
    dev_data->shm_size = shm_size;
    dev_data->shm_dev = dev;

    rc = sysfs_create_group(&dev->kobj, &ucube_shm_attr_group);
    if (rc) {
        ucube_shm_free(dev);
        return rc;
    }

    dev_info(dev, "shared memory: %zu bytes at 0x%llx\n",
             dev_data->shm_size, (unsigned long long)dev_data->shm_phys);
    return 0;
}

void ucube_shm_free(struct device *dev)
{
    if (!dev_data->shm_va)
        return;

    sysfs_remove_group(&dev->kobj, &ucube_shm_attr_group);
    dma_free_coherent(dev, dev_data->shm_size, dev_data->shm_va, dev_data->shm_phys);
    dev_data->shm_va = NULL;
    dev_data->shm_phys = 0;
    dev_data->shm_size = 0;
    dev_data->shm_dev = NULL;
}

int ucube_shm_mmap(struct vm_area_struct *vma)
{
    if (!dev_data->shm_va)
        return -ENODEV;

    /* dma_mmap_coherent() checks vm_pgoff and the length against the buffer */
    return dma_mmap_coherent(dev_data->shm_dev, vma, dev_data->shm_va,
                             dev_data->shm_phys, dev_data->shm_size);
}
