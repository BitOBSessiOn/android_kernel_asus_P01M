/*
 * platform_t4k35.c: t4k35 platform data initilization file
 *
 * (C) Copyright 2008 Intel Corporation
 * Author:
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/atomisp_platform.h>
#include <asm/intel_scu_ipcutil.h>
#include <asm/intel-mid.h>
#include <media/v4l2-subdev.h>
#include <linux/regulator/consumer.h>
#include "platform_camera.h"
#include "platform_t4k35.h"
#include <linux/acpi_gpio.h>
#include <linux/lnw_gpio.h>
#include <asm/intel_scu_pmic.h>


static int camera_reset;
static int camera_power;
static int camera_1v2_power_enable;
static int camera_2v8_vcm_power_enable;

static int camera_vemmc1_on;  
static struct regulator *vemmc1_reg;  
#define VEMMC1_VAL 2850000  

static int camera_vprog1_on;
static struct regulator *vprog1_reg;
#define VPROG1_VAL 2800000

static int is_ctp(void)
{
	return INTEL_MID_BOARD(1, PHONE, CLVTP)
	       || INTEL_MID_BOARD(1, TABLET, CLVT);
}

static int is_victoriabay(void)
{
	return INTEL_MID_BOARD(2, PHONE, CLVTP, VB, PRO)
	       || INTEL_MID_BOARD(2, PHONE, CLVTP, VB, ENG)
	       || INTEL_MID_BOARD(3, PHONE, CLVTP, RHB, PRO, VVLITE)
	       || INTEL_MID_BOARD(3, PHONE, CLVTP, RHB, ENG, VVLITE)
	       || ((INTEL_MID_BOARD(2, PHONE, CLVTP, RHB, PRO)
		    || INTEL_MID_BOARD(2, PHONE, CLVTP, RHB, ENG))
		   && (SPID_HARDWARE_ID(CLVTP, PHONE, VB, PR1A)   
		       || SPID_HARDWARE_ID(CLVTP, PHONE, VB, PR1B)
		       || SPID_HARDWARE_ID(CLVTP, PHONE, VB, PR20)));
}

static int is_moorefield(void)
{
	return INTEL_MID_BOARD(1, PHONE, MOFD) ||
	       INTEL_MID_BOARD(1, TABLET, MOFD);
}

static int control_vcm_phy_power(u16 addr, bool on_off)
{
	int ret;
	u8 mask, bits;

	if (addr == PMIC_VLDOCNT)
		mask = PMIC_VLDOCNT_VSWITCHEN;
	else
		return -EINVAL;

	if (on_off)
		bits = mask;
	else
		bits = 0x00;

	ret = intel_scu_ipc_update_register(addr,
			bits, mask);

	/* Debounce 10ms for turn on VUSBPHY */
	if (on_off)
		usleep_range(10000, 11000);

	return ret;
}

/*
 * MRFLD VV primary camera sensor - T4K35 platform data
 */

static int t4k35_gpio_ctrl(struct v4l2_subdev *sd, int flag)
{
	int ret;

	if (camera_reset < 0) {
		ret = camera_sensor_gpio(-1, GP_CAMERA_0_RESET,
					GPIOF_DIR_OUT, 0);
		printk("%s: request gpio camera_reset = %d, flag = %d\n", __func__, ret, flag);
		if (ret < 0)
			return ret;
		camera_reset = ret;
	}

	if (flag) {
		gpio_set_value(camera_reset, 1);
		printk("%s: camera_reset = 1\n", __func__);	
	} else {
		gpio_set_value(camera_reset, 0);
		printk("%s: camera_reset = 0\n", __func__);
		gpio_free(camera_reset);
		printk("%s: gpio free camera_reset = %d\n", __func__, camera_reset);
		camera_reset = -1;		
	}
	/* min 250us -Initializing time of silicon */
	usleep_range(10000, 15000);

	return 0;
}

static int t4k35_flisclk_ctrl(struct v4l2_subdev *sd, int flag)
{
	static const unsigned int clock_khz = 19200;
//SR use osc clock 2 as rear MCLK, change to default clock 1 as rear MCLK after SR
	if (HW_ID == 0xFF) {
		HW_ID = Read_HW_ID();
	}
	pr_info("HW_ID = %d\n", HW_ID);
	if(HW_ID == 0x3){ //SR
		pr_info("SR board!!\n");
		return intel_scu_ipc_osc_clk(OSC_CLK_CAM1, flag ? clock_khz : 0);
	}
	else{//ER
		pr_info("ER board!!\n");
		return intel_scu_ipc_osc_clk(OSC_CLK_CAM0, flag ? clock_khz : 0);
	}
}

static int t4k35_power_ctrl(struct v4l2_subdev *sd, int flag)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0, gpio;

	if (camera_reset < 0) {
		ret = camera_sensor_gpio(-1, GP_CAMERA_0_RESET,
					GPIOF_DIR_OUT, 0);
		printk("%s: request gpio camera_reset = %d, flag = %d\n", __func__, ret, flag);
		if (ret < 0)
			return ret;
		camera_reset = ret;
	}

	if (camera_reset >= 0){
		gpio_set_value(camera_reset, 0);
		printk("%s: camera_reset = 0\n", __func__);
		gpio_free(camera_reset);
		printk("%s: gpio free camera_reset = %d\n", __func__, camera_reset);
		camera_reset = -1;	
		msleep(1);
	}

	ret = intel_scu_ipc_msic_vprog1(flag);	//2.8V
	if (ret) {
		pr_err("%s: 2.8V power failed\n", __func__);
		return ret;
	}

	ret = intel_scu_ipc_msic_vprog2(flag);	//1.8V
	if (ret) {
		pr_err("%s: 1.8V power failed\n", __func__);
		return ret;
	}

	ret = control_vcm_phy_power(PMIC_VLDOCNT, flag);
	if (ret) {
		pr_err("%s: VCM 3.3V power failed\n", __func__);
		return ret;
	}

	if (camera_2v8_vcm_power_enable < 0) {

		gpio = camera_sensor_gpio(-1, "NFC-intr",
				GPIOF_DIR_OUT, 0);
		if (gpio < 0)
			return gpio;

		camera_2v8_vcm_power_enable = gpio;/*174*/
		/* set camera vcm power pin mode to gpio */
		lnw_gpio_set_alt(camera_2v8_vcm_power_enable, LNW_GPIO);
		pr_err("%s: set 2.8V vcm as GPIO\n", __func__);
	}

	if (camera_1v2_power_enable < 0) {

		gpio = camera_sensor_gpio(-1, "GP_CAMERA_2_PD",
				GPIOF_DIR_OUT, 0);
		if (gpio < 0)
			return gpio;

		camera_1v2_power_enable = gpio/*8*/;
		/* set camera vcm power pin mode to gpio */
		lnw_gpio_set_alt(camera_1v2_power_enable, LNW_GPIO);
		pr_err("%s: set 1.2V as GPIO\n", __func__);
	}

	if (flag) {
		pr_err("%s: Set camera vcm 2.8V on = %d\n", __func__, camera_2v8_vcm_power_enable);
		gpio_set_value(camera_2v8_vcm_power_enable, 1);
		
		pr_err("%s: Set camera 1.2V on = %d\n", __func__, camera_1v2_power_enable);
		gpio_set_value(camera_1v2_power_enable, 1);
	} else {
		pr_err("%s: Set camera 1.2V off\n", __func__);
		gpio_set_value(camera_1v2_power_enable, 0);
		gpio_free(camera_1v2_power_enable);
		camera_1v2_power_enable = -1;
		
		pr_err("%s: Set camera vcm 2.8V off\n", __func__);
		gpio_set_value(camera_2v8_vcm_power_enable, 0);
		gpio_free(camera_2v8_vcm_power_enable);
		camera_2v8_vcm_power_enable = -1;
	}		

	if (ret)
		pr_err("%s: t4k35 power failed\n", __func__);
	
	if (flag)
		usleep_range(1000, 1200);

	return ret;
}

static int t4k35_platform_init(struct i2c_client *client)
{
	int ret;

	return 0;
}

static int t4k35_platform_deinit(void)
{
	return 0;
}

static int t4k35_csi_configure(struct v4l2_subdev *sd, int flag)
{
	static const int LANES = 4;
	return camera_sensor_csi(sd, ATOMISP_CAMERA_PORT_PRIMARY, LANES,
		ATOMISP_INPUT_FORMAT_RAW_10, atomisp_bayer_order_grbg, flag);
}


static struct camera_sensor_platform_data t4k35_sensor_platform_data = {
	.gpio_ctrl      = t4k35_gpio_ctrl,
	.flisclk_ctrl   = t4k35_flisclk_ctrl,
	.power_ctrl     = t4k35_power_ctrl,
	.csi_cfg        = t4k35_csi_configure,
	.platform_init = t4k35_platform_init,
	.platform_deinit = t4k35_platform_deinit,
};

void *t4k35_platform_data(void *info)
{
	camera_reset = -1;
	camera_power = -1;
	camera_1v2_power_enable = -1;
	camera_2v8_vcm_power_enable = -1;

	return &t4k35_sensor_platform_data;
}
