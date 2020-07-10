KERNEL_SRC := /home/zackary/nanopi/linux-4.14/

USB_MOD_NAME = aufs
USB_CFILES := \
	$(USB_MOD_NAME).c
#$(USB_MOD_NAME)-objs := $(USB_CFILES:.c=.o)

###############################################################################
# Common
###############################################################################
obj-m := $(USB_MOD_NAME).o

all:
	make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -C $(KERNEL_SRC) M=$(PWD) modules

clean:
	make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -C $(KERNEL_SRC) M=$(PWD) clean
