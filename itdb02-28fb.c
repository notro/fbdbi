/*
Documentation/devicetree/bindings/vendor-prefixes.txt
itead	ITEAD Intelligent Systems Co.Ltd
*/

/*
Documentation/devicetree/bindings/video/itead,itdb02-28.txt
*/


/*
 *
 * ITDB02-2.8 display by Itead Studios
 * ILI9325 controller
 *
 * Product page: http://imall.iteadstudio.com/itdb02-2-8.html
 */




#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>


#include <linux/device.h>

#include "core/lcdreg.h"
#include "core/fbdbi.h"
#include "ili9320.h"


/*

Verify that this configuration is within the Voltage limits
Display module configuration: Vcc = IOVcc = Vci = 3.3V

 Voltages
----------
Vci                                =   3.3
Vci1           =  Vci * 0.80       =   2.64
DDVDH          =  Vci1 * 2         =   5.28
VCL            = -Vci1             =  -2.64
VREG1OUT       =  Vci * 1.85       =   4.88
VCOMH          =  VREG1OUT * 0.735 =   3.59
VCOM amplitude =  VREG1OUT * 0.98  =   4.79
VGH            =  Vci * 4          =  13.2
VGL            = -Vci * 4          = -13.2

 Limits
--------
Power supplies
1.65 < IOVcc < 3.30   =>  1.65 < 3.3 < 3.30
2.40 < Vcc   < 3.30   =>  2.40 < 3.3 < 3.30
2.50 < Vci   < 3.30   =>  2.50 < 3.3 < 3.30
Source/VCOM power supply voltage
 4.50 < DDVDH < 6.0   =>  4.50 <  5.28 <  6.0
-3.0  < VCL   < -2.0  =>  -3.0 < -2.64 < -2.0
VCI - VCL < 6.0       =>  5.94 < 6.0
Gate driver output voltage
 10  < VGH   < 20     =>   10 <  13.2  < 20
-15  < VGL   < -5     =>  -15 < -13.2  < -5
VGH - VGL < 32        =>   26.4 < 32
VCOM driver output voltage
VCOMH - VCOML < 6.0   =>  4.79 < 6.0

*/


static int itdb02_28_poweron(struct fbdbi_display *display)
{
	struct lcdreg *lcdreg = display->lcdreg;
	u16 bt = 6, vc = 0b011, vrh = 0b1101, vdv = 0b10010, vcm = 0b001010;
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
		ili9320_check_driver_code(lcdreg, 0x9325);

	bt &= 0b111;
	vc &= 0b111;
	vrh &= 0b1111;
	vdv &= 0b11111;
	vcm &= 0b111111;

	/* Initialization sequence from ILI9325 Application Notes */

	/* ----------- Start Initial Sequence ----------- */
	ret = lcdreg_writereg(lcdreg, 0x00E3, 0x3008); /* Set internal timing */
	if (ret) {
		dev_err(lcdreg->dev,
			"Initialization failed: %d\n", ret);
		return ret;
	}
	lcdreg_writereg(lcdreg, 0x00E7, 0x0012); /* Set internal timing */
	lcdreg_writereg(lcdreg, 0x00EF, 0x1231); /* Set internal timing */
	lcdreg_writereg(lcdreg, 0x0001, 0x0100); /* set SS and SM bit */
	lcdreg_writereg(lcdreg, 0x0002, 0x0700); /* set 1 line inversion */
	lcdreg_writereg(lcdreg, 0x0004, 0x0000); /* Resize register */
	lcdreg_writereg(lcdreg, 0x0008, 0x0207); /* set the back porch and front porch */
	lcdreg_writereg(lcdreg, 0x0009, 0x0000); /* set non-display area refresh cycle */
	lcdreg_writereg(lcdreg, 0x000A, 0x0000); /* FMARK function */
	lcdreg_writereg(lcdreg, 0x000C, 0x0000); /* RGB interface setting */
	lcdreg_writereg(lcdreg, 0x000D, 0x0000); /* Frame marker Position */
	lcdreg_writereg(lcdreg, 0x000F, 0x0000); /* RGB interface polarity */

	/* ----------- Power On sequence ----------- */
	lcdreg_writereg(lcdreg, 0x0010, 0x0000); /* SAP, BT[3:0], AP, DSTB, SLP, STB */
	lcdreg_writereg(lcdreg, 0x0011, 0x0007); /* DC1[2:0], DC0[2:0], VC[2:0] */
	lcdreg_writereg(lcdreg, 0x0012, 0x0000); /* VREG1OUT voltage */
	lcdreg_writereg(lcdreg, 0x0013, 0x0000); /* VDV[4:0] for VCOM amplitude */
	msleep(200); /* Dis-charge capacitor power voltage */
	lcdreg_writereg(lcdreg, 0x0010, /* SAP, BT[3:0], AP, DSTB, SLP, STB */
		(1 << 12) | (bt << 8) | (1 << 7) | (0b001 << 4));
	lcdreg_writereg(lcdreg, 0x0011, 0x220 | vc); /* DC1[2:0], DC0[2:0], VC[2:0] */
	msleep(50);
	lcdreg_writereg(lcdreg, 0x0012, vrh); /* Internal reference voltage= Vci; */
	msleep(50);
	lcdreg_writereg(lcdreg, 0x0013, vdv << 8); /* Set VDV[4:0] for VCOM amplitude */
	lcdreg_writereg(lcdreg, 0x0029, vcm); /* Set VCM[5:0] for VCOMH */
	lcdreg_writereg(lcdreg, 0x002B, 0x000C); /* Set Frame Rate */
	msleep(50);
	lcdreg_writereg(lcdreg, 0x0020, 0x0000); /* GRAM horizontal Address */
	lcdreg_writereg(lcdreg, 0x0021, 0x0000); /* GRAM Vertical Address */

	/*------------------ Set GRAM area --------------- */
	lcdreg_writereg(lcdreg, 0x0050, 0x0000); /* Horizontal GRAM Start Address */
	lcdreg_writereg(lcdreg, 0x0051, 0x00EF); /* Horizontal GRAM End Address */
	lcdreg_writereg(lcdreg, 0x0052, 0x0000); /* Vertical GRAM Start Address */
	lcdreg_writereg(lcdreg, 0x0053, 0x013F); /* Vertical GRAM Start Address */
	lcdreg_writereg(lcdreg, 0x0060, 0xA700); /* Gate Scan Line */
	lcdreg_writereg(lcdreg, 0x0061, 0x0001); /* NDL,VLE, REV */
	lcdreg_writereg(lcdreg, 0x006A, 0x0000); /* set scrolling line */

	/*-------------- Partial Display Control --------- */
	lcdreg_writereg(lcdreg, 0x0080, 0x0000);
	lcdreg_writereg(lcdreg, 0x0081, 0x0000);
	lcdreg_writereg(lcdreg, 0x0082, 0x0000);
	lcdreg_writereg(lcdreg, 0x0083, 0x0000);
	lcdreg_writereg(lcdreg, 0x0084, 0x0000);
	lcdreg_writereg(lcdreg, 0x0085, 0x0000);

	/*-------------- Panel Control ------------------- */
	lcdreg_writereg(lcdreg, 0x0090, 0x0010);
	lcdreg_writereg(lcdreg, 0x0092, 0x0600);
	lcdreg_writereg(lcdreg, 0x0007, 0x0133); /* 262K color and display ON */

	return 0;
}

static int itdb02_28_probe_common(struct lcdreg *lcdreg)
{
	struct device *dev = lcdreg->dev;
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
	display = devm_ili9320_init(lcdreg, &cfg);
	if (IS_ERR(display))
		return PTR_ERR(display);

	display->poweron = itdb02_28_poweron;

	return devm_fbdbi_register_dt(dev, display);
}

static int itdb02_28_i80_probe(struct i80_device *i80)
{
	struct device *dev = &i80->dev;
	struct lcdreg *lcdreg;
	struct lcdreg_i80_config config = {};
	int ret;

	pr_debug("%s()\n", __func__);
	ret = devm_lcdreg_i80_parse_dt(dev, &config);
	if (ret)
		return ret;

	lcdreg = devm_lcdreg_i80_init(i80, &config);
	if (IS_ERR(lcdreg))
		return PTR_ERR(lcdreg);

	return itdb02_28_probe_common(lcdreg);
}

static const struct of_device_id dt_ids[] = {
        { .compatible = "itead,itdb02-28" },
        {},
};
MODULE_DEVICE_TABLE(of, dt_ids);

static struct i80_driver itdb02_28_i80_driver = {
	.driver = {
		.name   = "itdb02_28fb",
		.owner  = THIS_MODULE,
                .of_match_table = of_match_ptr(dt_ids),
	},
	.probe  = itdb02_28_i80_probe,
};
module_i80_driver(itdb02_28_i80_driver);

/* MODULE_DESCRIPTION(""); */
MODULE_AUTHOR("Noralf Tronnes");
MODULE_LICENSE("GPL");
