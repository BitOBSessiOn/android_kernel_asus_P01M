/*
 * platform_ar0543.c: ar0543 platform data initilization file
 *
 * (C) Copyright 2014 Asus Corporation
 * Author:
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/atomisp_platform.h>
#include <asm/intel-mid.h>
#include <asm/intel_scu_ipcutil.h>
#include <media/v4l2-subdev.h>
#include <linux/regulator/consumer.h>
#include <linux/sfi.h>
#include <linux/mfd/intel_mid_pmic.h>
//#include <linux/vlv2_plat_clock.h>
#include "platform_camera.h"
#include "platform_ar0543.h"
#include <linux/lnw_gpio.h>


static int camera_reset;
static int camera_vcm_power_down;
static int gpio_cam_2p8_en;
static int gpio_vcm_2p8_en;

/*
 * camera sensor - ar0543 platform data
 */

static int ar0543_gpio_ctrl(struct v4l2_subdev *sd, int flag)
{
	int ret;
	int pin;

	pr_info("%s - E, flag: %d\n", __func__, flag);

/*
	HW_ID_EVB:		pr_info("Hardware VERSION = SR1 + NXP\n");
	HW_ID_SR1:		pr_info("Hardware VERSION = ER + NXP\n");
	HW_ID_PR:		pr_info("Hardware VERSION = ER + TI\n");
	HW_ID_ER:		pr_info("Hardware VERSION = PR + TI\n");
	HW_ID_MP:		pr_info("Hardware VERSION = PR + NXP\n");
*/

	switch (PROJECT_ID) {
	case PROJ_ID_FE380CG:
	case PROJ_ID_FE380CXG:
		if ((HW_ID == HW_ID_EVB) || (HW_ID == HW_ID_SR1) || (HW_ID == HW_ID_MP)) {
			ret = intel_scu_ipc_msic_vprog3(flag);
			pr_info("%s(%d): vcm_2p8_vprog3: %d\n", __func__, __LINE__, flag);
			if (ret) {
				pr_err("%s power vprog3 failed\n", __func__);
				return ret;
			}
		} else if ((HW_ID == HW_ID_ER) || (HW_ID == HW_ID_PR)) {
			if (flag) {
				if (gpio_vcm_2p8_en < 0) {
					int pin = 0;
					pin = camera_sensor_gpio(-1, "BT-reset",
							GPIOF_DIR_OUT, 1);
					if (pin < 0) {
						pr_info("%s(%d)-- error: vcm_2p8_en high fail\n", __func__, __LINE__);
						return pin;
					}
					gpio_vcm_2p8_en = pin; /*71*/
					/* set camera cam_2p8_en pin mode to gpio */
					lnw_gpio_set_alt(gpio_vcm_2p8_en, LNW_GPIO);
				}
				pr_info("%s(%d): vcm_2p8_en: high\n", __func__, __LINE__);
				gpio_set_value(gpio_vcm_2p8_en, 1);
			} else {
				if (gpio_vcm_2p8_en < 0) {
					int pin = 0;
					pin = camera_sensor_gpio(-1, "BT-reset",
								GPIOF_DIR_OUT, 1);
					if (pin < 0) {
						pr_info("%s(%d): vcm_2p8_en: 0 fail\n", __func__, __LINE__);
						return pin;
					}
					gpio_vcm_2p8_en = pin; /*71*/
					/* set camera cam_2p8_en pin mode to gpio */
					lnw_gpio_set_alt(gpio_vcm_2p8_en, LNW_GPIO);
				}
				gpio_set_value(gpio_vcm_2p8_en, 0);
				gpio_free(gpio_vcm_2p8_en);
				gpio_vcm_2p8_en = -1;
				pr_info("%s(%d): vcm_2p8_en: low\n", __func__, __LINE__);
			}
		}


		if (hardware_id == 0x5) {
			ret = intel_scu_ipc_msic_vprog3(flag);
			pr_info("%s(%d): vcm_2p8_vprog3: %d, hardware id = %d\n", __func__, __LINE__, flag, hardware_id);
			if (ret) {
				pr_err("%s power vprog3 failed\n", __func__);
				return ret;
			}
		} else if (hardware_id == 0x6) {
			if (flag) {
				if (gpio_vcm_2p8_en < 0) {
					int pin = 0;
					pin = camera_sensor_gpio(-1, "BT-reset",
							GPIOF_DIR_OUT, 1);
					if (pin < 0) {
						pr_info("%s(%d)-- error: vcm_2p8_en high fail\n", __func__, __LINE__);
						return pin;
					}
					gpio_vcm_2p8_en = pin; /*71*/
					/* set camera cam_2p8_en pin mode to gpio */
					lnw_gpio_set_alt(gpio_vcm_2p8_en, LNW_GPIO);
				}
				pr_info("%s(%d): vcm_2p8_en: high, hardware id = %d\n", __func__, __LINE__, hardware_id);
				gpio_set_value(gpio_vcm_2p8_en, 1);
			} else {
				if (gpio_vcm_2p8_en < 0) {
					int pin = 0;
					pin = camera_sensor_gpio(-1, "BT-reset",
								GPIOF_DIR_OUT, 1);
					if (pin < 0) {
						pr_info("%s(%d): vcm_2p8_en: 0 fail\n", __func__, __LINE__);
						return pin;
					}
					gpio_vcm_2p8_en = pin; /*71*/
					/* set camera cam_2p8_en pin mode to gpio */
					lnw_gpio_set_alt(gpio_vcm_2p8_en, LNW_GPIO);
				}
				gpio_set_value(gpio_vcm_2p8_en, 0);
				gpio_free(gpio_vcm_2p8_en);
				gpio_vcm_2p8_en = -1;
				pr_info("%s(%d): vcm_2p8_en: low, hardware id = %d\n", __func__, __LINE__, hardware_id);
			}
		}
		break;
	default:
		ret = intel_scu_ipc_msic_vprog3(flag);
		if (ret) {
			pr_err("%s power vprog3 failed\n", __func__);
			return ret;
		}		
	}
		
	if (flag)
		usleep_range(1000, 1200);

	if (camera_reset < 0) {

		ret = camera_sensor_gpio(-1, GP_CAMERA_0_RESET,
				GPIOF_DIR_OUT, 1);
		if (ret < 0)
			return ret;

		camera_reset = ret/*9*/;
		/* set camera reset pin mode to gpio */
		lnw_gpio_set_alt(camera_reset, LNW_GPIO);
	}

	if (camera_vcm_power_down < 0) {

		ret = camera_sensor_gpio(-1, "GP_CAMERA_2_PD",
				GPIOF_DIR_OUT, 1);
		if (ret < 0)
			return ret;

		camera_vcm_power_down = ret/*8*/;
		/* set camera vcm power pin mode to gpio */
		lnw_gpio_set_alt(camera_vcm_power_down, LNW_GPIO);
	}

	if (flag) {
		gpio_set_value(camera_reset, 1);
		gpio_set_value(camera_vcm_power_down, 1);
	} else {
		gpio_set_value(camera_reset, 0);
		gpio_free(camera_reset);
		camera_reset = -1;

		gpio_set_value(camera_vcm_power_down, 0);
		gpio_free(camera_vcm_power_down);
		camera_vcm_power_down = -1;
	}
	return 0;
}

static int ar0543_flisclk_ctrl(struct v4l2_subdev *sd, int flag)
{
	static const unsigned int clock_khz = 19200;
	int ret = 0;

	pr_info("%s - E, flag: %d\n", __func__, flag);

	if (intel_mid_identify_cpu() != INTEL_MID_CPU_CHIP_VALLEYVIEW2)
		return intel_scu_ipc_osc_clk(OSC_CLK_CAM0,
					     flag ? clock_khz : 0);

	return ret;
}

/*
 * The power_down gpio pin is to control ar0543's
 * internal power state.
 */

static int ar0543_power_ctrl(struct v4l2_subdev *sd, int flag)
{
	int ret = 0;

	/* TODO:
	 *   Control 2V8 enable pin for FE380CG/FE380CXG.
	 *   Should we control GPIO-11 for ME572C?
	 */

	pr_info("%s() %s++\n", __func__, (flag) ? ("on") : ("off"));

	if (flag) {
		/* VPROG2_1V8 on */
		pr_info("%s(%d): 1V8 on\n", __func__, __LINE__);
		ret = intel_scu_ipc_msic_vprog2(1);
		if (ret) {
			pr_err("%s power vprog2 failed\n", __func__);
			pr_info("%s() %s-- error\n", __func__, (flag) ? ("on") : ("off"));
			return ret;
		}
		usleep_range(1000, 1200);

		/* enable EXTCLK 19.2MHz */
		pr_info("%s(%d): enable EXTCLK 19.2MHz\n", __func__, __LINE__);
		ret = ar0543_flisclk_ctrl(sd, 1);
		if (ret) {
			pr_err("%s ar0543_flisclk_ctrl on err\n", __func__);
			pr_info("%s() %s-- error\n", __func__, (flag) ? ("on") : ("off"));
			return ret;
		}

		/* power-on: RESET_BAR & VCM */
		pr_info("%s(%d): ar0543_gpio_ctrl(1)\n", __func__, __LINE__);
		ret = ar0543_gpio_ctrl(sd, 1);
		if (ret) {
			pr_err("%s ar0543_gpio_ctrl(1) err\n", __func__);
			pr_info("%s() %s-- error\n", __func__, (flag) ? ("on") : ("off"));
			return ret;
		}

		msleep(5);

		/* VPROG1_2V8 on */
		pr_info("%s(%d): 2V8 on\n", __func__, __LINE__);
		ret = intel_scu_ipc_msic_vprog1(1);
		if (ret) {
			pr_err("%s power-up failed\n", __func__);
			pr_info("%s() %s: error\n", __func__, (flag) ? ("on") : ("off"));
		}

		switch (PROJECT_ID) {
		case PROJ_ID_FE380CG:
		case PROJ_ID_FE380CXG:
			/* cam_2p8_en: 1, if fe380 series. */

			/* TODO: fe380cxg here. */
			if (gpio_cam_2p8_en < 0) {
				int pin = 0;

				pin = camera_sensor_gpio(-1, "GP_CAM_2P8_EN",
						GPIOF_DIR_OUT, 1);
				if (pin < 0) {
					pr_info("%s(%d)-- error: cam_2p8_en high fail\n", __func__, __LINE__);
					return pin;
				}

				gpio_cam_2p8_en = pin/*11*/;
				/* set camera cam_2p8_en pin mode to gpio */
				lnw_gpio_set_alt(gpio_cam_2p8_en, LNW_GPIO);
			}
			pr_info("%s(%d): cam_2p8_en: high\n", __func__, __LINE__);
			gpio_set_value(gpio_cam_2p8_en, 1);
			break;
		default:
			break;
		}

		usleep_range(1000, 1200);
	} else {
		/* power-off: RESET_BAR & VCM */
		pr_info("%s(%d): ar0543_gpio_ctrl(0)\n", __func__, __LINE__);
		ret = ar0543_gpio_ctrl(sd, 0);
		if (ret) {
			pr_err("%s ar0543_gpio_ctrl(0) err\n", __func__);
			return ret;
		}

		switch (PROJECT_ID) {
		case PROJ_ID_FE380CG:
		case PROJ_ID_FE380CXG:
			/* cam_2p8_en: 0, if fe380 series. */

			/* TODO: fe380cxg here. */
			if (gpio_cam_2p8_en < 0) {
				int pin = 0;

				pin = camera_sensor_gpio(-1, "GP_CAM_2P8_EN",
							GPIOF_DIR_OUT, 1);
				if (pin < 0) {
					pr_info("%s(%d): cam_2p8_en: 0 fail\n", __func__, __LINE__);
					return pin;
				}

				gpio_cam_2p8_en = pin/*11*/;
				/* set camera cam_2p8_en pin mode to gpio */
				lnw_gpio_set_alt(gpio_cam_2p8_en, LNW_GPIO);
			}
			gpio_set_value(gpio_cam_2p8_en, 0);
			gpio_free(gpio_cam_2p8_en);
			gpio_cam_2p8_en = -1;
			pr_info("%s(%d): cam_2p8_en: low\n", __func__, __LINE__);
			break;
		default:
			break;
		}

		/* VPROG1_2V8 off */
		pr_info("%s(%d): 2V8 off\n", __func__, __LINE__);
		ret = intel_scu_ipc_msic_vprog1(0);
		if (ret) {
			pr_err("%s power-down failed\n", __func__);
			return ret;
		}

		msleep(2);

		/* VPROG2_1V8 off */
		pr_info("%s(%d): 1V8 off\n", __func__, __LINE__);
		ret = intel_scu_ipc_msic_vprog2(0);
		if (ret) {
			pr_err("%s power vprog2 failed\n", __func__);
			return ret;
		}

		/* disable EXTCLK 19.2MHz */
		pr_info("%s(%d): ar0543_flisclk_ctrl(0)\n", __func__, __LINE__);
		ret = ar0543_flisclk_ctrl(sd, 0);
		if (ret) {
			pr_err("%s ar0543_flisclk_ctrl off err\n", __func__);
			return ret;
		}
	}
	pr_info("%s() %s--\n", __func__, (flag) ? ("on") : ("off"));
	return ret;
}

static int ar0543_csi_configure(struct v4l2_subdev *sd, int flag)
{
	pr_info("%s(), flag: %d\n", __func__, flag);
	return camera_sensor_csi(sd, ATOMISP_CAMERA_PORT_PRIMARY, 2,
		ATOMISP_INPUT_FORMAT_RAW_10, atomisp_bayer_order_grbg, flag);
}

static int ar0543_platform_init(struct i2c_client *client)
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
	}
	pr_info("%s(%d):PCB ID = %x\n",
		__func__, __LINE__, PCB_ID);	

	switch (PROJECT_ID) {
	case PROJ_ID_FE380CG:
	case PROJ_ID_FE380CXG:
		project_id = PCB_ID & 0x7;
		hardware_id = (PCB_ID & 0x38) >> 3;
		offset_0 = (PCB_ID & 0x40) >> 6;
		offset_1 = (PCB_ID & 0x180) >> 7;
		offset_2 = (PCB_ID & 0x1E00) >> 9;
		offset_3 = (PCB_ID & 0x6000) >> 13;
		offset_4 = (PCB_ID & 0x8000) >> 15; 
	
		pr_info("%s: project id = %d, hardware id = %d, offset_0 = %d, offset_1 = %d, offset_2 = %d, offset_3 = %d, offset_4 = %d\n", __func__, project_id, hardware_id, offset_0, offset_1, offset_2, offset_3, offset_4);	
		break;
	default:
		break;
	}

	return 0;
}

static int ar0543_platform_deinit(void)
{
	pr_info("%s()\n", __func__);

	return 0;
}

static struct camera_sensor_platform_data ar0543_sensor_platform_data = {
	.gpio_ctrl	= ar0543_gpio_ctrl,
	.flisclk_ctrl	= ar0543_flisclk_ctrl,
	.power_ctrl	= ar0543_power_ctrl,
	.csi_cfg	= ar0543_csi_configure,
	.platform_init = ar0543_platform_init,
	.platform_deinit = ar0543_platform_deinit,
};

void *ar0543_platform_data(void *info)
{
	pr_info("%s()\n", __func__);

	camera_reset = -1;
	camera_vcm_power_down = -1;
	gpio_cam_2p8_en = -1;
	gpio_vcm_2p8_en = -1;

	return &ar0543_sensor_platform_data;
}
