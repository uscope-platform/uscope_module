#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>

#define MAX_DEVICES	1
/* Prototypes for device functions */
static int ucube_lkm_open(struct inode *, struct file *);
static int ucube_lkm_release(struct inode *, struct file *);
static ssize_t ucube_lkm_read(struct file *, char *, size_t, loff_t *);
static ssize_t ucube_lkm_write(struct file *, const char *, size_t, loff_t *);

dev_t device_number;
struct class *uCube_class;
struct uCube_device_data *dev_data;

const char *echo_msg = "TEST MESSAGE\n";

/* STRUCTURE FOR THE DEVICE SPECIFIC DATA*/
struct uCube_device_data {
    struct device dev;
    struct cdev cdev;
    unsigned int *dma_buffer;
    dma_addr_t physaddr;
};




/* This structure points to all of the device functions */
static struct file_operations file_ops = {
    .read = ucube_lkm_read,
    .write = ucube_lkm_write,
    .open = ucube_lkm_open,
    .release = ucube_lkm_release
};


static int ucube_lkm_open(struct inode *inode, struct file *file) {
    pr_info("%s: In open\n", __func__);
    return 0;
}

static int ucube_lkm_release(struct inode *inode, struct file *file) {
    pr_info("%s: In release\n", __func__);
    return 0;
}

static ssize_t ucube_lkm_read(struct file *flip, char *buffer, size_t count, loff_t *offset) {

    pr_info("%s: In read\n", __func__);
    pr_info("%s: dma buffer    %u\n", __func__, dev_data->dma_buffer[0]);

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
   

    dev_data = kzalloc(sizeof(*dev_data), GFP_KERNEL);
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
    dma_set_coherent_mask(&dev_data->dev, DMA_BIT_MASK(32));
    dev_data->dma_buffer = dma_alloc_coherent(&dev_data->dev, 1024*sizeof(int), &(dev_data->physaddr), GFP_KERNEL ||GFP_ATOMIC);

    pr_warn("%s: Allocated dma buffer at: %u\n", __func__, dev_data->physaddr);
    return rc;
}

static void __exit ucube_lkm_exit(void) {int i = 0;

	int major = MAJOR(device_number);
	dev_t my_device;
	
    dma_free_coherent(&dev_data->dev,1024*sizeof(int),dev_data->dma_buffer, dev_data->physaddr);

    my_device = MKDEV(major, i);
    cdev_del(&dev_data->cdev);
    device_destroy(uCube_class, my_device);

	class_destroy(uCube_class);
	unregister_chrdev_region(device_number, MAX_DEVICES);
	pr_info("%s: In exit\n", __func__);

}


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Filippo Savi");
MODULE_DESCRIPTION("uScope dma handler");
MODULE_VERSION("0.01");

module_init(ucube_lkm_init);
module_exit(ucube_lkm_exit);