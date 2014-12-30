/*
Documentation/devicetree/bindings/vendor-prefixes.txt

mi	Multi-Inno Technology Co.,Ltd

*/

/*
Documentation/devicetree/bindings/video/mi,mi0283qt.txt

* Multi-Inno MI0283QT Framebuffer Driver

The node for this driver must be a child node of a SPI controller, hence
all mandatory properties described in

	Documentation/devicetree/bindings/spi/spi-bus.txt

must be specified.

Required properties:
- compatible		Should be one of "mi,mi0283qt".

- power-supply		A regulator node for the supply voltage.

Optional properties:
- reset-gpios		Reset pin
- dc-gpios		D/C pin. The presence/absence of this GPIO determines
			the display interface mode (IM[3:0]):
			- absent:  IM=x101 3-wire 9-bit data serial interface
			- present: IM=x110 4-wire 8-bit data serial interface
- readable		LCD controller is readable. This depends on interface
			mode, master driver and wiring:
			- IM=11xx: MISO must be connected
			- IM=01xx: SPI master driver supports spi-3wire (SDA)
- rotate		Display rotation in degrees counter clockwise
- backlight		phandle of the backlight device attached to the panel

- format:		Framebuffer format:
			- "rgb565" (default)
			- "rgb888" RGB666 on display
			- "xrgb8888" RGB666 on display


Examples:

	mi0283qt@0{
		compatible = "mi,mi0283qt";
		reg = <0>;
		spi-max-frequency = <32000000>;

		power-supply = <&vdd>
		rotate = <270>;
		dc-gpios = <&gpio 25 0>;
		backlight = <&backlight>;
		readable;
	};

*/

/*
 * Multi-Inno MI0283QT Framebuffer Driver
 *
 * Copyright 2014 Noralf Tronnes
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>

#include "core/lcdreg.h"
#include "core/fbdbi.h"
#include "ili9341.h"
#include "mipi-dbi.h"


/*
 * The init sequence is taken from the datasheet and
 * applies to at least the following panels:
 * - MI0283QT-8
 * - MI0283QT-9
 * - MI0283QT-9A
 * - MI0283QT-11
 * - MI0283QT-13
 *
 * MI0283QT-2 is not supported (HX8347D)
 *
 */
static int mi0283qt_poweron(struct fbdbi_display *display)
{
	struct lcdreg *lcdreg = display->lcdreg;
	int ret;

	pr_info("%s()\n", __func__);

	ret = regulator_enable(display->power_supply);
	if (ret < 0) {
		dev_err(lcdreg->dev,
			"failed to enable power supply: %d\n", ret);
		return ret;
	}
	lcdreg_reset(lcdreg);
	ret = lcdreg_writereg(lcdreg, ILI9341_SWRESET);
	if (ret) {
		dev_err(lcdreg->dev,
			"lcdreg_writereg failed: %d\n", ret);
		return ret;
	}
	msleep(5);
	lcdreg_writereg(lcdreg, ILI9341_DISPOFF);

	lcdreg_writereg(lcdreg, ILI9341_PWCTRLB, 0x00, 0x83, 0x30);
	lcdreg_writereg(lcdreg, ILI9341_PWRSEQ, 0x64, 0x03, 0x12, 0x81);
	lcdreg_writereg(lcdreg, ILI9341_TCTRLA, 0x85, 0x01, 0x79);
	lcdreg_writereg(lcdreg, ILI9341_PWCTRLA, 0x39, 0x2c, 0x00, 0x34, 0x02);
	lcdreg_writereg(lcdreg, ILI9341_PUMP, 0x20);
	lcdreg_writereg(lcdreg, ILI9341_TCTRLB, 0x00, 0x00);

	/* Power Control */
	lcdreg_writereg(lcdreg, ILI9341_PWCTRL1, 0x26);
	lcdreg_writereg(lcdreg, ILI9341_PWCTRL2, 0x11);

	/* VCOM */
	lcdreg_writereg(lcdreg, ILI9341_VMCTRL1, 0x35, 0x3e);
	lcdreg_writereg(lcdreg, ILI9341_VMCTRL2, 0xbe);

/* Interface */
	if (lcdreg->little_endian) {
		pr_info("lcdreg->little_endian == true\n");
// doesn't work
		lcdreg_writereg(lcdreg, ILI9341_IFCTL, 0x01, 0x00, BIT(5));
	}

	/* Memory Access Control */
	lcdreg_writereg(lcdreg, ILI9341_PIXSET, 0x55);

	/* Frame rate */
	lcdreg_writereg(lcdreg, ILI9341_FRMCTR1, 0x00, 0x1B);

	/* Gamma */
	lcdreg_writereg(lcdreg, ILI9341_EN3GAM, 0x08);
	lcdreg_writereg(lcdreg, ILI9341_GAMSET, 0x01);
	lcdreg_writereg(lcdreg, ILI9341_PGAMCTRL,
			0x1f, 0x1a, 0x18, 0x0a, 0x0f, 0x06, 0x45, 0x87,
			0x32, 0x0a, 0x07, 0x02, 0x07, 0x05, 0x00);
	lcdreg_writereg(lcdreg, ILI9341_NGAMCTRL,
			0x00, 0x25, 0x27, 0x05, 0x10, 0x09, 0x3a, 0x78,
			0x4d, 0x05, 0x18, 0x0d, 0x38, 0x3a, 0x1f);

	/* DDRAM */
	lcdreg_writereg(lcdreg, ILI9341_ETMOD, 0x07);

	/* Display */
	lcdreg_writereg(lcdreg, ILI9341_DISCTRL, 0x0a, 0x82, 0x27, 0x00);

	lcdreg_writereg(lcdreg, ILI9341_SLPOUT);
	msleep(100);
	lcdreg_writereg(lcdreg, ILI9341_DISPON);
	msleep(100);
 
 	if (lcdreg_is_readable(lcdreg))
		mipi_dbi_check_diagnostics(lcdreg);


	if (lcdreg_is_readable(lcdreg)) {
		u32 val = 0;
		int i;

		for (i = 0x0A; i < 0x10; i++)
			lcdreg_readreg_buf32(lcdreg, i, &val, 1);

		lcdreg_readreg_buf32(lcdreg, 0x04, &val, 3);
		lcdreg_readreg_buf32(lcdreg, 0x09, &val, 4);
	}

	return 0;
 }

static int mi0283qt_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	static struct lcdreg *lcdreg;
	struct fbdbi_display *display;
	enum lcdreg_spi_mode mode;
	struct mipi_dbi_config mipicfg = {
		.xres = 240,
		.yres = 320,
		.addr_mode0 = ILI9341_MADCTL_MX,
		.addr_mode90 = ILI9341_MADCTL_MV | ILI9341_MADCTL_MY |
			       ILI9341_MADCTL_MX,
		.addr_mode180 = ILI9341_MADCTL_MY,
		.addr_mode270 = ILI9341_MADCTL_MV,
		.bgr = true,
	};

	pr_info("%s()\n", __func__);

	if (of_find_property(dev->of_node, "dc-gpios", NULL))
		mode = LCDREG_SPI_4WIRE;
	else
		mode = LCDREG_SPI_3WIRE;

	lcdreg = devm_lcdreg_spi_init_dt(spi, mode);
	if (IS_ERR(lcdreg))
		return PTR_ERR(lcdreg);

	lcdreg->quirks |= LCDREG_INDEX0_ON_READ;
#ifdef __LITTLE_ENDIAN
	lcdreg->little_endian = true;
#endif
	lcdreg->readable = of_property_read_bool(dev->of_node, "readable");

	mipicfg.format = fbdbi_of_format(dev, FBDBI_FORMAT_RGB565);
	display = devm_mipi_dbi_init(lcdreg, &mipicfg);
	if (IS_ERR(display))
		return PTR_ERR(display);

	display->poweron = mi0283qt_poweron;

	return devm_fbdbi_register_dt(dev, display);
}

static const struct of_device_id mi0283qt_ids[] = {
        { .compatible = "mi,mi0283qt" },
        {},
};
MODULE_DEVICE_TABLE(of, mi0283qt_ids);

static struct spi_driver mi0283qt_spi_driver = {
	.driver = {
		.name   = "mi0283qtfb",
		.owner  = THIS_MODULE,
                .of_match_table = mi0283qt_ids,
	},
	.probe  = mi0283qt_probe,
};
module_spi_driver(mi0283qt_spi_driver);

MODULE_ALIAS("spi:mi0283qt");

MODULE_DESCRIPTION("Multi-Inno MI0283QT Framebuffer Driver");
MODULE_AUTHOR("Noralf Tronnes");
MODULE_LICENSE("GPL");
