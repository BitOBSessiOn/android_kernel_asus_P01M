/*
 * platform_hm2051.c: hm2051 platform data initilization file
 *
 * (C) Copyright 2013 ASUSTeK COMPUTER INC
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
#include <linux/sfi.h>
#include <linux/lnw_gpio.h>
#include <asm/intel_scu_pmic.h>
#include "platform_camera.h"
#include "platform_hm2051.h"
#include <linux/clk.h>

static int camera_reset;
static int camera_power;

#define VPROG2_VAL 1800000
#define VPROG1_VAL 2800000

static int camera_vemmc1_on;  
static struct regulator *vemmc1_reg;  
#define VEMMC1_VAL 2850000  


static int camera_vprog2_on;
static int camera_vprog1_on; 
static int vcm_power_2_8v;
static int clk_status;

static struct regulator *vprog2_reg;
static struct regulator *vprog1_reg;

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


/*
 * CLV PR0 primary camera sensor - hm2051 platform data
 */

static int hm2051_gpio_ctrl(struct v4l2_subdev *sd, int flag)
{
	int ret;

	if (camera_reset < 0) {
		ret = camera_sensor_gpio(-1, "GP_SUB_CAM_PWDN",
					GPIOF_DIR_OUT, 1);
		if (ret < 0)
			return ret;
		camera_reset = ret;/*48*/
	}

	if (flag) {
		gpio_set_value(camera_reset, 1);
		printk("%s: camera_reset = 1\n", __func__);
		usleep_range(6000, 6500);
	} else {
		gpio_set_value(camera_reset, 0);
		printk("%s: camera_reset = 0\n", __func__);
		gpio_free(camera_reset);
		camera_reset = -1;
		/* 1us - Falling time of REGEN after XCLR H -> L */
		udelay(1);
	}

	return 0;
}

static int hm2051_flisclk_ctrl(struct v4l2_subdev *sd, int flag)
{
	static const unsigned int clock_khz = 19200;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
//SR use osc clock 3 as front MCLK, change to default clock 2 as front MCLK after SR
	if (HW_ID == 0xFF) {
		HW_ID = Read_HW_ID();
	}
	pr_info("HW_ID = %d\n", HW_ID);
	if(HW_ID == 0x3) //SR
	{
	#ifdef CONFIG_X86_INTEL_OSC_CLK
		int ret = 0;
		static const unsigned int clock_khz = 19200;
		struct clk *pclk = clk_get(NULL, "osc.3");
		if (pclk == NULL) {
			pr_err("%s: get osc clock failed\n", __func__);
			return -EINVAL;
		}
		clk_prepare(pclk);
		clk_set_rate(pclk, clock_khz);
		if (flag) {
			if (clk_status == 0) {
				ret = clk_enable(pclk);
				clk_status = 1;
				msleep(20);
				pr_info("%s: osc clock 3 enabled\n", __func__);
			}
		} else {
			if (clk_status == 1) {
				clk_disable(pclk);
				clk_status = 0;
				msleep(20);
				pr_info("%s: osc clock 3 disabled\n", __func__);
			}
		}
		clk_put(pclk);
		return ret;
#else
		pr_err("%s: hm2051 clock is not set\n", __func__);
		return 0;
#endif
	}
	else{
		pr_info("ER board, 2M as front camera\n");
		return intel_scu_ipc_osc_clk(OSC_CLK_CAM1, flag ? clock_khz : 0);
	}
}

/*
 * Checking the SOC type is temporary workaround to enable hm2051
 * on Bodegabay (tangier) platform. Once standard regulator devices
 * (e.g. vprog2, vprog1) and control functions (pmic_avp) are added
 * for the platforms with tangier, then we can revert this change.
 * (dongwon.kim@intel.com)
 */
static int hm2051_power_ctrl(struct v4l2_subdev *sd, int flag)
{
		struct i2c_client *client = v4l2_get_subdevdata(sd);
		int ret = 0, gpio;

		if (flag) {
			if (camera_reset < 0) {
				ret = camera_sensor_gpio(-1, "GP_SUB_CAM_PWDN",
							GPIOF_DIR_OUT, 1);
				if (ret < 0)
					return ret;
				camera_reset = ret;/*48*/
			}
	    
			if (camera_reset >= 0){
				gpio_set_value(camera_reset, 0);
				printk("%s: camera_reset = 0\n", __func__);
				msleep(1);
			}
		}

		if (flag) {
			ret = intel_scu_ipc_msic_vprog2(flag);	//1.8V
			if (ret) {
				pr_err("%s: 1.8V power failed\n", __func__);
				return ret;
			}

			ret = intel_scu_ipc_msic_vprog1(flag);	//2.8V
			if (ret) {
				pr_err("%s: 2.8V power failed\n", __func__);
				return ret;
			}
		} else {
			ret = intel_scu_ipc_msic_vprog1(flag);	//2.8V
			if (ret) {
				pr_err("%s: 2.8V power failed\n", __func__);
				return ret;
			}
			msleep(3);

			ret = intel_scu_ipc_msic_vprog2(flag);	//1.8V
			if (ret) {
				pr_err("%s: 1.8V power failed\n", __func__);
				return ret;
			}
		}

		if (flag)
			usleep_range(1000, 1200);

		return ret;
}

static int hm2051_csi_configure(struct v4l2_subdev *sd, int flag)
{
	return camera_sensor_csi(sd, ATOMISP_CAMERA_PORT_SECONDARY, 1,
		ATOMISP_INPUT_FORMAT_RAW_10, atomisp_bayer_order_bggr, flag);  //mod by marky
}

/*
 * Checking the SOC type is temporary workaround to enable hm2051
 * on Bodegabay (tangier) platform. Once standard regulator devices
 * (e.g. vprog2, vprog1) and control functions (pmic_avp) are added
 * for the platforms with tangier, then we can revert this change.
 * (dongwon.kim@intel.com)
 */
static int hm2051_platform_init(struct i2c_client *client)
{
	pr_info("%s()\n", __func__);

	return 0;
}

/*
 * Checking the SOC type is temporary workaround to enable hm2051 on Bodegabay
 * (tangier) platform once standard regulator devices (e.g. vprog2, vprog1) and
 * control functions (pmic_avp) are added for the platforms with tangier, then
 * we can revert this change.(dongwon.kim@intel.com
 */
static int hm2051_platform_deinit(void)
{
	pr_info("%s()\n", __func__);

	return 0;
}
static struct camera_sensor_platform_data hm2051_sensor_platform_data = {
	.gpio_ctrl      = hm2051_gpio_ctrl,
	.flisclk_ctrl   = hm2051_flisclk_ctrl,
	.power_ctrl     = hm2051_power_ctrl,
	.csi_cfg        = hm2051_csi_configure,
	.platform_init = hm2051_platform_init,
	.platform_deinit = hm2051_platform_deinit,
};

void *hm2051_platform_data(void *info)
{
	camera_reset = -1;
	camera_power = -1;
	vcm_power_2_8v = -1;
	clk_status = 0;

	return &hm2051_sensor_platform_data;
}

