ifneq ($(KERNELRELEASE),)
obj-m  := loopback.o

else
KDIR ?= /lib/modules/`uname -r`/build
O ?= build

default: | $(O)
	$(MAKE) -C $(KDIR) M=$$PWD MO=$(realpath $(O))

cc: | $(O)
	bear --output build/compile_commands.json -- $(MAKE) -C $(KDIR) M=$$PWD MO=$(realpath $(O))

$(O):
	mkdir -p $@

endif