PWD := $(shell pwd)
obj-m += ucube_lkm.o
ccflags-y := -std=gnu99

all:
	make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- -C /home/fils/git/uscope_module/linux-xlnx M=$(PWD) modules
clean:
	rm -rf *.o *.ko *.mod.* *.symvers *.order .*.cmd *.mod