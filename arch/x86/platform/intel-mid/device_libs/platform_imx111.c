/*
 * platform_imx111.c: imx111 platform data initilization file
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
#include "platform_camera.h"
#include "platform_imx111.h"

#define GP_CAMERA_CAM_1P2_EN	"MAIN_CAM_1V2_EN"
#define GP_CAMERA_MAIN_CAM_PWDN	"camera_0_reset"

static int camera_vprog1_on;
static int camera_vprog2_on;
static int camera_vprog3_on;
static int camera_power_1p2_en;
static int camera_reset;

/*
 * CLV PR0 primary camera sensor - IMX111 platform data
 */

static int imx111_gpio_ctrl(struct v4l2_subdev *sd, int flag)
{
	int ret;

	if (camera_reset < 0) {
#if 0
		camera_reset = 9;
		/* set camera reset pin mode to gpio */
		lnw_gpio_set_alt(camera_reset, LNW_GPIO);
#endif
		ret = camera_sensor_gpio(9, GP_CAMERA_MAIN_CAM_PWDN,
					GPIOF_DIR_OUT, 1);
		if (ret < 0) {
			printk("%s not available.\n", GP_CAMERA_MAIN_CAM_PWDN);
			return ret;
		}
		camera_reset = 9;
	}

	if (flag) {
		gpio_set_value(camera_reset, 0);
		msleep(20);
		gpio_set_value(camera_reset, 1);
	} else {
/*
		gpio_set_value(camera_reset, 0);
		if (camera_reset >= 0){
			gpio_set_value(camera_reset, 0);
			gpio_free(camera_reset);
			camera_reset = -1;
			msleep(10);
		}
*/
	}

	return 0;
}

static int imx111_flisclk_ctrl(struct v4l2_subdev *sd, int flag)
{
	static const unsigned int clock_khz = 19200;
	return intel_scu_ipc_osc_clk(OSC_CLK_CAM0, flag ? clock_khz : 0);
}

/*
 * Checking the SOC type is temporary workaround to enable IMX111
 * on Bodegabay (tangier) platform. Once standard regulator devices
 * (e.g. vprog1, vprog2) and control functions (pmic_avp) are added
 * for the platforms with tangier, then we can revert this change.
 * (dongwon.kim@intel.com)
 */
static int imx111_power_ctrl(struct v4l2_subdev *sd, int flag)
{
	int ret = 0;
	//int i = 0;

	if (camera_power_1p2_en < 0) {
#if 0
		camera_power_1p2_en = 63;
		/* set camera 1.2V enable pin mode to gpio */
		lnw_gpio_set_alt(camera_power_1p2_en, LNW_GPIO);
#endif

		ret = camera_sensor_gpio(63, GP_CAMERA_CAM_1P2_EN,
					 GPIOF_DIR_OUT, 0);
		if (ret < 0){
			printk("%s not available.\n", GP_CAMERA_CAM_1P2_EN);
			return ret;
		}
		camera_power_1p2_en = 63;

	}

	if (flag) {
		//turn on VCM 2.8V
		if (!camera_vprog3_on) {
			ret = intel_scu_ipc_msic_vprog3(1);
			if (!ret) {
				camera_vprog3_on = 1;
				printk("<<< VCM 2.8V = 1\n");
			}
			else{
				printk(KERN_ALERT "Failed to enable regulator vprog3\n");
				return ret;
			}
			msleep(10);
		}

		//turn on 2.8V power
		if (!camera_vprog1_on) {
			ret = intel_scu_ipc_msic_vprog1(1);
			if (!ret) {
				camera_vprog1_on = 1;
				printk("<<< 2.8V = 1\n");
			}
			else{
				printk(KERN_ALERT "Failed to enable regulator vprog1\n");
				return ret;
			}
			msleep(10);
		}

		//turn on 1.2V power
		if (camera_power_1p2_en >= 0){
			gpio_set_value(camera_power_1p2_en, 1);
			printk("<<< 1.2V = 1\n");
			msleep(10);
		}

		//turn on 1.8V power
		if (!camera_vprog2_on) {
			ret = intel_scu_ipc_msic_vprog2(1);
			if (!ret) {
				camera_vprog2_on = 1;
				printk("<<< 1.8V = 1\n");
			}
			else{
				printk(KERN_ALERT "Failed to enable regulator vprog2\n");
				return ret;
			}
			msleep(10);
		}
	} else {
		//turn off 1.8V power
		if (camera_vprog2_on) {
			ret = intel_scu_ipc_msic_vprog2(0);
			if (!ret) {
				camera_vprog2_on = 0;
				printk("<<< 1.8V = 0\n");
			}
			else {
				printk(KERN_ALERT "Failed to disable regulator vprog2\n");
				return ret;
			}
			msleep(10);
		}

		//turn off 1.2V power
		if (camera_power_1p2_en >= 0){
			gpio_set_value(camera_power_1p2_en, 0);
			printk("<<< 1.2V = 0\n");
			gpio_free(camera_power_1p2_en);
			camera_power_1p2_en = -1;
			msleep(10);
		}

		//turn off 2.8V power
		if (camera_vprog1_on) {
			ret = intel_scu_ipc_msic_vprog1(0);
			if (!ret) {
				camera_vprog1_on = 0;
				printk("<<< 2.8V = 0\n");
			}
			else {
				printk(KERN_ALERT "Failed to disable regulator vprog1\n");
				return ret;
			}
			msleep(10);
		}

		//turn off VCM 2.8V
		if (camera_vprog3_on) {
			ret = intel_scu_ipc_msic_vprog3(0);
			if (!ret) {
				camera_vprog3_on = 0;
				printk("<<< VCM 2.8V = 0\n");
			}
			else {
				printk(KERN_ALERT "Failed to disable regulator vprog3\n");
				return ret;
			}
			msleep(10);
		}
	}
	return ret;
}

static int imx111_csi_configure(struct v4l2_subdev *sd, int flag)
{
	static const int LANES = 2;
	return camera_sensor_csi(sd, ATOMISP_CAMERA_PORT_PRIMARY, LANES,
		ATOMISP_INPUT_FORMAT_RAW_10, atomisp_bayer_order_bggr, flag);
}

/*
 * Checking the SOC type is temporary workaround to enable IMX111
 * on Bodegabay (tangier) platform. Once standard regulator devices
 * (e.g. vprog1, vprog2) and control functions (pmic_avp) are added
 * for the platforms with tangier, then we can revert this change.
 * (dongwon.kim@intel.com)
 */
static int imx111_platform_init(struct i2c_client *client)
{
	return 0;
}

/*
 * Checking the SOC type is temporary workaround to enable IMX111 on Bodegabay
 * (tangier) platform once standard regulator devices (e.g. vprog1, vprog2) and
 * control functions (pmic_avp) are added for the platforms with tangier, then
 * we can revert this change.(dongwon.kim@intel.com
 */
static int imx111_platform_deinit(void)
{
	return 0;
}
static struct camera_sensor_platform_data imx111_sensor_platform_data = {
	.gpio_ctrl      = imx111_gpio_ctrl,
	.flisclk_ctrl   = imx111_flisclk_ctrl,
	.power_ctrl     = imx111_power_ctrl,
	.csi_cfg        = imx111_csi_configure,
	.platform_init = imx111_platform_init,
	.platform_deinit = imx111_platform_deinit,
};

void *imx111_platform_data(void *info)
{
	camera_reset = -1;
	camera_power_1p2_en = -1;

	return &imx111_sensor_platform_data;
}
