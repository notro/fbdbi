/*
 * Framebuffer Driver for Adafruit Monochrome OLED displays
 *
 * Copyright 2015 Noralf Tronnes
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>

#include "core/lcdreg.h"
#include "core/fbdbi.h"
#include "ssd1306.h"


/* Init sequence from github.com/adafruit/Adafruit_SSD1306 */
static int adafruit13_poweron(struct fbdbi_display *display)
{
	struct lcdreg *lcdreg = display->lcdreg;
	int ret;

	pr_info("%s()\n", __func__);

	ret = regulator_enable(display->power_supply);
	if (ret < 0) {
		dev_err(lcdreg->dev,
			"failed to enable power supply: %d\n", ret);
		return ret;
	}

	lcdreg_reset(lcdreg);

	ret = lcdreg_writereg(lcdreg, SSD1306_DISPLAY_OFF);
	if (ret) {
		dev_err(lcdreg->dev,
			"lcdreg_writereg failed: %d\n", ret);
		return ret;
	}

	if (lcdreg_is_readable(lcdreg))
		ssd1306_check_status(lcdreg, false);

	lcdreg_writereg(lcdreg, SSD1306_CLOCK_FREQ);
	lcdreg_writereg(lcdreg, 0x80);

	lcdreg_writereg(lcdreg, SSD1306_MULTIPLEX_RATIO);
	if (display->info->var.yres == 64)
		lcdreg_writereg(lcdreg, 0x3f);
	else
		lcdreg_writereg(lcdreg, 0x1f);

	lcdreg_writereg(lcdreg, SSD1306_DISPLAY_OFFSET);
	lcdreg_writereg(lcdreg, 0x0);

	lcdreg_writereg(lcdreg, SSD1306_DISPLAY_START_LINE);

	lcdreg_writereg(lcdreg, SSD1306_CHARGE_PUMP);
	lcdreg_writereg(lcdreg, 0x14);

	lcdreg_writereg(lcdreg, SSD1306_ADDRESS_MODE);
	lcdreg_writereg(lcdreg, 0x01); /* vertical */

	lcdreg_writereg(lcdreg, SSD1306_COL_RANGE);
	lcdreg_writereg(lcdreg, 0x00);
	lcdreg_writereg(lcdreg, display->info->var.xres - 1);

	lcdreg_writereg(lcdreg, SSD1306_PAGE_RANGE);
	lcdreg_writereg(lcdreg, 0x00);
	lcdreg_writereg(lcdreg, (display->info->var.yres / 8) - 1);

	lcdreg_writereg(lcdreg, SSD1306_SEG_REMAP_ON);

	lcdreg_writereg(lcdreg, SSD1306_COM_SCAN_REMAP);

	lcdreg_writereg(lcdreg, SSD1306_COM_PINS_CONFIG);
	if (display->info->var.yres == 64)
		lcdreg_writereg(lcdreg, 0x12);
	else
		lcdreg_writereg(lcdreg, 0x02);

	lcdreg_writereg(lcdreg, SSD1306_PRECHARGE_PERIOD);
	lcdreg_writereg(lcdreg, 0xf1);

	lcdreg_writereg(lcdreg, SSD1306_VCOMH);
	lcdreg_writereg(lcdreg, 0x40);

	lcdreg_writereg(lcdreg, SSD1306_RESUME_TO_RAM);

	lcdreg_writereg(lcdreg, SSD1306_NORMAL_DISPLAY);

	lcdreg_writereg(lcdreg, SSD1306_CONTRAST);
	if (display->info->var.yres == 64)
		lcdreg_writereg(lcdreg, 0xcf);
	else
		lcdreg_writereg(lcdreg, 0x8f);

	lcdreg_writereg(lcdreg, SSD1306_DISPLAY_ON);

	if (lcdreg_is_readable(lcdreg))
		ssd1306_check_status(lcdreg, true);

	return 0;
 }

static const struct of_device_id adafruit13_ids[] = {
        { .compatible = "ada,ssd1306-128x64", .data = (void *)64 },
        { .compatible = "ada,ssd1306-128x32", .data = (void *)32 },
        {},
};
MODULE_DEVICE_TABLE(of, adafruit13_ids);

static int adafruit13_probe_common(struct lcdreg *lcdreg)
{
	const struct of_device_id *of_id;
	struct device *dev = lcdreg->dev;
	struct fbdbi_display *display;
	struct ssd1306_config cfg = {
		.xres = 128,
	};

	pr_info("%s()\n", __func__);

	of_id = of_match_device(adafruit13_ids, dev);
	if (!of_id)
		return -EINVAL;

	cfg.yres = (u32)of_id->data;
	display = devm_ssd1306_init(lcdreg, &cfg);
	if (IS_ERR(display))
		return PTR_ERR(display);

	display->format = fbdbi_of_format(dev, FBDBI_FORMAT_RGB565);
	display->poweron = adafruit13_poweron;

	return devm_fbdbi_register_dt(dev, display);
}

static int adafruit13_spi_probe(struct spi_device *spi)
{
	struct lcdreg *lcdreg;

	pr_info("%s()\n", __func__);

	lcdreg = devm_lcdreg_spi_init_dt(spi, LCDREG_SPI_4WIRE);
	if (IS_ERR(lcdreg))
		return PTR_ERR(lcdreg);

	return adafruit13_probe_common(lcdreg);
}

static struct spi_driver adafruit13_spi_driver = {
	.driver = {
		.name   = "adafruit13fb",
		.owner  = THIS_MODULE,
                .of_match_table = adafruit13_ids,
	},
	.probe  = adafruit13_spi_probe,
};

static int adafruit13_i2c_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct lcdreg *lcdreg;

	pr_info("%s()\n", __func__);

	lcdreg = devm_lcdreg_i2c_init(client);
	if (IS_ERR(lcdreg))
		return PTR_ERR(lcdreg);

	return adafruit13_probe_common(lcdreg);
}


static const struct i2c_device_id ssd1307fb_i2c_id[] = {
	{ "adafruit13fb", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ssd1307fb_i2c_id);


static struct i2c_driver adafruit13_i2c_driver = {
	.driver = {
		.name   = "adafruit13fb",
                .of_match_table = adafruit13_ids,
	},
	.id_table = ssd1307fb_i2c_id,
	.probe  = adafruit13_i2c_probe,
};

static int __init adafruit13_init(void)
{
	int ret;

	pr_info("%s()\n", __func__);
	ret = spi_register_driver(&adafruit13_spi_driver);
	if (ret)
		return ret;

	ret = i2c_register_driver(THIS_MODULE, &adafruit13_i2c_driver);
	if (ret)
		spi_unregister_driver(&adafruit13_spi_driver);

	return ret;
}
module_init(adafruit13_init);

static void __exit adafruit13_exit(void)
{
	pr_info("%s()\n", __func__);
	spi_unregister_driver(&adafruit13_spi_driver);
	i2c_del_driver(&adafruit13_i2c_driver);
}
module_exit(adafruit13_exit);


MODULE_ALIAS("spi:ssd1306-128x64");
MODULE_ALIAS("spi:ssd1306-128x32");

MODULE_DESCRIPTION("Framebuffer Driver for Adafruit Monochrome OLED displays");
MODULE_AUTHOR("Noralf Tronnes");
MODULE_LICENSE("GPL");
