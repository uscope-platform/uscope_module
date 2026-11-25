obj-m += ucube_lkm.o
ucube_lkm-y := ucube_lkm_main.o \
               ucube_lkm_chrdev.o \
               ucube_lkm_fpga.o \
               ucube_lkm_fclk.o \
               ucube_lkm_dma.o \
               ucube_lkm_shm.o \
               ucube_lkm_ipi.o

# Local development settings (kernel path, toolchain, out-of-tree build dir).
# This file is git-ignored, so it never affects a Yocto build.
-include local.mk

# Fallback when no local.mk is present: build against the running host kernel
KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build

export ARCH
export CROSS_COMPILE

# Yocto passes O= and KERNEL_SRC= on the command line; ignore the dev build
# directory in that case so the module is built in place, as module.bbclass expects.
ifneq ($(origin O),undefined)
override MO :=
endif

SRC := $(shell pwd)

all: modules
modules:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) $(if $(MO),MO=$(MO)) modules
ifneq ($(MO),)
	@cp $(MO)/ucube_lkm.ko $(SRC)/
endif

modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) $(if $(MO),MO=$(MO)) modules_install

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) $(if $(MO),MO=$(MO)) clean
	@rm -f $(SRC)/ucube_lkm.ko
	@if [ -n "$(MO)" ]; then rm -rf $(MO); fi
