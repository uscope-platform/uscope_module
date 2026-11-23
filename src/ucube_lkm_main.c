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

static dev_t device_number;
static struct class *uCube_class;
struct scope_device_data *dev_data;

int irq_line;

int ucube_lkm_probe(struct platform_device *dev);
void ucube_lkm_remove(struct platform_device *dev);


static void free_device_data(struct device *d)
{
    
	struct scope_device_data *dev_data;

    pr_info("%s: In free_device_data\n", __func__);

    for(int i = 0; i< N_MINOR_NUMBERS; i++){
       dev_data = container_of(d, struct scope_device_data, devs[i]);
    }

	kfree(dev_data);
}


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

MODULE_DEVICE_TABLE(of, ucube_lkm_match_table);

int ucube_lkm_probe(struct platform_device *pdev){
    static const char * const fclk_names[2][4] = {
        { "pl0_ref", "pl1_ref", "pl2_ref", "pl3_ref" },
        { "pl0_ref", "pl1_ref", "pl2_ref", "pl3_ref" }
    };
    int rc;
    int i;
    char const * driver_mode;
    int device_type = 0;
    u32 enable_clk;
    
    pr_info("%s: In platform probe\n", __func__);

    irq_line = platform_get_irq(pdev, 0);

    rc = of_property_read_string(pdev->dev.of_node, "ucube,arch", &driver_mode);
    if (rc) {
        pr_err("%s: missing ucube,arch property in device-tree\n", __func__);
        return rc;
    }

    pr_info("%s: driver target is %s\n", __func__, driver_mode);
    dev_data->is_zynqmp = strncmp(driver_mode, "zynqmp", 6) == 0;
    device_type = dev_data->is_zynqmp ? 1: 0;
    rc = sysfs_create_group(&pdev->dev.kobj, &uscope_lkm_attr_group);

    /* GET HANDLES TO THE PL CLOCKS AND ENABLE THOSE THE DT ASKS FOR (Zynq-7000 and ZynqMP) */
    for (i = 0; i < 4; i++) {
        struct ucube_fclk *f = &dev_data->fclk[i];

        rc = of_property_read_u32_index(pdev->dev.of_node, "ucube,fclk-enable", i, &enable_clk);
        if (rc) {
            pr_err("%s: missing ucube,fclk-enable property in device-tree\n", __func__);
            return rc;
        }

        f->clk = devm_clk_get(&pdev->dev, fclk_names[device_type][i]);
        if (IS_ERR(f->clk)) {
            dev_err(&pdev->dev, "%s lookup failed: %ld\n",
                    fclk_names[device_type][i], PTR_ERR(f->clk));
            f->clk = NULL;
            continue;
        }

        ucube_fclk_add_files(&pdev->dev, f, i);

        if (enable_clk) {
            rc = ucube_fclk_set_enabled(f, true);
            if (rc)
                dev_err(&pdev->dev, "%s enable failed: %d\n", fclk_names[device_type][i], rc);
            else
                dev_info(&pdev->dev, "%s enabled at %lu Hz\n",
                         fclk_names[device_type][i], clk_get_rate(f->clk));
        }
    }

    /* Only touch the rates on Zynq-7000; on ZynqMP the PMU firmware owns them */
    if (!dev_data->is_zynqmp) {
        clk_set_rate(dev_data->fclk[0].clk, FCLK_0_DEFAULT_FREQ);
        clk_set_rate(dev_data->fclk[1].clk, FCLK_1_DEFAULT_FREQ);
        clk_set_rate(dev_data->fclk[2].clk, FCLK_2_DEFAULT_FREQ);
        clk_set_rate(dev_data->fclk[3].clk, FCLK_3_DEFAULT_FREQ);
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

void ucube_lkm_remove(struct platform_device *pdev){
    int i;
    pr_info("%s: In platform remove\n", __func__);
    sysfs_remove_group(&pdev->dev.kobj, &uscope_lkm_attr_group);
    for (i = 0; i < 4; i++) {
        struct ucube_fclk *f = &dev_data->fclk[i];

        if (!f->clk)
            continue;
        device_remove_file(&pdev->dev, &f->rate_attr);
        device_remove_file(&pdev->dev, &f->en_attr);
        ucube_fclk_set_enabled(f, false);   /* no-op if already off */
    }

    of_node_put(dev_data->fpga_node);
}


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
    uCube_class = class_create("uCube_scope");
    

    for(int i = 0; i< N_MINOR_NUMBERS; i++){
        devices[i] = MKDEV(major, i);
    }

    dev_data = kzalloc(sizeof(*dev_data), GFP_KERNEL);
    dev_data->new_data_available = 0;
    mutex_init(&dev_data->fclk_lock);

    for(int i = 0; i< N_MINOR_NUMBERS; i++){
        dev_data->devs[i].devt =  devices[i];
        dev_data->devs[i].class = uCube_class;
        dev_data->devs[i].release = free_device_data;
        dev_set_name(&dev_data->devs[i], device_names[i]);
        device_initialize(&dev_data->devs[i]);
        cdev_init(&dev_data->cdevs[i], &ucube_lkm_file_ops);
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
            GFP_KERNEL
        );
       pr_warn("%s: Allocated 32 bit dma buffer at: %llu\n", __func__, dev_data->physaddr);
    }else{
        dma_set_coherent_mask(&dev_data->devs[0], DMA_BIT_MASK(64));
        dev_data->dma_buffer_64 = dma_alloc_coherent(
            &dev_data->devs[0],
            dev_data->dma_buf_size,
            &(dev_data->physaddr),
            GFP_KERNEL
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


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Filippo Savi");
MODULE_DESCRIPTION("uScope dma handler");
MODULE_VERSION("1.11");

module_init(ucube_lkm_init);
module_exit(ucube_lkm_exit);
