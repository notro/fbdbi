
#ifndef __LINUX_ST7735_H
#define __LINUX_ST7735_H

#include "core/lcdreg.h"
#include "core/fbdbi.h"


#define ST7735_NOP        0x00
#define ST7735_SWRESET    0x01
#define ST7735_RDDID      0x04
#define ST7735_RDDST      0x09
#define ST7735_RDDPM      0x0A
#define ST7735_RDDMADCTL  0x0B
#define ST7735_RDDCOLMOD  0x0C
#define ST7735_RDDIM      0x0D
#define ST7735_RDDSM      0x0E
#define ST7735_SLPIN      0x10
#define ST7735_SLPOUT     0x11
#define ST7735_PTLON      0x12
#define ST7735_NORON      0x13
#define ST7735_INVOFF     0x20
#define ST7735_INVON      0x21
#define ST7735_GAMSET     0x26
#define ST7735_DISPOFF    0x28
#define ST7735_DISPON     0x29
#define ST7735_CASET      0x2A
#define ST7735_RASET      0x2B
#define ST7735_RAMWR      0x2C
#define ST7735_RGBSET     0x2D
#define ST7735_RAMRD      0x2E
#define ST7735_PTLAR      0x30
#define ST7735_TEOFF      0x34
#define ST7735_TEON       0x35
#define ST7735_MADCTL     0x36
#define ST7735_IDMOFF     0x38
#define ST7735_IDMON      0x39
#define ST7735_COLMOD     0x3A
#define ST7735_RDID1      0xDA
#define ST7735_RDID2      0xDB
#define ST7735_RDID3      0xDC

#define ST7735_MADCTL_MV  BIT(5)
#define ST7735_MADCTL_MX  BIT(6)
#define ST7735_MADCTL_MY  BIT(7)


int st7735_init(struct fbdbi_display *display, struct lcdreg *lcdreg);



#endif /* __LINUX_ST7735_H */
