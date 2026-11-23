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

#include "ucube_lkm.h"

irqreturn_t ucube_lkm_irq(int irq, void *dev_id)  {
    if(!dev_data->is_zynqmp){
        memcpy(dev_data->read_data_buffer_32, dev_data->dma_buffer_32, dev_data->dma_buf_size);
    }else{
        memcpy(dev_data->read_data_buffer_64, dev_data->dma_buffer_64, dev_data->dma_buf_size);
    }
    dev_data->new_data_available = 1;
    return IRQ_RETVAL(1);
}


static ssize_t dma_addr_show(struct device *dev, struct device_attribute *mattr, char *data) {
    #if defined(__arm__)
    return sprintf(data, "%lu\n", dev_data->physaddr);
    #elif defined(__aarch64__)
    return sprintf(data, "%llu\n", dev_data->physaddr);
    #else
    printk(KERN_ALERT "Hello, kernel. Unknown mode!\n");
    return 0;
    #endif
}

static ssize_t dma_addr_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len) {
        return 0;
}


static ssize_t dma_buf_size_show(struct device *dev, struct device_attribute *mattr, char *data) {
    return sprintf(data, "%u\n", dev_data->dma_buf_size);
}

static ssize_t dma_buf_size_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len) {
    unsigned long size;
    u64 prev_size;
    if(kstrtoul(buf, 0, &size))
        return -EINVAL;
    
    pr_info("%s: Requested buffer size: %s\n", __func__, buf);
    
    prev_size = dev_data->dma_buf_size;

    dev_data->dma_buf_size = size;
        
    if(!dev_data->is_zynqmp){
        dma_free_coherent(
            &dev_data->devs[0],
            prev_size,
            dev_data->dma_buffer_32,
            dev_data->physaddr
        );

        dev_data->dma_buffer_32 = dma_alloc_coherent(
            &dev_data->devs[0],
            dev_data->dma_buf_size,
            &(dev_data->physaddr),
            GFP_KERNEL 
        );


        vfree(dev_data->read_data_buffer_32);
        dev_data->read_data_buffer_32 = vmalloc(dev_data->dma_buf_size);
    } else{

        dma_free_coherent(
            &dev_data->devs[0],
            prev_size,
            dev_data->dma_buffer_64,
            dev_data->physaddr
        );

        dev_data->dma_buffer_64 = dma_alloc_coherent(
            &dev_data->devs[0],
            dev_data->dma_buf_size,
            &(dev_data->physaddr),
            GFP_KERNEL
        );

        vfree(dev_data->read_data_buffer_64);
        dev_data->read_data_buffer_64 = vmalloc(dev_data->dma_buf_size);
    }

    return len;
}

static DEVICE_ATTR(dma_addr, 0444, dma_addr_show, dma_addr_store);
static DEVICE_ATTR(dma_buf_size, 0644, dma_buf_size_show, dma_buf_size_store);

static struct attribute *uscope_lkm_attrs[] = {
	&dev_attr_dma_addr.attr,
	&dev_attr_dma_buf_size.attr,
	NULL,
};

const struct attribute_group uscope_lkm_attr_group = {
	.attrs = uscope_lkm_attrs,
};
