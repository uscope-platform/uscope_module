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

static __poll_t ucube_lkm_poll(struct file *flip , struct poll_table_struct * poll_struct){
    int minor = MINOR(flip->f_inode->i_rdev);
    if(minor == 0){
        __poll_t mask = 0;
        mask |= POLLIN | POLLRDNORM;
        return mask;
    }
    if(ucube_minor_rpu(minor)){
        /* Readable once this file's R5 has rung since its last read() */
        struct ucube_rpu *r = ucube_minor_rpu(minor);

        poll_wait(flip, &r->ipi_wq, poll_struct);
        if ((unsigned int)atomic_read(&r->ipi_count) != (uintptr_t)flip->private_data)
            return POLLIN | POLLRDNORM;
    }
    return 0;
}


static int ucube_lkm_mmap(struct file *filp, struct vm_area_struct *vma){
    uint64_t mapping_start_address = vma->vm_pgoff << PAGE_SHIFT;
    uint32_t mapping_size = vma->vm_end - vma->vm_start;
    uint64_t mapping_stop_address = mapping_start_address +  mapping_size;

    int minor = MINOR(filp->f_inode->i_rdev);
    uint64_t mapping_limit_base_0, mapping_limit_top_0;
    uint64_t mapping_limit_base_1, mapping_limit_top_1;

    /* The shared memory windows have their own mapping rules */
    if(ucube_minor_rpu(minor))
        return ucube_shm_mmap(ucube_minor_rpu(minor), vma);

    if(dev_data->is_zynqmp){
        mapping_limit_base_0 = ZYNQMP_BUS_0_ADDRESS_BASE;
        mapping_limit_top_0 = ZYNQMP_BUS_0_ADDRESS_TOP;
        mapping_limit_base_1 = ZYNQMP_BUS_1_ADDRESS_BASE;
        mapping_limit_top_1 = ZYNQMP_BUS_1_ADDRESS_TOP;
    } else {
        mapping_limit_base_0 = ZYNQ_BUS_0_ADDRESS_BASE;
        mapping_limit_top_0 = ZYNQ_BUS_0_ADDRESS_TOP;
        mapping_limit_base_1 = ZYNQ_BUS_1_ADDRESS_BASE;
        mapping_limit_top_1 = ZYNQ_BUS_1_ADDRESS_TOP;
    }
    switch (minor) {
        case 0:
            pr_err("%s: mmapping of data memory is not supported\n", __func__);
            return -1;
        case 1:
            if( mapping_start_address < mapping_limit_base_0 ){
                pr_err("%s: attempting to map memory below the control bus address range (%llx)\n", __func__, mapping_start_address);
                return -2;
            }
            if( mapping_stop_address > mapping_limit_top_0){
                pr_err("%s: attempting to map memory above the control bus address range (%llx)\n", __func__, mapping_stop_address);
                return -2;
            }
            break;
        case 2:
            if( mapping_start_address < mapping_limit_base_1 ){
                pr_err("%s: attempting to map memory below the core bus address range (%llx)\n", __func__, mapping_start_address);
                return -2;
            }
            if( mapping_stop_address > mapping_limit_top_1){
                pr_err("%s: attempting to map memory above the core bus address range (%llx)\n", __func__, mapping_stop_address);
                return -2;
            }
            break;
    }

    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
    if (remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff, mapping_size ,vma->vm_page_prot))
    return -EAGAIN;

    pr_info("%s: Mapped emmory from %llx to %llx\n", __func__, mapping_start_address, mapping_stop_address);
    return 0;
}



static long ucube_lkm_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
    int minor = MINOR(filp->f_inode->i_rdev);
    if(minor == 0){
        switch (cmd){
        case IOCTL_NEW_DATA_AVAILABLE:
            return dev_data->new_data_available;
            break;
        default:
            return -EINVAL;
            break;
        }
        return 0;
    } else if(minor == 3){
        switch (cmd){
        case IOCTL_PROGRAM_FPGA:
            pr_info("%s: FPGA BITSTREAM LENGTH: %lu\n", __func__, dev_data->bitstream_len);
            ucube_program_fpga();
            break;
        default:
            return -EINVAL;
            break;
        }
        return 0;
    } else if(ucube_minor_rpu(minor)){
        switch (cmd){
        case IOCTL_RING_RPU:
            return ucube_ipi_ring(ucube_minor_rpu(minor));
        default:
            return -EINVAL;
        }
    } else{
        return 0;
    }
    
}              


static int ucube_lkm_open(struct inode *inode, struct file *file) {

    pr_info("%s: In open\n", __func__);

    /* Shared memory: only doorbells arriving after open() count for this file */
    struct ucube_rpu *r = ucube_minor_rpu(MINOR(inode->i_rdev));

    if (r)
        file->private_data = (void *)(uintptr_t)(unsigned int)atomic_read(&r->ipi_count);

    return 0;
}

static int ucube_lkm_release(struct inode *inode, struct file *file) {
    pr_info("%s: In release\n", __func__);
    return 0;
}

static ssize_t ucube_lkm_read(
    struct file *flip, char __user *buffer, size_t count, loff_t *offset
) {
    size_t datalen;
    unsigned long ret;
    char result;
    int minor = MINOR(flip->f_inode->i_rdev);
    if(minor == 0){
        datalen = dev_data->dma_buf_size;

        if (count > datalen) {
            count = datalen;
        }

        if(!dev_data->is_zynqmp){
            ret = copy_to_user(buffer, dev_data->read_data_buffer_32, count);
        }else{
            ret = copy_to_user(buffer, dev_data->read_data_buffer_64, count);
        }

        if(ret) {
            return -EFAULT;
        }
        dev_data->new_data_available = 0;
        return count;    
    } else if(minor == 3){
        result = ucube_fpga_loaded() ?'1' : '0';
        if (copy_to_user(buffer, &result, 1)) return -EFAULT;
        return 1;
    } else if(ucube_minor_rpu(minor)){
        /* Wait for this file's R5 to ring; return how many doorbells arrived since the last read */
        struct ucube_rpu *r = ucube_minor_rpu(minor);
        unsigned int seen = (uintptr_t)flip->private_data;
        unsigned int now;
        u32 n;

        if (count < sizeof(n))
            return -EINVAL;
        if ((unsigned int)atomic_read(&r->ipi_count) == seen) {
            if (flip->f_flags & O_NONBLOCK)
                return -EAGAIN;
            if (wait_event_interruptible(r->ipi_wq,
                    (unsigned int)atomic_read(&r->ipi_count) != seen))
                return -ERESTARTSYS;
        }
        now = atomic_read(&r->ipi_count);
        n = now - seen;
        flip->private_data = (void *)(uintptr_t)now;
        if (copy_to_user(buffer, &n, sizeof(n)))
            return -EFAULT;
        return sizeof(n);
    }

    return 0;
}


static ssize_t ucube_lkm_write(
    struct file *flip, const char __user *buffer, size_t len, loff_t *offset
) {
    size_t needed;
    int minor = MINOR(flip->f_inode->i_rdev);
    if(minor == 3){
        needed = *offset + len;

        if (needed > BITSTREAM_BUFFER_SIZE)
            return -EINVAL;

        if (copy_from_user(dev_data->bitstream_buffer + *offset, buffer, len))
            return -EFAULT;

        *offset += len;
        if (dev_data->bitstream_len < *offset)
            dev_data->bitstream_len = *offset;
        return len;
    }
    
    pr_info("%s: In write with minor number %d\n", __func__, minor);
    return len;
}


/* This structure points to all of the device functions */
struct file_operations ucube_lkm_file_ops = {
    .read = ucube_lkm_read,
    .write = ucube_lkm_write,
    .open = ucube_lkm_open,
    .unlocked_ioctl = ucube_lkm_ioctl,
    .poll = ucube_lkm_poll,
    .release = ucube_lkm_release,
    .mmap = ucube_lkm_mmap
};
