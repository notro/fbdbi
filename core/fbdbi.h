
#ifndef __LINUX_FBDBI_H
#define __LINUX_FBDBI_H

#include <linux/backlight.h>
#include <linux/fb.h>
#include <linux/regulator/consumer.h>
#include <linux/spinlock.h>

#include "lcdreg.h"


enum fbdbi_format {
	FBDBI_FORMAT_NONE = 0,
	FBDBI_FORMAT_MONO10,
	FBDBI_FORMAT_RGB565,
	FBDBI_FORMAT_RGB888,
	FBDBI_FORMAT_XRGB8888,
};

/**

 * update -
 *          ys and ye are inclusive
 * rotate -
 * @set_color_mode -
 * @blank -
 * @poweron -
 * @poweroff -
 * @lcdreg -

 * @info -
 * @backlight -
 * @initialized -
 * @power_supply -
 */
struct fbdbi_display {
	u32 xres;
	u32 yres;
	enum fbdbi_format format;
bool bgr;

	int (*update)(struct fbdbi_display *display, unsigned ys, unsigned ye);
	int (*rotate)(struct fbdbi_display *display);
	int (*set_format)(struct fbdbi_display *display);
	int (*blank)(struct fbdbi_display *display, bool blank);
	int (*poweron)(struct fbdbi_display *display);
	int (*poweroff)(struct fbdbi_display *display);

	struct lcdreg *lcdreg;
void *controller_data;
	void *driver_private;
	struct fb_info *info;
	struct backlight_device *backlight;
	bool initialized;
	struct regulator *power_supply;
};

//enum fbdbi_sched {
//	FBTFT_AUTO,
//	FBTFT_ONESHOT,
//	FBTFT_DEFERRED,
//	FBTFT_FIXED,
//	FBTFT_CONTINUOUS,
//};

struct fbdbi {
	struct fbdbi_display *display;
	u32 pseudo_palette[16];

	spinlock_t dirty_lock;
	unsigned dirty_lines_start;
	unsigned dirty_lines_end;

//	enum fbdbi_sched sched;
};


extern void *devm_vzalloc(struct device *dev, unsigned long size);

extern struct fb_info *devm_framebuffer_alloc(size_t size, struct device *dev);
extern int devm_register_framebuffer(struct fb_info *info);

extern u32 fbdbi_of_value(struct device *dev, const char *propname, u32 def_value);
extern u32 fbdbi_of_format(struct device *dev, enum fbdbi_format def_format);

extern int devm_fbdbi_init(struct device *dev, struct fbdbi_display *display);
extern int devm_fbdbi_register(struct fbdbi_display *display);
extern int devm_fbdbi_register_dt(struct device *dev, struct fbdbi_display *display);

extern int fbdbi_display_update(struct fbdbi_display *display, unsigned regnr, unsigned ys, unsigned ye);
extern int fbdbi_display_poweroff(struct fbdbi_display *display);



static inline
void fbdbi_merge_display(struct fbdbi_display *display, const struct fbdbi_display *controller, struct lcdreg *lcdreg)
{
	if (!display->xres)
		display->xres = controller->xres;
	if (!display->yres)
		display->yres = controller->yres;
	if (!display->poweron)
		display->poweron = controller->poweron;
	if (!display->update)
		display->update = controller->update;
	if (!display->rotate)
		display->rotate = controller->rotate;
	display->lcdreg = lcdreg;
}


#endif /* __LINUX_FBDBI_H */
