# Kernel module: builds my_irq.ko from irqmodule.c + utils.c
obj-m := my_irq.o
my_irq-objs := irqmodule.o utils.o

# Buildroot output directory. Override on the command line, e.g.:
#   make BUILDROOT=/path/to/buildroot/output
BUILDROOT     ?= $(HOME)/buildroot/output
KDIR          ?= $(BUILDROOT)/build/linux-custom
ARCH          ?= arm64
CROSS_COMPILE ?= $(BUILDROOT)/host/bin/aarch64-buildroot-linux-gnu-

export ARCH CROSS_COMPILE

KBUILD_TARGETS := clean help modules modules_install

.PHONY: all $(KBUILD_TARGETS)

all: modules

$(KBUILD_TARGETS):
	$(MAKE) -C $(KDIR) M="$(shell pwd)" $@
