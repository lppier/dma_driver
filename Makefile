ifneq ($(KERNELRELEASE),)
obj-m := dma_driver.o
else
KDIR := /lib/modules/$$(uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$$PWD
endif

