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

#ifndef UCUBE_LKM_H
#define UCUBE_LKM_H

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/vmalloc.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/poll.h>
#include <asm/pgtable.h>
#include <linux/clk.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/fpga/fpga-mgr.h>
#include <linux/fpga/fpga-region.h>

#define N_MINOR_NUMBERS	4

#define N_SCOPE_CHANNELS 6
#define KERNEL_BUFFER_LENGTH N_SCOPE_CHANNELS*1024*sizeof(u64)
#define BITSTREAM_BUFFER_SIZE 32000000


#define IRQ_NUMBER 22

#define IOCTL_NEW_DATA_AVAILABLE 1
#define IOCTL_GET_BUFFER_ADDRESS 2
#define IOCTL_PROGRAM_FPGA 3


#define ZYNQ_BUS_0_ADDRESS_BASE 0x40000000
#define ZYNQ_BUS_0_ADDRESS_TOP 0x7fffffff

#define ZYNQ_BUS_1_ADDRESS_BASE 0x80000000
#define ZYNQ_BUS_1_ADDRESS_TOP 0xBfffffff

#define ZYNQMP_BUS_0_ADDRESS_BASE 0x400000000
#define ZYNQMP_BUS_0_ADDRESS_TOP  0x4FFFFFFFF

#define ZYNQMP_BUS_1_ADDRESS_BASE 0x500000000
#define ZYNQMP_BUS_1_ADDRESS_TOP  0x5FFFFFFFF



#define FCLK_0_DEFAULT_FREQ 100000000
#define FCLK_1_DEFAULT_FREQ 40000000
#define FCLK_2_DEFAULT_FREQ 40000000
#define FCLK_3_DEFAULT_FREQ 40000000

/* One PL reference clock plus its sysfs controls */
struct ucube_fclk {
    struct clk *clk;
    bool on;
    struct device_attribute rate_attr;
    struct device_attribute en_attr;
};

/* STRUCTURE FOR THE DEVICE SPECIFIC DATA*/
struct scope_device_data {
    struct device_node *fpga_node;
    struct device devs[N_MINOR_NUMBERS];
    struct cdev cdevs[N_MINOR_NUMBERS];
    u32 *read_data_buffer_32;
    u64 *read_data_buffer_64;
    u32 *dma_buffer_32;
    u64 *dma_buffer_64;
    u8 *bitstream_buffer;
    size_t bitstream_len; 
    dma_addr_t physaddr;
    int new_data_available;
    struct ucube_fclk fclk[4];
    struct mutex fclk_lock;
    bool is_zynqmp;
    u32 dma_buf_size;
};

/* Shared module state */
extern struct scope_device_data *dev_data;
extern int irq_line;
/* DMA and interrupt handling */
extern const struct attribute_group uscope_lkm_attr_group;
irqreturn_t ucube_lkm_irq(int irq, void *dev_id);

/* Character device operations */
extern struct file_operations ucube_lkm_file_ops;

/* FPGA programming */
int ucube_program_fpga(void);
bool ucube_fpga_loaded(void);

/* PL reference clock control */
int ucube_fclk_set_enabled(struct ucube_fclk *f, bool on);
void ucube_fclk_add_files(struct device *dev, struct ucube_fclk *f, int i);

#endif /* UCUBE_LKM_H */
