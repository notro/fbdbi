/*

Product page: http://cgi.ebay.com/ws/eBayISAPI.dll?ViewItem&item=181283191283

4.3" TFT LCD Module Display Touch Panel + SSD1963 For 51/AVR/STM32

Specification:
- SSD1963 touch panel controller  
- voltage:2.8-3.3V
- pcb adapter for lcd
- one sd card socket
- 2*20 Pin 2.54mm double row pin header interface for connecting MCU.
- can be driverd by 8051 / AVR / PIC and other low power controllers.
- Resolution: 480*272 Dots

*/

/*


SSD1963 dtasheet Rev 1.1 Jan 2010
http://www.techtoys.com.hk/Components/SSD1963QL9/SSD1963_1.1.pdf

Application Note for SSD1961/2/3
http://www.solomon-systech.com/files/ck/files/SSD1961_2_3_Application_note_v1.7.pdf

*/


#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

//#include <video/mipi_display.h>

#include <linux/backlight.h>
#include <linux/device.h>
#include <linux/fb.h>

#include "core/lcdreg.h"
#include "core/fbdbi.h"
#include "ssd1963.h"



// from 8051 example code

static int ebay181283191283_poweron(struct fbdbi_display *display)
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

	//PLL multiplier, set PLL clock to 120M
	//N=0x36 for 6.5M, 0x23 for 10M crystal
	ret = lcdreg_writereg(lcdreg, 0xE2, 0x2d, 0x02, 0x04);
	if (ret) {
		dev_err(lcdreg->dev,
			"lcdreg_writereg failed: %d\n", ret);
		return ret;
	}

	// PLL enable
	lcdreg_writereg(lcdreg, 0xE0, 0x01);
	msleep(1);
	lcdreg_writereg(lcdreg, 0xE0, 0x03);
	msleep(5);

	// software reset
	lcdreg_writereg(lcdreg, 0x01);
	msleep(5);

	//PLL setting for PCLK, depends on resolution
	lcdreg_writereg(lcdreg, 0xE6, 0x00, 0xff, 0xbe);

	//LCD SPECIFICATION
	lcdreg_writereg(lcdreg, 0xB0, 0x20, 0x00, 0x01, 0xDF, 0x01, 0x0F, 0x00);
	msleep(5);

	//HSYNC
	lcdreg_writereg(lcdreg, 0xB4, 0x02, 0x13, 0x00, 0x2B, 0x0A, 0x00, 0x08, 0x00);

	//VSYNC
	lcdreg_writereg(lcdreg, 0xB6, 0x01, 0x20, 0x00, 0x0C, 0x0A, 0x00, 0x04);

	//rotation
	lcdreg_writereg(lcdreg, 0x36, 0x00);

	//pixel data interface
	lcdreg_writereg(lcdreg, 0xF0, 0x03);
	msleep(5);

	//display on
	lcdreg_writereg(lcdreg, 0x29);

	//set PWM for B/L
	/*
	 * Par1 PWM freq: 06h : PWM signal frequency = PLL clock / (256 * PWMF[7:0]) / 256
	 * Par2 PWM duty cycle: 0xf0:
	 * Par3 C3=0 => PWM controlled by host
	 *      C0=1 => PWM enable
	 * Par4 0xF0  DBC manual brightness
	 * Par5 0x00  DBC minimum brightness
	 * Par6 0x00  Brightness prescaler
	 */
//	lcdreg_writereg(lcdreg, 0xBE, 0x06, 0xf0, 0x01, 0xf0, 0x00, 0x00);
	// Par2: keep backlight turned off
	lcdreg_writereg(lcdreg, 0xBE, 0x06, 0x00, 0x01, 0xf0, 0x00, 0x00);

	/*
	 * Dynamic Backlight Control configuration
	 * A6 = 0 => DBC enable
	 * A5 = 0 => Transition effect disable
	 * A[3:2] = 11 =>  Energy saving selection: Aggressive
	 * A0 = 1 => DBC enable
	 */
	lcdreg_writereg(lcdreg, 0xd0, 0x0d);

	//----------LCD RESET---GPIO0-------------------//
	//GPIO3=input, GPIO[2:0]=output
	//GPIO0 normal
/* FIXME: is this dec correct?
	 * All gpio as input and controlled by host
	 * GPIO0 is used as normal GPIO 
	 */
	lcdreg_writereg(lcdreg, 0xB8, 0x00, 0x01);
	/* all GPIO output 0 */
	lcdreg_writereg(lcdreg, 0xBA, 0x00);

	if (lcdreg_is_readable(lcdreg)) {
		u32 val;
		int i;

		printk("\n\n\n\n");

		for (i = 0x0a; i <= 0x0e; i++)
			lcdreg_readreg_buf32(lcdreg, i, &val, 1);
	}

	return 0;
}







static int ebay181283191283_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct lcdreg *lcdreg;
	struct fbdbi_display *display;
	struct lcdreg_par_config config = {};
	int ret;

	pr_info("%s()\n", __func__);
	ret = devm_lcdreg_par_parse_dt(dev, &config);
	if (ret)
		return ret;

	lcdreg = devm_lcdreg_par_init(pdev, &config);
	if (IS_ERR(lcdreg))
		return PTR_ERR(lcdreg);

	display = devm_kzalloc(dev, sizeof(*display), GFP_KERNEL);
	if (!display)
		return -ENOMEM;

	ret = ssd1963_init(display, lcdreg);
	if (ret)
		return ret;

	display->xres = 272;
	display->yres = 480;
//	display->bgr = true;
	display->poweron = ebay181283191283_poweron;

	ret = devm_fbdbi_init(dev, display);
	if (ret)
		return ret;

//	display->rotate = NULL;
	display->info->var.rotate = fbdbi_of_value(dev, "rotate", 0);

	display->backlight = ssd1963_backlight_register(lcdreg, display->info, 255);

	return devm_fbdbi_register(display);
}

static const struct of_device_id dt_ids[] = {
        { .compatible = "ebay181283191283" },
        {},
};
MODULE_DEVICE_TABLE(of, dt_ids);

static struct platform_driver ebay181283191283_driver = {
	.driver = {
		.name   = "ebay181283191283fb",
		.owner  = THIS_MODULE,
                .of_match_table = of_match_ptr(dt_ids),
	},
	.probe  = ebay181283191283_probe,
};
module_platform_driver(ebay181283191283_driver);

//MODULE_ALIAS("spi:" DRVNAME);
//MODULE_ALIAS("spi:tinylcd");

//MODULE_DESCRIPTION("Custom FB driver for tinylcd.com display");
MODULE_AUTHOR("Noralf Tronnes");
MODULE_LICENSE("GPL");
