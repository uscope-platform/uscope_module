KDIR  := /software/kernel_dev/linux-xlnx
PWD := $(shell pwd)
BUILD := $(CURDIR)/build
obj-m += ucube_lkm.o
ccflags-y := -std=gnu99


all:
	mkdir -p $(BUILD)
	$(MAKE)  ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -C $(KDIR)  M=$(CURDIR) MO=$(BUILD) modules
	cp $(BUILD)/ucube_lkm.ko $(CURDIR)/

modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(CURDIR) MO=$(BUILD) modules_install

clean:
	rm -rf $(BUILD) ucube_lkm.ko