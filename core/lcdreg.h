/*
 * Copyright (C) 2014 Noralf Tronnes
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __LINUX_LCDREG_H
#define __LINUX_LCDREG_H

#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>

#include "../i80/i80.h"


/**
 * struct lcdreg_transfer - LCD register transfer
 * @index - can have the following names:
 *          D/C (command=0, data=1)
 *          RS (register selection: index=0, data=1)
 *          D/I (data/index: index=0, data=1)
 * @buf - data array to transfer
 * @count - number of items in array
 * @width - override default regwidth
 * @slow - slow down write transfers (reading is always slow)
 */
struct lcdreg_transfer {
	unsigned index;
	void *buf;
	unsigned count;
	unsigned width;
};

/**
 * struct lcdreg - interface to LCD register
 * @dev: device interface
 * @lock - mutex for register access locking
 * @def_width - default register width

 * @readable - LCD register is readable

 * @quirks - Deviations from the MIPI DBI standard
 */
struct lcdreg {
	struct device *dev;
	struct mutex lock;
	unsigned def_width;
	bool little_endian;
	bool readable;

	int (*write)(struct lcdreg *reg, unsigned regnr, struct lcdreg_transfer *transfer);
	int (*read)(struct lcdreg *reg, unsigned regnr, struct lcdreg_transfer *transfer);
	void (*reset)(struct lcdreg *reg);

	u64 quirks;
/* slow down command (index=0) */
#define LCDREG_SLOW_INDEX0_WRITE	BIT(0)
/*
 * The MIPI DBI spec states that D/C should be HIGH during register reading.
 * However, not all SPI master drivers support cs_change on last transfer and
 * there are LCD controllers that ignore D/C on read.
 */
#define LCDREG_INDEX0_ON_READ		BIT(1)

#ifdef CONFIG_DEBUG_FS
	struct dentry *debugfs;
	u32 debugfs_read_width;
	char *debugfs_read_result;
#endif
};


/*

MIPI_DBI_TYPE_C_OPTION_1
9-bit, D/CX embedded

MIPI_DBI_TYPE_C_OPTION_2
16-bit using the lowest 9-bit, D/CX embedded

MIPI_DBI_TYPE_C_OPTION_3
8-bit, D/CX line

LCDREG_STARTBYTE

http://lxr.free-electrons.com/source/drivers/video/backlight/ili922x.c
http://elmicro.com/files/seeedstudio/st7781r_datasheet.pdf
http://www.glyn.co.nz/downloads/protected/EDT/Datasheets/TFT/Controllers/HX8310-A%20V0%206.pdf
http://www.adafruit.com/datasheets/ILI9325.pdf
| 0 | 1 | 1 | 1 | 0 | ID | RS | RW |
| 0 | 1 | 1 | 1 | 0 | ID | RS | RW |
| 0 | 1 | 1 | 1 | 0 | ID | RS | RW |
| 0 | 1 | 1 | 1 | 0 | ID | RS | RW |
#define START_BYTE(id, rs, rw)  (0x70 | (((id) & 0x01) << 2) | (((rs) & 0x01) << 1) | ((rw) & 0x01))

http://www.displayfuture.com/Display/datasheet/controller/HX8238-A.pdf
| 0 | 1 | 1 | 1 | 0 | 0 | RS | RW |


http://www.ganasys.co.kr/kor/support_board2/pds_file/LG122325-SFLYH6V.pdf
http://www.futaba.com/products/display_modules/lcd_emulator/downloads/pdfs/LCD_Emulators.pdf
| 1 | 1 | 1 | 1 | 1 | RW | RS | 0 |
| 1 | 1 | 1 | 1 | 1 | RW | RS | 0 |

*/

enum lcdreg_spi_mode {
	LCDREG_SPI_4WIRE, /* 8-bit + D/CX line, MIPI DBI Type C option 3 */
	LCDREG_SPI_3WIRE, /* 9-bit inc. D/CX bit, MIPI DBI Type C option 1 */
	LCDREG_SPI_STARTBYTE1, /* | 0 | 1 | 1 | 1 | 0 | ID | RS | RW | */
	LCDREG_SPI_STARTBYTE2, /* | 1 | 1 | 1 | 1 | 1 | RW | RS |  0 | */
};


struct lcdreg_spi_config {
	enum lcdreg_spi_mode mode;
	unsigned def_width;
	bool readable;
	unsigned id;
u8 startbyte;
	char *dc_name;
	struct gpio_desc *dc;
	struct gpio_desc *reset;
};

struct lcdreg_par_config {
	unsigned def_width;
	struct gpio_desc *reset;
	struct gpio_desc *dc;
	struct gpio_desc *wr;
	struct gpio_desc *rd;
	struct gpio_desc *cs;
	struct gpio_desc *db[24];
};

struct lcdreg_i80_config {
	unsigned def_width;
	struct gpio_desc *reset;
};



/* http://lxr.free-electrons.com/ident?i=IS_ENABLED */

//#if IS_ENABLED(CONFIG_LCDREG)

extern struct gpio_desc *lcdreg_gpiod_get_index(struct device *dev, const char *name, unsigned int idx, int def_val);

static inline struct gpio_desc *lcdreg_gpiod_get(struct device *dev, const char *name, int def_val)
{
	return lcdreg_gpiod_get_index(dev, name, 0, def_val);
}


struct lcdreg *devm_lcdreg_init(struct device *dev,
					 struct lcdreg *reg);

extern int lcdreg_write(struct lcdreg *reg, unsigned regnr, struct lcdreg_transfer *transfer);
extern int lcdreg_write_buf32(struct lcdreg *reg, unsigned regnr, const u32 *data, unsigned count);

#define lcdreg_writereg(lcdreg, regnr, seq...) \
({\
        u32 d[] = { seq };\
        lcdreg_write_buf32(lcdreg, regnr, d, ARRAY_SIZE(d));\
})

extern int lcdreg_read(struct lcdreg *reg, unsigned regnr, struct lcdreg_transfer *transfer);
extern int lcdreg_readreg_buf32(struct lcdreg *reg, unsigned regnr, u32 *buf,
								unsigned count);

static inline void lcdreg_reset(struct lcdreg *reg)
{
	reg->reset(reg);
}

static inline void lcdreg_lock(struct lcdreg *reg)
{
	mutex_lock(&reg->lock);
}

static inline void lcdreg_unlock(struct lcdreg *reg)
{
	mutex_unlock(&reg->lock);
}

static inline bool lcdreg_is_readable(struct lcdreg *reg)
{
	return reg->readable;
}

struct lcdreg *devm_lcdreg_spi_init(struct spi_device *sdev,
				    const struct lcdreg_spi_config *config);
extern int devm_lcdreg_spi_parse_dt(struct device *dev,
					struct lcdreg_spi_config *config);
static inline struct lcdreg *devm_lcdreg_spi_init_dt(struct spi_device *sdev, enum lcdreg_spi_mode mode)
{
	struct lcdreg_spi_config spicfg = {
		.mode = mode,
	};
	int ret;

	ret = devm_lcdreg_spi_parse_dt(&sdev->dev, &spicfg);
	if (ret)
		return ERR_PTR(ret);

	return devm_lcdreg_spi_init(sdev, &spicfg);
}

extern int devm_lcdreg_par_parse_dt(struct device *dev,
					struct lcdreg_par_config *config);
struct lcdreg *devm_lcdreg_par_init(struct platform_device *pdev,
				    const struct lcdreg_par_config *config);

extern int devm_lcdreg_i80_parse_dt(struct device *dev,
					struct lcdreg_i80_config *config);
struct lcdreg *devm_lcdreg_i80_init(struct i80_device *i80dev,
				    const struct lcdreg_i80_config *config);


//#else

//#endif


static inline unsigned lcdreg_bytes_per_word(unsigned bits_per_word)
{
	if (bits_per_word <= 8)
		return 1;
	else if (bits_per_word <= 16)
		return 2;
	else /* bits_per_word <= 32 */
		return 4;
}

#if defined(CONFIG_DYNAMIC_DEBUG)
/*
	print_hex_dump_debug("    buf=", DUMP_PREFIX_NONE, 32,       \
			     groupsize, transfer->buf, len, false);  \
*/
#define lcdreg_dbg_transfer_buf(transfer)                            \
do {                                                                 \
	int groupsize = lcdreg_bytes_per_word(transfer->width);      \
	size_t len = min_t(size_t, 32, transfer->count * groupsize); \
                                                                     \
	print_hex_dump_debug("    buf=", DUMP_PREFIX_NONE, 32,       \
			groupsize, transfer->buf, len, false);       \
} while(0)
#elif defined(DEBUG)
#define lcdreg_dbg_transfer_buf(transfer)                            \
do {                                                                 \
	int groupsize = lcdreg_bytes_per_word(transfer->width);      \
	size_t len = min_t(size_t, 32, transfer->count * groupsize); \
                                                                     \
	print_hex_dump(KERN_DEBUG, "    buf=", DUMP_PREFIX_NONE, 32, \
			groupsize, transfer->buf, len, false);       \
} while(0)
#else
#define lcdreg_dbg_transfer_buf(transfer)
#endif /* DEBUG || CONFIG_DYNAMIC_DEBUG */

#endif /* __LINUX_LCDREG_H */
