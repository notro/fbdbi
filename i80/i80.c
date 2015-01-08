//#define DEBUG

/*
 * Intel 8080/8086 Bus
 *
 * Copyright (C) 2015 Noralf Tronnes
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/device.h>
#include <linux/idr.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include <linux/of_device.h>

#include <linux/slab.h>

#include "i80.h"


static DEFINE_MUTEX(i80_master_idr_lock);
static DEFINE_IDR(i80_master_idr);


/*
 *  Device
 */

static inline int i80_verify_and_set_address(struct i80_device *i80,
							u32 address)
{
	if (!i80->isopen)
		return -EINVAL;

	if (address & ~i80->address_mask)
		return -ENXIO;

	return i80_set_address(i80, i80->address | address);
}

int i80_read(struct i80_device *i80, u32 address, void *buf, size_t len)
{
	int ret;

	ret = i80_verify_and_set_address(i80, address);
	if (ret)
		return ret;

	return i80->master->read(i80, buf, len);
}
EXPORT_SYMBOL_GPL(i80_read);

int i80_write(struct i80_device *i80, u32 address, void *buf, size_t len)
{
	int ret;

	ret = i80_verify_and_set_address(i80, address);
	if (ret)
		return ret;

	return i80->master->write(i80, buf, len);
}
EXPORT_SYMBOL_GPL(i80_write);

static void i80dev_release(struct device *dev)
{
	struct i80_device *i80 = to_i80_device(dev);

	pr_debug("%s()\n", __func__);
	i80_master_put(i80->master);
	kfree(i80);
}

/**
 * i80_alloc_device - Allocate a new I80 device
 * @master: Controller to which device is connected
 * Context: can sleep
 *
 * Allows a driver to allocate and initialize a i80_device without
 * registering it immediately.  This allows a driver to directly
 * fill the i80_device with device parameters before calling
 * i80_add_device() on it.
 *
 * Caller is responsible to call i80_add_device() on the returned
 * i80_device structure to add it to the I80 master.  If the caller
 * needs to discard the i80_device without adding it, then it should
 * call i80_dev_put() on it.
 *
 * Returns a pointer to the new device, or NULL.
 */
struct i80_device *i80_alloc_device(struct i80_master *master)
{
	struct i80_device *i80;

	pr_debug("%s()\n", __func__);
	if (!i80_master_get(master))
		return NULL;

	i80 = kzalloc(sizeof(*i80), GFP_KERNEL);
	if (!i80) {
		i80_master_put(master);
		return NULL;
	}

	i80->master = master;
	i80->dev.parent = &master->dev;
	i80->dev.bus = &i80_bus_type;
	i80->dev.release = i80dev_release;
	device_initialize(&i80->dev);
	return i80;
}
EXPORT_SYMBOL_GPL(i80_alloc_device);

static void i80_dev_set_name(struct i80_device *i80)
{
	dev_set_name(&i80->dev, "%s.%u", dev_name(&i80->master->dev),
		     i80->address);
}

static int i80_dev_check(struct device *dev, void *data)
{
	struct i80_device *i80 = to_i80_device(dev);
	struct i80_device *new_i80 = data;

	if (i80->master == new_i80->master &&
	    (i80->address == new_i80->address ||
	    i80->address_mask & new_i80->address))
		return -EBUSY;
	return 0;
}

/**
 * i80_add_device - Add i80_device allocated with i80_alloc_device
 * @i80: i80_device to register
 *
 * Companion function to i80_alloc_device.  Devices allocated with
 * i80_alloc_device can be added onto the i80 bus with this function.
 *
 * Returns 0 on success; negative errno on failure
 */
int i80_add_device(struct i80_device *i80)
{
	struct i80_master *master = i80->master;
	struct device *dev = master->dev.parent;
	int ret;

	pr_debug("%s()\n", __func__);
	i80_dev_set_name(i80);

	ret = bus_for_each_dev(&i80_bus_type, NULL, i80, i80_dev_check);
	if (ret) {
		dev_err(dev, "address 0x%x already in use\n",
				i80->address);
		return ret;
	}

	/* Device may be bound to an active driver when this returns */
	ret = device_add(&i80->dev);
	if (ret < 0)
		dev_err(dev, "can't add %s (%d)\n",
				dev_name(&i80->dev), ret);
	else
		dev_info(dev, "registered device %s\n", dev_name(&i80->dev));

	return ret;
}
EXPORT_SYMBOL_GPL(i80_add_device);

void i80_unregister_device(struct i80_device *i80)
{
	pr_debug("%s(%s)\n", __func__, i80 ? "yes" : "no");
	if (i80) {
		of_node_put(i80->dev.of_node);
		device_unregister(&i80->dev);
	}
}
EXPORT_SYMBOL_GPL(i80_unregister_device);

#if defined(CONFIG_OF)
/**
 * of_register_i80_devices() - Register child devices onto the I80 bus
 * @master:	Pointer to i80_master device
 *
 * Registers an i80_device for each child node of master node which has a 'reg'
 * property.
 */
static void of_register_i80_devices(struct i80_master *master)
{
	struct i80_device *i80;
	struct device_node *np;
	u32 reg[2];
	int ret;

	pr_debug("%s()\n", __func__);
	if (!master->dev.of_node)
		return;

	for_each_available_child_of_node(master->dev.of_node, np) {
		/* Alloc an i80_device */
		i80 = i80_alloc_device(master);
		if (!i80) {
			dev_err(&master->dev, "i80_device alloc error for %s\n",
				np->full_name);
			i80_dev_put(i80);
			continue;
		}

		/* Select device driver */
		if (of_modalias_node(np, i80->modalias,
				     sizeof(i80->modalias)) < 0) {
			dev_err(&master->dev, "cannot find modalias for %s\n",
				np->full_name);
			i80_dev_put(i80);
			continue;
		}

		/* Device address and mask */
		ret = of_property_read_u32_array(np, "reg", reg, 2);
		if (ret) {
			dev_err(&master->dev,
				"failed to read reg property (%d) for %s\n",
				ret, np->full_name);
			i80_dev_put(i80);
			continue;
		}
		i80->address = reg[0];

		if ((reg[1] == 0x1) || (reg[1] & (reg[1] - 1))) {
			dev_err(&master->dev,
				"illegal address size 0x%x for %s\n",
				reg[1], np->full_name);
			i80_dev_put(i80);
			continue;
		}
		i80->address_mask = reg[1] - 1;

		if (~i80->address_mask && (i80->address & i80->address_mask)) {
			dev_err(&master->dev,
				"address 0x%x overlaps address mask 0x%x for %s\n",
				i80->address, i80->address_mask, np->full_name);
			i80_dev_put(i80);
			continue;
		}

		if (i80->address >= (1 << master->address_width)) {
			dev_err(&master->dev,
				"address too large 0x%x for %s\n",
				i80->address, np->full_name);
			i80_dev_put(i80);
			continue;
		}

		/* Store a pointer to the node in the device structure */
		of_node_get(np);
		i80->dev.of_node = np;

		/* Register the new device */
		request_module("%s%s", I80_MODULE_PREFIX, i80->modalias);
		ret = i80_add_device(i80);
		if (ret) {
			dev_err(&master->dev, "i80_device register error %s\n",
				np->full_name);
			i80_dev_put(i80);
		}
	}
}
#else
static void of_register_i80_devices(struct i80_master *master) { }
#endif


/*
 *  Driver
 */

#include <linux/clk/clk-conf.h>
#include <linux/pm_domain.h>
static int i80_drv_probe(struct device *_dev)
{
	struct i80_driver *drv = to_i80_driver(_dev->driver);
	struct i80_device *dev = to_i80_device(_dev);
	int ret;

	pr_debug("%s()\n", __func__);
	ret = of_clk_set_defaults(_dev->of_node, false);
	if (ret < 0)
		return ret;

	ret = dev_pm_domain_attach(_dev, true);
	if (ret == -EPROBE_DEFER)
		return ret;

	ret = drv->probe(dev);
	if (ret)
		dev_pm_domain_detach(_dev, true);

	return ret;
}

static int i80_drv_remove(struct device *dev)
{
	const struct i80_driver *sdrv = to_i80_driver(dev->driver);
	int ret;

	ret = sdrv->remove(to_i80_device(dev));
	dev_pm_domain_detach(dev, true);

	return ret;
}

static void i80_drv_shutdown(struct device *dev)
{
	const struct i80_driver *sdrv = to_i80_driver(dev->driver);

	sdrv->shutdown(to_i80_device(dev));
}

int __i80_driver_register(struct i80_driver *drv,
				struct module *owner)
{
	pr_debug("%s()\n", __func__);
	drv->driver.owner = owner;
	drv->driver.bus = &i80_bus_type;
	if (drv->probe)
		drv->driver.probe = i80_drv_probe;
	if (drv->remove)
		drv->driver.remove = i80_drv_remove;
	if (drv->shutdown)
		drv->driver.shutdown = i80_drv_shutdown;

	return driver_register(&drv->driver);
}
EXPORT_SYMBOL_GPL(__i80_driver_register);

void i80_driver_unregister(struct i80_driver *drv)
{
	pr_debug("%s()\n", __func__);
	driver_unregister(&drv->driver);
}
EXPORT_SYMBOL_GPL(i80_driver_unregister);


/*
 *  Master stuff - Bus Controller
 */


/* struct i80_master is freed on parent device release through devm_kzalloc */
static void i80_master_release(struct device *dev)
{
	pr_debug("%s()\n", __func__);
}

static struct class i80_master_class = {
	.name		= "i80_master",
	.owner		= THIS_MODULE,
	.dev_release	= i80_master_release,
};


struct i80_master *i80_nr_to_master(int nr)
{
	struct i80_master *master;

	pr_debug("%s(nr=%d)\n", __func__, nr);
	mutex_lock(&i80_master_idr_lock);
	master = idr_find(&i80_master_idr, nr);
	mutex_unlock(&i80_master_idr_lock);
	return master;
}

static int __unregister(struct device *dev, void *null)
{
	pr_debug("%s()\n", __func__);
	i80_unregister_device(to_i80_device(dev));
	return 0;
}

void devm_i80_unregister_master(struct i80_master *master)
{
	pr_debug("%s()\n", __func__);
	mutex_lock(&i80_master_idr_lock);
	idr_remove(&i80_master_idr, master->id);
	mutex_unlock(&i80_master_idr_lock);

	mutex_destroy(&master->bus_lock);
	device_for_each_child(&master->dev, NULL, __unregister);
	device_unregister(&master->dev);
}
EXPORT_SYMBOL_GPL(devm_i80_unregister_master);

static void _devm_i80_unregister(struct device *dev, void *res)
{
	pr_debug("%s()\n", __func__);
	devm_i80_unregister_master(*(struct i80_master **)res);
}

int devm_i80_register_master(struct device *dev, struct i80_master *master)
{
	struct i80_master **ptr;
	int id = master->id ? : -1;
	int ret;

	pr_debug("%s()\n", __func__);
	if (dev->of_node)
		id = of_alias_get_id(dev->of_node, "i80-");

	/* ida_simple_get() can be used if i80_nr_to_master() is dropped */
	mutex_lock(&i80_master_idr_lock);
	if (id < 0)
		id = idr_alloc(&i80_master_idr, master, 0, 0, GFP_KERNEL);
	else
		id = idr_alloc(&i80_master_idr, master, id, id + 1,
								GFP_KERNEL);
	mutex_unlock(&i80_master_idr_lock);
	if (id < 0)
		return id;

	master->id = id;

	ptr = devres_alloc(_devm_i80_unregister, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	mutex_init(&master->bus_lock);
	device_initialize(&master->dev);
	master->dev.class = &i80_master_class;
	master->dev.parent = get_device(dev);
	master->dev.of_node = dev->of_node;

	dev_set_name(&master->dev, "i80-%i", master->id);
	ret = device_add(&master->dev);
	if (!ret) {
		*ptr = master;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
		return ret;
	}

	dev_info(dev, "registered master %s\n", dev_name(&master->dev));

	/* Register devices from the device tree */
	of_register_i80_devices(master);

	return 0;
}
EXPORT_SYMBOL_GPL(devm_i80_register_master);


/*
 *  Bus
 */


static ssize_t
modalias_show(struct device *dev, struct device_attribute *a, char *buf)
{
	const struct i80_device	*i80 = to_i80_device(dev);

	pr_debug("%s()\n", __func__);
	return sprintf(buf, "%s%s\n", I80_MODULE_PREFIX, i80->modalias);
}
static DEVICE_ATTR_RO(modalias);

static struct attribute *i80_dev_attrs[] = {
	&dev_attr_modalias.attr,
	NULL,
};
ATTRIBUTE_GROUPS(i80_dev);

static const struct i80_device_id *i80_match_id(const struct i80_device_id *id,
						const struct i80_device *sdev)
{
	pr_debug("%s()\n", __func__);
	while (id->name[0]) {
		if (!strcmp(sdev->modalias, id->name))
			return id;
		id++;
	}
	return NULL;
}

const struct i80_device_id *i80_get_device_id(const struct i80_device *sdev)
{
	const struct i80_driver *sdrv = to_i80_driver(sdev->dev.driver);

	pr_debug("%s()\n", __func__);
	return i80_match_id(sdrv->id_table, sdev);
}
EXPORT_SYMBOL_GPL(i80_get_device_id);

static int i80_match_device(struct device *dev, struct device_driver *drv)
{
	const struct i80_device	*i80 = to_i80_device(dev);
	const struct i80_driver	*sdrv = to_i80_driver(drv);

	pr_debug("%s()\n", __func__);
	/* Attempt an OF style match */
	if (of_driver_match_device(dev, drv))
		return 1;

	if (sdrv->id_table)
		return !!i80_match_id(sdrv->id_table, i80);

	return strcmp(i80->modalias, drv->name) == 0;
}

static int i80_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	const struct i80_device	*i80 = to_i80_device(dev);

	pr_debug("%s()\n", __func__);
	add_uevent_var(env, "MODALIAS=%s%s", I80_MODULE_PREFIX, i80->modalias);
	return 0;
}

struct bus_type i80_bus_type = {
	.name		= "i80",
	.dev_groups	= i80_dev_groups,
	.match		= i80_match_device,
	.uevent		= i80_uevent,
/*	.pm		= &i80_pm, */
};
EXPORT_SYMBOL_GPL(i80_bus_type);

static int __init i80_init(void)
{
	int ret;

	ret = bus_register(&i80_bus_type);
	if (ret)
		return ret;

	ret = class_register(&i80_master_class);
	if (ret)
		bus_unregister(&i80_bus_type);

	return ret;
}
module_init(i80_init);

static void __exit i80_exit(void)
{
	bus_unregister(&i80_bus_type);
	class_unregister(&i80_master_class);
}
module_exit(i80_exit);

MODULE_AUTHOR("Noralf Tronnes");
MODULE_LICENSE("GPL");
