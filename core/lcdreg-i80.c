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
	void *buffer;
	struct gpio_desc *reset;
};

static inline struct lcdreg_i80 *to_lcdreg_i80(struct lcdreg *reg)
{
	return reg ? container_of(reg, struct lcdreg_i80, reg) : NULL;
}

static int lcdreg_i80_write_regnr(struct lcdreg *reg, unsigned regnr)
{
	struct lcdreg_i80 *i80lcd = to_lcdreg_i80(reg);
	struct i80_device *i80 = i80lcd->i80;
	struct i80_master *master = i80->master;
	void *buf = i80lcd->regnr_buf;
	unsigned len = lcdreg_bytes_per_word(reg->def_width);

	switch (len) {
	case 2:
		if (master->data_width == 16)
			*(u16 *)buf = regnr;
		else if (master->data_width == 8)
			*(u16 *)buf = cpu_to_be16(regnr);
		else
			return -EINVAL;
		break;
	case 1:
		*(u8 *)buf = regnr;
		break;
	default:
		return -EINVAL;
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

	if (transfer->width == master->data_width) {
		ret = i80_write(i80, transfer->index, transfer->buf, transfer->count * lcdreg_bytes_per_word(transfer->width));
		goto done;
	}

#ifdef __BIG_ENDIAN
	/* on big endian the byte order matches */
	if (master->data_width == 8 &&
	    (transfer->width == 16 || transfer->width ==  24)) {
		ret = i80_write(i80, transfer->index, transfer->buf, transfer->count * lcdreg_bytes_per_word(transfer->width));
		goto done;
	}
#endif

	if (!i80lcd->buffer) {
		i80lcd->buffer = devm_kzalloc(&i80->dev, PAGE_SIZE, GFP_KERNEL);
		if (!i80lcd->buffer) {
			ret = -ENOMEM;
			goto done;
		}
	}

	ret = -EINVAL;
	if (transfer->width == 16 && master->data_width == 8) {
		u16 *data16 = transfer->buf;
		u16 *buffer16 = i80lcd->buffer;
		unsigned remain = transfer->count;
		unsigned tx_array_size = PAGE_SIZE / 2;
		unsigned to_copy;
		int i;

		while (remain) {
			to_copy = remain > tx_array_size ? tx_array_size : remain;
			remain -= to_copy;
			dev_dbg(reg->dev, "    to_copy=%zu, remain=%zu\n",
						to_copy, remain);
			for (i = 0; i < to_copy; i++)
				buffer16[i] = cpu_to_be16(*data16++);
			ret = i80_write(i80, transfer->index, buffer16, to_copy * 2);
			if (ret < 0)
				goto done;
		}
	}

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

	if (transfer->width == master->data_width) {
		ret = i80_read(i80, transfer->index, transfer->buf, transfer->count * lcdreg_bytes_per_word(transfer->width));
		goto done;
	}

#ifdef __BIG_ENDIAN
	/* on big endian the byte order matches */
	if (master->data_width == 8 && transfer->width == 16) {
		ret = i80_write(i80, transfer->index, transfer->buf, transfer->count * lcdreg_bytes_per_word(transfer->width));
		goto done;
	}
#endif

	if (!i80lcd->buffer) {
		i80lcd->buffer = devm_kzalloc(&i80->dev, PAGE_SIZE, GFP_KERNEL);
		if (!i80lcd->buffer) {
			ret = -ENOMEM;
			goto done;
		}
	}

	ret = -EINVAL;
	if (transfer->width == 16 && master->data_width == 8) {
		u16 *data16 = transfer->buf;
		u16 *rxbuf16 = i80lcd->buffer;
		unsigned remain = transfer->count;
		unsigned rx_array_size = PAGE_SIZE / 2;
		unsigned to_copy;
		int i;

		while (remain) {
			to_copy = remain > rx_array_size ? rx_array_size : remain;
			remain -= to_copy;
			dev_dbg(reg->dev, "    to_copy=%zu, remain=%zu\n",
						to_copy, remain);
			ret = i80_read(i80, transfer->index, rxbuf16, to_copy * 2);
			if (ret < 0)
				goto done;

			for (i = 0; i < to_copy; i++)
				*data16++ = be16_to_cpu(rxbuf16[i]);
		}
	}

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

	i80lcd->regnr_buf = devm_kzalloc(&i80->dev, sizeof(u16), GFP_KERNEL);
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
