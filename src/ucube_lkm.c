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

#define MAX_DEVICES	1

#define KERNEL_BUFFER_LENGTH 6144
#define KERNEL_BUFFER_SIZE KERNEL_BUFFER_LENGTH*4

#define IRQ_NUMBER 22

#define IOCTL_NEW_DATA_AVAILABLE 1

/* Prototypes for device functions */
static int ucube_lkm_open(struct inode *, struct file *);
static int ucube_lkm_release(struct inode *, struct file *);
static ssize_t ucube_lkm_read(struct file *, char *, size_t, loff_t *);
static ssize_t ucube_lkm_write(struct file *, const char *, size_t, loff_t *);
static long ucube_lkm_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
__poll_t ucube_lkm_poll(struct file *, struct poll_table_struct *);
int ucube_lkm_probe(struct platform_device *dev);
int ucube_lkm_remove(struct platform_device *dev);


dev_t device_number;
struct class *uCube_class;
struct uCube_device_data *dev_data;

int irq_line;

/* STRUCTURE FOR THE DEVICE SPECIFIC DATA*/
struct uCube_device_data {
    struct device dev;
    struct cdev cdev;
    u32 *read_data_buffer;
    u32 *dma_buffer;
    dma_addr_t physaddr;
    int new_data_available;
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


/* This structure points to all of the device functions */
static struct file_operations file_ops = {
    .read = ucube_lkm_read,
    .write = ucube_lkm_write,
    .open = ucube_lkm_open,
    .unlocked_ioctl = ucube_lkm_ioctl,
    .poll = ucube_lkm_poll,
    .release = ucube_lkm_release
};


static irqreturn_t ucube_lkm_irq(int irq, void *dev_id)
{
    memcpy(dev_data->read_data_buffer, dev_data->dma_buffer, KERNEL_BUFFER_SIZE);
    dev_data->new_data_available = 1;
    return IRQ_RETVAL(1);
}


__poll_t ucube_lkm_poll(struct file *flip , struct poll_table_struct * poll_struct){
    return (POLLIN | POLLRDNORM);
}

static long ucube_lkm_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
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


static ssize_t ucube_lkm_write(struct file *flip, const char *buffer, size_t len, loff_t *offset) {
    pr_info("%s: In write\n", __func__);
    return 0;
}


static void free_device_data(struct device *d)
{
    
	struct uCube_device_data *dev_data;

    pr_info("%s: In free_device_data\n", __func__);
	dev_data = container_of(d, struct uCube_device_data, dev);
	kfree(dev_data);
}


static int __init ucube_lkm_init(void) {
    int rc;
    int major;
    dev_t uCube_device;
    

    /* DYNAMICALLY ALLOCATE DEVICE NUMBERS, CLASSES, ETC.*/
    pr_info("%s: In init\n", __func__);\

    rc = alloc_chrdev_region(&device_number, 0, MAX_DEVICES,"uCube DMA");
    
    if (rc) {
        pr_err("%s: Failed to obtain major/minors\nError:%d\n", __func__, rc);
        return rc;
    }

    major = MAJOR(device_number);
    uCube_class = class_create(THIS_MODULE, "uCube_scope");
    
    uCube_device = MKDEV(major, 0);
   

    rc = platform_driver_probe(&ucube_lkm_platform_driver, ucube_lkm_probe);

    dev_data = kzalloc(sizeof(*dev_data), GFP_KERNEL);
    dev_data->new_data_available = 0;
    dev_data->dev.devt = uCube_device;
    dev_data->dev.class = uCube_class;
    dev_data->dev.release = free_device_data;
    dev_set_name(&dev_data->dev, "uCube device");
    device_initialize(&dev_data->dev);

    cdev_init(&dev_data->cdev, &file_ops);
    rc = cdev_add(&dev_data->cdev, uCube_device, 1);
    if (rc) {
        pr_info("%s: Failed in adding cdev to subsystem "
                "retval:%d\n", __func__, rc);
    }
    else {
        device_create(uCube_class, NULL, uCube_device, NULL, "uCube_dev_%d", 0);
    }
    
    /*SETUP AND ALLOCATE DMA BUFFER*/
    dma_set_coherent_mask(&dev_data->dev, DMA_BIT_MASK(32));
    dev_data->dma_buffer = dma_alloc_coherent(&dev_data->dev, KERNEL_BUFFER_LENGTH*sizeof(int), &(dev_data->physaddr), GFP_KERNEL ||GFP_ATOMIC);
    pr_warn("%s: Allocated dma buffer at: %u\n", __func__, dev_data->physaddr);
    /*SETUP AND ALLOCATE DATA BUFFER*/
    dev_data->read_data_buffer = vmalloc(KERNEL_BUFFER_SIZE);
    
    if (rc) {
        pr_err("%s: Failed to initialize platform driver\nError:%d\n", __func__, rc);
        return rc;
    }
    /* SETUP INTERRUPT HANDLER*/      
    pr_warn("%s: setup interrupts\n", __func__);
    rc = request_irq(irq_line, ucube_lkm_irq, 0, "ucube_lkm", NULL);
    //pr_warn("%s: unassigned irqs: %lu\n", __func__, probe_irq_on());
    
    return rc;
}

static void __exit ucube_lkm_exit(void) {int i = 0;
	
	int major = MAJOR(device_number);
	dev_t my_device;
    
    pr_info("%s: In exit\n", __func__);
    free_irq(irq_line, NULL);

    dma_free_coherent(&dev_data->dev,KERNEL_BUFFER_LENGTH*sizeof(int),dev_data->dma_buffer, dev_data->physaddr);
    vfree(dev_data->read_data_buffer);
    
    platform_driver_unregister(&ucube_lkm_platform_driver);	
    
    my_device = MKDEV(major, i);
    cdev_del(&dev_data->cdev);
    device_destroy(uCube_class, my_device);

	class_destroy(uCube_class);
	unregister_chrdev_region(device_number, MAX_DEVICES);
    
    
    

}

int ucube_lkm_probe(struct platform_device *dev){
    pr_info("%s: In platform probe\n", __func__);

    irq_line = platform_get_irq(dev, 0);
    return 0;
}

int ucube_lkm_remove(struct platform_device *dev){
    pr_info("%s: In platform remove\n", __func__);
    return 0;
}


MODULE_DEVICE_TABLE(of, ucube_lkm_match_table);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Filippo Savi");
MODULE_DESCRIPTION("uScope dma handler");
MODULE_VERSION("0.01");

module_init(ucube_lkm_init);
module_exit(ucube_lkm_exit);