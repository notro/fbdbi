
#ifndef __LINUX_HX8340_H
#define __LINUX_HX8340_H


#define HX8340_NOP         0x00
#define HX8340_SWRESET     0x01
#define HX8340_RDDIDIF     0x04
#define HX8340_RDDST       0x09
#define HX8340_RDDPM       0x0A
#define HX8340_RDDMADCTL   0x0B
#define HX8340_RDDCOLMOD   0x0C
#define HX8340_RDDIM       0x0D
#define HX8340_RDDSM       0x0E
#define HX8340_RDDSDR      0x0F

#define HX8340_SLPIN       0x10
#define HX8340_SLPOUT      0x11
#define HX8340_PTLON       0x12
#define HX8340_NORON       0x13

#define HX8340_INVOFF      0x20
#define HX8340_INVON       0x21
#define HX8340_GAMSET      0x26
#define HX8340_DISPOFF     0x28
#define HX8340_DISPON      0x29
#define HX8340_CASET       0x2A
#define HX8340_PASET       0x2B
#define HX8340_RAMWR       0x2C
#define HX8340_RAMRD       0x2E
#define HX8340_RGBSET      0x2D

#define HX8340_PLTAR       0x30
#define HX8340_VSCRDEF     0x33
#define HX8340_TEOFF       0x34
#define HX8340_TEON        0x35
#define HX8340_MADCTL      0x36
#define HX8340_VSCRSADD    0x37
#define HX8340_IDMOFF      0x38
#define HX8340_IDMON       0x39
#define HX8340_COLMOD      0x3A

#define HX8340_SETOSC      0xB0
#define HX8340_SETPWCTR1   0xB1
#define HX8340_SETPWCTR2   0xB2
#define HX8340_SETPWCTR3   0xB3
#define HX8340_SETPWCTR4   0xB4
#define HX8340_SETPWCTR5   0xB5
#define HX8340_SETDISCTRL  0xB6
#define HX8340_SETFRMCTRL  0xB7
#define HX8340_SETDISCYCC  0xB8
#define HX8340_SETINVCTRL  0xB9
#define HX8340_RGBBPCTR    0xBA
#define HX8340_SETRGBIF    0xBB
#define HX8340_SETDODC     0xBC
#define HX8340_SETINTMODE  0xBD
#define HX8340_SETPANEL    0xBE

#define HX8340_SETONOFF    0xC0
#define HX8340_SETEXTCMD   0xC1
#define HX8340_SETGAMMAP   0xC2
#define HX8340_SETGAMMAN   0xC3
#define HX8340_SETOTP      0xC7

#define HX8340_RDID1       0xDA
#define HX8340_RDID2       0xDB
#define HX8340_RDID3       0xDC

#define HX8340_MADCTL_MV   BIT(5)
#define HX8340_MADCTL_MX   BIT(6)
#define HX8340_MADCTL_MY   BIT(7)

#endif /* __LINUX_HX8340_H */
