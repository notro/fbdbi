#define VERBOSE_DEBUG
#define DEBUG

#include <asm/unaligned.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/spi/spi.h>

#include "lcdreg.h"


/*
 FIXME: switch to ACTIVE_LOW mode, or maybe leave as-is, since not all gpio controllers support flags
 
 https://www.kernel.org/doc/Documentation/devicetree/bindings/gpio/gpio.txt
 
 */

static unsigned long bits_per_word_mask;
module_param(bits_per_word_mask, ulong, 0);
MODULE_PARM_DESC(bits_per_word_mask, "Override SPI master bits_per_word_mask");

static bool dma = true;
module_param(dma, bool, 0);
MODULE_PARM_DESC(dma, "Use DMA buffer");


struct lcdreg_spi {
	struct lcdreg reg;
	enum lcdreg_spi_mode mode;
	void *txbuf;
	unsigned txbuflen;
	u8 *startbuf;
	dma_addr_t startbuf_dma;
	void *txbuf_dc;
	unsigned id;
u8 startbyte;
	struct gpio_desc *dc;
	struct gpio_desc *reset;
	u32 bits_per_word_mask;
};

static inline struct lcdreg_spi *to_lcdreg_spi(struct lcdreg *reg)
{
	return reg ? container_of(reg, struct lcdreg_spi, reg) : NULL;
}

static inline bool lcdreg_spi_is_bpw_supported(struct lcdreg_spi *spi,
								unsigned bpw)
{
	return (SPI_BPW_MASK(bpw) & spi->bits_per_word_mask) ? true : false;
}

// TODO
//	if (spi->mode == LCDREG_SPI_STARTBYTE1 ||
//	    spi->mode == LCDREG_SPI_STARTBYTE2)
static inline bool lcdreg_spi_use_startbyte(struct lcdreg_spi *spi)
{
	return spi->startbyte;
}

#ifdef VERBOSE_DEBUG
static void lcdreg_vdbg_dump_spi(const struct device *dev, struct spi_message *m, u8 *startbyte)
{
	struct spi_transfer *tmp;
	struct list_head *pos;
	int i = 0;

	if (startbyte)
		dev_info(dev, "spi_message: dma=%u, startbyte=0x%02X\n", m->is_dma_mapped, startbyte[0]);
	else
		dev_info(dev, "spi_message: dma=%u\n", m->is_dma_mapped);

	list_for_each(pos, &m->transfers) {
		tmp = list_entry(pos, struct spi_transfer, transfer_list);
		if (tmp->tx_buf)
			pr_info("    tr%i: bpw=%i, len=%u, tx_buf(%p)=[%*ph]\n", i, tmp->bits_per_word, tmp->len, tmp->tx_buf, tmp->len > 64 ? 64 : tmp->len, tmp->tx_buf);
		if (tmp->rx_buf)
			pr_info("    tr%i: bpw=%i, len=%u, rx_buf(%p)=[%*ph]\n", i, tmp->bits_per_word, tmp->len, tmp->rx_buf, tmp->len > 64 ? 64 : tmp->len, tmp->rx_buf);
		i++;
	}
}
#else
static void lcdreg_vdbg_dump_spi(const struct device *dev, struct spi_message *m, u8 *startbyte)
{
}
#endif

/*
   From 3.15 SPI core handles vmalloc'ed buffers if master->can_dma


vmalloc part is not tested


 */

static int
lcdreg_spi_transfer(struct lcdreg *reg, struct lcdreg_transfer *transfer)
{
	struct lcdreg_spi *spi = to_lcdreg_spi(reg);
	struct spi_device *sdev = to_spi_device(reg->dev);
	const bool vmalloced_buf = is_vmalloc_addr(transfer->buf);
	struct page *vm_page;
	bool do_dma = false;
	void *buf = transfer->buf;
/*	const int desc_len = vmalloced_buf ? PAGE_SIZE : master->max_dma_len; */
	const int desc_len = PAGE_SIZE;

	size_t len = transfer->count * lcdreg_bytes_per_word(transfer->width);
	size_t min;

	struct spi_transfer *tr, *tmp;
	size_t trs = DIV_ROUND_UP(len, desc_len) + 1;
	struct spi_message m;
	int ret, i = 0;
	struct list_head *pos;

	lcdreg_dbg_write(reg->dev, __func__, transfer);

	if (transfer->index == 1 && len > 128 && dma)
		do_dma = true;

	tr = kzalloc(trs * sizeof(*tr), GFP_KERNEL);
	if (!tr)
		return -ENOMEM;

	if ((transfer->index == 0) && (reg->quirks & LCDREG_SLOW_INDEX0_WRITE))
		for (i = 0; i < trs; i++)
			tr[i].speed_hz = min_t(u32, 2000000,
					       sdev->max_speed_hz / 2);

	do {
		i = 0;
		spi_message_init(&m);

/* TODO: use lcdreg_spi_use_startbyte() */
		if (spi->startbyte) {
			if (!spi->startbuf) {
				if (do_dma)
					spi->startbuf = dmam_alloc_coherent(&sdev->dev, 1, &spi->startbuf_dma, GFP_DMA);
				else
					spi->startbuf = devm_kmalloc(&sdev->dev, 1, GFP_KERNEL);
				if (!spi->startbuf) {
					kfree(tr);
					return -ENOMEM;
				}
			}
			if (transfer->index == 0)
				spi->startbuf[0] = spi->startbyte;
			else
				spi->startbuf[0] = spi->startbyte | 0x2;
			tr[i].tx_buf = spi->startbuf;
			if (do_dma)
				tr[i].tx_dma = spi->startbuf_dma;
			tr[i].len = 1;
			tr[i].bits_per_word = 8;
			spi_message_add_tail(&tr[i++], &m);
		}
		if (do_dma)
			m.is_dma_mapped = 1;

		/* in case there's a touch controller on the same bus
		 * chop up into multiple messages
		 */
		while (len && i < 4) {
			min = min_t(size_t, len, desc_len);

			/* 
			 * transfer->buf might not be on a PAGE boundary
			 * align to PAGE by doing a small transfer
			 */
			if (vmalloced_buf && (buf == transfer->buf)) {
				min = min_t(size_t, min, PAGE_SIZE - offset_in_page(buf));
			}

			if (vmalloced_buf) {
				vm_page = vmalloc_to_page(buf);
				if (!vm_page) {
					dev_dbg(&sdev->dev,
						"vmalloc_to_page returned NULL\n");
					ret = -ENOMEM;
					goto transfer_out;
				}
				/*
				 * TODO: page_address can return NULL:
				 * https://github.com/torvalds/linux/commit/c1aefbdd050e1fb15e92bcaf34d95b17ea952097
				 */
				tr[i].tx_buf = page_address(vm_page) +
							offset_in_page(buf);
			} else {
				tr[i].tx_buf = buf;
			}

			tr[i].len = min;
			tr[i].bits_per_word = transfer->width;
			if (do_dma) {
				tr[i].tx_dma = dma_map_single(&sdev->dev, (void *) tr[i].tx_buf,
									tr[i].len, DMA_TO_DEVICE);
				if (dma_mapping_error(&sdev->dev, tr[i].tx_dma)) {
					dev_dbg(&sdev->dev, "dma TX %d bytes error\n", tr[i].len);
					ret = -EINVAL;
					goto transfer_out;
				}
			}
			buf += min;
			len -= min;
			spi_message_add_tail(&tr[i], &m);
			++i;
		}
		lcdreg_vdbg_dump_spi(&sdev->dev, &m, spi->startbuf);
		ret = spi_sync(sdev, &m);
		if (do_dma) {
			list_for_each(pos, &m.transfers) {
				tmp = list_entry(pos, struct spi_transfer, transfer_list);
				dma_unmap_single(&sdev->dev, tmp->tx_dma, tmp->len, DMA_TO_DEVICE);
			}
		}
		if (ret)
			goto transfer_out;

	} while (len);

transfer_out:
	kfree(tr);

	return ret;
}


static int lcdreg_spi_write_one(struct lcdreg *reg, struct lcdreg_transfer *transfer)
{
	struct lcdreg_spi *spi = to_lcdreg_spi(reg);

	lcdreg_dbg_write(reg->dev, __func__, transfer);

	if (spi->dc)
		gpiod_set_value_cansleep(spi->dc, transfer->index);

	if (lcdreg_spi_is_bpw_supported(spi, transfer->width))
		return lcdreg_spi_transfer(reg, transfer);

#ifdef __BIG_ENDIAN
	/* on big endian the byte order matches for 16 */
	if (transfer->width == 16) {
		transfer->width = 8;
		transfer->count *= 2;
		return lcdreg_spi_transfer(reg, transfer);
	}
#endif

	if (!spi->txbuf) {
		spi->txbuf = devm_kzalloc(reg->dev, spi->txbuflen, GFP_KERNEL);
		if (!spi->txbuf)
			return -ENOMEM;
		dev_dbg(reg->dev, "allocated %u KiB transmit buffer\n",
						spi->txbuflen / 1024);
	}
	if (transfer->width == 9) {
		struct lcdreg_transfer tr = {
			.buf = spi->txbuf,
			.index = transfer->index,
			.width = 8,
		};
		u16 *src = transfer->buf;
		u8 *dst = spi->txbuf;
		unsigned size = transfer->count;
		unsigned added = 0;
		int bits, i, j;
		u64 val, dc, tmp;

/* buf len is not handled, this assumes that txbuf can hold the data, which it does for 9bit emulation 

		if ((size + (size / 8) + 1) > txbuflen) {
			dev_err(reg->dev,
				"%s: error: transfer->count=%u too big\n",
				__func__, size);
			return -EINVAL;
		}

*/

		if ((size % 8) != 0) {
			dev_err(reg->dev,
				"%s: error: transfer->count=%u must be divisible by 8\n",
				__func__, size);
			return -EINVAL;
		}

		for (i = 0; i < size; i += 8) {
			tmp = 0;
			bits = 63;
			for (j = 0; j < 7; j++) {
				dc = (*src & 0x0100) ? 1 : 0;
				val = *src & 0x00FF;
				src++;
				tmp |= dc << bits;
				bits -= 8;
				tmp |= val << bits--;
			}
			tmp |= ((*src & 0x0100) ? 1 : 0);
			*(u64 *)dst = cpu_to_be64(tmp);
			dst += 8;
			*dst++ = (u8)(*src++ & 0x00FF);
			added++;
		}
		tr.count = size + added;
		return lcdreg_spi_transfer(reg, &tr);
	}

	if (transfer->width == 16) {
		struct lcdreg_transfer tr = {
			.buf = spi->txbuf,
			.index = transfer->index,
			.width = 8,
		};
		u16 *data16 = transfer->buf;
		u16 *txbuf16 = spi->txbuf;
		unsigned remain = transfer->count;
		unsigned tx_array_size = spi->txbuflen / 2;
		unsigned to_copy;
		int i, ret = 0;

		while (remain) {
			to_copy = remain > tx_array_size ? tx_array_size : remain;
			dev_dbg(reg->dev, "    to_copy=%zu, remain=%zu\n",
						to_copy, remain - to_copy);

			for (i = 0; i < to_copy; i++)
				txbuf16[i] = cpu_to_be16(data16[i]);

			data16 = data16 + to_copy;
			transfer->count = to_copy * 2;
			tr.count = to_copy * 2;
			ret = lcdreg_spi_transfer(reg, &tr);
			if (ret < 0)
				return ret;
			remain -= to_copy;
		}
		return ret;
	}
	if (transfer->width == 24) {
		struct lcdreg_transfer tr = {
			.buf = spi->txbuf,
			.index = transfer->index,
			.width = 8,
		};
		u32 *data32 = transfer->buf;
		u8 *txbuf8;
		unsigned remain = transfer->count;
		unsigned tx_array_size = spi->txbuflen / 4;
		unsigned to_copy;
		int i, ret = 0;

		while (remain) {
			to_copy = remain > tx_array_size ? tx_array_size : remain;
			dev_dbg(reg->dev, "    to_copy=%zu, remain=%zu\n",
						to_copy, remain - to_copy);
			txbuf8 = spi->txbuf;
			for (i = 0; i < to_copy; i++) {
				*txbuf8++ = data32[i] >> 16;
				*txbuf8++ = data32[i] >> 8;
				*txbuf8++ = data32[i];
			}
			data32 += to_copy;
			remain -= to_copy;
			tr.count = to_copy * 3;
			ret = lcdreg_spi_transfer(reg, &tr);
			if (ret < 0)
				return ret;
		}
		return ret;
	}
	dev_err(reg->dev, "transfer width %u is not supported\n",
						transfer->width);
	return -EINVAL;
}

static int lcdreg_spi_write_9bit_dc(struct lcdreg *reg, struct lcdreg_transfer *transfer)
{
	struct lcdreg_spi *spi = to_lcdreg_spi(reg);
	struct lcdreg_transfer tr = {
		.index = transfer->index,
	};
	u8 *data8 = transfer->buf;
	u16 *data16 = transfer->buf;
	unsigned width;
	u16 *txbuf16;
	unsigned remain;
	unsigned tx_array_size;
	unsigned to_copy;
	int pad, i, ret;

width = transfer->width;
	lcdreg_dbg_write(reg->dev, __func__, transfer);

	if (width != 8 && width != 16) {
		dev_err(reg->dev, "transfer width %u is not supported\n",
								width);
		return -EINVAL;
	}

	if (!spi->txbuf_dc) {
		spi->txbuf_dc = devm_kzalloc(reg->dev, spi->txbuflen,
							GFP_KERNEL);
		if (!spi->txbuf_dc)
			return -ENOMEM;
		dev_info(reg->dev, "allocated %u KiB 9-bit dc buffer\n",
						spi->txbuflen / 1024);
	}

	tr.buf = spi->txbuf_dc;
	txbuf16 = spi->txbuf_dc;
	remain = transfer->count;
	if (width == 8)
		tx_array_size = spi->txbuflen / 2;
	else
		tx_array_size = spi->txbuflen / 4;

	/* If we're emulating 9-bit, the buffer has to be divisible by 8.
	   Pad with no-ops if necessary (assuming here that zero is a no-op)
	   FIX: If video buf isn't divisible by 8, it will break.
	 */
	if (!test_bit(9 - 1, &bits_per_word_mask) && width == 8 &&
						remain < tx_array_size) {
		pad = (transfer->count % 8) ? 8 - (transfer->count % 8) : 0;
		if (transfer->index == 0)
			for (i = 0; i < pad; i++)
				*txbuf16++ = 0x000;
		for (i = 0; i < remain; i++) {
			*txbuf16 = *data8++;
			if (transfer->index)
				*txbuf16++ |= 0x0100;
		}
		if (transfer->index == 1)
			for (i = 0; i < pad; i++)
				*txbuf16++ = 0x000;
		tr.width = 9;
		tr.count = pad + remain;
		return lcdreg_spi_write_one(reg, &tr);
	}

	while (remain) {
		to_copy = remain > tx_array_size ? tx_array_size : remain;
		remain -= to_copy;
		dev_dbg(reg->dev, "    to_copy=%zu, remain=%zu\n",
					to_copy, remain);

		if (width == 8) {
			for (i = 0; i < to_copy; i++) {
				txbuf16[i] = *data8++;
				if (transfer->index)
					txbuf16[i] |= 0x0100;
			}
		} else {
			for (i = 0; i < (to_copy * 2); i += 2) {
				txbuf16[i]     = *data16 >> 8;
				txbuf16[i + 1] = *data16++ & 0xFF;
				if (transfer->index) {
					txbuf16[i]     |= 0x0100;
					txbuf16[i + 1] |= 0x0100;
				}
			}
		}
		tr.buf = spi->txbuf_dc;
		tr.width = 9;
		tr.count = to_copy * 2;
		ret = lcdreg_spi_write_one(reg, &tr);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int lcdreg_spi_write(struct lcdreg *reg, unsigned regnr, struct lcdreg_transfer *transfer)
{
	struct lcdreg_spi *spi = to_lcdreg_spi(reg);
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

	if (spi->mode == LCDREG_SPI_3WIRE)
		ret = lcdreg_spi_write_9bit_dc(reg, &tr);
	else
		ret = lcdreg_spi_write_one(reg, &tr);
	kfree(tr.buf);
	if (ret || !transfer || !transfer->count)
		return ret;

	if (!transfer->width)
		transfer->width = reg->def_width;
	if (spi->mode == LCDREG_SPI_3WIRE)
		ret = lcdreg_spi_write_9bit_dc(reg, transfer);
	else
		ret = lcdreg_spi_write_one(reg, transfer);

	return ret;
}

/*
   <CMD> <DM> <PA>
   CMD = Command
   DM = Dummy read
   PA = Parameter or display data

   ST7735R read:
     Parallel: <CMD> <DM> <PA>
     SPI: 8-bit plain, 24- and 32-bit needs 1 dummy clock cycle

   ILI9320:
     Parallel: no dummy read, page 51 in datasheet
     SPI (startbyte): One byte of invalid dummy data read after the start byte.

   ILI9340:
     Parallel: no info about dummy read
     SPI: same as ST7735R

   ILI9341:
     Parallel: no info about dummy read
     SPI: same as ST7735R

   SSD1289:
     Parallel: 1 dummy read

 */

static int lcdreg_spi_read_startbyte(struct lcdreg *reg, unsigned regnr, struct lcdreg_transfer *transfer)
{
	struct lcdreg_spi *spi = to_lcdreg_spi(reg);
	struct spi_device *sdev = to_spi_device(reg->dev);
	struct spi_message m;
	struct spi_transfer trtx = {
		.speed_hz = min_t(u32, 2000000, sdev->max_speed_hz / 2),
		.bits_per_word = 8,
		.len = 1,
	};
	struct spi_transfer trrx = {
		.speed_hz = trtx.speed_hz,
		.bits_per_word = 8,
		.len = (transfer->count * 2) + 1,
	};
	u8 *txbuf, *rxbuf;
	int i, ret;

printk("\n\n\n");

	transfer->width = transfer->width ? : reg->def_width;
	lcdreg_dbg_read_in(reg->dev, __func__, transfer);

	if (WARN_ON(transfer->width != 16 || !transfer->count))
		return -EINVAL;

	if (!reg->readable)
		return -EACCES;

	ret = lcdreg_writereg(reg, regnr);
	if (ret)
		return ret;

	txbuf = kzalloc(1, GFP_KERNEL);
	if (!txbuf)
		return -ENOMEM;

	rxbuf = kzalloc(trrx.len, GFP_KERNEL);
	if (!rxbuf) {
		kfree(txbuf);
		return -ENOMEM;
	}

	*txbuf = spi->startbyte;
// TODO: support both startbyte modes
	if (transfer->index)
		*txbuf |=  0x3;
	trtx.tx_buf = txbuf;
	trrx.rx_buf = rxbuf;
	spi_message_init(&m);
	spi_message_add_tail(&trtx, &m);
	spi_message_add_tail(&trrx, &m);
	ret = spi_sync(sdev, &m);
	lcdreg_vdbg_dump_spi(&sdev->dev, &m, txbuf);
	kfree(txbuf);
	if (ret) {
		kfree(rxbuf);
		return ret;
	}

	rxbuf++;
	for (i = 0; i < transfer->count; i++) {
		((u16 *)transfer->buf)[i] = get_unaligned_be16(rxbuf);
		rxbuf += 2;
	}
	kfree(trrx.rx_buf);
	lcdreg_dbg_read_out(reg->dev, __func__, transfer);

	return 0;
}

static int lcdreg_spi_read(struct lcdreg *reg, unsigned regnr, struct lcdreg_transfer *transfer)
{
	struct lcdreg_spi *spi = to_lcdreg_spi(reg);
	struct spi_device *sdev = to_spi_device(reg->dev);
	struct spi_message m;
	struct spi_transfer trtx = {
		.speed_hz = min_t(u32, 2000000, sdev->max_speed_hz / 2),
		.bits_per_word = reg->def_width,
		.len = 1,
	};
	struct spi_transfer trrx = {
		.speed_hz = trtx.speed_hz,
		.rx_buf = transfer->buf,
		.len = transfer->count,
	};
	void *txbuf = NULL;
	int i, ret;

printk("\n\n\n");

	transfer->width = transfer->width ? : reg->def_width;
	lcdreg_dbg_read_in(reg->dev, __func__, transfer);

	if (WARN_ON(transfer->width != reg->def_width || !transfer->count))
		return -EINVAL;

	if (!reg->readable)
		return -EACCES;

	txbuf = kzalloc(16, GFP_KERNEL);
	if (!txbuf)
		return -ENOMEM;

	spi_message_init(&m);
	trtx.tx_buf = txbuf;
	trrx.bits_per_word = transfer->width;
	if (lcdreg_spi_use_startbyte(spi)) {
//TODO		return reg->read(reg, transfer);
	} else {
		if (spi->mode == LCDREG_SPI_4WIRE) {
			if (trtx.bits_per_word == 8) {
				*(u8 *)txbuf = regnr;
			} else if (trtx.bits_per_word == 16) {
				if (lcdreg_spi_is_bpw_supported(spi, trtx.bits_per_word)) {
					*(u16 *)txbuf = regnr;
				} else {
					*(u16 *)txbuf = cpu_to_be16(regnr);
					trtx.bits_per_word = 8;
					trtx.len = 2;
				}
			} else {
				return -EINVAL;
			}
			gpiod_set_value_cansleep(spi->dc, 0);
		} else if (spi->mode == LCDREG_SPI_3WIRE) {
			if (lcdreg_spi_is_bpw_supported(spi, 9)) {
				trtx.bits_per_word = 9;
				*(u16 *)txbuf = regnr; /* dc=0 */
			} else {
				/* 8x 9-bit words, pad with leading zeroes (no-ops) */
				((u8 *)txbuf)[8] = regnr;
			}
		} else {
			kfree(txbuf);
			return -EINVAL;
		}
		spi_message_add_tail(&trtx, &m);

		if (spi->mode == LCDREG_SPI_4WIRE && transfer->index) {
			trtx.cs_change = 1; /* not always supported */
			lcdreg_vdbg_dump_spi(&sdev->dev, &m, NULL);
			ret = spi_sync(sdev, &m);
			if (ret) {
				kfree(txbuf);
				return ret;
			}
			gpiod_set_value_cansleep(spi->dc, 1);
			spi_message_init(&m);
		}
	}

	spi_message_add_tail(&trrx, &m);
	ret = spi_sync(sdev, &m);
	lcdreg_vdbg_dump_spi(&sdev->dev, &m, NULL);
	kfree(txbuf);
	if (ret)
		return ret;

	if (!lcdreg_spi_is_bpw_supported(spi, trrx.bits_per_word) &&
						(trrx.bits_per_word == 16))
		for (i = 0; i < transfer->count; i++)
			((u16 *)transfer->buf)[i] = be16_to_cpu(((u16 *)transfer->buf)[i]);

	lcdreg_dbg_read_out(reg->dev, __func__, transfer);

	return 0;
}

static void lcdreg_spi_reset(struct lcdreg *reg)
{
	struct lcdreg_spi *spi = to_lcdreg_spi(reg);

	if (!spi->reset)
		return;

	dev_info(reg->dev, "%s()\n", __func__);
	gpiod_set_value_cansleep(spi->reset, 0);
	msleep(20);
	gpiod_set_value_cansleep(spi->reset, 1);
	msleep(120);
}

u32 lcdreg_of_value(struct device *dev, const char *propname, u32 def_value)
{
	u32 val = def_value;
	int ret;

	ret = of_property_read_u32(dev->of_node, propname, &val);
	if (ret && ret != -EINVAL)
		dev_err(dev, "error reading property '%s' (%i)\n", propname, ret);
dev_info(dev, "%s(%s) = %u\n", __func__, propname, val);
	return val;
}

int devm_lcdreg_spi_parse_dt(struct device *dev, struct lcdreg_spi_config *config)
{
	char *dc_name = config->dc_name ? : "dc";

	config->reset = lcdreg_gpiod_get(dev, "reset", 0);
	if (IS_ERR(config->reset))
		return PTR_ERR(config->reset);

	if (config->mode == LCDREG_SPI_4WIRE) {
		config->dc = lcdreg_gpiod_get(dev, dc_name, 0);
		if (IS_ERR(config->dc))
			return PTR_ERR(config->dc);

	} else if (config->mode == LCDREG_SPI_STARTBYTE1) {
		config->id = lcdreg_of_value(dev, "id", 0);
config->startbyte = 0x70;
	}
	return 0;
}
EXPORT_SYMBOL(devm_lcdreg_spi_parse_dt);

struct lcdreg *devm_lcdreg_spi_init(struct spi_device *sdev,
					const struct lcdreg_spi_config *config)
{
	struct lcdreg_spi *spi;

	spi = devm_kzalloc(&sdev->dev, sizeof(*spi), GFP_KERNEL);
	if (spi == NULL)
		return ERR_PTR(-ENOMEM);

	if (!bits_per_word_mask) {
		if (sdev->master->bits_per_word_mask)
			spi->bits_per_word_mask = sdev->master->bits_per_word_mask;
		else
			spi->bits_per_word_mask = SPI_BPW_MASK(8);
	}
	dev_dbg(&sdev->dev, "bits_per_word_mask: 0x%04x",
					spi->bits_per_word_mask);
	spi->mode = config->mode;
	spi->reg.def_width = config->def_width;
	spi->reg.readable = config->readable;
	if (!spi->txbuflen)
		spi->txbuflen = PAGE_SIZE;
spi->startbyte = config->startbyte;
	spi->id = config->id;
	spi->reset = config->reset;
	spi->dc = config->dc;
	if (spi->mode == LCDREG_SPI_4WIRE && !spi->dc) {
		dev_err(&sdev->dev, "missing 'dc' gpio\n");
		return ERR_PTR(-EINVAL);
	}

	spi->reg.write = lcdreg_spi_write;
	if (lcdreg_spi_use_startbyte(spi))
		spi->reg.read = lcdreg_spi_read_startbyte;
	else
		spi->reg.read = lcdreg_spi_read;
	spi->reg.reset = lcdreg_spi_reset;

pr_debug("spi->reg.def_width: %u\n", spi->reg.def_width);
if (spi->reset)
	pr_debug("spi->reset: %i\n", desc_to_gpio(spi->reset));
if (spi->dc)
	pr_debug("spi->dc: %i\n", desc_to_gpio(spi->dc));
pr_debug("spi->mode: %u\n", spi->mode);
pr_debug("spi->startbyte: %u\n", spi->startbyte);




	return devm_lcdreg_init(&sdev->dev, &spi->reg);
}
EXPORT_SYMBOL_GPL(devm_lcdreg_spi_init);

MODULE_LICENSE("GPL");
