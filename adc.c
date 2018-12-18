/*
 *
 * Example code for testing Analog-to-Digital Converter (ADC) on board AT91SAM9G20-EK.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <stdbool.h>
#include <time.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <pthread.h>
#include <string.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <netdb.h>

#include <arpa/inet.h>
#include <ctype.h>
#include <sys/msg.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <syslog.h>
#include <asm/termbits.h>


#define ADC_TIMER	0
#define ADC_CHAN_SELECT	1
#define ADC_CHAN_GET	2
#define ADC_CHAN_READ	3
#define ADC_CHAN_START	4
#define ADC_CHAN_STOP	5
#define ADC_RESET	6
#define ADC_ENABLE	7


#pragma pack(push,1)
typedef struct {
    unsigned short cel;
    unsigned short dro;
} s_adc;
#pragma pack(pop)

pid_t pid_main = 0;
FILE *pid_fp;
const char * pid_name = "/var/run/adc.pid";
const char * drv_name = "/dev/at91_adc";
s_adc adc;
unsigned char sel_chan = 3;
int fd, loops;
unsigned char SIGHUPs = 1, SIGTERMs = 1, SIGINTs = 1, SIGKILLs = 1, SIGSEGVs = 1, SIGTRAPs = 1;
//******************************************************************************************
//******************************************************************************************
// *****************************************************************************************
unsigned int get_timer_sec(unsigned int t)
{
    return ((unsigned int)time(NULL) + t);
}
// **************************************************************************
int check_delay_sec(unsigned int t)
{
    if ((unsigned int)time(NULL) >= t)  return 1; else return 0;
}
// **************************************************************************
void _SigProc(int sig)
{
    switch (sig) {
        case SIGTERM :
	    if (SIGTERMs) {
		SIGTERMs = 0;
		printf("SIGTERM : termination signal (term)\n");
	    }
	    loops = 0;
        break;
        case SIGINT :
	    if (SIGINTs) {
		SIGINTs = 0;
		printf("SIGINT : interrupt from keyboard (term)\n");
	    }
	    loops = 0;
        break;
        case SIGKILL :
	    if (SIGKILLs) {
		SIGKILLs = 0;
		printf("SIGKILL : kill signal (term)\n");
	    }
	    loops = 0;
        break;
        case SIGSEGV :
	    if (SIGSEGVs) {
		SIGSEGVs = 0;
		printf("SIGSEGV : invalid memory reference (core)\n");
	    }
	loops = 0;
        break;
        case SIGTRAP :
	    if (SIGTRAPs) {
		SIGTRAPs = 0;
		printf("SIGTRAP : trace/breakpoint trap (core)\n");
	    }
	    loops = 0;
        break;
	case SIGHUP :
	    if (fd > 0) {
		int c = sel_chan;
		c++; if (c == 4) c = 0;
		ADC_Select(c);
	    }
	break;
    }
}
// **************************************************************************
void OutOfJob()
{
    if (fd > 0) close(fd);
    if (pid_main) unlink(pid_name);
}
// **************************************************************************
int ADC_Data()
{
    return (ioctl(fd, ADC_CHAN_READ, sel_chan));
}
// **************************************************************************
void ADC_ALL(float v)
{
    memset(&adc, 0, sizeof(s_adc));
    adc.cel = v;
    adc.dro = (v - adc.cel) * 1000;
}
//***************************************************************************
int ADC_Select(int chan)
{
int ret = -1;

    ret = ioctl(fd, ADC_CHAN_SELECT, chan);

    if (!ret) sel_chan = chan;

    return ret;

}
//******************************************************************************************
//******************************************************************************************
//******************************************************************************************
int main(int argc, char ** argv)
{
char chaka[128] = {0};
struct sigaction Ata, OAta;
int data_adc = -1, chn;
float data_adc_v;
unsigned int tmr_loop;


    pid_main = getpid();

    if (!(pid_fp = fopen(pid_name, "w"))) {
	memset(chaka, 0, sizeof(chaka));
	sprintf(chaka,"adc: unable to create pid file %s: %s\n",pid_name, strerror(errno)); printf(chaka);
	return -1;
    } else {
	fprintf(pid_fp, "%i\n", (int)pid_main);
	fclose(pid_fp);
    }

    if (argc > 1) {
	memset(chaka, 0, sizeof(chaka));
	strcpy(chaka, argv[1]);
	chn = atoi(chaka);
	if ((chn < 1) || (chn > 4)) {
	    OutOfJob();
	    return -1;
	} else sel_chan = chn - 1;
    }


    memset((unsigned char *)&Ata, 0, sizeof(struct sigaction));
    memset((unsigned char *)&OAta, 0, sizeof(struct sigaction));
    Ata.sa_handler = &_SigProc;
    Ata.sa_flags   = 0;
    sigaction(SIGHUP,  &Ata, &OAta);
    sigaction(SIGSEGV, &Ata, &OAta);
    sigaction(SIGTERM, &Ata, &OAta);
    sigaction(SIGINT,  &Ata, &OAta);
    sigaction(SIGKILL, &Ata, &OAta);
    sigaction(SIGTRAP, &Ata, &OAta);


//-----------------------------------------------------------------------------------
    fd = open(drv_name, O_RDWR);
    if (fd < 0) {
	sprintf(chaka,"Can't open %s (%s)\n", drv_name, strerror(errno));
	printf(chaka);
	if (pid_main) unlink(pid_name);
	return -1;
    }

    ADC_Select(sel_chan);

    loops = 1;
    tmr_loop = get_timer_sec(1);

//---------------------------------------------------------------------------------

    while (loops) {

	if (check_delay_sec(tmr_loop)) {
	    data_adc = ADC_Data();
	    data_adc_v = data_adc;
	    data_adc_v *= 3222; data_adc_v = data_adc_v / 1000000;
	    ADC_ALL(data_adc_v);
	    sprintf(chaka,"ADC (%d) [%d,%d v] (%d)\n", sel_chan, adc.cel, adc.dro, data_adc);
	    printf(chaka);
	    tmr_loop = get_timer_sec(1);
	} else usleep(1000);

    }//while (loops).... end

//---------------------------------------------------------------------------------

    OutOfJob();

    return 0;

}

