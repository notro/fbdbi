#define DEBUG


/*
 * MIPI Display Bus Interface (DBI) LCD controller support
 *
 * Copyright 2014 Noralf Tronnes
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <video/mipi_display.h>

#include "core/lcdreg.h"
#include "core/fbdbi.h"
#include "mipi-dbi.h"

struct mipi_dbi_controller {
	unsigned addr_mode0;
	unsigned addr_mode90;
	unsigned addr_mode180;
	unsigned addr_mode270;
	struct fbdbi_display display;
};

static inline struct mipi_dbi_controller *to_controller(struct fbdbi_display *display)
{
	return display ? container_of(display, struct mipi_dbi_controller, display) : NULL;
}

static int mipi_dbi_update(struct fbdbi_display *display, unsigned ys, unsigned ye)
{
	struct lcdreg *lcdreg = display->lcdreg;
	u16 xs = 0;
	u16 xe = display->info->var.xres - 1;
	int ret;

	pr_debug("%s(ys=%u, ye=%u): xres=%u, yres=%u\n", __func__, ys, ye, display->info->var.xres, display->info->var.yres);

	lcdreg_lock(display->lcdreg);
	ret = lcdreg_writereg(lcdreg, MIPI_DCS_SET_COLUMN_ADDRESS,
		    (xs >> 8) & 0xFF, xs & 0xFF, (xe >> 8) & 0xFF, xe & 0xFF);
	ret |= lcdreg_writereg(lcdreg, MIPI_DCS_SET_PAGE_ADDRESS,
		    (ys >> 8) & 0xFF, ys & 0xFF, (ye >> 8) & 0xFF, ye & 0xFF);
	ret |= fbdbi_display_update(display, MIPI_DCS_WRITE_MEMORY_START, ys, ye);
	lcdreg_unlock(display->lcdreg);

	return ret;
}

static int mipi_dbi_rotate(struct fbdbi_display *display)
{
	struct lcdreg *lcdreg = display->lcdreg;
	struct mipi_dbi_controller *controller = to_controller(display);
	u8 val;

	pr_debug("%s(): rotate=%u\n", __func__, display->info->var.rotate);

	switch (display->info->var.rotate) {
	case 0:
	default:
		val = controller->addr_mode0;
		break;
	case 90:
		val = controller->addr_mode90;
		break;
	case 180:
		val = controller->addr_mode180;
		break;
	case 270:
		val = controller->addr_mode270;
		break;
	}

	return lcdreg_writereg(lcdreg, MIPI_DCS_SET_ADDRESS_MODE, val);
}

static int mipi_dbi_set_format(struct fbdbi_display *display)
{
	struct lcdreg *lcdreg = display->lcdreg;
	u8 val;

	pr_debug("%s(): format=%i, bits_per_pixel=%u\n", __func__, display->format, display->info->var.bits_per_pixel);

	switch (display->format) {
	case FBDBI_FORMAT_RGB565:
		val = 0x05;
		break;
	case FBDBI_FORMAT_RGB888:
	case FBDBI_FORMAT_XRGB8888:
		val = 0x06;
		break;
	default:
		return -EINVAL;
	}

	return lcdreg_writereg(lcdreg, MIPI_DCS_SET_PIXEL_FORMAT, val);
}

static const struct fbdbi_display mipi_dbi_display = {
	.xres = 0,
	.yres = 0,
	.update = mipi_dbi_update,
	.rotate = mipi_dbi_rotate,
	.set_format = mipi_dbi_set_format,
	.poweroff = fbdbi_display_poweroff,
};

struct fbdbi_display *devm_mipi_dbi_init(struct lcdreg *lcdreg,
					 struct mipi_dbi_config *config)
{
	struct mipi_dbi_controller *controller;
	struct fbdbi_display *display;
	bool bgr = config->bgr;

	pr_debug("%s()\n", __func__);

	controller = devm_kzalloc(lcdreg->dev, sizeof(*controller), GFP_KERNEL);
	if (!controller)
		return ERR_PTR(-ENOMEM);

	display = &controller->display;
	*display = mipi_dbi_display;
	display->lcdreg = lcdreg;
	display->lcdreg->def_width = config->regwidth ? : 8;
	display->xres = config->xres;
	display->yres = config->yres;
	display->format = config->format ? : FBDBI_FORMAT_RGB565;
#ifdef __LITTLE_ENDIAN
	if (display->format == FBDBI_FORMAT_RGB888)
		bgr = !bgr;
#endif
	controller->addr_mode0 = config->addr_mode0 | (bgr << 3);
	controller->addr_mode90 = config->addr_mode90 | (bgr << 3);
	controller->addr_mode180 = config->addr_mode180 | (bgr << 3);
	controller->addr_mode270 = config->addr_mode270 | (bgr << 3);

	return display;
}
EXPORT_SYMBOL(devm_mipi_dbi_init);

bool mipi_dbi_check_diagnostics(struct lcdreg *reg)
{
	u32 val;
	int ret;

	ret = lcdreg_readreg_buf32(reg, MIPI_DCS_GET_DIAGNOSTIC_RESULT, &val, 1);
	if (ret) {
		dev_warn(reg->dev,
			"failed to read from controller: %d", ret);
		return false;
	}

	/*
	 * Chip Attachment Detection and Display Glass Break Detection
	 * bits are optional (masked out)
	 */
	if (!((val & 0b11001111) && 0b11000000)) {
		dev_warn(reg->dev,
			"controller diagnostics failed: 0x%02X", val);
		return false;
	}

	dev_dbg(reg->dev, "%s: OK\n", __func__);

	return true;
}
EXPORT_SYMBOL(mipi_dbi_check_diagnostics);


MODULE_DESCRIPTION("MIPI DBI LCD controller support");
MODULE_AUTHOR("Noralf Tronnes");
MODULE_LICENSE("GPL");
