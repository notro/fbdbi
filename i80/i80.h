/*
 * Intel 8080 compatible bus interface
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

#ifndef __LINUX_I80_H
#define __LINUX_I80_H

#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>
#include <linux/scatterlist.h>


/* ----------------------------------------------------------------------- */

/* Should be in: include/linux/mod_devicetable.h */
/* i80 */

#define I80_NAME_SIZE   32
#define I80_MODULE_PREFIX "i80:"

struct i80_device_id {
	char name[I80_NAME_SIZE];
	kernel_ulong_t driver_data;     /* Data private to the driver */
};


/* ----------------------------------------------------------------------- */

extern struct bus_type i80_bus_type;

struct i80_device;

struct i80_master {
	struct device dev;
	int id;
	u32 address_width;
	u32 data_width;
	bool readable;
	bool writable;
	struct mutex bus_lock;

	int (*set_address)(struct i80_device *i80, u32 address);
	int (*read)(struct i80_device *i80, void *buf, size_t len);
	int (*write)(struct i80_device *i80, void *buf, size_t len);

};

static inline struct i80_master *i80_master_get(struct i80_master *master)
{
	if (!master || !get_device(&master->dev))
		return NULL;
	return master;
}

static inline void i80_master_put(struct i80_master *master)
{
	if (master)
		put_device(&master->dev);
}

extern int devm_i80_register_master(struct device *dev,
					struct i80_master *master);

/**
 * @address - Chip address on the address bus
 *            Zero means the chip is always selected.
 * @address_mask - The part of the address bus that pertains to the device in question.
 */

struct i80_device {
	struct device dev;
	struct i80_master *master;
	char modalias[I80_NAME_SIZE];

	u32 address;
	u32 address_mask;
	bool isopen;
};

extern void i80_unregister_device(struct i80_device *i80);

static inline struct i80_device *to_i80_device(struct device *dev)
{
	return dev ? container_of(dev, struct i80_device, dev) : NULL;
}

static inline struct i80_device *i80_dev_get(struct i80_device *i80)
{
	return (i80 && get_device(&i80->dev)) ? i80 : NULL;
}

static inline void i80_dev_put(struct i80_device *i80)
{
	if (i80)
		put_device(&i80->dev);
}

static inline int i80_set_address(struct i80_device *i80, u32 address)
{
	if (!i80->isopen)
		return -EINVAL;

	return i80->master->set_address(i80, address);
}

static inline void i80_open(struct i80_device *i80)
{
	mutex_lock(&i80->master->bus_lock);
	i80->isopen = true;
}

static inline void i80_close(struct i80_device *i80)
{
	i80_set_address(i80, 0);
	i80->isopen = false;
	mutex_unlock(&i80->master->bus_lock);
}

extern int i80_read(struct i80_device *i80, u32 address, void *buf,
							size_t len);
extern int i80_write(struct i80_device *i80, u32 address, void *buf,
							size_t len);


struct i80_driver {
	const struct i80_device_id *id_table;
	int (*probe)(struct i80_device *i80);
	int (*remove)(struct i80_device *i80);
	void (*shutdown)(struct i80_device *i80);
	struct device_driver driver;
};

static inline struct i80_driver *to_i80_driver(struct device_driver *drv)
{
	return drv ? container_of(drv, struct i80_driver, driver) : NULL;
}

extern int __i80_driver_register(struct i80_driver *drv,
				 struct module *owner);
extern void i80_driver_unregister(struct i80_driver *drv);

#define i80_driver_register(drv) \
	__i80_driver_register(drv, THIS_MODULE)


#define module_i80_driver(__i80_driver) \
	module_driver(__i80_driver, i80_driver_register, \
			i80_driver_unregister)

#endif /* __LINUX_I80_H */
