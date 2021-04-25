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
#include <asm/pgtable-2level-types.h> // SILENCE VSCODE ERROR
#include <linux/clk.h>
#include <linux/device.h>

#define N_MINOR_NUMBERS	3

#define KERNEL_BUFFER_LENGTH 6144
#define KERNEL_BUFFER_SIZE KERNEL_BUFFER_LENGTH*4

#define IRQ_NUMBER 22

#define IOCTL_NEW_DATA_AVAILABLE 1

#define BUS_0_ADDRESS_BASE 0x40000000
#define BUS_0_ADDRESS_TOP 0x7fffffff

#define BUS_1_ADDRESS_BASE 0x80000000
#define BUS_1_ADDRESS_TOP 0xBfffffff

#define FCLK_0_DEFAULT_FREQ 100000000
#define FCLK_1_DEFAULT_FREQ 40000000
#define FCLK_2_DEFAULT_FREQ 40000000
#define FCLK_3_DEFAULT_FREQ 40000000

/* Prototypes for device functions */
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
    struct device devs[3];
    struct cdev cdevs[3];
    u32 *read_data_buffer;
    u32 *dma_buffer;
    dma_addr_t physaddr;
    int new_data_available;
    struct clk *fclk[4];
};


static ssize_t fclk_0_show(struct device *dev, struct device_attribute *mattr, char *data) {
    unsigned long freq = clk_get_rate(dev_data->fclk[0]);
    return sprintf(data, "%lu\n", freq);
}

static ssize_t fclk_1_show(struct device *dev, struct device_attribute *mattr, char *data) {
    unsigned long freq = clk_get_rate(dev_data->fclk[1]);
    return sprintf(data, "%lu\n", freq);
}
static ssize_t fclk_2_show(struct device *dev, struct device_attribute *mattr, char *data) {
    unsigned long freq = clk_get_rate(dev_data->fclk[2]);
    return sprintf(data, "%lu\n", freq);
}
static ssize_t fclk_3_show(struct device *dev, struct device_attribute *mattr, char *data) {
    unsigned long freq = clk_get_rate(dev_data->fclk[3]);
    return sprintf(data, "%lu\n", freq);
}

ssize_t fclk_0_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len) {
    unsigned long freq;
    if(kstrtoul(buf, 0, &freq))
		return -EINVAL;
    clk_set_rate(dev_data->fclk[0], freq);
    return len;
}

static ssize_t fclk_1_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len) {
    unsigned long freq;
    if(kstrtoul(buf, 0, &freq))
		return -EINVAL;
    clk_set_rate(dev_data->fclk[1], freq);
    return len;
}

static ssize_t fclk_2_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len) {
    unsigned long freq;
    if(kstrtoul(buf, 0, &freq))
		return -EINVAL;
    clk_set_rate(dev_data->fclk[3], freq);
    return len;
}

static ssize_t fclk_3_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len) {
    unsigned long freq;
    if(kstrtoul(buf, 0, &freq))
		return -EINVAL;
    clk_set_rate(dev_data->fclk[3], freq);
    return len;
}

static DEVICE_ATTR(fclk_0, S_IRUGO|S_IWUSR, fclk_0_show, fclk_0_store);
static DEVICE_ATTR(fclk_1, S_IRUGO|S_IWUSR, fclk_1_show, fclk_1_store);
static DEVICE_ATTR(fclk_2, S_IRUGO|S_IWUSR, fclk_2_show, fclk_2_store);
static DEVICE_ATTR(fclk_3, S_IRUGO|S_IWUSR, fclk_3_show, fclk_3_store);


static struct attribute *uscope_lkm_attrs[] = {
	&dev_attr_fclk_0.attr,
	&dev_attr_fclk_1.attr,
	&dev_attr_fclk_2.attr,
	&dev_attr_fclk_3.attr,
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


static irqreturn_t ucube_lkm_irq(int irq, void *dev_id)
{
    memcpy(dev_data->read_data_buffer, dev_data->dma_buffer, KERNEL_BUFFER_SIZE);
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

    uint32_t mapping_start_address = vma->vm_pgoff*4096;
    uint32_t mapping_stop_address = vma->vm_pgoff*4096+vma->vm_end - vma->vm_start;

    int minor = MINOR(filp->f_inode->i_rdev);

    switch (minor) {
        case 0:
            return -1;
            pr_err("%s: mmapping of data memory is not supported\n", __func__);
            break;
        case 1:
            if( mapping_start_address < BUS_0_ADDRESS_BASE || mapping_stop_address > BUS_0_ADDRESS_TOP){
                pr_err("%s: mapping out of bus 0 address range\n", __func__);
                return -2;
            }
            break;
        case 2:
            if( mapping_start_address < BUS_1_ADDRESS_BASE || mapping_stop_address > BUS_1_ADDRESS_TOP){
                pr_err("%s: mmapping out of bus 1 address range\n", __func__);
                return -2;
            }
            break;
    }

    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
    pr_warn("%s: In mmapped from %x to %x\n", __func__, mapping_start_address,mapping_stop_address);
    if (remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,vma->vm_end - vma->vm_start,vma->vm_page_prot))
    return -EAGAIN;
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
    int minor = MINOR(flip->f_inode->i_rdev);
    if(minor == 0){
        size_t datalen = KERNEL_BUFFER_SIZE;

        pr_info("%s: In read\n", __func__);

        if (count > datalen) {
            count = datalen;
        }

        if (copy_to_user(buffer, dev_data->read_data_buffer, count)) {
            return -EFAULT;
        }
        dev_data->new_data_available = 0;
        return count;    
    }
    return 0;
}


static ssize_t ucube_lkm_write(struct file *flip, const char *buffer, size_t len, loff_t *offset) {
    int minor = MINOR(flip->f_inode->i_rdev);
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
    int cdev_rcs[3];
    dev_t devices[3];
    const char* const device_names[] = { "uscope_data", "uscope_BUS_0", "uscope_BUS_1"}; 
    
    /* DYNAMICALLY ALLOCATE DEVICE NUMBERS, CLASSES, ETC.*/
    pr_info("%s: In init\n", __func__);\

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

    /*SETUP AND ALLOCATE DMA BUFFER*/
    dma_set_coherent_mask(&dev_data->devs[0], DMA_BIT_MASK(32));
    dev_data->dma_buffer = dma_alloc_coherent(&dev_data->devs[0], KERNEL_BUFFER_LENGTH*sizeof(int), &(dev_data->physaddr), GFP_KERNEL ||GFP_ATOMIC);
    pr_warn("%s: Allocated dma buffer at: %u\n", __func__, dev_data->physaddr);
    /*SETUP AND ALLOCATE DATA BUFFER*/
    dev_data->read_data_buffer = vmalloc(KERNEL_BUFFER_SIZE);
    

    /* SETUP INTERRUPT HANDLER*/      
    pr_warn("%s: setup interrupts\n", __func__);
    irq_rc = request_irq(irq_line, ucube_lkm_irq, 0, "ucube_lkm", NULL);
    //pr_warn("%s: unassigned irqs: %lu\n", __func__, probe_irq_on());

    return irq_rc;
}

static void __exit ucube_lkm_exit(void) {

	int major = MAJOR(device_number);
    
    pr_info("%s: In exit\n", __func__);
    free_irq(irq_line, NULL);

    dma_free_coherent(&dev_data->devs[0],KERNEL_BUFFER_LENGTH*sizeof(int),dev_data->dma_buffer, dev_data->physaddr);
    vfree(dev_data->read_data_buffer);
    
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
    pr_info("%s: In platform probe\n", __func__);
    
    irq_line = platform_get_irq(pdev, 0);


    rc = sysfs_create_group(&pdev->dev.kobj, &uscope_lkm_attr_group);

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

    return 0;
}

int ucube_lkm_remove(struct platform_device *pdev){
    pr_info("%s: In platform remove\n", __func__);
    
    sysfs_remove_group(&pdev->dev.kobj, &uscope_lkm_attr_group);
    return 0;
}


MODULE_DEVICE_TABLE(of, ucube_lkm_match_table);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Filippo Savi");
MODULE_DESCRIPTION("uScope dma handler");
MODULE_VERSION("0.01");

module_init(ucube_lkm_init);
module_exit(ucube_lkm_exit);
