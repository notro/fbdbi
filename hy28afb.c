

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/spi/spi.h>

//#include <linux/fb.h>

#include "core/lcdreg.h"
#include "core/fbdbi.h"
#include "ili9320.h"



static int hy28a_poweron(struct fbdbi_display *display)
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

	if (lcdreg_is_readable(lcdreg))
		ili9320_check_driver_code(lcdreg, 0x9320);

	/* Initialization sequence from ILI9320 Application Notes */

	/* *********** Start Initial Sequence ********* */
	lcdreg_writereg(lcdreg, 0x00E5, 0x8000); /* Set the Vcore voltage and this setting is must. */
	lcdreg_writereg(lcdreg, 0x0000, 0x0001); /* Start internal OSC. */
	lcdreg_writereg(lcdreg, 0x0001, 0x0100); /* set SS and SM bit */
	lcdreg_writereg(lcdreg, 0x0002, 0x0700); /* set 1 line inversion */
	lcdreg_writereg(lcdreg, 0x0004, 0x0000); /* Resize register */
	lcdreg_writereg(lcdreg, 0x0008, 0x0202); /* set the back and front porch */
	lcdreg_writereg(lcdreg, 0x0009, 0x0000); /* set non-display area refresh cycle */
	lcdreg_writereg(lcdreg, 0x000A, 0x0000); /* FMARK function */
	lcdreg_writereg(lcdreg, 0x000C, 0x0000); /* RGB interface setting */
	lcdreg_writereg(lcdreg, 0x000D, 0x0000); /* Frame marker Position */
	lcdreg_writereg(lcdreg, 0x000F, 0x0000); /* RGB interface polarity */

	/* ***********Power On sequence *************** */
	lcdreg_writereg(lcdreg, 0x0010, 0x0000); /* SAP, BT[3:0], AP, DSTB, SLP, STB */
	lcdreg_writereg(lcdreg, 0x0011, 0x0007); /* DC1[2:0], DC0[2:0], VC[2:0] */
	lcdreg_writereg(lcdreg, 0x0012, 0x0000); /* VREG1OUT voltage */
	lcdreg_writereg(lcdreg, 0x0013, 0x0000); /* VDV[4:0] for VCOM amplitude */
	msleep(200); /* Dis-charge capacitor power voltage */
	lcdreg_writereg(lcdreg, 0x0010, 0x17B0); /* SAP, BT[3:0], AP, DSTB, SLP, STB */
	lcdreg_writereg(lcdreg, 0x0011, 0x0031); /* R11h=0x0031 at VCI=3.3V DC1[2:0], DC0[2:0], VC[2:0] */
	msleep(50);
	lcdreg_writereg(lcdreg, 0x0012, 0x0138); /* R12h=0x0138 at VCI=3.3V VREG1OUT voltage */
	msleep(50);
	lcdreg_writereg(lcdreg, 0x0013, 0x1800); /* R13h=0x1800 at VCI=3.3V VDV[4:0] for VCOM amplitude */
	lcdreg_writereg(lcdreg, 0x0029, 0x0008); /* R29h=0x0008 at VCI=3.3V VCM[4:0] for VCOMH */
	msleep(50);
	lcdreg_writereg(lcdreg, 0x0020, 0x0000); /* GRAM horizontal Address */
	lcdreg_writereg(lcdreg, 0x0021, 0x0000); /* GRAM Vertical Address */

	/* ------------------ Set GRAM area --------------- */
lcdreg_writereg(lcdreg, 0x0050, 0); /* Horizontal GRAM Start Address */
lcdreg_writereg(lcdreg, 0x0051, 239); /* Horizontal GRAM End Address */
lcdreg_writereg(lcdreg, 0x0052, 0); /* Vertical GRAM Start Address */
lcdreg_writereg(lcdreg, 0x0053, 319); /* Vertical GRAM Start Address */
	lcdreg_writereg(lcdreg, 0x0060, 0x2700); /* Gate Scan Line */
	lcdreg_writereg(lcdreg, 0x0061, 0x0001); /* NDL,VLE, REV */
	lcdreg_writereg(lcdreg, 0x006A, 0x0000); /* set scrolling line */

	/* -------------- Partial Display Control --------- */
	lcdreg_writereg(lcdreg, 0x0080, 0x0000);
	lcdreg_writereg(lcdreg, 0x0081, 0x0000);
	lcdreg_writereg(lcdreg, 0x0082, 0x0000);
	lcdreg_writereg(lcdreg, 0x0083, 0x0000);
	lcdreg_writereg(lcdreg, 0x0084, 0x0000);
	lcdreg_writereg(lcdreg, 0x0085, 0x0000);

	/* -------------- Panel Control ------------------- */
	lcdreg_writereg(lcdreg, 0x0090, 0x0010);
	lcdreg_writereg(lcdreg, 0x0092, 0x0000);
	lcdreg_writereg(lcdreg, 0x0093, 0x0003);
	lcdreg_writereg(lcdreg, 0x0095, 0x0110);
	lcdreg_writereg(lcdreg, 0x0097, 0x0000);
	lcdreg_writereg(lcdreg, 0x0098, 0x0000);
	lcdreg_writereg(lcdreg, 0x0007, 0x0173); /* 262K color and display ON */

//lcdreg_writereg(lcdreg, 0x3, (lcdreg->bgr << 12) | 0x30);
//lcdreg_writereg(lcdreg, 0x3, (1 << 12) | 0x30);

	return 0;
}


static int fbdbi_driver_probe_spi(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	static struct lcdreg *lcdreg;
	struct fbdbi_display *display;
	struct ili9320_config cfg = {
//		.addr_mode0 = ILI9341_MADCTL_MX,
//		.addr_mode90 = ILI9341_MADCTL_MV | ILI9341_MADCTL_MY |
//			       ILI9341_MADCTL_MX,
//		.addr_mode180 = ILI9341_MADCTL_MY,
//		.addr_mode270 = ILI9341_MADCTL_MV,

		.addr_mode0 = 0x30,
		.addr_mode90 = 0x18,
		.addr_mode180 = 0x00,
		.addr_mode270 = 0x28,
		.bgr = true,
	};

	pr_info("%s()\n", __func__);

	lcdreg = devm_lcdreg_spi_init_dt(spi, LCDREG_SPI_STARTBYTE1);
	if (IS_ERR(lcdreg))
		return PTR_ERR(lcdreg);

	lcdreg->readable = true;

	display = devm_ili9320_init(lcdreg, &cfg);
	if (IS_ERR(display))
		return PTR_ERR(display);

	display->poweron = hy28a_poweron;

	return devm_fbdbi_register_dt(dev, display);
}

static const struct of_device_id dt_ids[] = {
        { .compatible = "hy28a" },
        {},
};
MODULE_DEVICE_TABLE(of, dt_ids);

static struct spi_driver hy28a_spi_driver = {
	.driver = {
		.name   = "hy28afb",
		.owner  = THIS_MODULE,
                .of_match_table = of_match_ptr(dt_ids),
	},
	.probe  = fbdbi_driver_probe_spi,
};
module_spi_driver(hy28a_spi_driver);

MODULE_ALIAS("spi:hy28a");

//MODULE_DESCRIPTION("Custom FB driver for tinylcd.com display");
MODULE_AUTHOR("Noralf Tronnes");
MODULE_LICENSE("GPL");
