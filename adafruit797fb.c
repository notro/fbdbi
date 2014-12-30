/*
Documentation/devicetree/bindings/vendor-prefixes.txt

ada 	Adafruit

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
			- present: IM=x110 4-wire 9-bit data serial interface
- rotate		Display rotation in degrees counter clockwise
- backlight		phandle of the backlight device attached to the panel
- readable		It is possible to read from the controller registers:
			- IM=11xx: MISO is connected
			- IM=01xx: SPI master driver supports spi-3wire (SDA)

- bits-per-pixel:	<16> for RGB565 (default),
			<18> for RGB666 (framebuffer is RG888)


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
 * Adafruit 2.2" (#797) Framebuffer Driver
 *
 * Copyright 2014 Noralf Tronnes
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/spi/spi.h>


#include "core/lcdreg.h"
#include "core/fbdbi.h"
#include "hx8340.h"
#include "mipi-dbi.h"


static int adafruit797_poweron(struct fbdbi_display *display)
{
	struct lcdreg *lcdreg = display->lcdreg;
	int ret;

	pr_info("%s()\n", __func__);

	if (display->power_supply) {
		ret = regulator_enable(display->power_supply);
		if (ret < 0) {
			dev_err(lcdreg->dev,
				"failed to enable power supply: %d\n", ret);
			return ret;
		}
	}

	lcdreg_reset(lcdreg);

	/* BTL221722-276L startup sequence, from datasheet */

	/* SETEXTCOM: Set extended command set (C1h)
	   This command is used to set extended command set access enable.
	   Enable: After command (C1h), must write: ffh,83h,40h */
	lcdreg_writereg(lcdreg, HX8340_SETEXTCMD, 0xFF, 0x83, 0x40);

	/* Sleep out
	   This command turns off sleep mode.
	   In this mode the DC/DC converter is enabled, Internal oscillator
	   is started, and panel scanning is started. */
	lcdreg_writereg(lcdreg, HX8340_SLPOUT);
	msleep(150);

	/* Undoc'd register? */
	lcdreg_writereg(lcdreg, 0xCA, 0x70, 0x00, 0xD9);

	/* SETOSC: Set Internal Oscillator (B0h)
	   This command is used to set internal oscillator related settings */
	/*	OSC_EN: Enable internal oscillator */
	/*	Internal oscillator frequency: 125% x 2.52MHz */
	lcdreg_writereg(lcdreg, HX8340_SETOSC, 0x01, 0x11);

	/* Drive ability setting */
	lcdreg_writereg(lcdreg, 0xC9, 0x90, 0x49, 0x10, 0x28, 0x28, 0x10, 0x00, 0x06);
	msleep(20);

	/* SETPWCTR5: Set Power Control 5(B5h)
	   This command is used to set VCOM Low and VCOM High Voltage */
	/* VCOMH 0110101 :  3.925 */
	/* VCOML 0100000 : -1.700 */
	/* 45h=69  VCOMH: "VMH" + 5d   VCOML: "VMH" + 5d */
	lcdreg_writereg(lcdreg, HX8340_SETPWCTR5, 0x35, 0x20, 0x45);

	/* SETPWCTR4: Set Power Control 4(B4h)
		VRH[4:0]:	Specify the VREG1 voltage adjusting.
				VREG1 voltage is for gamma voltage setting.
		BT[2:0]:	Switch the output factor of step-up circuit 2
				for VGH and VGL voltage generation. */
	lcdreg_writereg(lcdreg, HX8340_SETPWCTR4, 0x33, 0x25, 0x4C);
	msleep(10);

	/* Interface Pixel Format (3Ah)
	   This command is used to define the format of RGB picture data,
	   which is to be transfer via the system and RGB interface. */
	/* RGB interface: 16 Bit/Pixel	*/
	lcdreg_writereg(lcdreg, HX8340_COLMOD, 0x05);

	/* Display on (29h)
	   This command is used to recover from DISPLAY OFF mode.
	   Output from the Frame Memory is enabled. */
	lcdreg_writereg(lcdreg, HX8340_DISPON);
	msleep(10);

	return 0;
}

static int fbdbi_driver_probe_spi(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	static struct lcdreg *lcdreg;
	struct fbdbi_display *display;
	struct mipi_dbi_config mipicfg = {
		.xres = 176,
		.yres = 220,
		.addr_mode0 = 0,
		.addr_mode90 = HX8340_MADCTL_MV | HX8340_MADCTL_MY,
		.addr_mode180 = HX8340_MADCTL_MX | HX8340_MADCTL_MY,
		.addr_mode270 = HX8340_MADCTL_MX | HX8340_MADCTL_MV,
		.bgr = true,
	};

	pr_info("%s()\n", __func__);

	lcdreg = devm_lcdreg_spi_init_dt(spi, LCDREG_SPI_3WIRE);
	if (IS_ERR(lcdreg))
		return PTR_ERR(lcdreg);

	display = devm_mipi_dbi_init(lcdreg, &mipicfg);
	if (IS_ERR(display))
		return PTR_ERR(display);

	display->poweron = adafruit797_poweron;

	return devm_fbdbi_register_dt(dev, display);
}

static const struct of_device_id dt_ids[] = {
        { .compatible = "adafruit797" },
        {},
};
MODULE_DEVICE_TABLE(of, dt_ids);


static struct spi_driver adafruit22_spi_driver = {
	.driver = {
		.name   = "adafruit797fb",
		.owner  = THIS_MODULE,
                .of_match_table = of_match_ptr(dt_ids),
	},
	.probe  = fbdbi_driver_probe_spi,
};
module_spi_driver(adafruit22_spi_driver);

MODULE_ALIAS("spi:adafruit797");

MODULE_DESCRIPTION("Adafruit 2.2\" (#797) Framebuffer Driver");
MODULE_AUTHOR("Noralf Tronnes");
MODULE_LICENSE("GPL");
