
/*

Not readable on the Pi at least: SDA

http://www.sainsmart.com/sainsmart-1-8-spi-lcd-module-with-microsd-led-backlight-for-arduino-mega-atmel-atmega.html

Signal Definition:
  GND : Power Ground
  VCC : 5V power input
  CS : Chipselect for LCD,
  SDA : LCD Data for SPI
  SCL : SCLK for TFT Clock
  RS/DC : Command/Data Selection
  RESET : LCD controller reset, active low
  CS (SD-CS) : Chipselect for TF Card,
  CLK (SD-Clock): SPI Clock
  MOSI (SD-DI) : SPI Master out Slave in
  MISO (SD-DO) : SPI Master in Slave out


 */


#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/spi/spi.h>

#include <linux/device.h>
#include <linux/fb.h>


#include "core/lcdreg.h"
#include "core/fbdbi.h"
#include "st7735.h"
#include "mipi-dbi.h"


static int sainsmart18_poweron(struct fbdbi_display *display)
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

	lcdreg->reset(lcdreg);

	/* SWRESET - Software reset */
	lcdreg_writereg(lcdreg, 0x01);
	msleep(150);

	/* SLPOUT - Sleep out & booster on */
	lcdreg_writereg(lcdreg, 0x11);
	msleep(500);

	/* FRMCTR1 - frame rate control: normal mode
	     frame rate = fosc / (1 x 2 + 40) * (LINE + 2C + 2D) */
	lcdreg_writereg(lcdreg, 0xB1, 0x01, 0x2C, 0x2D);

	/* FRMCTR2 - frame rate control: idle mode
	     frame rate = fosc / (1 x 2 + 40) * (LINE + 2C + 2D) */
	lcdreg_writereg(lcdreg, 0xB2, 0x01, 0x2C, 0x2D);

	/* FRMCTR3 - frame rate control - partial mode
	     dot inversion mode, line inversion mode */
	lcdreg_writereg(lcdreg, 0xB3, 0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D);

	/* INVCTR - display inversion control
	     no inversion */
	lcdreg_writereg(lcdreg, 0xB4, 0x07);

	/* PWCTR1 - Power Control
	     -4.6V, AUTO mode */
	lcdreg_writereg(lcdreg, 0xC0, 0xA2, 0x02, 0x84);

	/* PWCTR2 - Power Control
	     VGH25 = 2.4C VGSEL = -10 VGH = 3 * AVDD */
	lcdreg_writereg(lcdreg, 0xC1, 0xC5);

	/* PWCTR3 - Power Control
	     Opamp current small, Boost frequency */
	lcdreg_writereg(lcdreg, 0xC2, 0x0A, 0x00);

	/* PWCTR4 - Power Control
	     BCLK/2, Opamp current small & Medium low */
	lcdreg_writereg(lcdreg, 0xC3,0x8A,0x2A);

	/* PWCTR5 - Power Control */
	lcdreg_writereg(lcdreg, 0xC4, 0x8A, 0xEE);

	/* VMCTR1 - Power Control */
	lcdreg_writereg(lcdreg, 0xC5, 0x0E);

	/* INVOFF - Display inversion off */
	lcdreg_writereg(lcdreg, 0x20);

	/* COLMOD - Interface pixel format */
	lcdreg_writereg(lcdreg, 0x3A, 0x05);

	/* DISPON - Display On */
	lcdreg_writereg(lcdreg, 0x29);
	msleep(100);

	/* NORON - Partial off (Normal) */
	lcdreg_writereg(lcdreg, 0x13);
	msleep(10);

	return 0;
}


static int fbdbi_driver_probe_spi(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	static struct lcdreg *lcdreg;
	struct fbdbi_display *display;
	struct mipi_dbi_config mipicfg = {
		.xres = 128,
		.yres = 160,
		.addr_mode0 = ST7735_MADCTL_MX | ST7735_MADCTL_MY,
		.addr_mode90 = ST7735_MADCTL_MX | ST7735_MADCTL_MV,
		.addr_mode180 = 0,
		.addr_mode270 = ST7735_MADCTL_MY | ST7735_MADCTL_MV,
	};

	pr_info("%s()\n", __func__);

	lcdreg = devm_lcdreg_spi_init_dt(spi, LCDREG_SPI_4WIRE);
	if (IS_ERR(lcdreg))
		return PTR_ERR(lcdreg);

	display = devm_kzalloc(dev, sizeof(*display), GFP_KERNEL);
	if (!display)
		return -ENOMEM;

	display = devm_mipi_dbi_init(lcdreg, &mipicfg);
	if (IS_ERR(display))
		return PTR_ERR(display);

	display->poweron = sainsmart18_poweron;

	return devm_fbdbi_register_dt(dev, display);
}

//static int fbdbi_driver_remove_spi(struct spi_device *spi)
//{
//	pr_info("%s()\n", __func__);
//	return 0;
//}

static const struct of_device_id dt_ids[] = {
        { .compatible = "sainsmart18" },
        {},
};
MODULE_DEVICE_TABLE(of, dt_ids);


static struct spi_driver fbdbi_driver_spi_driver = {
	.driver = {
		.name   = "sainsmart18fb",
		.owner  = THIS_MODULE,
                .of_match_table = of_match_ptr(dt_ids),
	},
	.probe  = fbdbi_driver_probe_spi,
//	.remove = fbdbi_driver_remove_spi,
};

static int __init fbdbi_driver_module_init(void)
{
	return spi_register_driver(&fbdbi_driver_spi_driver);
}

static void __exit fbdbi_driver_module_exit(void)
{
	spi_unregister_driver(&fbdbi_driver_spi_driver);
}

module_init(fbdbi_driver_module_init);
module_exit(fbdbi_driver_module_exit);

//module_fbdbi_spi_driver("sainsmart18fb", "sainsmart18", sainsmart18_probe);


//MODULE_ALIAS("spi:" DRVNAME);
//MODULE_ALIAS("spi:tinylcd");

//MODULE_DESCRIPTION("Custom FB driver for tinylcd.com display");
MODULE_AUTHOR("Noralf Tronnes");
MODULE_LICENSE("GPL");
