//#define DEBUG
// included in fbdbi.h : backlight, fb, spinlock

#include <linux/device.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/vmalloc.h>

#include "fbdbi.h"

/*
devm_vzalloc is located here temporarily

TODO: 
ask on mm list to find out where to put this

Documentation/driver-model/devres.txt:
MEM
  devm_vzalloc()
*/
static void devm_vzalloc_release(struct device *dev, void *res)
{
pr_info("%s()\n", __func__);
	vfree(*(void **)res);
}
/**
 * devm_vzalloc - Resource-managed vzalloc
 * @dev:          Device to allocate memory for
 * @size:         allocation size
 * 
 * Managed vzalloc.  Memory allocated with this function is
 * automatically freed on driver detach.
 *
 * RETURNS:
 * Pointer to allocated memory on success, NULL on failure.
 */
void *devm_vzalloc(struct device *dev, unsigned long size)
{
	void **ptr;

pr_info("%s()\n", __func__);
	ptr = devres_alloc(devm_vzalloc_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return NULL;

	*ptr = vzalloc(size);
	if (!*ptr) {
		devres_free(ptr);
		return NULL;
	}

	devres_add(dev, ptr);

	return *ptr;
}
EXPORT_SYMBOL(devm_vzalloc);


/*

include/linux/fb.h:

extern struct fb_info *framebuffer_alloc(size_t size, struct device *dev);
extern struct fb_info *devm_framebuffer_alloc(size_t size, struct device *dev);

extern int register_framebuffer(struct fb_info *fb_info);
extern int devm_register_framebuffer(struct fb_info *info);

Documentation/driver-model/devres.txt:
FBDEV
  devm_framebuffer_alloc()
  devm_register_framebuffer()

*/
static void devm_framebuffer_release(struct device *dev, void *res)
{
pr_info("%s()\n", __func__);
	framebuffer_release(*(struct fb_info **)res);
}

/**
 * devm_framebuffer_alloc - Resource-managed framebuffer_alloc
 *
 * @size: size of driver private data, can be zero
 * @dev: pointer to the device for this fb
 *
 * Creates a new frame buffer info structure. Also reserves @size bytes
 * for driver private data (info->par). info->par (if any) will be
 * aligned to sizeof(long).
 *
 * Returns the new structure, or NULL if an error occurred.
 *
 */
struct fb_info *devm_framebuffer_alloc(size_t size, struct device *dev)
{
	struct fb_info **ptr;

pr_info("%s()\n", __func__);
	if (WARN_ON(!dev))
		return NULL;

	ptr = devres_alloc(devm_framebuffer_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return NULL;

	*ptr = framebuffer_alloc(size, dev);
	if (!*ptr) {
		devres_free(ptr);
		return NULL;
	}

	devres_add(dev, ptr);

	return *ptr;
}

static void devm_unregister_framebuffer(struct device *dev, void *res)
{
pr_info("%s()\n", __func__);
	unregister_framebuffer(*(struct fb_info **)res);
}

/**
 * devm_register_framebuffer - Resource-managed register_framebuffer
 * @fb_info: frame buffer info structure
 * 
 * Registers a frame buffer device @fb_info.
 * 
 * Returns negative errno on error, or zero for success.
 *
 */
int devm_register_framebuffer(struct fb_info *info)
{
	struct fb_info **ptr;
	int ret;

pr_info("%s()\n", __func__);
	ptr = devres_alloc(devm_unregister_framebuffer, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	ret = register_framebuffer(info);
	if (!ret) {
		*ptr = info;
		devres_add(info->device, ptr);
	} else {
		devres_free(ptr);
	}
	
	return ret;
}

/* ************************************************************************************************************** */

u32 fbdbi_of_value(struct device *dev, const char *propname, u32 def_value)
{
	u32 val = def_value;
	int ret;

	ret = of_property_read_u32(dev->of_node, propname, &val);
	if (ret && ret != -EINVAL)
		dev_err(dev, "error reading property '%s' (%i), using default %u\n", propname, ret, val);
	else
		dev_dbg(dev, "%s(%s) = %u\n", __func__, propname, val);

	return val;
}
EXPORT_SYMBOL(fbdbi_of_value);

u32 fbdbi_of_format(struct device *dev, enum fbdbi_format def_format)
{
	const char *fmt_str;
	int ret;

	ret = of_property_read_string(dev->of_node, "format", &fmt_str);
	if (ret)
		return def_format;

	dev_dbg(dev, "%s: format = %s\n", __func__, fmt_str);

	if (!strcmp(fmt_str, "mono01"))
		return FBDBI_FORMAT_MONO10;
	if (!strcmp(fmt_str, "rgb565"))
		return FBDBI_FORMAT_RGB565;
	if (!strcmp(fmt_str, "rgb888"))
		return FBDBI_FORMAT_RGB888;
	if (!strcmp(fmt_str, "xrgb8888"))
		return FBDBI_FORMAT_XRGB8888;

	dev_err(dev, "Invalid format: %s. Using default.\n", fmt_str);

	return def_format;
}
EXPORT_SYMBOL(fbdbi_of_format);

static void fbdbi_deferred_io(struct fb_info *info, struct list_head *pagelist)
{
	struct fbdbi *fbdbi = info->par;
	struct fbdbi_display *display = fbdbi->display;
	unsigned dirty_lines_start, dirty_lines_end;
	struct page *page;
	unsigned long index;
	unsigned y_low = 0, y_high = 0;
	int count = 0;

	spin_lock(&fbdbi->dirty_lock);
	dirty_lines_start = fbdbi->dirty_lines_start;
	dirty_lines_end = fbdbi->dirty_lines_end;
	/* set display line markers as clean */
	fbdbi->dirty_lines_start = display->info->var.yres - 1;
	fbdbi->dirty_lines_end = 0;
	spin_unlock(&fbdbi->dirty_lock);

	/* Mark display lines as dirty */
	list_for_each_entry(page, pagelist, lru) {
		count++;
		index = page->index << PAGE_SHIFT;
		y_low = index / info->fix.line_length;
		y_high = (index + PAGE_SIZE - 1) / info->fix.line_length;
		if (y_high > info->var.yres - 1)
			y_high = info->var.yres - 1;
		if (y_low < dirty_lines_start)
			dirty_lines_start = y_low;
		if (y_high > dirty_lines_end)
			dirty_lines_end = y_high;
	}

	display->update(display, dirty_lines_start, dirty_lines_end);
	// notify error?
}




static void fbdbi_mkdirty(struct fb_info *info, int y, int height)
{
	struct fbdbi *fbdbi = info->par;
	struct fb_deferred_io *fbdefio = info->fbdefio;

	/* Mark the specified display lines/area as dirty */
	spin_lock(&fbdbi->dirty_lock);
	if (y < fbdbi->dirty_lines_start)
		fbdbi->dirty_lines_start = y;
	if (y + height - 1 > fbdbi->dirty_lines_end)
		fbdbi->dirty_lines_end = y + height - 1;
	spin_unlock(&fbdbi->dirty_lock);

	/* Schedule deferred_io to update display (no-op if already on queue)*/
	schedule_delayed_work(&info->deferred_work, fbdefio->delay);
}

static void fbdbi_fb_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
	sys_fillrect(info, rect);
	fbdbi_mkdirty(info, rect->dy, rect->height);
}

static void fbdbi_fb_copyarea(struct fb_info *info, const struct fb_copyarea *area)
{
	sys_copyarea(info, area);
	fbdbi_mkdirty(info, area->dy, area->height);
}

static void fbdbi_fb_imageblit(struct fb_info *info, const struct fb_image *image)
{
	sys_imageblit(info, image);
	fbdbi_mkdirty(info, image->dy, image->height);
}

static ssize_t fbdbi_fb_write(struct fb_info *info,
			const char __user *buf, size_t count, loff_t *ppos)
{
	ssize_t res;

	res = fb_sys_write(info, buf, count, ppos);
	/* TODO: only mark changed area
	   update all for now */
	fbdbi_mkdirty(info, 0, info->var.yres);

	return res;
}

static unsigned int chan_to_field(unsigned chan, struct fb_bitfield *bf)
{
	chan &= 0xffff;
	chan >>= 16 - bf->length;
	return chan << bf->offset;
}

static int fbdbi_fb_setcolreg(unsigned regno,
			       unsigned red, unsigned green, unsigned blue,
			       unsigned transp, struct fb_info *info)
{
	unsigned val;
	int ret = 1;

	switch (info->fix.visual) {
	case FB_VISUAL_TRUECOLOR:
		if (regno < 16) {
			u32 *pal = info->pseudo_palette;

			val  = chan_to_field(red,   &info->var.red);
			val |= chan_to_field(green, &info->var.green);
			val |= chan_to_field(blue,  &info->var.blue);

			pal[regno] = val;
			ret = 0;
		}
		break;

	}
	return ret;
}

static void dump_fb_var_screeninfo(struct fb_var_screeninfo *var, const char *name)
{
#define pr_var(_v)	pr_info("  " #_v " = %u\n", var->_v)
#define pr_color(_v)	pr_info("  " #_v ": %u,%u\n", var->_v.offset, var->_v.length)

	pr_info("%s('%s'):\n", __func__, name);

	pr_var(xres);
	pr_var(yres);
	pr_var(xres_virtual);
	pr_var(yres_virtual);
	pr_var(xoffset);
	pr_var(yoffset);
	pr_var(bits_per_pixel);
	pr_var(grayscale);
	pr_color(red);
	pr_color(green);
	pr_color(blue);
	pr_color(transp);
	pr_var(nonstd);
	pr_var(activate);
	pr_var(height);
	pr_var(width);
	pr_var(accel_flags);
	pr_var(pixclock);
	pr_var(left_margin);
	pr_var(right_margin);
	pr_var(upper_margin);
	pr_var(lower_margin);
	pr_var(hsync_len);
	pr_var(vsync_len);
	pr_var(sync);
	pr_var(vmode);
	pr_var(rotate);
	pr_var(colorspace);
	pr_info("\n");
#undef pr_var
}

/* Only rotation is supported, using rotate or xres/yres swapping. */
static int fbdbi_fb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct fbdbi *fbdbi = info->par;
	struct fbdbi_display *display = fbdbi->display;
	u32 rotate = var->rotate;

pr_info("%s()\n", __func__);
//dump_fb_var_screeninfo(var, "var");
//dump_fb_var_screeninfo(&info->var, "info->var");
	if (!display->rotate)
		return -ENOSYS;

	if (var->xres != info->var.xres || var->yres != info->var.yres) {
		if (var->xres == display->xres && var->yres == display->yres)
			rotate = 0;
		else if (var->xres == display->yres && var->yres == display->xres)
			rotate = 90;
		else
			return -EINVAL;
	}

	*var = info->var;
	var->rotate = rotate;

	switch (var->rotate) {
	case 0:
	case 180:
		var->xres = display->xres;
		var->yres = display->yres;
		break;
	case 90:
	case 270:
		var->xres = display->yres;
		var->yres = display->xres;
		break;
	default:
		return -EINVAL;
	}
	var->xres_virtual = var->xres;
	var->yres_virtual = var->yres;

	return 0;
}

static int fbdbi_fb_set_par(struct fb_info *info)
{
	struct fbdbi *fbdbi = info->par;
	struct fbdbi_display *display = fbdbi->display;
	int ret;

pr_info("%s()\n", __func__);
//dump_fb_var_screeninfo(&info->var, __func__);
	if (!display->rotate)
		return -ENOSYS;

	lcdreg_lock(display->lcdreg);
	switch (info->var.bits_per_pixel) {
	case 1:
		info->fix.line_length = info->var.xres / 8;
		break;
	case 8:
		info->fix.line_length = info->var.xres;
		break;
	case 16:
		info->fix.line_length = info->var.xres * 2;
		break;
	case 24:
		info->fix.line_length = info->var.xres * 3;
		break;
	case 32:
		info->fix.line_length = info->var.xres * 4;
		break;
	}
	ret = display->rotate(display);
	lcdreg_unlock(display->lcdreg);

	return ret;
}

/*
 * Why is fb_ops.fb_blank needed when these panels mostly support
 * white blanking? (backlight shining through)
 *
 * fb_blank() doesn't fire the FB_EVENT_BLANK event unless
 * fb_ops.fb_blank is set and returns 0.
 * Whitout this event, backlight is not turned off on blanking.
 *
 * If however the backlight is always on (hardwired ON), fb_ops.fb_blank
 * must return -EINVAL or else fbcon won't blank/clear the console.
 */
static int fbdbi_fb_blank(int blank, struct fb_info *info)
{
	struct fbdbi *fbdbi = info->par;
	struct fbdbi_display *display = fbdbi->display;

dev_info(info->dev, "%s(blank=%d)\n", __func__, blank);

	if (display->blank)
		return display->blank(display, blank ? true : false);
	else if (display->backlight)
		return 0; /* fire FB_EVENT_BLANK event to turn off backlight */

	/* let the caller handle blanking */
	return -EINVAL;
}

/*
    unregister_framebuffer() calls put_fb_info(fb_info)
    fb_destroy is called i ref is zero
 */
static void fbdbi_fb_destroy(struct fb_info *info)
{
	struct fbdbi *fbdbi = info->par;
	struct fbdbi_display *display = fbdbi->display;

pr_info("%s()\n", __func__);
	fb_deferred_io_cleanup(info);
	if (display->backlight) {
		display->backlight->props.brightness = 0;
		backlight_update_status(display->backlight);
	}
	if (display->poweroff)
		display->poweroff(display);
}

static const struct fb_ops fbdbi_fb_ops = {
//	.owner =          THIS_MODULE,
//	.fb_open =        fbdbi_fb_open,
//	.fb_release =     fbdbi_fb_release,
	.fb_read =        fb_sys_read,
	.fb_write =       fbdbi_fb_write,
	.fb_check_var =   fbdbi_fb_check_var,
	.fb_set_par =     fbdbi_fb_set_par,
	.fb_setcolreg =   fbdbi_fb_setcolreg,
	.fb_blank =       fbdbi_fb_blank,
//	.fb_pan_display = fbdbi_fb_pan_display, 
	.fb_fillrect =    fbdbi_fb_fillrect,
	.fb_copyarea =    fbdbi_fb_copyarea,
	.fb_imageblit =   fbdbi_fb_imageblit,
//	.fb_cursor =      fbdbi_fb_cursor,
//	.fb_rotate =      fbdbi_fb_rotate,
//	.fb_sync =        fbdbi_fb_sync,
	.fb_destroy =     fbdbi_fb_destroy,
};



int devm_fbdbi_init(struct device *dev, struct fbdbi_display *display)
{
	struct fb_info *info;
	struct fbdbi *fbdbi;
	u8 *vmem;
	int vmem_size = display->xres * display->yres;

pr_info("%s(xres=%u, yres=%u)\n", __func__, display->xres, display->yres);
	if (!vmem_size)
		return -EINVAL;

	info = devm_framebuffer_alloc(sizeof(*fbdbi), dev);
	if (!info)
		return -ENOMEM;

	fbdbi = info->par;
	fbdbi->display = display;
	display->info = info;

	info->fbops = devm_kmalloc(dev, sizeof(*info->fbops), GFP_KERNEL);
	if (!info->fbops)
		return -ENOMEM;

	*info->fbops = fbdbi_fb_ops;
	info->fbops->owner = dev->driver->owner;
	info->flags = FBINFO_DEFAULT | FBINFO_VIRTFB;
	info->pseudo_palette = fbdbi->pseudo_palette;
	strncpy(info->fix.id, dev->driver->name, 16);
	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.visual = FB_VISUAL_TRUECOLOR;
	info->fix.accel = FB_ACCEL_NONE;
	info->var.xres = display->xres;
	info->var.yres = display->yres;
	info->var.xres_virtual = info->var.xres;
	info->var.yres_virtual = info->var.yres;

	switch (display->format) {
	case FBDBI_FORMAT_MONO10:
		info->var.bits_per_pixel = 1;
		vmem_size /= 8;
		info->fix.visual = FB_VISUAL_MONO10;
		info->var.red.length = 1;
		info->var.red.offset = 0;
		info->var.green.length = 1;
		info->var.green.offset = 0;
		info->var.blue.length = 1;
		info->var.blue.offset = 0;
		break;
	case FBDBI_FORMAT_RGB565:
		info->var.bits_per_pixel = 16;
		vmem_size *= 2;
		info->var.red.offset = 11;
		info->var.red.length = 5;
		info->var.green.offset = 5;
		info->var.green.length = 6;
		info->var.blue.offset = 0;
		info->var.blue.length = 5;
		break;
	case FBDBI_FORMAT_RGB888:
		info->var.bits_per_pixel = 24;
		vmem_size *= 3;
		info->var.red.offset = 16;
		info->var.red.length = 8;
		info->var.green.offset = 8;
		info->var.green.length = 8;
		info->var.blue.offset = 0;
		info->var.blue.length = 8;
		break;
	case FBDBI_FORMAT_XRGB8888:
		info->var.bits_per_pixel = 32;
		vmem_size *= 4;
		info->var.red.offset = 16;
		info->var.red.length = 8;
		info->var.green.offset = 8;
		info->var.green.length = 8;
		info->var.blue.offset = 0;
		info->var.blue.length = 8;
		break;
	default:
		return -EINVAL;
	}
	vmem = devm_vzalloc(dev, vmem_size);
	if (!vmem)
		return -ENOMEM;

	info->screen_base = (u8 __force __iomem *)vmem;
	info->fix.smem_len = vmem_size;
// also set in set_par
	info->fix.line_length = vmem_size / display->yres;

	info->fbdefio = devm_kzalloc(dev, sizeof(*info->fbdefio), GFP_KERNEL);
	if (!info->fbdefio)
		return -ENOMEM;

	info->fbdefio->delay = HZ / 20;
	info->fbdefio->deferred_io = fbdbi_deferred_io;

	return 0;
}
EXPORT_SYMBOL(devm_fbdbi_init);

int devm_fbdbi_register(struct fbdbi_display *display)
{
	int ret;

pr_info("%s()\n", __func__);

	fb_deferred_io_init(display->info);

	if (!display->initialized) {
		if (display->poweron) {
			ret = display->poweron(display);
			if (ret)
				return ret;
		}

		if (display->set_format) {
			ret = display->set_format(display);
			if (ret)
				return ret;
		}

		if (display->rotate) {
			ret = fbdbi_fb_check_var(&display->info->var,
							display->info);
			if (ret)
				return ret;

			ret = fbdbi_fb_set_par(display->info);
			if (ret)
				return ret;
		}
		ret = display->update(display, 0, display->info->var.yres - 1);
		if (ret)
			return ret;
	}

	ret = devm_register_framebuffer(display->info);
	if (ret)
		return ret;

	if (1) {
		struct fb_videomode mode = {
			.xres = display->info->var.yres,
			.yres = display->info->var.xres,
		};

		ret = fb_add_videomode(&mode, &display->info->modelist);
		if (ret)
			return ret;


	}

	if (display->backlight) {
		/*
		 * This is needed for the backlight to turn off also on the
		 * very first console blanking event
		 */
		display->backlight->fb_bl_on[display->info->node] = true;
		if (!display->backlight->use_count)
			display->backlight->use_count++;

		if (display->backlight->props.brightness == 0)
			display->backlight->props.brightness = display->backlight->props.max_brightness;
		backlight_update_status(display->backlight);
	}

	dev_info(display->info->dev,
		"%s frame buffer, %dx%d, %d KiB video memory, fps=%lu\n",
		display->info->fix.id, display->info->var.xres, display->info->var.yres,
		display->info->fix.smem_len >> 10,
		HZ/display->info->fbdefio->delay);

	return 0;
}
EXPORT_SYMBOL(devm_fbdbi_register);

int devm_fbdbi_register_dt(struct device *dev, struct fbdbi_display *display)
{
	struct device_node *backlight;
	int ret;

	ret = devm_fbdbi_init(dev, display);
	if (ret)
		return ret;

	display->info->var.rotate = fbdbi_of_value(dev, "rotate", 0);
	display->initialized = of_property_read_bool(dev->of_node,
						     "initialized");

	display->power_supply = devm_regulator_get(dev, "power");
	if (IS_ERR(display->power_supply))
		 return PTR_ERR(display->power_supply);

	backlight = of_parse_phandle(dev->of_node, "backlight", 0);
	if (backlight) {
		display->backlight = of_find_backlight_by_node(backlight);
		of_node_put(backlight);
	
		if (!display->backlight)
			return -EPROBE_DEFER;
	}

	return devm_fbdbi_register(display);
}
EXPORT_SYMBOL(devm_fbdbi_register_dt);

int fbdbi_display_update(struct fbdbi_display *display, unsigned regnr, unsigned ys, unsigned ye)
{
	struct lcdreg_transfer tr = {
		.index = 1,
	};
	unsigned height = ye - ys + 1;
	unsigned offset = ys * display->info->fix.line_length;

	pr_debug("height=%u, offset=%u, line_length=%i\n", height, offset, display->info->fix.line_length);

	tr.buf = display->info->screen_base + offset;
	switch (display->format) {
	case FBDBI_FORMAT_MONO10:
		tr.width = 8;
		tr.count = height * display->info->fix.line_length / 8;
		break;
	case FBDBI_FORMAT_RGB565:
		tr.width = 16;
		tr.count = height * display->info->fix.line_length / 2;
		break;
	case FBDBI_FORMAT_RGB888:
		tr.width = 8;
		tr.count = height * display->info->fix.line_length;
		break;
	case FBDBI_FORMAT_XRGB8888:
		tr.width = 24;
		tr.count = height * display->info->fix.line_length / 4;
		break;
	default:
		return -EINVAL;
	}

	return lcdreg_write(display->lcdreg, regnr, &tr);
}
EXPORT_SYMBOL(fbdbi_display_update);

int fbdbi_display_poweroff(struct fbdbi_display *display)
{
	if (display->power_supply)
		regulator_disable(display->power_supply);

	return 0;
}
EXPORT_SYMBOL(fbdbi_display_poweroff);


//static int fbdbi_module_init(void)
//{
//	pr_info("%s\n", __func__);
////debugfs stuff
//	return 0;
//}
//module_init(fbdbi_module_init);
//
//static void fbdbi_module_exit(void)
//{
//	pr_info("%s\n", __func__);
////debugfs stuff
//}
//module_exit(fbdbi_module_exit);


MODULE_LICENSE("GPL");
