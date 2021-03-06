#ifndef __LINUX_ILI9341_H
#define __LINUX_ILI9341_H


#define ILI9341_NOP        0x00
#define ILI9341_SWRESET    0x01
#define ILI9341_RDDIDIF    0x04
#define ILI9341_RDDST      0x09
#define ILI9341_RDDPM      0x0A
#define ILI9341_RDDMADCTL  0x0B
#define ILI9341_RDDCOLMOD  0x0C
#define ILI9341_RDDIM      0x0D
#define ILI9341_RDDSM      0x0E
#define ILI9341_RDDSDR     0x0F

#define ILI9341_SLPIN      0x10
#define ILI9341_SLPOUT     0x11
#define ILI9341_PTLON      0x12
#define ILI9341_NORON      0x13

#define ILI9341_DINVOFF    0x20
#define ILI9341_DINVON     0x21
#define ILI9341_GAMSET     0x26
#define ILI9341_DISPOFF    0x28
#define ILI9341_DISPON     0x29
#define ILI9341_CASET      0x2A
#define ILI9341_PASET      0x2B
#define ILI9341_RAMWR      0x2C
#define ILI9341_RGBSET     0x2D
#define ILI9341_RAMRD      0x2E

#define ILI9341_PLTAR      0x30
#define ILI9341_VSCRDEF    0x33
#define ILI9341_TEOFF      0x34
#define ILI9341_TEON       0x35
#define ILI9341_MADCTL     0x36
#define ILI9341_VSCRSADD   0x37
#define ILI9341_IDMOFF     0x38
#define ILI9341_IDMON      0x39
#define ILI9341_PIXSET     0x3A

#define ILI9341_FRMCTR1    0xB1
#define ILI9341_FRMCTR2    0xB2
#define ILI9341_FRMCTR3    0xB3
#define ILI9341_INVTR      0xB4
#define ILI9341_PRCTR      0xB5
#define ILI9341_DISCTRL    0xB6
#define ILI9341_ETMOD      0xB7

#define ILI9341_PWCTRL1    0xC0
#define ILI9341_PWCTRL2    0xC1
#define ILI9341_VMCTRL1    0xC5
#define ILI9341_VMCTRL2    0xC7
#define ILI9341_PWCTRLA    0xCB
#define ILI9341_PWCTRLB    0xCF

#define ILI9341_RDID1      0xDA
#define ILI9341_RDID2      0xDB
#define ILI9341_RDID3      0xDC
#define ILI9341_RDID4      0xD3

#define ILI9341_PGAMCTRL   0xE0
#define ILI9341_NGAMCTRL   0xE1
#define ILI9341_DGAMCTRL1  0xE2
#define ILI9341_DGAMCTRL2  0xE3
#define ILI9341_TCTRLA     0xE8
#define ILI9341_TCTRLB     0xEA
#define ILI9341_PWRSEQ     0xED

#define ILI9341_EN3GAM     0xF2
#define ILI9341_IFCTL      0xF6
#define ILI9341_PUMP       0xF7

#define ILI9341_MADCTL_MV  BIT(5)
#define ILI9341_MADCTL_MX  BIT(6)
#define ILI9341_MADCTL_MY  BIT(7)


#endif /* __LINUX_ILI9341_H */
