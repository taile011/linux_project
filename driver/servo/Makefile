obj-m += SG90.o

KDIR := /home/ps/work/linux/IMX6ULL/linux-imx-4.1.15-2.1.0-g3dc0a4b-v2.7
PWD ?= $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
