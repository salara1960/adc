#!/bin/sh

install -p -m 644 at91_adc.ko /lib/modules/`uname -r`/kernel/drivers/
/sbin/depmod -a `uname -r`

