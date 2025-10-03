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

/* Prototypes for device functions */
static int ucube_program_fpga(void);

bool ucube_fpga_loaded(void);
static int ucube_lkm_open(struct inode *, struct file *);
static int ucube_lkm_release(struct inode *, struct file *);
static ssize_t ucube_lkm_read(struct file *, char *, size_t, loff_t *);
static ssize_t ucube_lkm_write(struct file *, const char *, size_t, loff_t *);
static long ucube_lkm_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
static int ucube_lkm_mmap(struct file *filp, struct vm_area_struct *vma);
static __poll_t ucube_lkm_poll(struct file *, struct poll_table_struct *);
int ucube_lkm_probe(struct platform_device *dev);
int ucube_lkm_remove(struct platform_device *dev);


static dev_t device_number;
static struct class *uCube_class;
static struct scope_device_data *dev_data;

static int irq_line;

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
    struct clk *fclk[4];
    bool is_zynqmp;
    u32 dma_buf_size;
};


int ucube_program_fpga(void){
    int ret;
    struct fpga_image_info *info;
    struct fpga_region *region;


    pr_info("%s: Start FPGA programming", __func__);

    
    region = fpga_region_class_find(NULL, dev_data->fpga_node, device_match_of_node);
    if (!region) return -ENODEV;


    info = fpga_image_info_alloc(&dev_data->devs[3]);
    if (!info) return -ENOMEM;
    
    info->buf = dev_data->bitstream_buffer;
    info->count = dev_data->bitstream_len;
    region->info = info;
    ret = fpga_region_program_fpga(region);

    pr_info("%s: Programming successfull", __func__);

    region->info = NULL;
    fpga_image_info_free(info);

    put_device(&region->dev);

    return ret;
}


bool ucube_fpga_loaded(void){
    
    struct fpga_region *region;
    struct fpga_manager *mgr;
    region = fpga_region_class_find(NULL, dev_data->fpga_node, device_match_of_node);
    if (!region) {
        pr_err("%s: FPGA region not found\n", __func__);
        return -ENODEV;
    }
    
    // Get the FPGA manager from the region
    mgr = region->mgr;
    if (!mgr) {
        put_device(&region->dev);
        pr_err("%s: FPGA manager not found\n", __func__);
        return -ENODEV;
    }

    put_device(&region->dev);

    return mgr->state == FPGA_MGR_STATE_OPERATING;
}

static ssize_t fclk_0_show(struct device *dev, struct device_attribute *mattr, char *data) {
    if(!dev_data->is_zynqmp){
        unsigned long freq = clk_get_rate(dev_data->fclk[0]);
        return sprintf(data, "%lu\n", freq);
    } else {
        return 0;
    }
}
static ssize_t fclk_1_show(struct device *dev, struct device_attribute *mattr, char *data) {
    if(!dev_data->is_zynqmp){
        unsigned long freq = clk_get_rate(dev_data->fclk[1]);
        return sprintf(data, "%lu\n", freq);
    } else {
        return 0;
    }
}
static ssize_t fclk_2_show(struct device *dev, struct device_attribute *mattr, char *data) {
    if(!dev_data->is_zynqmp){
        unsigned long freq = clk_get_rate(dev_data->fclk[2]);
        return sprintf(data, "%lu\n", freq);
    } else {
        return 0;
    }
}
static ssize_t fclk_3_show(struct device *dev, struct device_attribute *mattr, char *data) {
    if(!dev_data->is_zynqmp){
        unsigned long freq = clk_get_rate(dev_data->fclk[3]);
        return sprintf(data, "%lu\n", freq);
    } else {
        return 0;
    }
}

ssize_t fclk_0_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len) {
    if(!dev_data->is_zynqmp){
        unsigned long freq;
        if(kstrtoul(buf, 0, &freq))
            return -EINVAL;
        clk_set_rate(dev_data->fclk[0], freq);
        return len;
    } else {
        return 0;
    }
}

static ssize_t fclk_1_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len) {
    if(!dev_data->is_zynqmp){
        unsigned long freq;
        if(kstrtoul(buf, 0, &freq))
            return -EINVAL;
        clk_set_rate(dev_data->fclk[1], freq);
        return len;
    } else {
        return 0;
    }
}

static ssize_t fclk_2_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len) {
    if(!dev_data->is_zynqmp){
        unsigned long freq;
        if(kstrtoul(buf, 0, &freq))
            return -EINVAL;
        clk_set_rate(dev_data->fclk[2], freq);
        return len;
    } else {
        return 0;
    }
}

static ssize_t fclk_3_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len) {
    if(!dev_data->is_zynqmp){
        unsigned long freq;
        if(kstrtoul(buf, 0, &freq))
            return -EINVAL;
        clk_set_rate(dev_data->fclk[3], freq);
        return len;
    } else {
        return 0;
    }
}

static ssize_t dma_addr_show(struct device *dev, struct device_attribute *mattr, char *data) {
    #if defined(__arm__)
    return sprintf(data, "%lu\n", dev_data->physaddr);
    #elif defined(__aarch64__)
    return sprintf(data, "%llu\n", dev_data->physaddr);
    #else
    printk(KERN_ALERT "Hello, kernel. Unknown mode!\n");
    return 0
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
            GFP_KERNEL ||GFP_ATOMIC
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
            GFP_KERNEL ||GFP_ATOMIC
        );

        vfree(dev_data->read_data_buffer_64);
        dev_data->read_data_buffer_64 = vmalloc(dev_data->dma_buf_size);
    }

    return len;
}

static DEVICE_ATTR(fclk_0, S_IRUGO|S_IWUSR, fclk_0_show, fclk_0_store);
static DEVICE_ATTR(fclk_1, S_IRUGO|S_IWUSR, fclk_1_show, fclk_1_store);
static DEVICE_ATTR(fclk_2, S_IRUGO|S_IWUSR, fclk_2_show, fclk_2_store);
static DEVICE_ATTR(fclk_3, S_IRUGO|S_IWUSR, fclk_3_show, fclk_3_store);
static DEVICE_ATTR(dma_addr, S_IRUGO, dma_addr_show, dma_addr_store);
static DEVICE_ATTR(dma_buf_size, S_IRUGO|S_IWUSR, dma_buf_size_show, dma_buf_size_store);

static struct attribute *uscope_lkm_attrs[] = {
	&dev_attr_fclk_0.attr,
	&dev_attr_fclk_1.attr,
	&dev_attr_fclk_2.attr,
	&dev_attr_fclk_3.attr,
	&dev_attr_dma_addr.attr,
	&dev_attr_dma_buf_size.attr,
	NULL,
};

const struct attribute_group uscope_lkm_attr_group = {
	.attrs = uscope_lkm_attrs,
};

static struct of_device_id ucube_lkm_match_table[] = {
     {.compatible = "ucube_lkm"},
     {}
};

static struct platform_driver ucube_lkm_platform_driver = {
        .probe = ucube_lkm_probe,
        .remove = ucube_lkm_remove,
        .driver = {
                .name = "ucube_lkm",
                .owner = THIS_MODULE,
                .of_match_table = of_match_ptr(ucube_lkm_match_table),
        },
};


static irqreturn_t ucube_lkm_irq(int irq, void *dev_id)  {
    if(!dev_data->is_zynqmp){
        memcpy(dev_data->read_data_buffer_32, dev_data->dma_buffer_32, dev_data->dma_buf_size);
    }else{
        memcpy(dev_data->read_data_buffer_64, dev_data->dma_buffer_64, dev_data->dma_buf_size);
    }
    dev_data->new_data_available = 1;
    return IRQ_RETVAL(1);
}


static __poll_t ucube_lkm_poll(struct file *flip , struct poll_table_struct * poll_struct){
    int minor = MINOR(flip->f_inode->i_rdev);
    if(minor == 0){
        __poll_t mask = 0;
        mask |= POLLIN | POLLRDNORM;
        return mask;
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
            return -1;
            pr_err("%s: mmapping of data memory is not supported\n", __func__);
            break;
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
        pr_info("%s: In ioctl\n CMD: %u\n ARG: %lu\n", __func__, cmd, arg);
        switch (cmd){
        case IOCTL_NEW_DATA_AVAILABLE:
            return dev_data->new_data_available;
            break;
        default:
            return -EINVAL;
            break;
        }
        return 0;
    }else if(minor == 3){
        pr_info("%s: In ioctl\n CMD: %u\n ARG: %lu\n", __func__, cmd, arg);
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
    } else{
        return 0;
    }
    
}              


static int ucube_lkm_open(struct inode *inode, struct file *file) {

    pr_info("%s: In open\n", __func__);

    return 0;
}

static int ucube_lkm_release(struct inode *inode, struct file *file) {
    pr_info("%s: In release\n", __func__);
    return 0;
}

static ssize_t ucube_lkm_read(struct file *flip, char *buffer, size_t count, loff_t *offset) {
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
    }

    return 0;
}


static ssize_t ucube_lkm_write(struct file *flip, const char *buffer, size_t len, loff_t *offset) {
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


static void free_device_data(struct device *d)
{
    
	struct scope_device_data *dev_data;

    pr_info("%s: In free_device_data\n", __func__);

    for(int i = 0; i< N_MINOR_NUMBERS; i++){
       dev_data = container_of(d, struct scope_device_data, devs[i]);
    }

	kfree(dev_data);
}


/* This structure points to all of the device functions */
static struct file_operations file_ops = {
    .read = ucube_lkm_read,
    .write = ucube_lkm_write,
    .open = ucube_lkm_open,
    .unlocked_ioctl = ucube_lkm_ioctl,
    .poll = ucube_lkm_poll,
    .release = ucube_lkm_release,
    .mmap = ucube_lkm_mmap
};



static int __init ucube_lkm_init(void) {
    int dev_rc, platform_rc, irq_rc;
    int major;
    int cdev_rcs[N_MINOR_NUMBERS];
    dev_t devices[N_MINOR_NUMBERS];
    const char* const device_names[] = { "uscope_data", "uscope_BUS_0", "uscope_BUS_1", "uscope_bitstream"}; 
    
    /* DYNAMICALLY ALLOCATE DEVICE NUMBERS, CLASSES, ETC.*/
    pr_info("%s: In init\n", __func__);

    dev_rc = alloc_chrdev_region(&device_number, 0, N_MINOR_NUMBERS,"uCube DMA");
    
    if (dev_rc) {
        pr_err("%s: Failed to obtain major/minors\nError:%d\n", __func__, dev_rc);
        return dev_rc;
    }

    major = MAJOR(device_number);
    uCube_class = class_create(THIS_MODULE, "uCube_scope");
    

    for(int i = 0; i< N_MINOR_NUMBERS; i++){
        devices[i] = MKDEV(major, i);
    }

    dev_data = kzalloc(sizeof(*dev_data), GFP_KERNEL);
    dev_data->new_data_available = 0;

    for(int i = 0; i< N_MINOR_NUMBERS; i++){
        dev_data->devs[i].devt =  devices[i];
        dev_data->devs[i].class = uCube_class;
        dev_data->devs[i].release = free_device_data;
        dev_set_name(&dev_data->devs[i], device_names[i]);
        device_initialize(&dev_data->devs[i]);
        cdev_init(&dev_data->cdevs[i], &file_ops);
        cdev_rcs[i] = cdev_add(&dev_data->cdevs[i], devices[i], 1);
        if (cdev_rcs[i]) {
        pr_info("%s: Failed in adding cdev[%d] to subsystem "
                "retval:%d\n", __func__, i, cdev_rcs[i]);
        }
        else {
            device_create(uCube_class, NULL, devices[i], NULL, device_names[i]);
        }
        pr_info("%s: finished setup for endpoint: %s\n", __func__, device_names[i]);
    }
    
    /* SETUP PLATFORM DRIVER */
    platform_rc = platform_driver_register(&ucube_lkm_platform_driver);
    if (platform_rc) {
        pr_err("%s: Failed to initialize platform driver\nError:%d\n", __func__, platform_rc);
        return platform_rc;
    }

    dev_data->dma_buf_size = KERNEL_BUFFER_LENGTH;
    /*SETUP AND ALLOCATE DMA BUFFER*/
    if(!dev_data->is_zynqmp){
        dma_set_coherent_mask(&dev_data->devs[0], DMA_BIT_MASK(32));
        dev_data->dma_buffer_32 = dma_alloc_coherent(
            &dev_data->devs[0],
            dev_data->dma_buf_size,
            &(dev_data->physaddr),
            GFP_KERNEL ||GFP_ATOMIC
        );
       pr_warn("%s: Allocated 32 bit dma buffer at: %llu\n", __func__, dev_data->physaddr);
    }else{
        dma_set_coherent_mask(&dev_data->devs[0], DMA_BIT_MASK(64));
        dev_data->dma_buffer_64 = dma_alloc_coherent(
            &dev_data->devs[0],
            dev_data->dma_buf_size,
            &(dev_data->physaddr),
            GFP_KERNEL ||GFP_ATOMIC
        );
        pr_warn("%s: Allocated 64 bit dma buffer at: %llu\n", __func__, dev_data->physaddr);
    }
    
    
    /*SETUP AND ALLOCATE DATA BUFFER*/

    if(!dev_data->is_zynqmp){
        dev_data->read_data_buffer_32 = vmalloc(dev_data->dma_buf_size);
    } else{
        dev_data->read_data_buffer_64 = vmalloc(dev_data->dma_buf_size);
    }

    /* SETUP INTERRUPT HANDLER*/      
    pr_warn("%s: setup interrupts\n", __func__);
    irq_rc = request_irq(irq_line, ucube_lkm_irq, 0, "ucube_lkm", NULL);
    //pr_warn("%s: unassigned irqs: %lu\n", __func__, probe_irq_on());

    // Allocate bistream buffer
    dev_data->bitstream_buffer = vmalloc(BITSTREAM_BUFFER_SIZE);
    
    return irq_rc;
}

static void __exit ucube_lkm_exit(void) {

	int major = MAJOR(device_number);
    
    pr_info("%s: In exit\n", __func__);
    free_irq(irq_line, NULL);

    if(!dev_data->is_zynqmp){
        dma_free_coherent(
            &dev_data->devs[0],
            dev_data->dma_buf_size,
            dev_data->dma_buffer_32,
            dev_data->physaddr
        );
        vfree(dev_data->read_data_buffer_32);
    } else{
        dma_free_coherent(
            &dev_data->devs[0],
            dev_data->dma_buf_size,
            dev_data->dma_buffer_64,
            dev_data->physaddr
        );
        vfree(dev_data->read_data_buffer_64);
    }

    vfree(dev_data->bitstream_buffer);
    
    
    platform_driver_unregister(&ucube_lkm_platform_driver);	
    

    for(int i = 0; i< N_MINOR_NUMBERS; i++){
        int device = MKDEV(major, i);

        cdev_del(&dev_data->cdevs[i]);
        device_destroy(uCube_class, device);
    }


	class_destroy(uCube_class);
	unregister_chrdev_region(device_number, N_MINOR_NUMBERS);
    
}

int ucube_lkm_probe(struct platform_device *pdev){
    int rc;
	char const * driver_mode;

    pr_info("%s: In platform probe\n", __func__);
    
    irq_line = platform_get_irq(pdev, 0);

    of_property_read_string(pdev->dev.of_node, "ucubever", &driver_mode);

    pr_info("%s: driver target is %s\n", __func__, driver_mode);
    dev_data->is_zynqmp = strncmp(driver_mode, "zynqmp", 6)==0;



    rc = sysfs_create_group(&pdev->dev.kobj, &uscope_lkm_attr_group);
    if(!dev_data->is_zynqmp){
        /* GET HANDLES TO CLOCK STRUCTURES */
        dev_data->fclk[0] = devm_clk_get(&pdev->dev, "fclk0");
        dev_data->fclk[1] = devm_clk_get(&pdev->dev, "fclk1");
        dev_data->fclk[2] = devm_clk_get(&pdev->dev, "fclk2");
        dev_data->fclk[3] = devm_clk_get(&pdev->dev, "fclk3");
        clk_prepare_enable(dev_data->fclk[0]);
        clk_prepare_enable(dev_data->fclk[1]);
        clk_prepare_enable(dev_data->fclk[2]);
        clk_prepare_enable(dev_data->fclk[3]);
        
        clk_set_rate(dev_data->fclk[0], FCLK_0_DEFAULT_FREQ);
        clk_set_rate(dev_data->fclk[1], FCLK_1_DEFAULT_FREQ);
        clk_set_rate(dev_data->fclk[2], FCLK_2_DEFAULT_FREQ);
        clk_set_rate(dev_data->fclk[3], FCLK_3_DEFAULT_FREQ);
    }

    dev_data->fpga_node = of_find_compatible_node(NULL, NULL, "fpga-region");
    if (!dev_data->fpga_node){
        pr_warn("%s: Unable to get FPGA device node", __func__);
        return -ENODEV;
    } else {
        pr_info("Matched fpga-region: %pOF\n", dev_data->fpga_node);
    }

    return 0;
}

int ucube_lkm_remove(struct platform_device *pdev){
    pr_info("%s: In platform remove\n", __func__);
    sysfs_remove_group(&pdev->dev.kobj, &uscope_lkm_attr_group);
    of_node_put(dev_data->fpga_node);
    return 0;
}


MODULE_DEVICE_TABLE(of, ucube_lkm_match_table);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Filippo Savi");
MODULE_DESCRIPTION("uScope dma handler");
MODULE_VERSION("0.01");

module_init(ucube_lkm_init);
module_exit(ucube_lkm_exit);