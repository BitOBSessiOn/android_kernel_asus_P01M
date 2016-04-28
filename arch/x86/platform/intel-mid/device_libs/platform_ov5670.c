/*
 * platform_ov5670.c: ov5670 platform data initilization file
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
#include "platform_ov5670.h"
#include <linux/clk.h>

#define VPROG1_VAL 2800000
#define VPROG2_VAL 1800000
#define VEMMC1_VAL 2850000  

static int camera_reset;
static int camera_power;
static int camera_vemmc1_on;
static int camera_vprog1_on;
static int camera_vprog2_on;
static int camera_1v2_power_enable;
static int camera_2v8_vcm_power_enable;
static int vcm_power_2_8v;
static int clk_status;

static struct regulator *vemmc1_reg;  
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
 * CLV PR0 primary camera sensor - OV5670 platform data
 */

static int ov5670_gpio_ctrl(struct v4l2_subdev *sd, int flag)
{
	int ret;
	
	if (offset_3 == 0) {
		if (camera_reset < 0) {
			ret = camera_sensor_gpio(-1, "GP_SUB_CAM_PWDN",
						GPIOF_DIR_OUT, 0);
			printk("%s: request gpio camera_reset = %d, flag = %d\n", __func__, ret, flag);
			if (ret < 0)
				return ret;
			camera_reset = ret;/*48*/
			printk("%s: 8M sku offset_3 = %d\n", __func__, offset_3);
		}
	} else {
		if (camera_reset < 0) {
			ret = camera_sensor_gpio(-1, GP_CAMERA_0_RESET,
						GPIOF_DIR_OUT, 0);
			printk("%s: request gpio camera_reset = %d, flag = %d\n", __func__, ret, flag);
			if (ret < 0)
				return ret;
			camera_reset = ret; /*9*/
			printk("%s: 5M sku offset_3 = %d\n", __func__, offset_3);
		}
	}

	if (flag) {
		gpio_set_value(camera_reset, 1);
		printk("%s: camera_reset = 1\n", __func__);
		usleep_range(6000, 6500);
	} else {
		gpio_set_value(camera_reset, 0);
		printk("%s: camera_reset = 0\n", __func__);
		if (offset_3 == 0) {
			gpio_free(camera_reset);
			printk("%s: gpio free camera_reset = %d\n", __func__, camera_reset);
			camera_reset = -1;
		}
		/* 1us - Falling time of REGEN after XCLR H -> L */
		udelay(1);
	}

	return 0;
}

static int ov5670_flisclk_ctrl(struct v4l2_subdev *sd, int flag)
{
	static const unsigned int clock_khz = 19200;
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	pr_info("%s(), flag = %d\n", __func__, flag);
//SR use osc clock 2,3 as rear/front MCLK, change to default clock 1,2 as rear/front MCLK after SR
	pr_info("HW_ID = %d\n", HW_ID);
	if(HW_ID == 0x3) //SR
	{
		pr_info("SR board \n");
		if (offset_3 == 0) {
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
		} else {
			return intel_scu_ipc_osc_clk(OSC_CLK_CAM1, flag ? clock_khz : 0);
		}
	}
	else{ //ER 
		if (offset_3 == 0) { //as front camera
			pr_info("ER board, 5M as front camera\n");
			return intel_scu_ipc_osc_clk(OSC_CLK_CAM1, flag ? clock_khz : 0);
		}else{ //as rear camera
			pr_info("ER board, 5M as rear camera\n");
			return intel_scu_ipc_osc_clk(OSC_CLK_CAM0, flag ? clock_khz : 0);
		}
	}
}

/*
 * Checking the SOC type is temporary workaround to enable OV5670
 * on Bodegabay (tangier) platform. Once standard regulator devices
 * (e.g. vprog2, vprog1) and control functions (pmic_avp) are added
 * for the platforms with tangier, then we can revert this change.
 * (dongwon.kim@intel.com)
 */
static int ov5670_power_ctrl(struct v4l2_subdev *sd, int flag)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0, gpio;

	if (offset_3 != 0) {
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
	}
	
	if (camera_1v2_power_enable < 0) {
		gpio = camera_sensor_gpio(-1, "GP_CAMERA_2_PD",
				GPIOF_DIR_OUT, 0);
		if (gpio < 0)
			return gpio;
		camera_1v2_power_enable = gpio;
		/* set camera vcm power pin mode to gpio */
		lnw_gpio_set_alt(camera_1v2_power_enable, LNW_GPIO);
		pr_err("%s: Set 1.2V as GPIO\n", __func__);
	}

	if (flag) {
		if (offset_3 == 0) {
			if (camera_reset < 0) {
		
				ret = camera_sensor_gpio(-1, "GP_SUB_CAM_PWDN",
							GPIOF_DIR_OUT, 0);
				printk("%s: request gpio camera_reset = %d, flag = %d\n", __func__, ret, flag);
				if (ret < 0)
					return ret;
		
				camera_reset = ret;/*48*/
				printk("%s: 8M sku offset_3 = %d\n", __func__, offset_3);
			}
		} else {
			if (camera_reset < 0) {
				ret = camera_sensor_gpio(-1, GP_CAMERA_0_RESET,
							GPIOF_DIR_OUT, 0);
				printk("%s: request gpio camera_reset = %d, flag = %d\n", __func__, ret, flag);
				if (ret < 0)
					return ret;
				camera_reset = ret; /*9*/
				printk("%s: 5M sku offset_3 = %d\n", __func__, offset_3);
			}
		}

		if (camera_reset >= 0){
			gpio_set_value(camera_reset, 0);
			printk("%s: camera_reset = 0\n", __func__);
			msleep(1);
		}
	}

	if (flag) {
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

		if (offset_3 != 0) {
			ret = control_vcm_phy_power(PMIC_VLDOCNT, flag);
			pr_err("%s: Set camera vcm 2.8V(V_SWITCH) on\n", __func__);
			if (ret) {
				pr_err("%s: VCM 3.3V power failed\n", __func__);
				return ret;
			}
		}

		if (offset_3 != 0) {
			pr_err("%s: Set camera vcm 2.8V on = %d\n", __func__, camera_2v8_vcm_power_enable);
			gpio_set_value(camera_2v8_vcm_power_enable, 1);
		}
		pr_err("%s: Set camera 1.2V on = %d\n", __func__, camera_1v2_power_enable);
		gpio_set_value(camera_1v2_power_enable, 1);
	}else {
		pr_err("%s: Set camera 1.2V off\n", __func__);
		gpio_set_value(camera_1v2_power_enable, 0);
		gpio_free(camera_1v2_power_enable);
		camera_1v2_power_enable = -1;
		if (offset_3 != 0) {
			pr_err("%s: Set camera vcm 2.8V off\n", __func__);
			gpio_set_value(camera_2v8_vcm_power_enable, 0);
			gpio_free(camera_2v8_vcm_power_enable);
			camera_2v8_vcm_power_enable = -1;
		}

		if (offset_3 != 0) {
			ret = control_vcm_phy_power(PMIC_VLDOCNT, flag);
			pr_err("%s: Set camera vcm 2.8V(V_SWITCH) off\n", __func__);
			if (ret) {
				pr_err("%s: VCM 3.3V power failed\n", __func__);
				return ret;
			}
		}
		
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
	}
	
	if (flag)
		usleep_range(1000, 1200);

	return ret;
}

static int ov5670_csi_configure(struct v4l2_subdev *sd, int flag)
{
	static const int LANES = 2;
	
	if (offset_3 == 0) {
		return camera_sensor_csi(sd, ATOMISP_CAMERA_PORT_SECONDARY, LANES,
    	           ATOMISP_INPUT_FORMAT_RAW_10, atomisp_bayer_order_bggr, flag);		
	} else {
		return camera_sensor_csi(sd, ATOMISP_CAMERA_PORT_PRIMARY, LANES,
				   ATOMISP_INPUT_FORMAT_RAW_10, atomisp_bayer_order_bggr, flag);
	}
}

/*
 * Checking the SOC type is temporary workaround to enable OV5670
 * on Bodegabay (tangier) platform. Once standard regulator devices
 * (e.g. vprog2, vprog1) and control functions (pmic_avp) are added
 * for the platforms with tangier, then we can revert this change.
 * (dongwon.kim@intel.com)
 */
static int ov5670_platform_init(struct i2c_client *client)
{
	pr_info("%s()\n", __func__);

	if (PROJECT_ID == 0xFF) {
		PROJECT_ID = Read_PROJ_ID();
	}
	pr_info("%s(%d):Project ID = %d\n",
		__func__, __LINE__, PROJECT_ID);

	if (HW_ID == 0xFF) {
		HW_ID = Read_HW_ID();
	}
	pr_info("%s(%d):HW ID = %d\n",
		__func__, __LINE__, HW_ID);	

	if (PCB_ID == 0xFF) {
		PCB_ID = Read_PCB_ID();

		pr_info("%s(%d):PCB ID = %x\n",
			__func__, __LINE__, PCB_ID);

		switch (PROJECT_ID) {
		case PROJ_ID_Z580C:
			project_id = PCB_ID & 0x7;
			hardware_id = (PCB_ID & 0x38) >> 3;
			offset_0 = (PCB_ID & 0x40) >> 6;
			offset_1 = (PCB_ID & 0x80) >> 7;
			offset_2 = (PCB_ID & 0x700) >> 8;
			offset_3 = (PCB_ID & 0x1800) >> 11;
			offset_4 = (PCB_ID & 0x2000) >> 13; 

			//offset_0: Panel ID, offset_1: Wifi ID, offset_2: RF ID, offset_3: Main Camera ID, offset_4: Sub Camera ID
			//PCB_ID = pentry->project_id | pentry->hardware_id << 3 | pentry->offset_0 << 6 | pentry->offset_1 << 7 | pentry->offset_2 << 8 | pentry->offset_3 << 11 | pentry->offset_4 << 13;
	
			pr_info("%s: project id = %d, hardware id = %d, offset_0 = %d, offset_1 = %d, offset_2 = %d, offset_3 = %d, offset_4 = %d\n", __func__, project_id, hardware_id, offset_0, offset_1, offset_2, offset_3, offset_4);
			break;
		default:
			break;
		}
	}

	return 0;
}

/*
 * Checking the SOC type is temporary workaround to enable OV5670 on Bodegabay
 * (tangier) platform once standard regulator devices (e.g. vprog2, vprog1) and
 * control functions (pmic_avp) are added for the platforms with tangier, then
 * we can revert this change.(dongwon.kim@intel.com
 */
static int ov5670_platform_deinit(void)
{
	pr_info("%s()\n", __func__);

	return 0;
}
static struct camera_sensor_platform_data ov5670_sensor_platform_data = {
	.gpio_ctrl      = ov5670_gpio_ctrl,
	.flisclk_ctrl   = ov5670_flisclk_ctrl,
	.power_ctrl     = ov5670_power_ctrl,
	.csi_cfg        = ov5670_csi_configure,
	.platform_init = ov5670_platform_init,
	.platform_deinit = ov5670_platform_deinit,
};

void *ov5670_platform_data(void *info)
{
	camera_reset = -1;
	camera_power = -1;
	camera_1v2_power_enable = -1;
	camera_2v8_vcm_power_enable = -1;
	vcm_power_2_8v = -1;
	clk_status = 0;

	return &ov5670_sensor_platform_data;
}
