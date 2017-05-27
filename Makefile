EXTRA_CFLAGS += -Wall -Os
export ${EXTRA_CFLAGS}
GRP=wheel

obj-m := tmr_mod.o
tmr_mod-objs:= tmr.o

MODULE_NAME = tmr_mod
KVER := `uname -r`

all:
	make -C /lib/modules/$(KVER)/build M=${PWD} modules
clean:
	make -C /lib/modules/$(KVER)/build M=${PWD} clean
install:
	chown :$(GRP) $(MODULE_NAME).ko
	chmod g+rw $(MODULE_NAME).ko
	sudo install -p -m 644 $(MODULE_NAME).ko /lib/modules/$(KVER)/kernel/drivers/
	sudo /sbin/depmod -a
