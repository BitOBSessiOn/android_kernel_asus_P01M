/*
 * include/linux/NVTtouch_touch.h
 *
 * 
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#ifndef 	_LINUX_NVT_TOUCH_H
#define		_LINUX_NVT_TOUCH_H

#include <linux/earlysuspend.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/switch.h>       // add by Tom for switch device

//*************************TouchScreen Work Part*****************************

#define NVTTOUCH_I2C_NAME "NVT-ts"
#define TOUCH_SDEV_NAME  "touch"
#define NVTTOUCH_RST_PIN 191
#define NVTTOUCH_INT_PIN 116

//define I2C Address
#define FW_Address 0x01

//define default resolution of the touchscreen
#define TOUCH_MAX_HEIGHT 	1024			
#define TOUCH_MAX_WIDTH		600

//set i2c Bus number
#define NVT_BUS_NUM 7

/* trigger mode
// #define IRQ_TYPE_EDGE_RISING 	1
// #define IRQ_TYPE_EDGE_FALLING 	2
// #define IRQ_TYPE_LEVEL_HIGH		4
// #define IRQ_TYPE_LEVEL_LOW 		8
*/
#define INT_TRIGGER_TYPE IRQ_TYPE_EDGE_FALLING

#define POLL_TIME		10	//actual query spacing interval:POLL_TIME+6

#define NVTTOUCH_MULTI_TOUCH
#ifdef NVTTOUCH_MULTI_TOUCH
	#define MAX_FINGER_NUM	5  
#else
	#define MAX_FINGER_NUM	1	
#endif

#define ModeB		0
#define NVT_TOUCH_CTRL_DRIVER 1
#define UPDATE_FIRMWARE 1
#define SELF_TEST 	0
#define ReportRate	0
#define Qualcomm	0
#define NVT_PROXIMITY_FUNC_SUPPORT 0
#define MTK_Mode	0
#define XY_REMAPPING 	0

#define IC 5    //2:NT11002 3:NT11003 4:NT11004 5:NT11205 6:NT11306
#if (IC==2||IC==4)
	#define BUFFER_LENGTH    16384
    #define HW_Address 0x7F
#elif (IC==3||IC==6)
	#define BUFFER_LENGTH    28672
    #define HW_Address 0x7F
#elif (IC==5)
	#define BUFFER_LENGTH    32768
    #define HW_Address 0x70
#endif


struct NVTtouch_ts_data {
	uint16_t addr;
	uint8_t bad_data;
	struct i2c_client *client;
	struct input_dev *input_dev;
	int use_reset;		//use RESET flag
	int use_irq;		//use EINT flag
	int irq_status;
	int read_mode;		//read moudle mode,20110221 by andrew
	struct hrtimer timer;
	struct work_struct  work;
	struct delayed_work touch_debug_work;
	struct delayed_work touch_usb_work;
	struct workqueue_struct *touch_debug_wq;
	struct workqueue_struct *touch_usb_wq;
	struct switch_dev touch_sdev; // add by Tom for switch device
	char phys[32];
	int retry;
	struct early_suspend early_suspend;
	int (*power)(struct NVTtouch_ts_data * ts, int on);
	uint16_t abs_x_max;
	uint16_t abs_y_max;
	uint8_t x_num;
	uint8_t y_num;
	uint8_t max_touch_num;
	uint8_t max_button_num;
	uint8_t int_trigger_type;
	uint8_t chip_ID;
	uint8_t green_wake_mode;
	spinlock_t touch_spinlock;
};

#if NVT_TOUCH_CTRL_DRIVER
struct nvt_flash_data {
	rwlock_t lock;
	unsigned char bufferIndex;
	unsigned int length;
	struct i2c_client *client;
};
#endif
static const char *NVTtouch_ts_name = "NVTCapacitiveTouchScreen";
static struct workqueue_struct *NVTtouch_wq;
struct i2c_client * i2c_connect_client_NVTtouch = NULL; 
	
#ifdef CONFIG_HAS_EARLYSUSPEND
static void NVTtouch_ts_early_suspend(struct early_suspend *h);
static void NVTtouch_ts_late_resume(struct early_suspend *h);
#endif 

//*****************************End of Part I *********************************

//*************************Touchkey Surpport Part*****************************
#define HAVE_TOUCH_KEY
#ifdef HAVE_TOUCH_KEY
	#define READ_COOR_ADDR 0x00
	const uint16_t touch_key_array[]={
									  KEY_BACK,				//MENU
									  KEY_HOME,				//HOME
									  KEY_MENU				//CALL
									 }; 
	#define MAX_KEY_NUM	 (sizeof(touch_key_array)/sizeof(touch_key_array[0]))
#else
	#define READ_COOR_ADDR 0x00
#endif

//*************************End of Touchkey Surpport Part*****************************


#endif /* _LINUX_NVT_TOUCH_H */
