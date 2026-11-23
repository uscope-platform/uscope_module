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
