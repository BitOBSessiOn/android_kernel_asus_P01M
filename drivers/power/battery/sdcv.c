/*
 * Copyright (c) 2014, ASUSTek, Inc. All Rights Reserved.
 * Written by chris chang chris1_chang@asus.com
 */
#include <asm/intel_scu_pmic.h>
#include <linux/iio/consumer.h>
#include <linux/printk.h>
#include <linux/delay.h>
#include <linux/err.h>

#include "sdcv.h"

int sd_cv_sw_take_control()
{
    int retval = 0;
    int cmd_retry_count = 3;

    do {
        retval = intel_scu_ipc_update_register(
                SDCV_CHGRCTRL0_ADDR,
                SDCV_SWCONTROL_ENABLE,
                SDCV_CHGRCTRL0_SWCONTROL_MASK);
        if (retval) {
            cmd_retry_count--;
            pr_err("%s: * Error enabling sw control. "
                "Charging may continue in h/w control mode\n",
                __func__);
            msleep(1);
        }
    } while (retval && cmd_retry_count > 0);

    cmd_retry_count = 3;

    do {
        retval = intel_scu_ipc_update_register(
                SDCV_CHGRCTRL0_ADDR,
                SDCV_CCSM_OFF_ENABLE,
                SDCV_CHGRCTRL0_CCSM_OFF_MASK);
        if (retval) {
            cmd_retry_count--;
            pr_err("%s: * Error disable CCSM. "
                "Charging may continue in h/w control mode\n",
                __func__);
            msleep(1);
        }
    } while (retval && cmd_retry_count > 0);

    return retval;
}

/*
 * sd_cv_get_adc_value - gets the ADC code from the register
 * @alert_reg_h: The 'high' register address
 *
 */
static int sd_cv_get_adc_value(uint16_t alert_reg_h, int *auto_cur_sel)
{
    int ret;
    uint16_t adc_val;
    uint8_t l, h;
    int battid_cursrc;

    /* multiple 1000 first (here's no floating point calculation in kernel) */
    int auto_cur_sel_uA[] = {
            0, 1125, 2250, 4500, 9000, 18000, 36000, 72000, 144000
        };

    /* Reading high register address */
    ret = intel_scu_ipc_ioread8(alert_reg_h, &h);
    if (ret)
        goto exit;

    /* Get the address of alert_reg_l */
    ++alert_reg_h;

    /* Reading low register address */
    ret = intel_scu_ipc_ioread8(alert_reg_h, &l);
    if (ret)
        goto exit;

    /* Concatenate 'h' and 'l' to get 12-bit ADC code */
    adc_val = ((h & 0x0F) << 8) | l;

    if (auto_cur_sel) {
        /* Get the battery id current source to decide auto current selection */
        battid_cursrc = (h & 0xF0) >> 4;
        *auto_cur_sel = auto_cur_sel_uA[battid_cursrc];
    }

    return adc_val;

exit:
    return ret;

}

/*
 * sd_cv_batt_id_resistor - calculate the battery id resistor (Ohm)
 * return - the resistor value (Ohm)
 */
int sd_cv_batt_id_resistor()
{
    int ret;
    int val = 0;
    int resistor = 0;
    int adc = 0;
    int auto_cur_sel = 0;
    int battid_cursrc = 0;
    struct iio_channel *indio_chan = NULL;

    /* Get battid pmic channel */
    indio_chan = iio_channel_get(NULL, SDCV_BATTID_CHANNEL_NAME);
    if (IS_ERR_OR_NULL(indio_chan)) {
        pr_err("%s: IIO channel get error!!\n", __func__);
        return -1;
    }

    /* Read pmic battid ADC */
#if 0
    ret = iio_read_channel_raw(indio_chan, &val);
    if (ret) {
        pr_err("%s: unable to read batid", __func__);
        return -1;
    }
    pr_info("%s: IIO channel read Sucess, val=%.3X\n", __func__, val);
#endif

    adc = sd_cv_get_adc_value(SDCV_BATTIDRSLTH_REG, &auto_cur_sel);
    if (adc < 0 || auto_cur_sel == 0) {
        pr_err("%s: fail to get batt id result from 0x%X",
            __func__, SDCV_BATTIDRSLTH_REG);
        return -1;
    }
    pr_info("%s: adc=0x%X, auto_cur_sel=%d\n", __func__, adc, auto_cur_sel);

    /* The formula for calculating the battery id resistor */
    resistor = adc * 293 * 1000 / auto_cur_sel;

    /* Release adc channel */
    iio_channel_release(indio_chan);

    return resistor;
}

/*
 * sd_cv_batt_vol - calculate the battery voltage
 * return - the battery voltage value (mV)
 */
int sd_cv_batt_vol()
{
    int ret;
    int val = 0;
    int volt = 0;
    int adc = 0;
    int battid_cursrc = 0;
    struct iio_channel *indio_chan = NULL;

    /* Get vbat pmic channel */
    indio_chan = iio_channel_get(NULL, SDCV_VBAT_CHANNEL_NAME);
    if (IS_ERR_OR_NULL(indio_chan)) {
        pr_err("%s: IIO channel get error!!\n", __func__);
        return -1;
    }

    /* Read pmic vbat ADC */
    ret = iio_read_channel_raw(indio_chan, &val);
    if (ret) {
        pr_err("%s: unable to read vbat", __func__);
        return -1;
    }
    pr_debug("%s: IIO channel read Sucess, val=%.3X\n", __func__, val);

    adc = sd_cv_get_adc_value(SDCV_VBATRSLTH_REG, NULL);
    if (adc < 0) {
        pr_err("%s: fail to get vbat result from 0x%X",
            __func__, SDCV_VBATRSLTH_REG);
        return -1;
    }
    pr_debug("%s: adc=0x%X\n", __func__, adc);

    /* The formula for calculating the battery voltage */
    volt = adc * 1250 / 1000;

    /* Release adc channel */
    iio_channel_release(indio_chan);

    return volt;
}

