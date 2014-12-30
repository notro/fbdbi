#define DEBUG

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "lcdreg.h"


struct lcdreg_par {
	struct lcdreg reg;
	unsigned buswidth;
	struct lcdreg_par_io_lookup *lookup;
	unsigned prev_data;
	/* gpios */
	struct gpio_desc *cs;
	struct gpio_desc *wr;
	struct gpio_desc *rd;
	struct gpio_desc *db[24];
	struct gpio_desc *dc;
	struct gpio_desc *reset;
};

static inline struct lcdreg_par *to_lcdreg_par(struct lcdreg *reg)
{
	return reg ? container_of(reg, struct lcdreg_par, reg) : NULL;
}


static int lcdreg_par_transfer(struct lcdreg *reg, struct lcdreg_transfer *transfer)
{
	struct lcdreg_par *par = to_lcdreg_par(reg);
	u16 *data = transfer->buf;
	u16 tmp;
	unsigned len = transfer->count;
	int i;

	lcdreg_dbg_write(reg->dev, __func__, transfer);
	if (par->cs)
		gpiod_set_value(par->cs, 0);

	gpiod_set_value(par->dc, transfer->index);

	while (len--) {
		gpiod_set_value(par->wr, 0);

		tmp = *data;
		if (par->prev_data != tmp)
			for (i = 0; i < 16; i++) {
				gpiod_set_value(par->db[i], (tmp & 1));
				tmp >>= 1;
			}
		par->prev_data = *data++;

		gpiod_set_value(par->wr, 1);
	}

	if (par->cs)
		gpiod_set_value(par->cs, 1);

	return 0;
}

static int lcdreg_par_write(struct lcdreg *reg, unsigned regnr, struct lcdreg_transfer *transfer)
{
	struct lcdreg_transfer tr = {
		.width = reg->def_width,
		.count = 1,
	};
	int ret;

	tr.buf = kmalloc(sizeof(u32), GFP_KERNEL);
	if (!tr.buf)
		return -ENOMEM;

	if (reg->def_width <= 8)
		((u8 *)tr.buf)[0] = regnr;
	else
		((u16 *)tr.buf)[0] = regnr;

	ret = lcdreg_par_transfer(reg, &tr);
	kfree(tr.buf);
	if (ret || !transfer || !transfer->count)
		return ret;

	if (!transfer->width)
		transfer->width = reg->def_width;

	return lcdreg_par_transfer(reg, transfer);
}

static int lcdreg_par_direction(struct lcdreg *reg, bool input)
{
	struct lcdreg_par *par = to_lcdreg_par(reg);
	int i, ret;

printk("%s(%u): par->buswidth = %u\n", __func__, input, par->buswidth);
	for (i = 0; i < par->buswidth; i++) {
		if (input)
			ret = gpiod_direction_input(par->db[i]);
		else
			ret = gpiod_direction_output(par->db[i], 0);

//printk("%i as %s (ret=%i)\n", desc_to_gpio(par->db[i]), dire, ret);
		if (ret)
			return ret;
	}
	par->prev_data = ~0;

	return 0;
}

static int lcdreg_par_read(struct lcdreg *reg, unsigned regnr, struct lcdreg_transfer *transfer)
{
	struct lcdreg_par *par = to_lcdreg_par(reg);
	u16 *data = transfer->buf;
	unsigned len = transfer->count;
	int i, ret;

	lcdreg_dbg_read_in(reg->dev, __func__, transfer);
	if (!reg->readable)
		return -EACCES;

//	if (transfer->count != 1 || transfer->width > 32)
//		return -EINVAL;

	ret = lcdreg_writereg(reg, regnr);
	if (ret)
		return (ret);

	gpiod_set_value_cansleep(par->dc, transfer->index);

	ret = lcdreg_par_direction(reg, true);
	if (ret)
		return ret;

	if (par->cs)
		gpiod_set_value(par->cs, 0);

	while (len--) {
		gpiod_set_value(par->rd, 0);
		*data = 0;
		for (i = 0; i < 16; i++) {
			*data |= gpiod_get_value(par->db[i]) << i;
		}
		data++;
		gpiod_set_value(par->rd, 1);
	}

	if (par->cs)
		gpiod_set_value(par->cs, 1);

	ret = lcdreg_par_direction(reg, false);
	lcdreg_dbg_read_out(reg->dev, __func__, transfer);

	return ret;
}

static void lcdreg_par_reset(struct lcdreg *reg)
{
	struct lcdreg_par *par = to_lcdreg_par(reg);

	if (!par->reset)
		return;

	dev_info(reg->dev, "%s()\n", __func__);
	gpiod_set_value_cansleep(par->reset, 0);
	msleep(20);
	gpiod_set_value_cansleep(par->reset, 1);
	msleep(120);
}

int devm_lcdreg_par_parse_dt(struct device *dev, struct lcdreg_par_config *config)
{
	int i;

	config->reset = lcdreg_gpiod_get(dev, "reset", 0);
	if (IS_ERR(config->reset))
		return PTR_ERR(config->reset);

	config->cs = lcdreg_gpiod_get(dev, "cs", 1);
	if (IS_ERR(config->cs))
		return PTR_ERR(config->cs);

	config->wr = lcdreg_gpiod_get(dev, "wr", 1);
	if (IS_ERR(config->wr))
		return PTR_ERR(config->wr);

	config->rd = lcdreg_gpiod_get(dev, "rd", 1);
	if (IS_ERR(config->rd))
		return PTR_ERR(config->rd);

	config->dc = lcdreg_gpiod_get(dev, "dc", 0);
	if (IS_ERR(config->dc))
		return PTR_ERR(config->dc);

	for (i = 0; i < 24; i++) {
		config->db[i] = lcdreg_gpiod_get_index(dev, "db", i, 0);
		if (!config->db[i])
			break;
		if (IS_ERR(config->db[i]))
			return PTR_ERR(config->db[i]);
	}

	return 0;
}
EXPORT_SYMBOL(devm_lcdreg_par_parse_dt);

struct lcdreg *devm_lcdreg_par_init(struct platform_device *pdev,
				    const struct lcdreg_par_config *config)
{
	struct lcdreg_par *par;
	int i;

	pr_info("%s()\n", __func__);
	par = devm_kzalloc(&pdev->dev, sizeof(*par), GFP_KERNEL);
	if (!par)
		return ERR_PTR(-ENOMEM);

	par->reg.def_width = config->def_width;
	printk("reg.def_width: %u\n", par->reg.def_width);

	par->reset = config->reset;
	par->cs = config->cs;
	par->wr = config->wr;
	par->rd = config->rd;
	par->dc = config->dc;
	for (i = 0; i < 24; i++) {
		if (!config->db[i])
			break;
		par->db[i] = config->db[i];
	}
	par->buswidth = i;
printk("par->buswidth = %u\n", par->buswidth);
	par->prev_data = ~0;

	if (!par->dc || !par->wr) {
		dev_err(&pdev->dev, "missing 'dc' or 'wr' gpio\n");
		return ERR_PTR(-EINVAL);
	}

	par->reg.write = lcdreg_par_write;
	par->reg.read = lcdreg_par_read;
	par->reg.reset = lcdreg_par_reset;
	if (par->rd)
		par->reg.readable = true;

	return devm_lcdreg_init(&pdev->dev, &par->reg);
}
EXPORT_SYMBOL_GPL(devm_lcdreg_par_init);

MODULE_LICENSE("GPL");
