TARGET_MODULE := kdrv

obj-m := $(TARGET_MODULE).o

ccflags-y := -std=gnu99 -Wno-declaration-after-statement

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all: client
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	$(RM) client out

load:
	sudo insmod $(TARGET_MODULE).ko
unload:
	sudo rmmod $(TARGET_MODULE) || true >/dev/null

check: all
	$(MAKE) unload
	$(MAKE) load
	./rbtree
	$(MAKE) unload