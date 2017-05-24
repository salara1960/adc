
EXTRA_CFLAGS += -Wall -march=armv5te -mtune=arm926ej-s -mfloat-abi=soft
export ${EXTRA_CFLAGS}

obj-m := at91_adc.o
k16_mod-objs:= at91_adc.o

MODULE_NAME = at91_adc
KVER = 2.6.33

all:
	make -C /sam/linux-$(KVER) M=${PWD} modules
clean:
	make -C /sam/linux-$(KVER) M=${PWD} clean
install:
	install -p -m 644 $(MODULE_NAME).ko ${SYSROOT}/lib/modules/$(KVER)/kernel/drivers/
	/sbin/depmod -a $(KVER)
