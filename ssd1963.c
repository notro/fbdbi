
#define DEBUG

#include "ssd1963.h"


//#include <linux/delay.h>
//#include <linux/kernel.h>
#include <linux/module.h>
//#include <linux/of.h>
//#include <linux/platform_device.h>
//
//#include <video/mipi_display.h>
//
#include <linux/backlight.h>
//#include <linux/device.h>
//#include <linux/fb.h>
//
#include "core/lcdreg.h"
#include "core/fbdbi.h"


static int ssd1963_update(struct fbdbi_display *display, unsigned ys, unsigned ye)
{
	struct lcdreg *par = display->lcdreg;
	u16 xs = 0;
	u16 xe = display->info->var.xres - 1;
	int ret;

	pr_debug("%s(ys=%u, ye=%u): xres=%u, yres=%u\n", __func__, ys, ye, display->info->var.xres, display->info->var.yres);

	lcdreg_lock(display->lcdreg);
	ret = lcdreg_writereg(par, SSD1963_SET_COLUMN_ADDRESS,
		(xs >> 8) & 0xFF, xs & 0xFF, (xe >> 8) & 0xFF, xe & 0xFF);
	ret |= lcdreg_writereg(par, SSD1963_SET_PAGE_ADDRESS,
		(ys >> 8) & 0xFF, ys & 0xFF, (ye >> 8) & 0xFF, ye & 0xFF);
	ret |= fbdbi_display_update(display, SSD1963_WRITE_MEMORY_START, ys, ye);
	lcdreg_unlock(display->lcdreg);

	return ret;
}

static int ssd1963_rotate(struct fbdbi_display *display)
{
//	struct lcdreg *par = display->lcdreg;
//
	pr_info("%s(): rotate=%u)\n", __func__, display->info->var.rotate);
//	switch (display->info->var.rotate) {
//	}
//
	return 0;
}



static const struct fbdbi_display ssd1963 = {
	.xres = 480,
	.yres = 800,
	.update = ssd1963_update,
	.rotate = ssd1963_rotate,
};



int ssd1963_init(struct fbdbi_display *display, struct lcdreg *lcdreg)
{
	pr_info("%s()\n", __func__);

	fbdbi_merge_display(display, &ssd1963, lcdreg);
	lcdreg->def_width = 16;

	return 0;
}
EXPORT_SYMBOL(ssd1963_init);


#if defined(CONFIG_BACKLIGHT_CLASS_DEVICE)

struct ssd1963_backlight {
	struct lcdreg *lcdreg;
	struct fb_info *info;
	u8 pwmf;
};


static int ssd1963_bl_update_status(struct backlight_device *bl)
{
	struct ssd1963_backlight *ssd1963_bl = bl_get_data(bl);
	struct lcdreg *lcdreg = ssd1963_bl->lcdreg;
//	struct fbdbi_display *display = bl_get_data(bl);
	int brightness = bl->props.brightness;

	if (bl->props.power != FB_BLANK_UNBLANK ||
	    bl->props.fb_blank != FB_BLANK_UNBLANK ||
	    bl->props.state & BL_CORE_FBBLANK)
		brightness = 0;

dev_info(&bl->dev, "%s(power=%i, fb_blank=%i, state=0x%x): brightness = %i\n", __func__, bl->props.power, bl->props.fb_blank, bl->props.state, brightness);

	lcdreg_lock(lcdreg);

	/*
	 * Par1 PWM freq: 06h : PWM signal frequency = PLL clock / (256 * PWMF[7:0]) / 256
	 * Par2 PWM duty cycle: 0xf0:
	 * Par3 C3=0 => PWM controlled by host
	 *      C0=1 => PWM enable
	 * Par4 0xF0  DBC manual brightness
	 * Par5 0x00  DBC minimum brightness
	 * Par6 0x00  Brightness prescaler
	 */
//	write_reg(lcdreg, 0xBE, 0x06, 0xf0, 0x01, 0xf0, 0x00, 0x00);
//	write_reg(lcdreg, 0xBE, ssd1963_bl->pwmf, 0xf0, 0x01, 0xf0);

//	if (brightness)
//		write_reg(lcdreg, 0xBE, 0x06, 0xf0, 0x01, 0xf0, 0x00, 0x00);
//	else
//		write_reg(lcdreg, 0xBE, 0x06, 0x00, 0x01, 0xf0, 0x00, 0x00);

	lcdreg_writereg(lcdreg, 0xBE, 0x06, brightness, 0x01);

	lcdreg_unlock(lcdreg);

	return 0;
}

static int ssd1963_bl_check_fb(struct backlight_device *bl, struct fb_info *info)
{
	struct ssd1963_backlight *ssd1963_bl = bl_get_data(bl);

dev_info(&bl->dev, "%s(%p, %p): use_count=%i, ret=%u\n", __func__, ssd1963_bl->info, info, bl->use_count, (!ssd1963_bl->info || info == ssd1963_bl->info));
	return (!ssd1963_bl->info || info == ssd1963_bl->info);
}

static const struct backlight_ops ssd1963_bl_ops = {

// this one implements suspend/resume functionality on the backlight class, using update_status
// but maybe we have to control this tightly because of no lcdreg availability during suspend?
//	.options        = BL_CORE_SUSPENDRESUME,

	.update_status = ssd1963_bl_update_status,
	.check_fb = ssd1963_bl_check_fb,
};

/*
 * info can be NULL

FIXME: Console blanking: doesn't turn off backlight on first blank.

After new kernel build it didn't turn off bl at all.
pi@raspberrypi ~ $ dmesg | grep ssd1963_bl
[    8.901313] backlight ebay181283191283fb: ssd1963_bl_update_status(power=0, fb_blank=0, state=0x0): brightness = 255
[  134.105482] backlight ebay181283191283fb: ssd1963_bl_check_fb(da84b000, da84b000): use_count=0, ret=1
[  141.315902] backlight ebay181283191283fb: ssd1963_bl_check_fb(da84b000, da84b000): use_count=-1, ret=1
[  201.562801] backlight ebay181283191283fb: ssd1963_bl_check_fb(da84b000, da84b000): use_count=0, ret=1


 */
struct backlight_device *ssd1963_backlight_register(struct lcdreg *lcdreg, struct fb_info *info, int brightness)
{
	struct device *dev = lcdreg->dev;
	struct ssd1963_backlight *ssd1963_bl;
	struct backlight_device *bl;
	const struct backlight_properties props = {
		.brightness = brightness,
		.max_brightness = 255,
		.type = BACKLIGHT_RAW,
		.power = FB_BLANK_UNBLANK,
//		.state = ,
	};

	pr_info("%s()\n", __func__);

	ssd1963_bl = devm_kzalloc(dev, sizeof(*ssd1963_bl), GFP_KERNEL);
	if (!ssd1963_bl) {
		dev_err(dev, "Failed to allocate memory for SSD1963 backlight device\n");
		return NULL;
	}

	ssd1963_bl->lcdreg = lcdreg;
	ssd1963_bl->info = info;
	ssd1963_bl->pwmf = 0;

	bl = devm_backlight_device_register(dev, dev_driver_string(dev), dev, ssd1963_bl, &ssd1963_bl_ops, &props);
	if (IS_ERR(bl)) {
		dev_err(dev, "Failed to register SSD1963 backlight device (%ld)\n", PTR_ERR(bl));
		devm_kfree(dev, ssd1963_bl);
		return NULL;
	}

	return bl;
}
EXPORT_SYMBOL(ssd1963_backlight_register);

#endif /* CONFIG_BACKLIGHT_CLASS_DEVICE */


//MODULE_DESCRIPTION("Custom FB driver for tinylcd.com display");
MODULE_AUTHOR("Noralf Tronnes");
MODULE_LICENSE("GPL");
