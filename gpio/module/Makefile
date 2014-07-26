obj-m := hello.o gpio-mcp23008.o i2c.o spi-mcp3002.o pcf8591.o
KERNELVERSION ?= $(shell uname -r)
KDIR := /lib/modules/$(KERNELVERSION)/build
PWD := $(shell pwd)
all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules
install: all
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install
	depmod -a
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
