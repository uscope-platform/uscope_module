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

/*
 * R5 clock: the file shows and takes the R5 frequency (RPLL / RPU_CLK_DIV).
 * Only RPU_CLK_MIN..RPU_CLK_MAX is accepted.
 */
static ssize_t rpu_clk_show(struct device *dev, struct device_attribute *a, char *buf)
{
    return sysfs_emit(buf, "%lu\n", clk_get_rate(rate_to_fclk(a)->clk) / RPU_CLK_DIV);
}

static ssize_t rpu_clk_store(struct device *dev, struct device_attribute *a,
                             const char *buf, size_t len)
{
    unsigned long r5_hz;
    int rc;

    if (kstrtoul(buf, 0, &r5_hz))
        return -EINVAL;
    if (r5_hz < RPU_CLK_MIN || r5_hz > RPU_CLK_MAX) {
        dev_err(dev, "rpu_clk: %lu Hz refused, allowed range is %u-%u Hz\n",
                r5_hz, RPU_CLK_MIN, RPU_CLK_MAX);
        return -ERANGE;
    }

    rc = clk_set_rate(rate_to_fclk(a)->clk, r5_hz * RPU_CLK_DIV);
    return rc ? rc : len;
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

static int fclk_add_file(struct device *dev, struct device_attribute *a,
                         const char *name, const char *suffix,
                         ssize_t (*show)(struct device *, struct device_attribute *, char *),
                         ssize_t (*store)(struct device *, struct device_attribute *,
                                          const char *, size_t))
{
    sysfs_attr_init(&a->attr);      /* needed for dynamically allocated attrs (lockdep) */
    a->attr.name = devm_kasprintf(dev, GFP_KERNEL, "%s%s", name, suffix);
    if (!a->attr.name)
        return -ENOMEM;
    a->attr.mode = 0644;
    a->show = show;
    a->store = store;
    return device_create_file(dev, a);
}

/* Create <name> (rate in Hz) and <name>_en (0/1) for any ucube_fclk */
void ucube_clk_add_files(struct device *dev, struct ucube_fclk *f, const char *name)
{
    fclk_add_file(dev, &f->rate_attr, name, "",    fclk_show,    fclk_store);
    fclk_add_file(dev, &f->en_attr,   name, "_en", fclk_en_show, fclk_en_store);
}

/* rpu_clk (R5 frequency in Hz, guarded) and rpu_clk_en (0/1) */
void ucube_rpu_clk_add_files(struct device *dev, struct ucube_fclk *f)
{
    fclk_add_file(dev, &f->rate_attr, "rpu_clk", "",    rpu_clk_show, rpu_clk_store);
    fclk_add_file(dev, &f->en_attr,   "rpu_clk", "_en", fclk_en_show, fclk_en_store);
}

void ucube_fclk_add_files(struct device *dev, struct ucube_fclk *f, int i)
{
    char name[16];

    snprintf(name, sizeof(name), "fclk_%d", i);
    ucube_clk_add_files(dev, f, name);
}
