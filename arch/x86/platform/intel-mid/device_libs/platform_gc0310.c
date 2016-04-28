/*
 * platform_gc0310.c: gc0310 platform data initilization file
 *
 * (C) Copyright 2014 ASUS Corporation
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
#include "platform_gc0310.h"
#include <linux/acpi_gpio.h>
#include <linux/lnw_gpio.h>

static int camera_reset;
static int gpio_cam_pwdn;
static int gpio_cam_2p8_en;

/*
 * MFLD PR2 secondary camera sensor - gc0310 platform data
 */
static int gc0310_gpio_ctrl(struct v4l2_subdev *sd, int flag)
{
	int ret;

	pr_info("%s - E, flag: %d\n", __func__, flag);

	if (gpio_cam_pwdn < 0) {
		ret = camera_sensor_gpio(-1, "GP_SUB_CAM_PWDN",
						GPIOF_DIR_OUT, 0);
		if (ret < 0)
			return ret;
		gpio_cam_pwdn = ret; /*55*/
		/* set camera pwdn pin mode to gpio */
		lnw_gpio_set_alt(gpio_cam_pwdn, LNW_GPIO);
	}

#if 0
	if (camera_reset < 0) {
#if 0
		ret = camera_sensor_gpio(-1, GP_CAMERA_1_RESET,
						GPIOF_DIR_OUT, 1);
		if (ret < 0)
			return ret;
#endif
		camera_reset = 10/*ret*/;
		/* set camera reset pin mode to gpio */
		lnw_gpio_set_alt(camera_reset, LNW_GPIO);
	}
#endif
	if (flag == 1) {
		/* gc0310 Power down:
		 * FE380CG front: gpio55 high to low
		 * FE380CXG rear: gpio9 high to low
		 */
		pr_info("%s(%d): pwdn(0)\n", __func__, __LINE__);
		gpio_set_value(gpio_cam_pwdn, 0);
		pr_info("%s(%d): pwdn(1)\n", __func__, __LINE__);
		gpio_set_value(gpio_cam_pwdn, 1);
		usleep_range(10, 20);
		pr_info("%s(%d): pwdn(0)\n", __func__, __LINE__);
		gpio_set_value(gpio_cam_pwdn, 0);
#if 0
		pr_info("%s(%d): reset(1)\n", __func__, __LINE__);
		gpio_set_value(camera_reset, 1);
#endif
	} else if (flag == 2) {
		pr_info("%s(%d): pwdn(1)\n", __func__, __LINE__);
		gpio_set_value(gpio_cam_pwdn, 1);
	} else if (flag == 0) {
		/* gc0310 Power down:
		 * FE380CG front: gpio55 low to high
		 * FE380CXG rear: gpio9 low to high
		 */
		pr_info("%s(%d): pwdn(0)\n", __func__, __LINE__);
		gpio_set_value(gpio_cam_pwdn, 0);
		gpio_free(gpio_cam_pwdn);
		gpio_cam_pwdn = -1;

#if 0
		pr_info("%s(%d): reset(0)\n", __func__, __LINE__);
		gpio_set_value(camera_reset, 0);
		gpio_free(camera_reset);
		camera_reset = -1;
#endif
	}

	return 0;
}

static int gc0310_flisclk_ctrl(struct v4l2_subdev *sd, int flag)
{
	static const unsigned int clock_khz = 19200;

	pr_info("%s(), flag: %d\n", __func__, flag);

	pr_info("%s: CLK_CAM1\n", __func__);
	return intel_scu_ipc_osc_clk(OSC_CLK_CAM1,
				flag ? clock_khz : 0);
}

static int gc0310_power_ctrl(struct v4l2_subdev *sd, int flag)
{
	int ret = 0;

	/* TODO:
	 *   Control 2V8 enable pin for FE380CG/FE380CXG.
	 *   Should we control GPIO-11 for ME572C?
	 */

	pr_info("%s() %s++\n", __func__, (flag) ? ("on") : ("off"));

	if (flag) {
		/* 1V8 on */
		pr_info("%s(%d): 1V8 on\n", __func__, __LINE__);
		ret = intel_scu_ipc_msic_vprog2(1);
		if (ret) {
			pr_err("%s power vprog2 failed\n", __func__);
			return ret;
		}

		usleep_range(1000, 1500);

		/* 2V8 on */
		pr_info("%s(%d): 2V8 on\n", __func__, __LINE__);
		ret = intel_scu_ipc_msic_vprog1(1);
		if (ret) {
			pr_err("%s power failed\n", __func__);
			return ret;
		}

		usleep_range(1000, 1500);

		/* cam_2p8_en: 1, if fe380 series. */
#if 0
		/* TODO: fe380cxg here. */
		if (gpio_cam_2p8_en < 0) {
#if 0
			ret = camera_sensor_gpio(-1, GP_CAM_2P8_EN,
					GPIOF_DIR_OUT, 1);
			if (ret < 0)
				return ret;
#endif
			gpio_cam_2p8_en = 11/*ret*/;
			/* set camera cam_2p8_en pin mode to gpio */
			lnw_gpio_set_alt(gpio_cam_2p8_en, LNW_GPIO);
		}
		gpio_set_value(gpio_cam_2p8_en, 1);

		usleep_range(1000, 5000);
#endif

		/* Enable MCLK: 19.2MHz */
		pr_info("%s(%d): mclk on\n", __func__, __LINE__);
		ret = gc0310_flisclk_ctrl(sd, 1);
		if (ret) {
			pr_err("%s flisclk_ctrl on failed\n", __func__);
			return ret;
		}

		usleep_range(5000, 6000);

		/* Power-down & Reset */
		ret = gc0310_gpio_ctrl(sd, 1);
		if (ret) {
			pr_err("%s gpio_ctrl on failed\n", __func__);
			return ret;
		}
		usleep_range(5, 10); /* Delay for I2C cmds: 100 mclk cycles */
	} else {
		/* Power-down & Reset */
		ret = gc0310_gpio_ctrl(sd, 2);
		if (ret) {
			pr_err("%s gpio_ctrl off- Highstage failed\n", __func__);
			return ret;
		}

		usleep_range(5000, 6000);

		/* Disable MCLK */
		pr_info("%s(%d): mclk off\n", __func__, __LINE__);
		ret = gc0310_flisclk_ctrl(sd, 0);
		if (ret) {
			pr_err("%s flisclk_ctrl off failed\n", __func__);
			return ret;
		}

		usleep_range(1000, 5000);

		/* cam_2p8_en: 0, if fe380 series. */

#if 0
		/* TODO: fe380cxg here. */
		if (gpio_cam_2p8_en < 0) {

			ret = camera_sensor_gpio(-1, GP_CAM_2P8_EN,
					GPIOF_DIR_OUT, 1);
			if (ret < 0)
				return ret;
				
			gpio_cam_2p8_en = 11/*ret*/;
			/* set camera cam_2p8_en pin mode to gpio */
			lnw_gpio_set_alt(gpio_cam_2p8_en, LNW_GPIO);
		}
		gpio_set_value(gpio_cam_2p8_en, 0);
		gpio_free(gpio_cam_2p8_en);
		gpio_cam_2p8_en = -1;
#endif

		/* 2V8 off */
		pr_info("%s(%d): 2V8 off\n", __func__, __LINE__);
		ret = intel_scu_ipc_msic_vprog1(0);
		if (ret) {
			pr_err("%s power vprog1 failed\n", __func__);
			return ret;
		}

		msleep(2);

		/* 1V8 off */
		pr_info("%s(%d): 1V8 off\n", __func__, __LINE__);
		ret = intel_scu_ipc_msic_vprog2(0);
		if (ret)
			pr_err("%s power failed\n", __func__);

		msleep(6);

		ret = gc0310_gpio_ctrl(sd, 0);
                if (ret) {
                        pr_err("%s gpio_ctrl off- Lowstage failed\n", __func__);
                        return ret;
                }

	}

	pr_info("%s() %s--\n", __func__, (flag) ? ("on") : ("off"));
	return ret;
}

static int gc0310_csi_configure(struct v4l2_subdev *sd, int flag)
{
	/* The secondary MIPI port in moorefield doesn't support 2-lane.
	 * So, we use TERTIARY instead of SECONDARY. */
	pr_info("%s: port: SECONDARY; MIPI lane num: 1\n", __func__);
	return camera_sensor_csi(sd, ATOMISP_CAMERA_PORT_SECONDARY, 1,
		ATOMISP_INPUT_FORMAT_RAW_8, atomisp_bayer_order_rggb, flag);

}

static int gc0310_platform_init(struct i2c_client *client)
{
	int ret;

	pr_info("%s()\n", __func__);

	if (gpio_cam_pwdn < 0) {
		ret = camera_sensor_gpio(-1, "GP_SUB_CAM_PWDN",
					GPIOF_DIR_OUT, 1);

		if (ret < 0) {
			pr_info("adogu: %s(%d): camera_sensor_gpio fail\n", __func__, __LINE__);
			return ret;
		}
		gpio_cam_pwdn = ret; /*55*/;
		/* set camera reset pin mode to gpio */
		lnw_gpio_set_alt(gpio_cam_pwdn, LNW_GPIO);

		pr_info("%s(%d): pwdn(0)\n", __func__, __LINE__);
		gpio_set_value(gpio_cam_pwdn, 0);
		gpio_free(gpio_cam_pwdn);
		gpio_cam_pwdn = -1;
	}


	return 0;
}

static int gc0310_platform_deinit(void)
{
	pr_info("%s()\n", __func__);

	return 0;
}

static struct camera_sensor_platform_data gc0310_sensor_platform_data = {
	.gpio_ctrl	= gc0310_gpio_ctrl,
	.flisclk_ctrl	= gc0310_flisclk_ctrl,
	.power_ctrl	= gc0310_power_ctrl,
	.csi_cfg	= gc0310_csi_configure,
	.platform_init = gc0310_platform_init,
	.platform_deinit = gc0310_platform_deinit,
};

void *gc0310_platform_data(void *info)
{

	pr_info("%s()\n", __func__);

	gpio_cam_pwdn = -1;
	camera_reset = -1;
	gpio_cam_2p8_en = -1;

	return &gc0310_sensor_platform_data;
}

