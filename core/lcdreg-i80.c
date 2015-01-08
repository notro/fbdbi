//#define DEBUG

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "lcdreg.h"
#include "../i80/i80.h"


struct lcdreg_i80 {
	struct i80_device *i80;
	struct lcdreg reg;
	unsigned buswidth;
	void *regnr_buf;
	/* gpios */
struct gpio_desc *cs;
	struct gpio_desc *dc;
	struct gpio_desc *reset;
};

static inline struct lcdreg_i80 *to_lcdreg_i80(struct lcdreg *reg)
{
	return reg ? container_of(reg, struct lcdreg_i80, reg) : NULL;
}


/*

NO conversion yet, only 8->8 and 16->16.

*/


/* FIXME: emulation */
static int lcdreg_i80_write_regnr(struct lcdreg *reg, unsigned regnr)
{
	struct lcdreg_i80 *i80lcd = to_lcdreg_i80(reg);
	void *buf = i80lcd->regnr_buf;
	unsigned len = lcdreg_bytes_per_word(reg->def_width);
//	int ret = -ENOSYS;

//	if (reg->def_width == master->data_width) {

	switch (len) {
	case 4:
		*(u32 *)buf = regnr;
		break;
	case 2:
		*(u16 *)buf = regnr;
		break;
	default:
		*(u8 *)buf = regnr;
		break;
	}

	return i80_write(i80lcd->i80, 0, buf, len);
}

static int lcdreg_i80_write(struct lcdreg *reg, unsigned regnr, struct lcdreg_transfer *transfer)
{
	struct lcdreg_i80 *i80lcd = to_lcdreg_i80(reg);
	struct i80_device *i80 = i80lcd->i80;
	struct i80_master *master = i80->master;
	int ret;

	i80_open(i80);

	ret = lcdreg_i80_write_regnr(reg, regnr);
	if (ret || !transfer || !transfer->count)
		goto done;

/* FIXME: emulation */
	if (transfer->width == master->data_width)
		ret = i80_write(i80, transfer->index, transfer->buf, transfer->count * lcdreg_bytes_per_word(transfer->width));

done:
	i80_close(i80);

	return ret;
}


static int lcdreg_i80_read(struct lcdreg *reg, unsigned regnr, struct lcdreg_transfer *transfer)
{
	struct lcdreg_i80 *i80lcd = to_lcdreg_i80(reg);
	struct i80_device *i80 = i80lcd->i80;
	struct i80_master *master = i80->master;
	int ret;

	if (!reg->readable)
		return -EACCES;

	if (!transfer || !transfer->count)
		return -EINVAL;

	i80_open(i80);

	ret = lcdreg_i80_write_regnr(reg, regnr);
	if (ret)
		goto done;

/* FIXME: emulation */
	if (transfer->width == master->data_width)
		ret = i80_read(i80, transfer->index, transfer->buf, transfer->count * lcdreg_bytes_per_word(transfer->width));

done:
	i80_close(i80);

	return ret;
}

static void lcdreg_i80_reset(struct lcdreg *reg)
{
	struct lcdreg_i80 *i80lcd = to_lcdreg_i80(reg);

	if (!i80lcd->reset)
		return;

	dev_info(reg->dev, "%s()\n", __func__);
	gpiod_set_value_cansleep(i80lcd->reset, 0);
	msleep(20);
	gpiod_set_value_cansleep(i80lcd->reset, 1);
	msleep(120);
}

int devm_lcdreg_i80_parse_dt(struct device *dev, struct lcdreg_i80_config *config)
{
	config->reset = lcdreg_gpiod_get_index(dev, "reset", 0, 0);
	if (IS_ERR(config->reset))
		return PTR_ERR(config->reset);

	return 0;
}
EXPORT_SYMBOL(devm_lcdreg_i80_parse_dt);

struct lcdreg *devm_lcdreg_i80_init(struct i80_device *i80,
				    const struct lcdreg_i80_config *config)
{
	struct i80_master *master = i80->master;
	struct lcdreg_i80 *i80lcd;

	pr_info("%s()\n", __func__);

	if (!master->writable)
		return ERR_PTR(-EINVAL);

	i80lcd = devm_kzalloc(&i80->dev, sizeof(*i80lcd), GFP_KERNEL);
	if (!i80lcd)
		return ERR_PTR(-ENOMEM);

	i80lcd->regnr_buf = devm_kzalloc(&i80->dev, sizeof(u32), GFP_KERNEL);
	if (!i80lcd->regnr_buf)
		return ERR_PTR(-ENOMEM);

	i80lcd->i80 = i80;
	i80lcd->reg.readable = master->readable;
	i80lcd->buswidth = master->data_width;
	i80lcd->reset = config->reset;

	i80lcd->reg.write = lcdreg_i80_write;
	i80lcd->reg.read = lcdreg_i80_read;
	i80lcd->reg.reset = lcdreg_i80_reset;

	return devm_lcdreg_init(&i80->dev, &i80lcd->reg);
}
EXPORT_SYMBOL_GPL(devm_lcdreg_i80_init);

MODULE_LICENSE("GPL");
