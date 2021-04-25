PWD := $(shell pwd)
obj-m += ucube_lkm.o
ccflags-y := -std=gnu99

all:
	make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- -C /software/module/linux-xlnx/ M=$(PWD) modules
 
modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) modules_install

clean:
	rm -rf *.o *.ko *.mod.* *.symvers *.order .*.cmd *.mod