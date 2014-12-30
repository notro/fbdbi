
#ifndef __LINUX_ILI9320_H
#define __LINUX_ILI9320_H

#include "core/lcdreg.h"
#include "core/fbdbi.h"

#define ILI9320_DRIVER_CODE_READ                   0x00
#define ILI9320_START_OSCILLATION                  0x00
#define ILI9320_DRIVER_OUTPUT_CONTROL_1            0x01
#define ILI9320_LCD_DRIVING_CONTROL                0x02
#define ILI9320_ENTRY_MODE                         0x03
#define ILI9320_RESIZE_CONTROL                     0x04
#define ILI9320_DISPLAY_CONTROL_1                  0x07
#define ILI9320_DISPLAY_CONTROL_2                  0x08
#define ILI9320_DISPLAY_CONTROL_3                  0x09
#define ILI9320_DISPLAY_CONTROL_4                  0x0A
#define ILI9320_RGB_DISPLAY_INTERFACE_CONTROL_1    0x0C
#define ILI9320_FRAME_MAKER_POSITION               0x0D
#define ILI9320_RGB_DISPLAY_INTERFACE_CONTROL_2    0x0F
#define ILI9320_POWER_CONTROL_1                    0x10
#define ILI9320_POWER_CONTROL_2                    0x11
#define ILI9320_POWER_CONTROL_3                    0x12
#define ILI9320_POWER_CONTROL_4                    0x13
#define ILI9320_HORIZONTAL_GRAM_ADDRESS_SET        0x20
#define ILI9320_VERTICAL_GRAM_ADDRESS_SET          0x21
#define ILI9320_WRITE_DATA_TO_GRAM                 0x22
#define ILI9320_POWER_CONTROL_7                    0x29
#define ILI9320_FRAME_RATE_AND_COLOR_CONTROL       0x2B
#define ILI9320_GAMMA_CONTROL_1                    0x30
#define ILI9320_GAMMA_CONTROL_2                    0x31
#define ILI9320_GAMMA_CONTROL_3                    0x32
#define ILI9320_GAMMA_CONTROL_4                    0x35
#define ILI9320_GAMMA_CONTROL_5                    0x36
#define ILI9320_GAMMA_CONTROL_6                    0x37
#define ILI9320_GAMMA_CONTROL_7                    0x38
#define ILI9320_GAMMA_CONTROL_8                    0x39
#define ILI9320_GAMMA_CONTROL_9                    0x3C
#define ILI9320_GAMMA_CONTROL_10                   0x3D
#define ILI9320_HORIZONTAL_ADDRESS_START_POSITION  0x50
#define ILI9320_HORIZONTAL_ADDRESS_END_POSITION    0x51
#define ILI9320_VERTICAL_ADDRESS_START_POSITION    0x52
#define ILI9320_VERTICAL_ADDRESS_END_POSITION      0x53
#define ILI9320_DRIVER_OUTPUT_CONTROL_2            0x60
#define ILI9320_BASE_IMAGE_DISPLAY_CONTROL         0x61
#define ILI9320_VERTICAL_SCROLL_CONTROL            0x6A
#define ILI9320_PARTIAL_IMAGE_1_DISPLAY_POSITION   0x80
#define ILI9320_PARTIAL_IMAGE_1_AREA_START_LINE    0x81
#define ILI9320_PARTIAL_IMAGE_1_AREA_END_LINE      0x82
#define ILI9320_PARTIAL_IMAGE_2_DISPLAY_POSITION   0x83
#define ILI9320_PARTIAL_IMAGE_2_AREA_START_LINE    0x84
#define ILI9320_PARTIAL_IMAGE_2_AREA_END_LINE      0x85
#define ILI9320_PANEL_INTERFACE_CONTROL_1          0x90
#define ILI9320_PANEL_INTERFACE_CONTROL_2          0x92
#define ILI9320_PANEL_INTERFACE_CONTROL_3          0x93
#define ILI9320_PANEL_INTERFACE_CONTROL_4          0x95
#define ILI9320_PANEL_INTERFACE_CONTROL_5          0x97
#define ILI9320_PANEL_INTERFACE_CONTROL_6          0x98


struct ili9320_config {
	u32 xres;
	u32 yres;
	unsigned addr_mode0;
	unsigned addr_mode90;
	unsigned addr_mode180;
	unsigned addr_mode270;
	bool bgr;
};

struct fbdbi_display *devm_ili9320_init(struct lcdreg *lcdreg,
					struct ili9320_config *config);

extern bool ili9320_check_driver_code(struct lcdreg *lcdreg, u16 expected);

#endif /* __LINUX_ILI9320_H */
