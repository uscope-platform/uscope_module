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

/*
 * IPI doorbell between Linux and the R5, through the kernel IPI mailbox driver
 * (ZynqMP IPI registers are only reachable from Linux through the secure
 * firmware, which that driver handles).
 *
 * Device tree, in the uScope node:
 *   mboxes = <&{/axi/ipi@ff340000/child@ff310000} 0>,
 *            <&{/axi/ipi@ff340000/child@ff310000} 1>;
 *   mbox-names = "tx", "rx";
 *
 * Userspace, on /dev/uscope_shm:
 *   ioctl(fd, IOCTL_RING_RPU)  ring the R5
 *   poll()/read()              wait for the R5 to ring back; read() returns a
 *                              u32 with the number of doorbells since last read
 */

#include "ucube_lkm.h"

/* R5 rang: acknowledge (or the next doorbell never arrives) and wake readers */
static void ucube_ipi_rx(struct mbox_client *cl, void *msg)
{
    if (mbox_send_message(dev_data->ipi_rx, NULL) < 0)
        dev_err(cl->dev, "IPI ack failed\n");

    atomic_inc(&dev_data->ipi_count);
    wake_up_interruptible(&dev_data->ipi_wq);
}

void ucube_ipi_init(struct device *dev)
{
    struct mbox_client *cl = &dev_data->ipi_cl;

    if (!of_property_present(dev->of_node, "mbox-names"))
        return;     /* no doorbell configured */

    cl->dev = dev;
    cl->rx_callback = ucube_ipi_rx;
    cl->tx_block = false;
    cl->knows_txdone = false;

    dev_data->ipi_tx = mbox_request_channel_byname(cl, "tx");
    if (IS_ERR(dev_data->ipi_tx)) {
        dev_err(dev, "IPI tx channel: %ld\n", PTR_ERR(dev_data->ipi_tx));
        dev_data->ipi_tx = NULL;
        return;
    }

    dev_data->ipi_rx = mbox_request_channel_byname(cl, "rx");
    if (IS_ERR(dev_data->ipi_rx)) {
        dev_err(dev, "IPI rx channel: %ld\n", PTR_ERR(dev_data->ipi_rx));
        dev_data->ipi_rx = NULL;
        mbox_free_channel(dev_data->ipi_tx);
        dev_data->ipi_tx = NULL;
        return;
    }

    dev_info(dev, "IPI doorbell ready\n");
}

void ucube_ipi_free(void)
{
    if (dev_data->ipi_rx)
        mbox_free_channel(dev_data->ipi_rx);
    if (dev_data->ipi_tx)
        mbox_free_channel(dev_data->ipi_tx);
    dev_data->ipi_rx = NULL;
    dev_data->ipi_tx = NULL;
}

int ucube_ipi_ring(void)
{
    int rc;

    if (!dev_data->ipi_tx)
        return -ENODEV;

    rc = mbox_send_message(dev_data->ipi_tx, NULL);
    return rc < 0 ? rc : 0;
}
