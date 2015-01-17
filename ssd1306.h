/*
 * SSD1306 LCD controller support
 *
 * Copyright 2015 Noralf Tronnes
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __LINUX_SSD1306_H
#define __LINUX_SSD1306_H

#include "core/lcdreg.h"
#include "core/fbdbi.h"

#define SSD1306_ADDRESS_MODE        0x20
#define SSD1306_COL_RANGE           0x21
#define SSD1306_PAGE_RANGE          0x22
#define SSD1306_DISPLAY_START_LINE  0x40
#define SSD1306_CONTRAST            0x81
#define SSD1306_CHARGE_PUMP         0x8d
#define SSD1306_SEG_REMAP_OFF       0xa0
#define SSD1306_SEG_REMAP_ON        0xa1
#define SSD1306_RESUME_TO_RAM       0xa4
#define SSD1306_ENTIRE_DISPLAY_ON   0xa5
#define SSD1306_NORMAL_DISPLAY      0xa6
#define SSD1306_INVERSE_DISPLAY     0xa7
#define SSD1306_MULTIPLEX_RATIO     0xa8
#define SSD1306_DISPLAY_OFF         0xae
#define SSD1306_DISPLAY_ON          0xaf
#define SSD1306_START_PAGE_ADDRESS  0xb0
#define SSD1306_COM_SCAN_NORMAL     0xc0
#define SSD1306_COM_SCAN_REMAP      0xc8
#define SSD1306_DISPLAY_OFFSET      0xd3
#define SSD1306_CLOCK_FREQ          0xd5
#define SSD1306_PRECHARGE_PERIOD    0xd9
#define SSD1306_COM_PINS_CONFIG     0xda
#define SSD1306_VCOMH               0xdb

struct ssd1306_config {
	u32 xres;
	u32 yres;
};

struct fbdbi_display *devm_ssd1306_init(struct lcdreg *lcdreg,
					 struct ssd1306_config *config);
extern bool ssd1306_check_status(struct lcdreg *reg, bool display_is_on);

#endif /* __LINUX_SSD1306_H */
