#define DEBUG

#include <linux/module.h>
#include "core/lcdreg.h"
#include "core/fbdbi.h"
#include "ili9320.h"


struct ili9320_controller {
	unsigned addr_mode0;
	unsigned addr_mode90;
	unsigned addr_mode180;
	unsigned addr_mode270;
	struct fbdbi_display display;
};

static inline struct ili9320_controller *to_controller(struct fbdbi_display *display)
{
	return display ? container_of(display, struct ili9320_controller, display) : NULL;
}


#define WIDTH		240
#define HEIGHT		320

static int ili9320_update(struct fbdbi_display *display, unsigned ys, unsigned ye)
{
	struct lcdreg *lcdreg = display->lcdreg;
	u16 horizontal, vertical;
	int ret;

	pr_debug("%s(ys=%u, ye=%u): xres=%u, yres=%u\n", __func__, ys, ye, display->info->var.xres, display->info->var.yres);

	switch (display->info->var.rotate) {
	case 0:
	default:
		horizontal = 0;
		vertical = ys;
		break;
	case 180:
		horizontal = WIDTH - 1 - 0;
		vertical = HEIGHT - 1 - ys;
		break;
	case 270:
		horizontal = WIDTH - 1 - ys;
		vertical = 0;
		break;
	case 90:
		horizontal = ys;
		vertical = HEIGHT - 1 - 0;
		break;
	}

	lcdreg_lock(display->lcdreg);
	ret = lcdreg_writereg(lcdreg, ILI9320_HORIZONTAL_GRAM_ADDRESS_SET, horizontal);
	ret |= lcdreg_writereg(lcdreg, ILI9320_VERTICAL_GRAM_ADDRESS_SET, vertical);
	ret |= fbdbi_display_update(display, ILI9320_WRITE_DATA_TO_GRAM, ys, ye);
	lcdreg_unlock(display->lcdreg);

	return ret;
}

static int ili9320_rotate(struct fbdbi_display *display)
{
	struct lcdreg *lcdreg = display->lcdreg;
	struct ili9320_controller *controller = to_controller(display);
	u16 val;

	pr_info("%s(): rotate=%u\n", __func__, display->info->var.rotate);

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

	return lcdreg_writereg(lcdreg, ILI9320_ENTRY_MODE, val);
}

static const struct fbdbi_display ili9320_display = {
	.xres = 240,
	.yres = 320,
	.update = ili9320_update,
	.rotate = ili9320_rotate,
	.poweroff = fbdbi_display_poweroff,
};

struct fbdbi_display *devm_ili9320_init(struct lcdreg *lcdreg,
					struct ili9320_config *config)
{
	struct ili9320_controller *controller;
	struct fbdbi_display *display;

	pr_info("%s()\n", __func__);

	controller = devm_kzalloc(lcdreg->dev, sizeof(*controller), GFP_KERNEL);
	if (!controller)
		return ERR_PTR(-ENOMEM);

	display = &controller->display;
	*display = ili9320_display;
	display->lcdreg = lcdreg;
	display->lcdreg->def_width = 16;
	if (config->xres)
		display->xres = config->xres;
	if (config->yres)
		display->yres = config->yres;
	display->format = FBDBI_FORMAT_RGB565;
	controller->addr_mode0 = config->addr_mode0 | (config->bgr << 12);
	controller->addr_mode90 = config->addr_mode90 | (config->bgr << 12);
	controller->addr_mode180 = config->addr_mode180 | (config->bgr << 12);
	controller->addr_mode270 = config->addr_mode270 | (config->bgr << 12);

	return display;
}
EXPORT_SYMBOL(devm_ili9320_init);

bool ili9320_check_driver_code(struct lcdreg *lcdreg, u16 expected)
{
	u32 val32;
	int ret;

	ret = lcdreg_readreg_buf32(lcdreg, 0x0000, &val32, 1);
	if (ret) {
		dev_err(lcdreg->dev, "failed to read Driver Code: %i\n", ret);
		return false;
	}
	if (val32 == expected) {
		//TODO : dev_dbg
		dev_info(lcdreg->dev, "%s: OK\n", __func__);
		return true;
	} else {
		dev_warn(lcdreg->dev,
			 "Driver Code mismatch: 0x%04x != 0x%04x\n",
			 val32, expected);
		return false;
	}
}
EXPORT_SYMBOL(ili9320_check_driver_code);

//MODULE_DESCRIPTION("Custom FB driver for tinylcd.com display");
MODULE_AUTHOR("Noralf Tronnes");
MODULE_LICENSE("GPL");
