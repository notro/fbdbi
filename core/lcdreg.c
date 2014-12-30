#include <linux/device.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/gfp.h>
#include <linux/module.h>

#include <linux/debugfs.h>
#include <linux/uaccess.h>

#include "lcdreg.h"




static struct dentry *lcdreg_debugfs_root;




/**
 * gpiod_get_index_optional - obtain an optional GPIO from a multi-index GPIO
 *                            function
 * @dev: GPIO consumer, can be NULL for system-global GPIOs
 * @con_id: function within the GPIO consumer
 * @index: index of the GPIO to obtain in the consumer
 * @flags: optional GPIO initialization flags
 *
 * This is equivalent to gpiod_get_index(), except that when no GPIO with the
 * specified index was assigned to the requested function it will return NULL.
 * This is convenient for drivers that need to handle optional GPIOs.
 */
// http://lxr.free-electrons.com/ident?i=devm_gpiod_get_optional


// use flags to set direction and value
struct gpio_desc *lcdreg_gpiod_get_index(struct device *dev, const char *name, unsigned int idx, int def_val)
{
	struct gpio_desc *desc;
	int ret;

//	desc = devm_gpiod_get(dev, name);
	desc = devm_gpiod_get_index(dev, name, idx, 0);
	if (IS_ERR(desc) && PTR_ERR(desc) == -ENOENT)
		return NULL;
	if (IS_ERR(desc)) {
		dev_err(dev, "failed to get gpio '%s' (%li)\n", name, PTR_ERR(desc));
		return desc;
	}
	ret = gpiod_direction_output(desc, def_val);
	if (ret)
		return ERR_PTR(ret);

/*
 * FIXME: fix drivers/pinctrl/pinctrl-bcm2835.c
 *
 * gpiod_direction_output should set the output value, but bcm2835_gpio_direction_output doesn't honour this.
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

dev_info(dev, "%s(%s, %u) = %i, value = %i\n", __func__, name, idx, desc_to_gpio(desc), gpiod_get_value(desc));

	return desc;
}
EXPORT_SYMBOL(lcdreg_gpiod_get_index);



static int debugfs_unsigned_set(void *data, u64 val)
{
	*(u32 *)data = val;
	return 0;
}
static int debugfs_unsigned_get(void *data, u64 *val)
{
	*val = *(u32 *)data;
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(fops_unsigned, debugfs_unsigned_get, debugfs_unsigned_set, "%llu\n");
DEFINE_SIMPLE_ATTRIBUTE(fops_unsigned_ro, debugfs_unsigned_get, NULL, "%llu\n");

struct dentry *debugfs_create_unsigned(const char *name, umode_t mode,
                                 struct dentry *parent, unsigned *value)
{
	/* if there are no write bits set, make read only */
	if (!(mode & S_IWUGO))
		return debugfs_create_file(name, mode, parent, value, &fops_unsigned_ro);

	return debugfs_create_file(name, mode, parent, value, &fops_unsigned);
}

static int lcdreg_userbuf_to_u32(const char __user *user_buf, size_t count, u32 *dest, size_t dest_size)
{
	char *buf, *start;
	unsigned long value;
	int ret = 0;
	int i;

//printk("%s(count=%zu)\n", __func__, count);
	buf = kmalloc(count, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (copy_from_user(buf, user_buf, count)) {
		kfree(buf);
		return -EFAULT;
	}
	buf[count] = 0;
	for (i = 0; i < count; i++)
		if (buf[i] == ' ' || buf[i] == '\n')
			buf[i] = 0;
	i = 0;
	start = buf;
	while (start < buf + count) {
		if (*start == 0) {
			start++;
			continue;
		}
		if (i == dest_size) {
			ret = -EFBIG;
			break;
		}
		if (kstrtoul(start, 16, &value)) {
			ret = -EINVAL;
			break;
		}
		while (*start != 0)
			start++;
		dest[i++] = value;
	};
	kfree(buf);
	if (ret)
		return ret;

	return i ? i : -EINVAL;
}

static ssize_t lcdreg_write_write_file(struct file *file,
					const char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct lcdreg *reg = file->private_data;
	int ret;
	u32 txbuf[128];

	ret = lcdreg_userbuf_to_u32(user_buf, count, txbuf, ARRAY_SIZE(txbuf));
	if (ret <= 0)
		return -EINVAL;

	/* Userspace has been fiddling around behind the kernel's back */
	add_taint(TAINT_USER, LOCKDEP_STILL_OK);

	lcdreg_lock(reg);
	ret = lcdreg_write_buf32(reg, txbuf[0], txbuf + 1, ret - 1);
	lcdreg_unlock(reg);

	return ret ? ret : count;
}

static const struct file_operations lcdreg_write_fops = {
	.open = simple_open,
	.write = lcdreg_write_write_file,
	.llseek = default_llseek,
};



/*

[95419.116211] lcdreg_read_read_file(count=32768, ppos=0)
[95419.116262] fb_ili9320 spi0.0: lcdreg_spi_read(in): index=1, count=1, width=16
[95419.116282] fb_ili9320 spi0.0: lcdreg_spi_read_raw(in): index=1, count=3, width=8
[95419.116462] fb_ili9320 spi0.0: spi_message: dma=0, startbyte=0x73,
[95419.116490]     tr: bpw=8, len=1, tx_buf(db3631e0)=[73]
[95419.116506]     tr: bpw=8, len=3, rx_buf(db363420)=[93 93 20]
[95419.116522] fb_ili9320 spi0.0: lcdreg_spi_read_raw(out):
[95419.116537]     buf=93 93 20
[95419.116552] fb_ili9320 spi0.0: lcdreg_spi_read(out):
[95419.116564]     buf=9320
[95419.116600] ret=5

[95419.116700] lcdreg_read_read_file(count=32768, ppos=5)
[95419.116730] fb_ili9320 spi0.0: lcdreg_spi_read(in): index=1, count=1, width=16
[95419.116749] fb_ili9320 spi0.0: lcdreg_spi_read_raw(in): index=1, count=3, width=8
[95419.116872] fb_ili9320 spi0.0: spi_message: dma=0, startbyte=0x73,
[95419.116894]     tr: bpw=8, len=1, tx_buf(db3631e0)=[73]
[95419.116910]     tr: bpw=8, len=3, rx_buf(db363260)=[93 93 20]
[95419.116925] fb_ili9320 spi0.0: lcdreg_spi_read_raw(out):
[95419.116936]     buf=93 93 20
[95419.116950] fb_ili9320 spi0.0: lcdreg_spi_read(out):
[95419.116963]     buf=9320
[95419.116977] ret=0


Do this in open instead, like debugfs_create_u32_array()
http://lxr.free-electrons.com/ident?i=debugfs_create_u32_array

*/

static ssize_t lcdreg_read_write_file(struct file *file,
					const char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct lcdreg *reg = file->private_data;
	struct lcdreg_transfer tr = {
		.index = (reg->quirks & LCDREG_INDEX0_ON_READ) ? 0 : 1,
		.count = 1,
	};
	u32 txbuf[1];
	char *buf = NULL;
	int ret;


//	printk("%s(count=%zu, ppos=%llu)\n", __func__, count, *ppos);
	if (!reg->debugfs_read_width)
		reg->debugfs_read_width = reg->def_width;
	tr.width = reg->debugfs_read_width;

	ret = lcdreg_userbuf_to_u32(user_buf, count, txbuf, ARRAY_SIZE(txbuf));
//	if (ret <= 0)
	if (ret != 1)
		return -EINVAL;

	tr.buf = kmalloc(lcdreg_bytes_per_word(tr.width), GFP_KERNEL);
	if (!tr.buf)
		return -ENOMEM;

	/* Userspace has been fiddling around behind the kernel's back */
	add_taint(TAINT_USER, LOCKDEP_STILL_OK);

	lcdreg_lock(reg);
	ret = lcdreg_read(reg, txbuf[0], &tr);
	lcdreg_unlock(reg);
	if (ret) {
		goto error_out;
	}

	if (!reg->debugfs_read_result) {
		reg->debugfs_read_result = kmalloc(16, GFP_KERNEL);
		if (!reg->debugfs_read_result) {
			ret = -ENOMEM;
			goto error_out;
		}
	}
	buf = reg->debugfs_read_result;

	switch (tr.width) {
	case 8:
		snprintf(buf, 16, "%02x\n", *(u8 *)tr.buf);
		break;
	case 16:
		snprintf(buf, 16, "%04x\n", *(u16 *)tr.buf);
		break;
	case 24:
	case 32:
		snprintf(buf, 16, "%08x\n", *(u32 *)tr.buf);
		break;
	default:
		ret = -EINVAL;
		goto error_out;
	}

error_out:
	kfree(tr.buf);
	if (ret) {
		kfree(reg->debugfs_read_result);
		reg->debugfs_read_result = NULL;
		return ret;
	}

	return count;
}

static ssize_t lcdreg_read_read_file(struct file *file,
				     char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	struct lcdreg *reg = file->private_data;

//	printk("%s(count=%zu, ppos=%llu)\n", __func__, count, *ppos);
	if (*ppos < 0 || !count)
		return -EINVAL;

	if (!reg->debugfs_read_result)
		return -ENODATA;

	return simple_read_from_buffer(user_buf, count, ppos, reg->debugfs_read_result, strlen(reg->debugfs_read_result));
}

static const struct file_operations lcdreg_read_fops = {
	.open = simple_open,
	.read = lcdreg_read_read_file,
	.write = lcdreg_read_write_file,
	.llseek = default_llseek,
};






void lcdreg_debugfs_init(struct lcdreg *reg)
{
	if (!lcdreg_debugfs_root)
		return;

	reg->debugfs = debugfs_create_dir(dev_name(reg->dev), lcdreg_debugfs_root);
	if (!reg->debugfs) {
		dev_warn(reg->dev, "Failed to create debugfs directory\n");
		return;
	}

/* FIXME: why not u32 and debugfs_create_u32 ? */
	debugfs_create_unsigned("def_width", 0440, reg->debugfs, &reg->def_width);

//	debugfs_create_u32("write_width", 0660, reg->debugfs, &reg->debugfs_write_width);
	debugfs_create_file("write", 0220, reg->debugfs, reg, &lcdreg_write_fops);

	debugfs_create_u32("read_width", 0660, reg->debugfs, &reg->debugfs_read_width);
	debugfs_create_file("read", 0660, reg->debugfs, reg, &lcdreg_read_fops);
}

void lcdreg_debugfs_exit(struct lcdreg *reg)
{
	debugfs_remove_recursive(reg->debugfs);
}

/**
 * @reg - lcdreg
 * @regnr - Rgister number
 * @data - Optional data buffer, maybe NULL
 * @count - Number of data values
 */
int lcdreg_write_buf32(struct lcdreg *reg, unsigned regnr, const u32 *data, unsigned count)
{
	struct lcdreg_transfer tr = {
		.index = 1,
		.width = reg->def_width,
		.count = count,
	};
	int i, ret;

	tr.buf = kmalloc(count * sizeof(*data), GFP_KERNEL);
	if (!tr.buf)
		return -ENOMEM;

	if (reg->def_width <= 8)
		for (i = 0; i < tr.count; i++)
			((u8 *)tr.buf)[i] = data[i];
	else
		for (i = 0; i < tr.count; i++)
			((u16 *)tr.buf)[i] = data[i];
	ret = lcdreg_write(reg, regnr, &tr);
	kfree(tr.buf);

	return ret;
}
EXPORT_SYMBOL(lcdreg_write_buf32);

int lcdreg_readreg_buf32(struct lcdreg *reg, unsigned regnr, u32 *buf,
							unsigned count)
{
	struct lcdreg_transfer tr = {
		.index = (reg->quirks & LCDREG_INDEX0_ON_READ) ? 0 : 1,
		.count = count,
	};
	int i, ret;

	if (!buf || !count)
		return -EINVAL;

	tr.buf = kmalloc(count * sizeof(*buf), GFP_KERNEL);
	if (!tr.buf)
		return -ENOMEM;

	ret = lcdreg_read(reg, regnr, &tr);
	if (ret) {
		kfree(tr.buf);
		return ret;
	}

	if (reg->def_width <= 8)
		for (i = 0; i < count; i++)
			buf[i] = ((u8 *)tr.buf)[i];
	else
		for (i = 0; i < count; i++)
			buf[i] = ((u16 *)tr.buf)[i];
	kfree(tr.buf);

	return ret;
}
EXPORT_SYMBOL(lcdreg_readreg_buf32);

static void devm_lcdreg_release(struct device *dev, void *res)
{
	struct lcdreg *reg = *(struct lcdreg **)res;

	lcdreg_debugfs_exit(reg);
	mutex_destroy(&reg->lock);
//	if (lcdreg->exit)
//		lcdreg->exit(reg);

}

struct lcdreg *devm_lcdreg_init(struct device *dev, struct lcdreg *reg)
{

	struct lcdreg **ptr;

	if (!dev || !reg)
		return ERR_PTR(-EINVAL);

	ptr = devres_alloc(devm_lcdreg_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	*ptr = reg;
	devres_add(dev, ptr);
	reg->dev = dev;
	mutex_init(&reg->lock);
//	reg->def_width = config->def_width;
	lcdreg_debugfs_init(reg);

	return reg;
}
EXPORT_SYMBOL_GPL(devm_lcdreg_init);














static int lcdreg_module_init(void)
{
	lcdreg_debugfs_root = debugfs_create_dir("lcdreg", NULL);
	if (!lcdreg_debugfs_root)
		pr_warn("lcdreg: Failed to create debugfs root\n");
	return 0;
}
module_init(lcdreg_module_init);

static void lcdreg_module_exit(void)
{
	debugfs_remove_recursive(lcdreg_debugfs_root);
}
module_exit(lcdreg_module_exit);


MODULE_LICENSE("GPL");
