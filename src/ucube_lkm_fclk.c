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

int ucube_fclk_set_enabled(struct ucube_fclk *f, bool on)
{
    int rc = 0;

    if (!f->clk)
        return -ENODEV;

    mutex_lock(&dev_data->fclk_lock);
    if (on != f->on) {
        if (on)
            rc = clk_prepare_enable(f->clk);
        else
            clk_disable_unprepare(f->clk);
        if (!rc)
            f->on = on;
    }
    mutex_unlock(&dev_data->fclk_lock);
    return rc;
}

static inline struct ucube_fclk *rate_to_fclk(struct device_attribute *a)
{
    return container_of(a, struct ucube_fclk, rate_attr);
}

static inline struct ucube_fclk *en_to_fclk(struct device_attribute *a)
{
    return container_of(a, struct ucube_fclk, en_attr);
}

static ssize_t fclk_show(struct device *dev, struct device_attribute *a, char *buf)
{
    return sysfs_emit(buf, "%lu\n", clk_get_rate(rate_to_fclk(a)->clk));
}

static ssize_t fclk_store(struct device *dev, struct device_attribute *a,
                          const char *buf, size_t len)
{
    unsigned long freq;

    if (kstrtoul(buf, 0, &freq))
        return -EINVAL;
    clk_set_rate(rate_to_fclk(a)->clk, freq);
    return len;
}

static ssize_t fclk_en_show(struct device *dev, struct device_attribute *a, char *buf)
{
    return sysfs_emit(buf, "%d\n", en_to_fclk(a)->on);
}

static ssize_t fclk_en_store(struct device *dev, struct device_attribute *a,
                             const char *buf, size_t len)
{
    bool on;
    int rc = kstrtobool(buf, &on);

    if (rc)
        return rc;
    rc = ucube_fclk_set_enabled(en_to_fclk(a), on);
    return rc ? rc : len;
}

static int fclk_add_file(struct device *dev, struct device_attribute *a, int i,
                         const char *suffix,
                         ssize_t (*show)(struct device *, struct device_attribute *, char *),
                         ssize_t (*store)(struct device *, struct device_attribute *,
                                          const char *, size_t))
{
    sysfs_attr_init(&a->attr);      /* needed for dynamically allocated attrs (lockdep) */
    a->attr.name = devm_kasprintf(dev, GFP_KERNEL, "fclk_%d%s", i, suffix);
    if (!a->attr.name)
        return -ENOMEM;
    a->attr.mode = 0644;
    a->show = show;
    a->store = store;
    return device_create_file(dev, a);
}

void ucube_fclk_add_files(struct device *dev, struct ucube_fclk *f, int i)
{
    fclk_add_file(dev, &f->rate_attr, i, "",    fclk_show,    fclk_store);
    fclk_add_file(dev, &f->en_attr,   i, "_en", fclk_en_show, fclk_en_store);
}
