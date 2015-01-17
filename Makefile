ifneq ($(KERNELRELEASE),)
# kbuild part of makefile

# Optionally, include config file to allow out of tree kernel modules build
-include $(src)/.config

obj-y                           += core/
obj-y                           += i80/

obj-m += hy28afb.o
obj-m += sainsmart18fb.o
obj-m += adafruit797fb.o
obj-m += ebay181283191283fb.o
obj-m += ssd1963.o
obj-m += ili9320.o
obj-m += mipi-dbi.o
obj-m += ssd1306.o

obj-m += mi0283qtfb.o
obj-m += itdb02-28fb.o


else
# normal makefile
KDIR ?= /lib/modules/`uname -r`/build

default: .config
	$(MAKE) -C $(KDIR) M=$$PWD modules

.config:
	grep config Kconfig | cut -d' ' -f2 | sed 's@^@CONFIG_@; s@$$@=m@' > .config

install:
	$(MAKE) -C $(KDIR) M=$$PWD modules_install


clean:
	rm -rf *.o *~ .depend .*.cmd *.ko *.mod.c .tmp_versions \
	       modules.order Module.symvers

endif
