********************************************************************************
##   adc - application ADC (with kernel device driver for ADC on AT91SAM9G20)
********************************************************************************

## Description

A simple linux application with kernel module for ADC on board AT91SAM9G20-EK

## Package files:

* adc.c		- example source code for testing ADC (10bit) on board AT91SAM9G20-EK

* Makefile	- make file (example compilation scenario)

* drv/		- folder with kernel device driver project (for kernel 2.6.33)
```
    at91_adc.c	- source code of kernel driver for ADC (10bit) on board AT91SAM9G20-EK
    at91_adc.h	- header for source code
    Makefile	- make file (module compilation scenario)
    inst.sh	- installation driver script
```

##  Use driver:
```
    1. Install driver: ./inst.sh
	This script copy at91_adc.ko to folder /lib/modules/2.6.33/kernel/drivers
	and add to file /lib/modules/2.6.33/modules.dep next string : kernel/drivers/at91_adc.ko:
    2. Load driver to memory: modprobe at91_adc
    3. Driver support next command :
	3.1. cat /proc/at91_adc_data - For look battery voltage
	3.2. echo "start" >/proc/at91_adc_data - start ADC (by default ADC autostart, when driver loaded to memory)
	3.3. echo "stop" >/proc/at91_adc_data - stop ADC
	3.4. echo "channel=X" >/proc/at91_adc_data  - set ADC channel (where X=1,2,3,4  battery - channel=4)
```
    P.S.
	For remove driver from memory : modprobe -r at91_adc

* README.md


