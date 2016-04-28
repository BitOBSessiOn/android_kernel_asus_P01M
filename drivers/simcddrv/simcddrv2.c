/*
SIM Card Detection DRiVer

Author: Chihyen Yeh

Purpose: Just used to request sim card detection gpio

Data: 2012-11-19

*/

#include <linux/module.h>  
#include <linux/kernel.h> 
#include <linux/init.h> 
#include <linux/major.h>
#include <linux/device.h>
#include <linux/poll.h>
#include <linux/gpio.h>

#define MAJOR_NUM	60
#define MODULE_NAME	"SIMCD"
/*
Define the SIM Card detection GPIO name 
and GPIO number
*/
#define SIM1_CD_NAME "n_sim1_cd"
//#define SIM2_CD_NAME "n_sim2_cd"
#define SIM1_CD_NUM 113
#define SIM2_CD_NUM 112
#define TEST_GPIO 69 //sd_cd for ME371 but still can chagned

#define SIM1_GPIO_TAG "sim1_cd"
#define SIM2_GPIO_TAG "sim2_cd"
#define STRING_SIZE 8


static ssize_t simcd_read(struct file *filp, char *buf, size_t count, loff_t *ppos)
{
	int sim1_cd_high_low;
	int sim2_cd_high_low;
	int retval=0;
	

	printk("device read\n");
	sim1_cd_high_low = gpio_get_value(SIM1_CD_NUM);
	sim2_cd_high_low = gpio_get_value(SIM2_CD_NUM);

	
	return retval;
}

static ssize_t simcd_write(struct file *filp, char *buf, size_t count, loff_t *ppos)
{
	printk("device write\n");	
	return count;
}

static int simcd_open(struct inode *inode, struct file *filp)
{
	printk("device open\n");	
	return 0;
}

static int simcd_release(struct inode *inode, struct file *filp)
{
	printk("device close\n");	
	return 0;
}

struct file_operations simcd_fops =
{
	read:	 simcd_read,
	write:   simcd_write,
	open:	 simcd_open,
	release: simcd_release,
};

static int simcd_init(void)
{	
	
	int err;
	int irq1,irq2;
	if (register_chrdev(0,"simcd",&simcd_fops) < 0){
		printk("%s:can not get major %d\n", MODULE_NAME, MAJOR_NUM);
		return (-EBUSY);
	}
	printk("Device %s: started\n", MODULE_NAME);
	err=gpio_request(SIM1_CD_NUM, SIM1_GPIO_TAG);
	gpio_direction_input(SIM1_CD_NUM);
	if (err!=0){
		printk("SIM1 CD GPIO request fail: %d",err);
	}
	err=gpio_request(SIM2_CD_NUM, SIM2_GPIO_TAG);
	gpio_direction_input(SIM2_CD_NUM);
	if (err!=0){
		printk("SIM2 CD GPIO request fail: %d",err);
	}
	return 0;
}

static void simcd_exit(void)
{
	gpio_free(SIM1_CD_NUM);
	gpio_free(SIM2_CD_NUM);
	unregister_chrdev(MAJOR_NUM,"simcd");
	printk("Device %s: removed", MODULE_NAME);
}

module_init(simcd_init);
module_exit(simcd_exit);
