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
#include <linux/mailbox_client.h>
#include <linux/wait.h>
#include <linux/atomic.h>

#define N_MINOR_NUMBERS	6

/* One shared memory + doorbell device per RPU core: /dev/uscope_shm_<core> */
#define N_RPU_CORES 2
#define SHM_MINOR_BASE 4        /* minor of /dev/uscope_shm_0 */

#define N_SCOPE_CHANNELS 6
#define KERNEL_BUFFER_LENGTH N_SCOPE_CHANNELS*1024*sizeof(u64)
#define BITSTREAM_BUFFER_SIZE 32000000


#define IRQ_NUMBER 22

#define IOCTL_NEW_DATA_AVAILABLE 1
#define IOCTL_GET_BUFFER_ADDRESS 2
#define IOCTL_PROGRAM_FPGA 3
#define IOCTL_RING_RPU 4        /* on /dev/uscope_shm_<core>: ring that core */


#define ZYNQ_BUS_0_ADDRESS_BASE 0x40000000
#define ZYNQ_BUS_0_ADDRESS_TOP 0x7fffffff

#define ZYNQ_BUS_1_ADDRESS_BASE 0x80000000
#define ZYNQ_BUS_1_ADDRESS_TOP 0xBfffffff

#define ZYNQMP_BUS_0_ADDRESS_BASE 0x400000000
#define ZYNQMP_BUS_0_ADDRESS_TOP  0x4FFFFFFFF

#define ZYNQMP_BUS_1_ADDRESS_BASE 0x500000000
#define ZYNQMP_BUS_1_ADDRESS_TOP  0x5FFFFFFFF



/* R5 clock = RPLL / RPU_CLK_DIV; rpu_clk accepts RPU_CLK_MIN..RPU_CLK_MAX */
#define RPU_CLK_DIV 2
#define RPU_CLK_MIN 375000000U
#define RPU_CLK_MAX 500000000U

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

/* Per RPU core: shared memory window and IPI doorbell (ucube_lkm_shm.c, ucube_lkm_ipi.c) */
struct ucube_rpu {
    int core;
    /* Shared memory window */
    void *shm_va;
    dma_addr_t shm_phys;
    size_t shm_size;
    /* IPI doorbell, DT mailboxes "tx_<core>" / "rx_<core>" */
    struct mbox_client ipi_cl;
    struct mbox_chan *ipi_tx;
    struct mbox_chan *ipi_rx;
    wait_queue_head_t ipi_wq;
    atomic_t ipi_count;         /* doorbells received from this core */
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
    struct ucube_fclk rpu_pll;      /* ZynqMP RPLL, normally the R5 clock source */
    struct mutex fclk_lock;
    bool is_zynqmp;
    u32 dma_buf_size;
    /* Platform device the shared memory windows were allocated for */
    struct device *shm_dev;
    struct ucube_rpu rpu[N_RPU_CORES];
};

/* Shared module state */
extern struct scope_device_data *dev_data;

/* RPU core served by a /dev/uscope_shm_<core> minor, or NULL for other minors */
static inline struct ucube_rpu *ucube_minor_rpu(int minor)
{
    if (minor < SHM_MINOR_BASE || minor >= SHM_MINOR_BASE + N_RPU_CORES)
        return NULL;
    return &dev_data->rpu[minor - SHM_MINOR_BASE];
}

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
void ucube_clk_add_files(struct device *dev, struct ucube_fclk *f, const char *name);
void ucube_rpu_clk_add_files(struct device *dev, struct ucube_fclk *f);


/* Shared memory window */
int ucube_shm_alloc(struct device *dev);
void ucube_shm_free(struct device *dev);
int ucube_shm_mmap(struct ucube_rpu *r, struct vm_area_struct *vma);

/* IPI doorbell */
void ucube_ipi_init(struct device *dev);
void ucube_ipi_free(void);
int ucube_ipi_ring(struct ucube_rpu *r);

#endif /* UCUBE_LKM_H */
