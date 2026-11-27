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
 * IPI doorbells between Linux and the R5 cores, through the kernel IPI mailbox
 * driver (ZynqMP IPI registers are only reachable from Linux through the secure
 * firmware, which that driver handles). One doorbell per RPU core.
 *
 * Device tree, in the uScope node (APU IPI7 to RPU0 / RPU1):
 *   mboxes = <&{/axi/ipi@ff340000/child@ff310000} 0>,
 *            <&{/axi/ipi@ff340000/child@ff310000} 1>,
 *            <&{/axi/ipi@ff340000/child@ff320000} 0>,
 *            <&{/axi/ipi@ff340000/child@ff320000} 1>;
 *   mbox-names = "tx_0", "rx_0", "tx_1", "rx_1";
 * A core whose pair is missing simply has no doorbell.
 *
 * Userspace, on /dev/uscope_shm_<core>:
 *   ioctl(fd, IOCTL_RING_RPU)  ring that R5
 *   poll()/read()              wait for that R5 to ring back; read() returns a
 *                              u32 with the number of doorbells since last read
 */

#include "ucube_lkm.h"

/* R5 rang: acknowledge (or the next doorbell never arrives) and wake readers */
static void ucube_ipi_rx(struct mbox_client *cl, void *msg)
{
    struct ucube_rpu *r = container_of(cl, struct ucube_rpu, ipi_cl);

    if (mbox_send_message(r->ipi_rx, NULL) < 0)
        dev_err(cl->dev, "IPI %d ack failed\n", r->core);

    /* The firmware asking for its shared memory window: answered here */
    if (ucube_shm_announce(r))
        return;

    atomic_inc(&r->ipi_count);
    wake_up_interruptible(&r->ipi_wq);
}

static void ucube_ipi_init_core(struct device *dev, struct ucube_rpu *r)
{
    struct mbox_client *cl = &r->ipi_cl;
    char tx_name[8], rx_name[8];

    snprintf(tx_name, sizeof(tx_name), "tx_%d", r->core);
    snprintf(rx_name, sizeof(rx_name), "rx_%d", r->core);

    /* No pair for this core in the DT: it has no doorbell */
    if (of_property_match_string(dev->of_node, "mbox-names", tx_name) < 0)
        return;

    cl->dev = dev;
    cl->rx_callback = ucube_ipi_rx;
    cl->tx_block = false;
    cl->knows_txdone = false;

    r->ipi_tx = mbox_request_channel_byname(cl, tx_name);
    if (IS_ERR(r->ipi_tx)) {
        dev_err(dev, "IPI %s channel: %ld\n", tx_name, PTR_ERR(r->ipi_tx));
        r->ipi_tx = NULL;
        return;
    }

    r->ipi_rx = mbox_request_channel_byname(cl, rx_name);
    if (IS_ERR(r->ipi_rx)) {
        dev_err(dev, "IPI %s channel: %ld\n", rx_name, PTR_ERR(r->ipi_rx));
        r->ipi_rx = NULL;
        mbox_free_channel(r->ipi_tx);
        r->ipi_tx = NULL;
        return;
    }

    dev_info(dev, "IPI doorbell %d ready\n", r->core);

    /*
     * Tell a core that is already running about its (new) shared memory
     * window: it rings back, and the receive callback announces the window.
     * A core that is not running just finds a stale doorbell when it starts,
     * which the firmware discards.
     */
    ucube_shm_map_mailbox(dev, r);
    if (r->shm_mailbox)
        ucube_ipi_ring(r);
}

void ucube_ipi_init(struct device *dev)
{
    for (int i = 0; i < N_RPU_CORES; i++)
        ucube_ipi_init_core(dev, &dev_data->rpu[i]);
}

void ucube_ipi_free(void)
{
    for (int i = 0; i < N_RPU_CORES; i++) {
        struct ucube_rpu *r = &dev_data->rpu[i];

        if (r->ipi_rx)
            mbox_free_channel(r->ipi_rx);
        if (r->ipi_tx)
            mbox_free_channel(r->ipi_tx);
        r->ipi_rx = NULL;
        r->ipi_tx = NULL;
    }
}

int ucube_ipi_ring(struct ucube_rpu *r)
{
    int rc;

    if (!r->ipi_tx)
        return -ENODEV;

    rc = mbox_send_message(r->ipi_tx, NULL);
    return rc < 0 ? rc : 0;
}
