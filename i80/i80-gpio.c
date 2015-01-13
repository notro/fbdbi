//#define DEBUG

/*
 * Intel 8080/8086 Bus GPIO implementation
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

#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "i80.h"

#define I80_MAX_BUSWIDTH 32

struct i80_gpio {
	struct i80_master master;
	struct gpio_desc *wr;
	struct gpio_desc *rd;
	struct gpio_desc *address[I80_MAX_BUSWIDTH];
	struct gpio_desc *data[I80_MAX_BUSWIDTH];

	u32 curr_address;
	u32 curr_data;

	int direction;
#define I80_GPIO_DIR_NOT_SET  0
#define I80_GPIO_DIR_INPUT    1
#define I80_GPIO_DIR_OUTPUT   2
};

static inline struct i80_gpio *to_i80_gpio(struct i80_device *i80)
{
	return i80 ? container_of(i80->master, struct i80_gpio, master) : NULL;
}

static int i80_gpio_set_address(struct i80_device *i80, u32 address)
{
	struct i80_gpio *i80gpio = to_i80_gpio(i80);
	int i;

	dev_dbg(&i80->dev, "%s(address=0x%x)\n", __func__, address);
	if (i80gpio->curr_address != address || address == ~0) {
		i80gpio->curr_address = address;
		for (i = 0; i < i80gpio->master.address_width; i++) {
			gpiod_set_value(i80gpio->address[i], (address & 1));
			address >>= 1;
		}
	}
	return 0;
}

static int i80_gpio_direction(struct i80_gpio *i80gpio, bool input)
{
	int i, ret;

	switch (i80gpio->direction) {
	case I80_GPIO_DIR_NOT_SET:
		break;
	case I80_GPIO_DIR_INPUT:
		if (input)
			return 0;
		break;
	case I80_GPIO_DIR_OUTPUT:
		if (!input)
			return 0;
		break;
	}
	i80gpio->direction = input ? I80_GPIO_DIR_INPUT : I80_GPIO_DIR_OUTPUT;

	pr_debug("%s(%s)\n", __func__, input ? "in" : "out");
	for (i = 0; i < i80gpio->master.data_width; i++) {
		if (input)
			ret = gpiod_direction_input(i80gpio->data[i]);
		else
			ret = gpiod_direction_output(i80gpio->data[i], 0);
		if (ret)
			return ret;
	}

	return 0;
}

static inline void i80_gpio_write_val(struct i80_gpio *i80gpio, u32 val)
{
	int i;

	gpiod_set_value(i80gpio->wr, 0);

	if (i80gpio->curr_data != val || val == ~0) {
		i80gpio->curr_data = val;
		for (i = 0; i < i80gpio->master.data_width; i++) {
			gpiod_set_value(i80gpio->data[i], (val & 1));
			val >>= 1;
		}
	}

	gpiod_set_value(i80gpio->wr, 1);
}

static int i80_gpio_write(struct i80_device *i80, void *buf, size_t len)
{
	struct i80_gpio *i80gpio = to_i80_gpio(i80);
	u32 width = i80->master->data_width;
	int i, ret;

	dev_dbg(&i80->dev, "%s(buf=%p, len=%zu)\n", __func__, buf, len);
	ret = i80_gpio_direction(i80gpio, false);
	if (ret)
		return ret;

	i80gpio->curr_data = ~0;

	if (width > 16) {
		u32 *buf32 = buf;

		for (i = 0; i < len / 4; i++)
			i80_gpio_write_val(i80gpio, *buf32++);
	} else if (width > 8) {
		u16 *buf16 = buf;

		for (i = 0; i < len / 2; i++)
			i80_gpio_write_val(i80gpio, *buf16++);
	} else {
		u8 *buf8 = buf;

		for (i = 0; i < len; i++)
			i80_gpio_write_val(i80gpio, *buf8++);
	}

	return 0;
}

static inline void i80_gpio_read_val(struct i80_gpio *i80gpio, u32 *val)
{
	int i;

	*val = 0;
	gpiod_set_value(i80gpio->rd, 0);

	for (i = 0; i < i80gpio->master.data_width; i++)
		*val |= gpiod_get_value(i80gpio->data[i]) << i;

	gpiod_set_value(i80gpio->rd, 1);
}

static int i80_gpio_read(struct i80_device *i80, void *buf, size_t len)
{
	struct i80_gpio *i80gpio = to_i80_gpio(i80);
	u32 val, width = i80->master->data_width;
	int i, ret;

	dev_dbg(&i80->dev, "%s(buf=%p, len=%zu)\n", __func__, buf, len);
	ret = i80_gpio_direction(i80gpio, true);
	if (ret)
		return ret;

	if (width > 16) {
		u32 *buf32 = buf;

		for (i = 0; i < len / 4; i++) {
			i80_gpio_read_val(i80gpio, &val);
			*buf32++ = val;
		}
	} else if (width > 8) {
		u16 *buf16 = buf;

		for (i = 0; i < len / 2; i++) {
			i80_gpio_read_val(i80gpio, &val);
			*buf16++ = val;
		}
	} else {
		u8 *buf8 = buf;

		for (i = 0; i < len; i++) {
			i80_gpio_read_val(i80gpio, &val);
			*buf8++ = val;
		}
	}

	return 0;
}

struct gpio_desc *i80_gpiod_get_index(struct device *dev, const char *name,
						unsigned int idx, int def_val)
{
	struct gpio_desc *desc;
	int ret;

	desc = devm_gpiod_get_index(dev, name, idx, 0);
	if (IS_ERR(desc) && PTR_ERR(desc) == -ENOENT)
		return NULL;
	if (IS_ERR(desc)) {
		dev_err(dev, "failed to get gpio '%s' (%li)\n", name,
								PTR_ERR(desc));
		return desc;
	}
	ret = gpiod_direction_output(desc, def_val);
	if (ret)
		return ERR_PTR(ret);

/*
 * FIXME: fix drivers/pinctrl/pinctrl-bcm2835.c
 *
 * gpiod_direction_output should set the output value,
 * but bcm2835_gpio_direction_output doesn't honour this.
 * use gpiod_set_value as a temporary fix.
 *
 * gpiod_direction_output - set the GPIO direction to output
 * @desc:       GPIO to set to output
 * @value:      initial output value of the GPIO
 *
 * http://lxr.free-electrons.com/ident?i=_gpiod_direction_output_raw
 * http://lxr.free-electrons.com/ident?i=bcm2835_gpio_direction_output
 *
 */
	gpiod_set_value(desc, def_val);
	dev_dbg(dev, "%s(%s, %u) = %i, value = %i\n", __func__, name, idx,
				desc_to_gpio(desc), gpiod_get_value(desc));

	return desc;
}

int i80_gpio_parse_dt(struct device *dev, struct i80_gpio *i80gpio)
{
	int i;

	i80gpio->wr = i80_gpiod_get_index(dev, "wr", 0, 1);
	if (IS_ERR(i80gpio->wr))
		return PTR_ERR(i80gpio->wr);

	i80gpio->rd = i80_gpiod_get_index(dev, "rd", 0, 1);
	if (IS_ERR(i80gpio->rd))
		return PTR_ERR(i80gpio->rd);

	for (i = 0; i < I80_MAX_BUSWIDTH; i++) {
		i80gpio->address[i] = i80_gpiod_get_index(dev, "address", i, 0);
		if (!i80gpio->address[i])
			break;
		if (IS_ERR(i80gpio->address[i]))
			return PTR_ERR(i80gpio->address[i]);
	}

	for (i = 0; i < I80_MAX_BUSWIDTH; i++) {
		i80gpio->data[i] = i80_gpiod_get_index(dev, "data", i, 0);
		if (!i80gpio->data[i])
			break;
		if (IS_ERR(i80gpio->data[i]))
			return PTR_ERR(i80gpio->data[i]);
	}

	return 0;
}





static int i80_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct i80_gpio *i80gpio;
	struct i80_master *master;
	int i, ret;

pr_info("%s()\n", __func__);
	i80gpio = devm_kzalloc(dev, sizeof(*i80gpio), GFP_KERNEL);
	if (!i80gpio)
		return -ENOMEM;

	master = &i80gpio->master;
	ret = i80_gpio_parse_dt(dev, i80gpio);
	if (ret)
		return ret;

	if (i80gpio->wr)
		master->writable = true;
	if (i80gpio->rd)
		master->readable = true;
	if (!i80gpio->wr && !i80gpio->rd) {
		dev_err(dev, "Missing WR and/or RD\n");
		return -EINVAL;
	}
	for (i = 0; i < I80_MAX_BUSWIDTH; i++)
		if (!i80gpio->address[i])
			break;
	master->address_width = i;
	for (i = 0; i < I80_MAX_BUSWIDTH; i++)
		if (!i80gpio->data[i])
			break;
	master->data_width = i;
	if (!master->data_width) {
		dev_err(dev, "Missing data bus\n");
		return -EINVAL;
	}

	i80gpio->curr_address = ~0;

	master->set_address = i80_gpio_set_address;
	master->read = i80_gpio_read;
	master->write = i80_gpio_write;


	return devm_i80_register_master(dev, master);
}

static const struct of_device_id dt_ids[] = {
	{ .compatible = "i80-gpio" },
	{},
};
MODULE_DEVICE_TABLE(of, dt_ids);

static struct platform_driver i80_gpio_driver = {
	.driver = {
		.name   = "i80-gpio",
		.owner  = THIS_MODULE,
		.of_match_table = of_match_ptr(dt_ids),
	},
	.probe  = i80_gpio_probe,
};
module_platform_driver(i80_gpio_driver);

MODULE_DESCRIPTION("Intel 8080/8086 Bus GPIO implementation");
MODULE_AUTHOR("Noralf Tronnes");
MODULE_LICENSE("GPL");
