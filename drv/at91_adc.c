#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <linux/ioctl.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/vmalloc.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/time.h>
#include <linux/stat.h>
#include "../arch/arm/mach-at91/include/mach/hardware.h"
#include "../arch/arm/mach-at91/include/mach/io.h"
#include "../arch/arm/mach-at91/include/mach/at91_pio.h"
#include "../arch/arm/mach-at91/include/mach/at91sam9260_matrix.h"

#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/smp_lock.h>

#include "at91_adc.h"

//*************************************************************

#define adc_read(reg)		ioread32(BaseAdrRAMADC + (reg))
#define adc_write(val, reg)	iowrite32((val), BaseAdrRAMADC + (reg))
#define AT91_DEFAULT_CONFIG             AT91_ADC_SHTIM  | \
                                        AT91_ADC_STARTUP | \
                                        AT91_ADC_PRESCAL | \
                                        AT91_ADC_SLEEP
#define CR_RESET	0x00000001	//hardware reset
#define CR_NORESET	0x00000000	//hardware reset clear
#define CR_STOP		0x00000000	//stop a-to-d conversion

#define INIT_CHER	0x00000008	//we use this channel (channel 3)
#define INIT_CHDR	0x00000007	//disable all channels (channel 3 - enable)

#define IER_INIT	0x00000000	//no interrupt enable
#define IDR_INIT	0x000F0F0F	//interrupt disable
#define IMR_INIT	0x00000000	//interrupt mask disable

#define SR_EOC3		0x00000008	//channel3 enable and conversion done
#define SR_OVRE3	0x00000800	//channel3 overrun error
#define SR_DRDY		0x00010000	//data ready in CS_ADC_LCDR
#define SR_GOVRE	0x00020000	//general error (data in CS_ADC_LCDR not valid, maybe)
#define SR_EDNRX	0x00040000
#define SR_RXBUF	0x00080000

//*************************************************************

#define mem_buf 128
#define mem_size 1024

#define ADC_TIMER	0
#define ADC_CHAN_SELECT	1
#define ADC_CHAN_GET	2
#define ADC_CHAN_READ	3
#define ADC_CHAN_START	4
#define ADC_CHAN_STOP	5
#define ADC_RESET	6
#define ADC_ENABLE	7
//--------------------------------------------------------------------------------------
#define CLASS_DEV_CREATE(class, devt, device, name) \
	device_create(class, device, devt, NULL, "%s", name)
#define CLASS_DEV_DESTROY(class, devt) \
	device_destroy(class, devt)

#define DevName "at91_adc"

#define timi_def 10
#define timi2_def 2

#define ABOUT_MODULE "ADC driver support : open, close, ioctl, proc_fs_entry"

//--------------------------------------------------------------------------------------
#define MODULE_PATH_DIR "/proc"
#define NAME_NODE DevName"_data"
#define PROC_COUNT 4096

static struct proc_dir_entry *own_proc_node = NULL, *parent=NULL;

//--------------------------------------------------------------------------------------
static struct class *adc_class = NULL;

static int my_dev_ready=0;

struct resource * adc_iomem_reg = NULL;
static void __iomem *BaseAdrRAMADC = NULL;

static void __iomem *at91_pioc_base = NULL;
struct clk *at91_adc_clk;

static unsigned char *ibuff=NULL;
static unsigned char *ibuffw=NULL;

static unsigned int my_msec;
static struct timer_list my_timer;

static atomic_t varta;

static int Major=0;
module_param(Major, int, 0);
static int Device_Open = 0;

struct rchan_sio {
  struct cdev cdev;
};

static int timi=timi_def;
static int timi2=timi2_def;

static atomic_t start_adc;
static atomic_t data_adc;
static atomic_t enable_adc;
static unsigned int mir_cr;
static unsigned int mir_cher;
static unsigned int mir_chdr;
static unsigned int mir_ier;
static unsigned int mir_idr;
static int chan_set=3, chan_set_in=3;

//************************************************************
//************************************************************
//************************************************************
static int mux_chan(int chan, int operation)
{
int pin_chan;

    if (chan<0 || chan>3) return -EINVAL;

    switch (chan) {
	case 0: pin_chan=AT91_PIN_PC0; break;
	case 1: pin_chan=AT91_PIN_PC1; break;
	case 2: pin_chan=AT91_PIN_PC2; break;
	case 3: pin_chan=AT91_PIN_PC3; break;
    	    default: return -EINVAL;
    }
    if (operation == 1)//chan_select
        at91_set_A_periph(pin_chan, 0);                         //Mux PIN to GPIO
    else               //chan_free
        at91_set_B_periph(pin_chan, 0);                         //Mux PIN to GPIO
    chan_set=chan;

    return 0;
}
//************************************************************
int rd_proc(struct file *filp, char *buf, size_t count, loff_t *offp)
{
int data=-1;
unsigned int data_v, cel, dro, ret=0;

    if (count>=PROC_COUNT) {

	memset(ibuff,0,mem_buf);
	if (atomic_read(&enable_adc)) {
	    data=atomic_read(&data_adc);
	    data_v=data*3222;
	    cel=data_v/1000000;
	    dro = (data_v % 1000000) / 1000;
	    sprintf(ibuff,"ADC (%d) [%d,%03d v] (%d)\n",chan_set+1, cel, dro, data);
	} else sprintf(ibuff,"\nADC (%d) no data\n",chan_set+1);
	ret = strlen(ibuff);
	//printk(KERN_INFO "\nread_proc: order %d bytes (buffer_size=%d)\n", count, procfs_buffer_size);

	copy_to_user(buf, ibuff, ret);

    }

    return ret;

}
//************************************************************
int wr_proc(struct file *filp, char *buf, size_t count, loff_t *offp)
{
int len=count;
unsigned char bt;

    if (len>mem_buf-1) len = mem_buf-1;

    memset(ibuffw,0,mem_buf);

    copy_from_user(ibuffw, buf, len);
    //printk(KERN_INFO "\nwrite_proc(%d): %s\n", count, ibuffw);
    if (strstr(ibuffw, "stop") != NULL) {

	if (atomic_read(&enable_adc)) atomic_set(&enable_adc, (int)0);

    } else if (strstr(ibuffw, "start") != NULL) {

	if (!atomic_read(&enable_adc)) atomic_set(&enable_adc, (int)1);

    } else if (strstr(ibuffw, "channel=") != NULL) {
	bt = (*(ibuffw + 8)) - 0x30;
	if ((bt > 0) && (bt <= 4)) {
	    bt--;
	    if ((int)bt != chan_set) {
		chan_set_in = bt;
		//printk(KERN_INFO "\nwrite_proc: chan_set=%d chan_set_in=%d\n",chan_set, chan_set_in);
	    }
	}
    }

    return len;
}
//************************************************************
static void adc_reset(void)
{
    mir_cr = (unsigned int)AT91_ADC_SWRST; adc_write(mir_cr, AT91_ADC_CR);   //Reset the ADC
    atomic_set(&start_adc, (int)0);
    atomic_set(&enable_adc, (int)0);
}
//************************************************************
static void adc_init(int chan)//we work with channel 3
{
unsigned int data=0;

    data = (unsigned int)AT91_DEFAULT_CONFIG;	adc_write(data, AT91_ADC_MR);//set mode

    mir_cher = (unsigned int)AT91_ADC_CH(chan);	adc_write(mir_cher, AT91_ADC_CHER);//chan3 - enable
    mir_chdr = (unsigned int)0;			adc_write(mir_chdr, AT91_ADC_CHDR);

    mir_ier = (unsigned int)IER_INIT;	adc_write(mir_ier, AT91_ADC_IER);//no interrupt enable
    mir_idr = (unsigned int)IDR_INIT;	adc_write(mir_idr, AT91_ADC_IDR);//interrupt disable

    mux_chan(chan, ADC_CHAN_SELECT);//request chan

}
//************************************************************
static void adc_start_conv(int chan)
{

    iowrite32(1 << chan, at91_pioc_base + 0x60);
    mir_cher = (unsigned int)AT91_ADC_CH(chan); adc_write(mir_cher,AT91_ADC_CHER);// Enable Channel
    mir_cr = (unsigned int)AT91_ADC_START; adc_write(mir_cr,AT91_ADC_CR);// Start the ADC
    atomic_set(&start_adc, (int)1);

}
//************************************************************
static void adc_stop_conv(int chan)
{
    mir_cr = (unsigned int)CR_STOP;	adc_write(mir_cr, AT91_ADC_CR);
    atomic_set(&start_adc, (int)0);
}
//************************************************************
static int adc_data_ready(int chan)
{
int ret=-1;
unsigned int data;

    data = adc_read(AT91_ADC_SR);
    if (data & AT91_ADC_EOC(chan)) {
	data = adc_read(AT91_ADC_CHR(chan));
	data &= AT91_ADC_DATA;
	ret=data;
    }

    return ret;
}
//************************************************************
//		    Timer 2 mcek
//************************************************************
void MyTimer(unsigned long d)
{
int rt=0;

    timi--; 
    if (!timi) {
	timi=timi_def;
	atomic_inc(&varta);
	timi2--;
	if (!timi2) {//20ms
	    timi2=timi2_def;
	    if (atomic_read(&enable_adc)) {
		if (atomic_read(&start_adc)) {
		    rt = adc_data_ready(chan_set);
		    if (rt != -1) {
			atomic_set(&data_adc, rt);
			atomic_set(&start_adc, (int)0);
		    }
		} else {
		    if (chan_set_in != chan_set) {
			mux_chan(chan_set, 0);
			chan_set = chan_set_in;
			mux_chan(chan_set, 1);
		    }
		    adc_start_conv(chan_set);//ADC START
		}
	    }
	}

    }//10ms timer

    my_timer.expires = jiffies + 1;
    mod_timer(&my_timer, jiffies + 1);

    return;
}
//***********************************************************
//		Open device
//************************************************************
static int rchan_open(struct inode *inode, struct file *filp) {

struct rchan_sio *sio;
int ret=-ENODEV;

    if (!Device_Open) {
	Device_Open++;
	sio = container_of(inode->i_cdev, struct rchan_sio, cdev);
	filp->private_data = sio;
	atomic_set(&start_adc, (int)0);
	atomic_set(&enable_adc, (int)1);
	ret = 0;
    } else ret = -EBUSY;

    return ret;
}
//***********************************************************
//		Close device
//************************************************************
static int rchan_release(struct inode *inode, struct file *filp)
{

    if (Device_Open>0) Device_Open--;

    atomic_set(&start_adc, (int)0);
    atomic_set(&enable_adc, (int)0);

    return 0;
}
//************************************************************
#ifdef HAVE_UNLOCKED_IOCTL
static long rchan_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
#else
static int rchan_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg)
{
#endif

int ret=-EINVAL, ch;

#ifdef HAVE_UNLOCKED_IOCTL
	    lock_kernel();
#endif
	    ch = arg&3;
	    switch (cmd) {
		case ADC_TIMER :
		    ret = atomic_read(&varta);
		break;
		case ADC_CHAN_SELECT :
		    if (ch!=chan_set) {
			ret = mux_chan(chan_set, 0);
			chan_set = chan_set_in = ch;
			ret = mux_chan(ch, 1);
		    }
		break;
		case ADC_CHAN_GET :
		    ret = chan_set;
		break;
		case ADC_CHAN_READ :
		    if (atomic_read(&enable_adc)) {
			ret=atomic_read(&data_adc);
		    } else ret=-1;
		break;
		case ADC_RESET :
		    adc_reset();
		    ret=0;
		break;
		case ADC_CHAN_START :
		    adc_start_conv(chan_set);//ADC
		    ret=0;
		break;
		case ADC_CHAN_STOP :
		    adc_stop_conv(chan_set);//ADC
		    ret=0;
		break;
		case ADC_ENABLE :
		    atomic_set(&enable_adc, (int)1);
		    ret=0;
		break;
		    default : ret = -EINVAL;
	    }
#ifdef HAVE_UNLOCKED_IOCTL
	    unlock_kernel();
#endif

    return ret;
}
//*************************************************************
//************************************************************
//*************************************************************
static struct file_operations rchan_fops = {
  .owner   = THIS_MODULE,
  .open    = rchan_open,
  .release = rchan_release,
#ifdef HAVE_UNLOCKED_IOCTL
  .unlocked_ioctl = rchan_unlocked_ioctl
#else
  .ioctl = rchan_ioctl
#endif
};
//*************************************************************
static struct file_operations proc_fops = {
  .read    = rd_proc,
  .write   = wr_proc,
};
//************************************************************
static void init_sio(struct rchan_sio *sio)
{
  dev_t dev = MKDEV(Major,0);
  cdev_init(&sio->cdev, &rchan_fops);
  cdev_add(&sio->cdev, dev, 1);
}
//************************************************************
static void deinit_sio(struct rchan_sio *sio)
{
  cdev_del(&sio->cdev);
}
//************************************************************

static struct rchan_sio chan_sio;

// ************************************************************
//		Init device
// ************************************************************
static int __init rchan_init(void)
{
dev_t dev;
int rc;
struct file *fp=NULL;

    printk(KERN_ALERT "\n");

    if (!Major) {
	if ((alloc_chrdev_region(&dev, 0, 1, DevName)) < 0){
	    printk(KERN_ALERT "%s: Allocation device failed\n",DevName);
	    return 1;
	}
	Major = MAJOR(dev);
	printk(KERN_ALERT "%s: device allocated with major number %d (ADC)\n",DevName,Major);
    } else {
	if (register_chrdev_region(MKDEV(Major,0),1,DevName)<0){
	    printk(KERN_ALERT "%s: Registration failed\n",DevName);
	    return 1;
	}
	printk(KERN_ALERT "%s: devices registered\n",DevName);
    }

    init_sio(&chan_sio);

    adc_class = class_create(THIS_MODULE, DevName);
    if (IS_ERR(adc_class)) {
	printk(KERN_ALERT "%s: bad class create\n",DevName);
	goto err_out;
    }
    CLASS_DEV_CREATE(adc_class, MKDEV(Major, 0), NULL, DevName);

    my_dev_ready=1;
//--------------------------------------------------------------------

    at91_adc_clk = clk_get(NULL,"adc_clk");
    clk_enable(at91_adc_clk);
    // Request and remap i/o memory region for ADC //CS_ADC_BASE
    if (check_mem_region(AT91SAM9260_BASE_ADC, 256)) {
	printk(KERN_ALERT "%s: i/o memory region for ADC already used\n",DevName);
	goto err_out;
    }
    if (!(adc_iomem_reg = request_mem_region(AT91SAM9260_BASE_ADC, 256, DevName))) {
	printk(KERN_ALERT "%s: can't request i/o memory region for ADC\n",DevName);
	goto err_out;
    }
    if (!(BaseAdrRAMADC = ioremap_nocache(AT91SAM9260_BASE_ADC, 256))) {
	printk(KERN_ALERT "%s: can't remap i/o memory for ADC\n",DevName);
	goto err_out;
    }
    at91_pioc_base = ioremap_nocache(AT91_BASE_SYS + AT91_PIOC, 512);
    if(!at91_pioc_base) {
	printk(KERN_ALERT "%s: can't remap i/o memory for PIOC\n",DevName);
	goto err_out;
    }

//--------------------------------------------------------------------

    ibuff = kmalloc(mem_size,GFP_KERNEL);
    if (ibuff == NULL){
	printk(KERN_ALERT "%s: KM for reading buffer allocation failed\n",DevName);
	goto err_out;
    }
    ibuffw = kmalloc(mem_size,GFP_KERNEL);
    if (ibuffw == NULL){
	printk(KERN_ALERT "%s: KM for writing buffer allocation failed\n",DevName);
	goto err_out;
    }

//-----------------   PROCFS INIT  ---------------------------------------
    fp = filp_open(MODULE_PATH_DIR, O_RDONLY, 0);
    parent = PDE(fp->f_dentry->d_inode);
    filp_close(fp, NULL);
    if (parent == NULL) {
	printk(KERN_INFO "Get proc dir entry [%s] failure\n", MODULE_PATH_DIR);
	goto err_out;
    }
    own_proc_node = create_proc_entry( NAME_NODE, S_IFREG | S_IRUGO | S_IWUGO, parent );
    if (own_proc_node!=NULL) {
	own_proc_node->uid = 0;
	own_proc_node->gid = 0;
	own_proc_node->proc_fops = &proc_fops;
	printk(KERN_ALERT "%s: create /proc/%s OK\n",DevName,NAME_NODE);
    } else printk(KERN_ALERT "%s: can't create /proc/%s\n",DevName,NAME_NODE);

//--------------------------------------------------------------------

	Device_Open = 0;
	my_msec=0;
	atomic_set(&varta, my_msec);
	//ADC
	adc_reset();
	adc_init(chan_set);
	//Timer start
	timi=timi_def;
	timi2=timi2_def;
	init_timer(&my_timer);
	my_timer.function = MyTimer;
	my_timer.expires = jiffies + 10;	// 10 msec
	add_timer(&my_timer);

	atomic_set(&enable_adc, (int)1);//ENABLE ADC

	printk(KERN_ALERT "%s: %s\n",DevName,ABOUT_MODULE);

    return 0;

err_out:

    if (my_dev_ready==1) {
	CLASS_DEV_DESTROY(adc_class, MKDEV(Major, 0));
	class_destroy(adc_class);
    }

    if (own_proc_node!=NULL) {
	remove_proc_entry(NAME_NODE, NULL);
	printk(KERN_ALERT "%s: /proc/%s removed.\n",DevName,NAME_NODE);
    }

    rc = -ENOMEM;

    if (adc_iomem_reg) release_mem_region(AT91SAM9260_BASE_ADC, 256);

    if (at91_pioc_base!=NULL) iounmap(at91_pioc_base);
    if (BaseAdrRAMADC!=NULL) iounmap(BaseAdrRAMADC);

    if (ibuff!=NULL) kfree(ibuff);
    if (ibuffw!=NULL) kfree(ibuffw);

    clk_disable(at91_adc_clk);
    clk_put(at91_adc_clk);

    return rc;

}
//************************************************************
//                Release device
//************************************************************
static void __exit rchan_exit(void)
{

    del_timer(&my_timer);		//Stop Timer

    adc_stop_conv(chan_set);		//ADC

    if (adc_iomem_reg) release_mem_region(AT91SAM9260_BASE_ADC, 256);

    if (at91_pioc_base!=NULL) iounmap(at91_pioc_base);
    if (BaseAdrRAMADC!=NULL) iounmap(BaseAdrRAMADC);

    if (ibuff!=NULL) kfree(ibuff);
    if (ibuffw!=NULL) kfree(ibuffw);

    clk_disable(at91_adc_clk);
    clk_put(at91_adc_clk);

    unregister_chrdev_region(MKDEV(Major, 0), 1);

    deinit_sio(&chan_sio);

    CLASS_DEV_DESTROY(adc_class, MKDEV(Major, 0));
    class_destroy(adc_class);

    if (own_proc_node!=NULL) {
	remove_proc_entry( NAME_NODE, NULL );
	printk(KERN_ALERT "%s: /proc/%s removed.\n",DevName,NAME_NODE);
    }


    printk(KERN_ALERT "%s: device unregistered, stop timer, release memory buffers\n",DevName);

    return;
}

module_init(rchan_init);
module_exit(rchan_exit);


MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("SalaraSoft <a.ilminsky@gmail.com>");

