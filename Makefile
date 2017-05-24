adc: adc.o
	${CROSS_COMPILE}gcc -o adc adc.o -I ${SYSROOT}/usr/include
	${CROSS_COMPILE}strip adc
adc.o: adc.c
	${CROSS_COMPILE}gcc -c adc.c -O2 -Wall -D_GNU_SOURCE  -I ${SYSROOT}/usr/include 
clean:
	rm -f *.o adc



