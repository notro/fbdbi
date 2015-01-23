//#define DEBUG

/*
 * SSD1306 LCD controller support
 *
 * Copyright 2015 Noralf Trønnes
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>

#include "ssd1306.h"

/*
 * TODO: contrast control is not implemented
 *       If gamma control is to be user configurable, maybe the same
 *       mechanism can be used for contrast.
 */

struct ssd1306_controller {
	struct fbdbi_display display;
	void *buf;
};

static inline struct ssd1306_controller *to_controller(struct fbdbi_display *display)
{
	return display ? container_of(display, struct ssd1306_controller, display) : NULL;
}

static int ssd1306_update(struct fbdbi_display *display, unsigned ys, unsigned ye)
{
	struct lcdreg *lcdreg = display->lcdreg;
	struct fb_var_screeninfo *var = &display->info->var;
	u8 *buf = to_controller(display)->buf;
	int x, y, i;
	int ret;
	struct lcdreg_transfer tr = {
		.index = 1,
		.width = 8,
		.buf = buf,
		.count = var->xres * var->yres / 8,
	};

	pr_debug("%s(ys=%u, ye=%u): xres=%u, yres=%u\n", __func__, ys, ye, var->xres, var->yres);

	lcdreg_lock(lcdreg);

	/*
	 * RGB565 is implemented on this monochrome controller for 2 reasons:
	 * 1. sys_imageblit and friends doesn't honour 
	 *    CONFIG_FB_CFB_REV_PIXELS_IN_BYTE which results in mirrored or
	 *    garbeled fonts using fbcon on systems like the Raspberry Pi.
	 * 2. Very few applications and no grahics libraries supports
	 *    monochrome framebuffers.
	 */
	if (display->format == FBDBI_FORMAT_RGB565) {
		u16 *vmem16 = (u16 *)display->info->screen_base;

		/* TODO: add better conversion as done in fb_agm1264k-fl */
		for (x = 0; x < var->xres; x++) {
			for (y = 0; y < var->yres / 8; y++) {
				*buf = 0x00;
				for (i = 0; i < 8; i++)
					*buf |= (vmem16[(y * 8 + i) * var->xres + x] ? 1 : 0) << i;
				buf++;
			}
		}
	} else { /* FBDBI_FORMAT_MONO10 */
		u8 *vmem8 = (u8 *)display->info->screen_base;

		for (x = 0; x < var->xres; x++) {
			for (y = 0; y < var->yres / 8; y++) {
				*buf = 0x00;
				for (i = 0; i < 8; i++) {
					int y1 = y * 8 + i;
					int idx = (y1 * (var->xres / 8)) + (x / 8);
					int mask = (1 << (7 - (x % 8)));
					*buf |= (vmem8[idx] & mask ? 1 : 0) << i;
				}
				buf++;
			}
		}
	}

	ret = lcdreg_write(lcdreg, SSD1306_DISPLAY_START_LINE, &tr);
	lcdreg_unlock(lcdreg);

	return ret;
}

static int ssd1306_blank(struct fbdbi_display *display, bool blank)
{
	pr_debug("%s(blank=%i)\n", __func__, blank);
	return lcdreg_writereg(display->lcdreg, blank ? SSD1306_DISPLAY_OFF :
							SSD1306_DISPLAY_ON);
}

static int ssd1306_set_format(struct fbdbi_display *display)
{
	pr_debug("%s(): format=%i, bits_per_pixel=%u\n", __func__, display->format, display->info->var.bits_per_pixel);

	switch (display->format) {
	case FBDBI_FORMAT_MONO10:
		break;
	case FBDBI_FORMAT_RGB565:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int ssd1306_poweroff(struct fbdbi_display *display)
{
	pr_debug("%s()\n", __func__);
	ssd1306_blank(display, true);

	return fbdbi_display_poweroff(display);
}

static const struct fbdbi_display ssd1306_display = {
	.update = ssd1306_update,
	.blank = ssd1306_blank,
	.set_format = ssd1306_set_format,
	.poweroff = ssd1306_poweroff,
};

struct fbdbi_display *devm_ssd1306_init(struct lcdreg *lcdreg,
					struct ssd1306_config *config)
{
	struct ssd1306_controller *controller;
	struct fbdbi_display *display;

	pr_debug("%s()\n", __func__);

	controller = devm_kzalloc(lcdreg->dev, sizeof(*controller),
				  GFP_KERNEL);
	if (!controller)
		return ERR_PTR(-ENOMEM);

	display = &controller->display;
	*display = ssd1306_display;
	display->lcdreg = lcdreg;
	display->lcdreg->def_width = 8;
	display->xres = config->xres ? : 128;
	display->yres = config->yres ? : 64;
	display->format = FBDBI_FORMAT_MONO10;

	controller->buf = devm_kzalloc(lcdreg->dev,
				       display->xres * display->yres / 8,
				       GFP_KERNEL);
	if (!controller->buf)
		return ERR_PTR(-ENOMEM);

	return display;
}
EXPORT_SYMBOL(devm_ssd1306_init);

bool ssd1306_check_status(struct lcdreg *reg, bool display_is_on)
{
	u32 val;
	int ret;

	ret = lcdreg_readreg_buf32(reg, 0x00, &val, 1);
	if (ret) {
		dev_err(reg->dev, "failed to read status: %i\n", ret);
		return false;
	}

	if (((val & BIT(6)) >> 6) == display_is_on) {
		dev_warn(reg->dev,
			"status check failed: 0x%02x", val);
		return false;
	}

	dev_dbg(reg->dev, "%s: OK\n", __func__);

	return true;
}
EXPORT_SYMBOL(ssd1306_check_status);

MODULE_DESCRIPTION("SSD1306 LCD controller support");
MODULE_AUTHOR("Noralf Tronnes");
MODULE_LICENSE("GPL");
