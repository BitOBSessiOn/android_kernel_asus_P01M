/*
 * phy-fusb300.c: fusb300 usb phy driver for type-c and PD
 *
 * Copyright (C) 2014 Intel Corporation
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. Seee the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * Author: Kannappan, R <r.kannappan@intel.com>
 */

//#define ASUS_ATD_INTERFACE
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/pm.h>
#include <linux/mod_devicetable.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/gpio.h>
//#include <linux/gpio/consumer.h>
#include <linux/acpi.h>
#include <linux/pm_runtime.h>
#include <linux/notifier.h>
#include <linux/suspend.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/usb_typec_phy.h>
#include <linux/HWVersion.h>

#include "usb_typec_detect.h"
#include "fusb300_platform.h"

extern int Read_PROJ_ID(void);

/* Status register bits */
#define FUSB300_STAT0_REG		0x40
#define FUSB300_STAT0_VBUS_OK		BIT(7)
#define FUSB300_STAT0_ACTIVITY		BIT(6)
#define FUSB300_STAT0_COMP		BIT(5)
#define FUSB300_STAT0_CRCCHK		BIT(4)
#define FUSB300_STAT0_ALERT		BIT(3)
#define FUSB300_STAT0_WAKE		BIT(2)
#define FUSB300_STAT0_BC_LVL		(BIT(1)|BIT(0))
#define FUSB300_STAT0_BC_LVL_MASK	3
#define FUSB300_BC_LVL_VRA		0
#define FUSB300_BC_LVL_USB		1
#define FUSB300_BC_LVL_1500		2
#define FUSB300_BC_LVL_3000		3

#define FUSB300_STAT1_REG		0x41
#define FUSB300_STAT1_RXSOP2		BIT(7)
#define FUSB300_STAT1_RXSOP1		BIT(6)
#define FUSB300_STAT1_RXEMPTY		BIT(5)
#define FUSB300_STAT1_RXFULL		BIT(4)
#define FUSB300_STAT1_TXEMPTY		BIT(3)
#define FUSB300_STAT1_TXFULL		BIT(2)
#define FUSB300_STAT1_OVERTEMP		BIT(1)
#define FUSB300_STAT1_SHORT		BIT(0)

#define FUSB300_INT_REG		0x42
#define FUSB300_INT_VBUS_OK	BIT(7)
#define FUSB300_INT_ACTIVITY	BIT(6)
#define FUSB300_INT_COMP		BIT(5)
#define FUSB300_INT_CRCCHK		BIT(4)
#define FUSB300_INT_ALERT		BIT(3)
#define FUSB300_INT_WAKE		BIT(2)
#define FUSB300_INT_COLLISION	BIT(1)
#define FUSB300_INT_BC_LVL		BIT(0)
/* Interrupt mask bits */
#define FUSB300_INT_MASK_REG		0xa
#define FUSB300_INT_MASK_VBUS_OK	BIT(7)
#define FUSB300_INT_MASK_ACTIVITY	BIT(6)
#define FUSB300_INT_MASK_COMP		BIT(5)
#define FUSB300_INT_MASK_CRCCHK		BIT(4)
#define FUSB300_INT_MASK_ALERT		BIT(3)
#define FUSB300_INT_MASK_WAKE		BIT(2)
#define FUSB300_INT_MASK_COLLISION	BIT(1)
#define FUSB300_INT_MASK_BC_LVL		BIT(0)

/* control */
#define FUSB300_CONTROL0_REG		0x6
#define FUSB300_CONTROL0_TX_FLUSH	BIT(6)
#define FUSB300_CONTROL0_MASK_INT	BIT(5)
#define FUSB300_CONTROL0_LOOPBACK	BIT(4)
#define FUSB300_CONTROL0_HOST_CUR	(BIT(3)|BIT(2))
#define FUSB300_CONTROL0_AUTO_PREAMBLE	BIT(1)
#define FUSB300_CONTROL0_TX_START	BIT(0)

#define FUSB300_HOST_CUR_MASK		3
#define FUSB300_HOST_CUR_SHIFT		2
#define FUSB300_HOST_CUR(x)		(((x) >> FUSB300_HOST_CUR_SHIFT) & \
					 FUSB300_HOST_CUR_MASK)
#define FUSB300_HOST_CUR_DISABLE	0
#define FUSB300_HOST_CUR_USB_SDP	1
#define FUSB300_HOST_CUR_1500		2
#define FUSB300_HOST_CUR_3000		3

#define FUSB300_CONTROL1_REG		0x7
#define FUSB300_CONTROL1_RX_FLUSH	BIT(1)
#define FUSB300_CONTROL1_BIST_MODE	BIT(3)

#define FUSB300_SOFT_POR_REG		0xc
#define FUSB300_SOFT_POR		BIT(1)

#define FUSB300_SWITCH0_REG		0x2
#define FUSB300_SWITCH0_PD_CC1_EN	BIT(0)
#define FUSB300_SWITCH0_PD_CC2_EN	BIT(1)
#define FUSB300_SWITCH0_PU_CC1_EN	BIT(6)
#define FUSB300_SWITCH0_PU_CC2_EN	BIT(7)
#define FUSB300_SWITCH0_PU_EN		(BIT(7)|BIT(6))
#define FUSB300_SWITCH0_PD_EN		(BIT(0)|BIT(1))
#define FUSB300_SWITCH0_PU_PD_MASK	3
#define FUSB300_SWITCH0_PU_SHIFT	6
#define FUSB300_SWITCH0_PD_SHIFT	0
#define FUSB300_SWITCH0_MEASURE_CC1	BIT(2)
#define FUSB300_SWITCH0_MEASURE_CC2	BIT(3)
#define FUSB300_VCONN_CC1_EN		BIT(4)
#define FUSB300_VCONN_CC2_EN		BIT(5)

#define FUSB300_MEAS_REG		0x4
#define FUSB300_MEAS_VBUS		BIT(6)
#define FUSB300_MEAS_RSLT_MASK		0x3f

#define FUSB300_MEASURE_VBUS		1
#define FUSB300_MEASURE_CC		2

#define FUSB300_HOST_RD_MIN		0x24
#define FUSB300_HOST_RD_MAX		0x3e
#define FUSB300_HOST_RA_MIN		0xa
#define FUSB300_HOST_RA_MAX		0x13

#define FUSB300_PWR_REG			0xb
#define FUSB300_PWR_BG_WKUP		BIT(0)
#define FUSB300_PWR_BMC			BIT(1)
#define FUSB300_PWR_MEAS		BIT(2)
#define FUSB300_PWR_OSC			BIT(3)
#define FUSB300_PWR_SHIFT		0

#define FUSB300_COMP_RD_LOW		0x24
#define FUSB300_COMP_RD_HIGH		0x3e
#define FUSB300_COMP_RA_LOW		0xa
#define FUSB300_COMP_RA_HIGH		0x12

#define USB_TYPEC_PD_VERSION		2

static int host_cur[4] = {
	TYPEC_CURRENT_UNKNOWN,
	TYPEC_CURRENT_USB,
	TYPEC_CURRENT_1500,
	TYPEC_CURRENT_3000
};

enum typec_time_divison_mode {
	typec_time_divison_none = 0,
	typec_time_divison_ufp,
	typec_time_divison_dfp,
	typec_time_divison_drp,
	typec_time_divison_invalid,
};

struct fusb300_chip {
	struct i2c_client *client;
	struct device *dev;
	struct regmap *map;
	struct mutex lock;
	struct typec_phy phy;
	bool i_vbus;
	u32 stored_int_reg;
	struct completion int_complete;
	spinlock_t irqlock;
	enum typec_time_divison_mode time_divison_mode;
	struct delayed_work tdm_work;
	struct fusb300_platform_data *pdata;
};

static int fusb300_wake_on_cc_change(struct fusb300_chip *chip);

static int fusb300_get_negotiated_cur(int val)
{
	if (val >= 0 && val < 4)
		return host_cur[val];
	return TYPEC_CURRENT_UNKNOWN;
}

static int fusb300_set_host_current(struct typec_phy *phy,
				    enum typec_current cur)
{
	struct fusb300_chip *chip;
	int ret;
	u8 i;
	u32 val;

	if (!phy)
		return -EINVAL;

	chip = dev_get_drvdata(phy->dev);
	for (i = 0; i < ARRAY_SIZE(host_cur); i++) {
		if (host_cur[i] == cur)
			break;
	}
	if (i >= ARRAY_SIZE(host_cur)) {
		dev_err(phy->dev, "%s: host current mismatch\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&chip->lock);
	regmap_read(chip->map, FUSB300_CONTROL0_REG, &val);
	val &= ~(FUSB300_HOST_CUR_MASK << FUSB300_HOST_CUR_SHIFT);
	val |= (i << FUSB300_HOST_CUR_SHIFT);
	dev_dbg(phy->dev, "control0 reg = %x cur = %d i = %d", val, cur, i);
	ret = regmap_write(chip->map, FUSB300_CONTROL0_REG, val);
	mutex_unlock(&chip->lock);

	return ret;
}

static enum typec_current fusb300_get_host_current(struct typec_phy *phy)
{
	struct fusb300_chip *chip;
	unsigned int val;
	int ret;

	if (!phy)
		return TYPEC_CURRENT_UNKNOWN;

	chip = dev_get_drvdata(phy->dev);
	ret = regmap_read(chip->map, FUSB300_CONTROL0_REG, &val);
	if (ret < 0)
		return TYPEC_CURRENT_UNKNOWN;
	return fusb300_get_negotiated_cur(FUSB300_HOST_CUR(val));
}


static int fusb300_en_pu(struct fusb300_chip *chip, bool en_pu, int cur)
{
	unsigned int val = 0;
	int ret;

	mutex_lock(&chip->lock);
	ret = regmap_read(chip->map, FUSB300_SWITCH0_REG, &val);
	if (ret < 0) {
		dev_err(&chip->client->dev, "error(%d) reading %x\n",
			ret, FUSB300_SWITCH0_REG);
		mutex_unlock(&chip->lock);
		return ret;
	}

	if (en_pu) {
		val &= ~FUSB300_SWITCH0_PD_EN;
		val |= FUSB300_SWITCH0_PU_EN;
	} else {
		val &= ~FUSB300_SWITCH0_PU_EN;
	}
	dev_dbg(chip->dev, "%s: assigning switch0 %x = %x", __func__,
		FUSB300_SWITCH0_REG, val);
	mutex_unlock(&chip->lock);
	ret = fusb300_set_host_current(&chip->phy, cur);
	if (ret < 0) {
		dev_err(&chip->client->dev,
			"error setting host cur%d", ret);
		return ret;
	}
	mutex_lock(&chip->lock);
	ret = regmap_write(chip->map, FUSB300_SWITCH0_REG, val);
	mutex_unlock(&chip->lock);
	if (ret < 0)
		dev_err(&chip->client->dev, "error(%d) writing %x\n",
			ret, FUSB300_SWITCH0_REG);

	return ret;
}

static int fusb300_en_pd(struct fusb300_chip *chip, bool en_pd)
{
	unsigned int val = 0;
	int ret;

	mutex_lock(&chip->lock);
	ret = regmap_read(chip->map, FUSB300_SWITCH0_REG, &val);
	if (ret < 0) {
		mutex_unlock(&chip->lock);
		dev_err(&chip->client->dev, "error(%d) reading %x\n",
			ret, FUSB300_SWITCH0_REG);
		return ret;
	}

	if (en_pd) {
		val |= FUSB300_SWITCH0_PD_EN;
		val &= ~FUSB300_SWITCH0_PU_EN;
	} else {
		val &= ~FUSB300_SWITCH0_PD_EN;
	}
	dev_dbg(chip->dev, "%s: switch0 %x = %x", __func__,
		FUSB300_SWITCH0_REG, val);
	ret = regmap_write(chip->map, FUSB300_SWITCH0_REG, val);
	mutex_unlock(&chip->lock);
	if (ret < 0)
		dev_err(&chip->client->dev, "error(%d) write %d",
			ret, FUSB300_SWITCH0_REG);
	return ret;
}

static int fusb300_switch_mode(struct typec_phy *phy, enum typec_mode mode)
{
	struct fusb300_chip *chip;
	int cur;

	if (!phy)
		return -ENODEV;

	dev_dbg(phy->dev, "%s: %d", __func__, mode);
	chip = dev_get_drvdata(phy->dev);

	if (mode == TYPEC_MODE_UFP) {
		fusb300_set_host_current(phy, 0);
		fusb300_en_pd(chip, true);
		mutex_lock(&chip->lock);
		phy->state = TYPEC_STATE_UNATTACHED_UFP;
		regmap_write(chip->map, FUSB300_MEAS_REG, 0x31);
		mutex_unlock(&chip->lock);
	} else if (mode == TYPEC_MODE_DFP) {
		cur = TYPEC_CURRENT_USB;
		mutex_lock(&chip->lock);
		phy->state = TYPEC_STATE_UNATTACHED_DFP;
		regmap_write(chip->map, FUSB300_MEAS_REG, 0x26);
		mutex_unlock(&chip->lock);
		fusb300_en_pu(chip, true, cur);
	} else if (mode == TYPEC_MODE_DRP) {
		fusb300_set_host_current(phy, 0);
		fusb300_wake_on_cc_change(chip);
	}
	return 0;
}

static int fusb300_mask_irq(struct typec_phy *phy, bool enable)
{
	struct fusb300_chip *chip;
	unsigned int val;

	if (!phy)
		return -ENODEV;

	chip = dev_get_drvdata(phy->dev);

	if (enable) {
		regmap_read(chip->map, FUSB300_CONTROL0_REG, &val);
		val &= ~FUSB300_CONTROL0_MASK_INT;
		regmap_write(chip->map, FUSB300_CONTROL0_REG, val);
	} else {
		regmap_read(chip->map, FUSB300_CONTROL0_REG, &val);
		val |= FUSB300_CONTROL0_MASK_INT;
		regmap_write(chip->map, FUSB300_CONTROL0_REG, val);
	}
	DBG(DBG_HW, "%s: irq %s\n", __func__,
	    enable ? "enabled" : "disabled");

	return 0;
}

#define TIMEOUT_MS_VBUS_STABLE 500
static int fusb300_wait_for_vbus(struct typec_phy *phy, bool rising)
{
	struct fusb300_chip *chip;
	unsigned int stat;
	int vbus_timeout_ms = TIMEOUT_MS_VBUS_STABLE;
	int ret = 0;

	if (!phy)
		return -ENODEV;

	chip = dev_get_drvdata(phy->dev);

	while (vbus_timeout_ms > 0) {
		ret = regmap_read(chip->map, FUSB300_STAT0_REG, &stat);
		if (ret < 0)
			break;
		if (rising) {
			if (stat & FUSB300_STAT0_VBUS_OK) {
				msleep(10);
				break;
			}
		} else {
			if ((stat & FUSB300_STAT0_VBUS_OK) == 0) {
				msleep(50);
				break;
			}
		}
		msleep(10);
		vbus_timeout_ms -= 10;
	}
	if (vbus_timeout_ms > 0) {
		DBG(DBG_HW, "%s: vbus stable in %dms, ret=%d\n", __func__,
		    TIMEOUT_MS_VBUS_STABLE - vbus_timeout_ms, ret);
	} else {
		ret = -EAGAIN;
		DBG(DBG_ERR, "%s: vbus is not stable in time\n", __func__);
	}

	return ret;
}

static int fusb300_setup_cc(struct typec_phy *phy, enum typec_cc_pin cc,
			    enum typec_state state)
{
	struct fusb300_chip *chip;
	unsigned int val = 0;

	if (!phy)
		return -ENODEV;

	dev_dbg(phy->dev, "%s cc: %d state: %s\n", __func__, cc, typec_phy_state_str(state));
	chip = dev_get_drvdata(phy->dev);

	mutex_lock(&chip->lock);
	phy->valid_cc = cc;

	switch (state) {
	case TYPEC_STATE_ATTACHED_UFP:
	case TYPEC_STATE_ATTACHED_DFP:
	case TYPEC_STATE_UNKNOWN:
	case TYPEC_STATE_POWERED:
		phy->state = state;
		break;
	default:
		break;
	}

	if (cc == TYPEC_PIN_CC1) {
		if (chip->pdata) {
			gpio_set_value(chip->pdata->gpio_usb3_mux_vdd_en, 1);
			gpio_set_value(chip->pdata->gpio_usb3_mux_sel, 0);
		}
		val |= FUSB300_SWITCH0_MEASURE_CC1;
		if (phy->state == TYPEC_STATE_ATTACHED_UFP)
			val |= FUSB300_SWITCH0_PD_CC1_EN;
		else if (phy->state == TYPEC_STATE_ATTACHED_DFP)
			val |= FUSB300_SWITCH0_PU_CC1_EN;
	} else if (cc == TYPEC_PIN_CC2) {
		if (chip->pdata) {
			gpio_set_value(chip->pdata->gpio_usb3_mux_vdd_en, 1);
			gpio_set_value(chip->pdata->gpio_usb3_mux_sel, 1);
		}
		val |= FUSB300_SWITCH0_MEASURE_CC2;
		if (phy->state == TYPEC_STATE_ATTACHED_UFP)
			val |= FUSB300_SWITCH0_PD_CC2_EN;
		else if (phy->state == TYPEC_STATE_ATTACHED_DFP)
			val |= FUSB300_SWITCH0_PU_CC2_EN;
	} else { /* cc removal */
		if (chip->pdata) {
			gpio_set_value(chip->pdata->gpio_usb3_mux_sel, 0);
			gpio_set_value(chip->pdata->gpio_usb3_mux_vdd_en, 0);
		}
		goto end;
	}

	regmap_write(chip->map, FUSB300_SWITCH0_REG, val);
end:
	mutex_unlock(&chip->lock);

	return 0;
}


#ifdef DEBUG
static void dump_registers(struct fusb300_chip *chip)
{
	struct regmap *regmap = chip->map;
	int ret;
	unsigned int val;

	ret = regmap_read(regmap, 1, &val);
	dev_info(chip->dev, "reg1 = %x", val);

	ret = regmap_read(regmap, 2, &val);
	dev_info(chip->dev, "reg2 = %x", val);

	ret = regmap_read(regmap, 3, &val);
	dev_info(chip->dev, "reg3 = %x", val);

	ret = regmap_read(regmap, 4, &val);
	dev_info(chip->dev, "reg4 = %x", val);

	ret = regmap_read(regmap, 5, &val);
	dev_info(chip->dev, "reg5 = %x", val);

	ret = regmap_read(regmap, 6, &val);
	dev_info(chip->dev, "reg6 = %x", val);

	ret = regmap_read(regmap, 7, &val);
	dev_info(chip->dev, "reg7 = %x", val);

	ret = regmap_read(regmap, 8, &val);
	dev_info(chip->dev, "reg8 = %x", val);

	ret = regmap_read(regmap, 9, &val);
	dev_info(chip->dev, "reg9 = %x", val);

	ret = regmap_read(regmap, 0xa, &val);
	dev_info(chip->dev, "rega = %x", val);

	ret = regmap_read(regmap, 0xb, &val);
	dev_info(chip->dev, "regb = %x", val);

	ret = regmap_read(regmap, 0xc, &val);
	dev_info(chip->dev, "regc = %x", val);

	ret = regmap_read(regmap, 0x40, &val);
	dev_info(chip->dev, "reg40 = %x", val);

	ret = regmap_read(regmap, 0x41, &val);
	dev_info(chip->dev, "reg41 = %x", val);

	ret = regmap_read(regmap, 0x42, &val);
	dev_info(chip->dev, "reg42 = %x", val);
}
#endif

static int fusb300_init_chip(struct fusb300_chip *chip)
{
	struct regmap *regmap = chip->map;
	unsigned int val;
	int ret;

	ret = regmap_write(chip->map, FUSB300_SOFT_POR_REG, 1);
	if (ret < 0) {
		dev_err(chip->dev, "error(%d) writing to reg:%x\n",
			ret, FUSB300_SOFT_POR_REG);
		return ret;
	}
	udelay(25);

	ret = regmap_write(regmap, FUSB300_PWR_REG, 7);
	if (ret < 0) {
		dev_err(chip->dev, "error(%d) writing to reg:%x\n",
			ret, FUSB300_PWR_REG);
		return ret;
	}

#ifdef DEBUG
	dump_registers(chip);
#endif

	ret = regmap_read(regmap, FUSB300_INT_REG, &val);
	if (ret < 0) {
		dev_err(chip->dev, "error(%d) reading reg:%x\n",
			ret, FUSB300_INT_REG);
		return ret;
	}
	dev_dbg(chip->dev, "init_chip int reg = %x", val);
	ret = regmap_read(regmap, FUSB300_STAT0_REG, &val);
	if (ret < 0) {
		dev_err(chip->dev, "error(%d) reading reg:%x\n",
			ret, FUSB300_STAT0_REG);
		return ret;
	}
	dev_dbg(chip->dev, "statreg = %x = %x", FUSB300_STAT0_REG, val);

	if (val & FUSB300_STAT0_VBUS_OK) {
		chip->i_vbus = true;
		regmap_write(regmap, FUSB300_SWITCH0_REG, 3); /* Enable PD  */
		regmap_write(regmap, FUSB300_MEAS_REG, 0x31);
	}

	return 0;
}

static irqreturn_t fusb300_interrupt(int id, void *dev)
{
	struct fusb300_chip *chip = dev;
	struct typec_phy *phy = &chip->phy;
	unsigned int int_reg, stat_reg, ctrl_reg;
	int ret;

	pm_runtime_get_sync(chip->dev);

	ret = regmap_read(chip->map, FUSB300_INT_REG, &int_reg);
	if (ret < 0) {
		dev_err(phy->dev, "read reg %x failed %d",
			FUSB300_INT_REG, ret);
		pm_runtime_put_sync(chip->dev);
		return IRQ_NONE;
	}

	regmap_read(chip->map, FUSB300_STAT0_REG, &stat_reg);
	dev_dbg(chip->dev, "int %x stat %x, phy state=%s", int_reg, stat_reg,
		typec_phy_state_str(phy->state));

	if (!phy->irq_fired) {
		if (stat_reg & FUSB300_INT_WAKE)
			int_reg |= FUSB300_INT_WAKE;
		phy->irq_fired = true;
	}
	if (int_reg & FUSB300_INT_WAKE &&
	    (phy->state == TYPEC_STATE_UNATTACHED_UFP ||
	     phy->state == TYPEC_STATE_UNATTACHED_DFP) &&
	    !chip->i_vbus) {
		unsigned int val;

		regmap_read(chip->map, FUSB300_SWITCH0_REG, &val);

		if (((val & FUSB300_SWITCH0_PD_EN) == 0) &&
		    ((val & FUSB300_SWITCH0_PU_EN) == 0)) {
			cancel_delayed_work(&chip->tdm_work);
			atomic_notifier_call_chain(&phy->notifier,
						   TYPEC_EVENT_DRP,
						   (void*)(10*1000));
		}
		complete(&chip->int_complete);
	}

	if (int_reg & FUSB300_INT_VBUS_OK) {
		if (stat_reg & FUSB300_STAT0_VBUS_OK) {
			if (!chip->i_vbus) {
				chip->i_vbus = true;
				if (phy->state == TYPEC_STATE_UNATTACHED_DFP)
					complete(&chip->int_complete);
				cancel_delayed_work(&chip->tdm_work);
				atomic_notifier_call_chain(&phy->notifier,
							   TYPEC_EVENT_VBUS, 0);
			} else
				dev_dbg(chip->dev, "Bypass VBUS event\n");
		} else {
			if (chip->i_vbus) {
				dev_dbg(chip->dev, "VBUS-off, notify event - TYPEC_EVENT_NONE\n");
				/* mark all INTs */
				regmap_read(chip->map, FUSB300_CONTROL0_REG, &ctrl_reg);
				ctrl_reg |= FUSB300_CONTROL0_MASK_INT;
				regmap_write(chip->map, FUSB300_CONTROL0_REG, ctrl_reg);

				chip->i_vbus = false;
				cancel_delayed_work(&chip->tdm_work);
				atomic_notifier_call_chain(&phy->notifier,
							   TYPEC_EVENT_NONE, 0);
				fusb300_wake_on_cc_change(chip);
			} else
				dev_dbg(chip->dev, "Bypass VBUS-off event\n");
		}
	}


	if (int_reg & (FUSB300_INT_COMP | FUSB300_INT_BC_LVL))
		complete(&chip->int_complete);


	if ((int_reg & FUSB300_INT_COMP) &&
	    (stat_reg & FUSB300_STAT0_COMP)) {
		if ((phy->state == TYPEC_STATE_ATTACHED_UFP) ||
		    (phy->state == TYPEC_STATE_ATTACHED_DFP)) {
			/* mark all INTs */
			regmap_read(chip->map, FUSB300_CONTROL0_REG, &ctrl_reg);
			ctrl_reg |= FUSB300_CONTROL0_MASK_INT;
			regmap_write(chip->map, FUSB300_CONTROL0_REG, ctrl_reg);

			atomic_notifier_call_chain(&phy->notifier,
						   TYPEC_EVENT_NONE, 0);
			fusb300_wake_on_cc_change(chip);
		}
	}

	if (int_reg & (FUSB300_INT_COMP | FUSB300_INT_BC_LVL))
		complete(&chip->int_complete);

	pm_runtime_put_sync(chip->dev);

	return IRQ_HANDLED;
}

static int fusb300_measure_cc(struct typec_phy *phy, enum typec_cc_pin pin,
			      struct typec_cc_psy *cc_psy,
			      unsigned long timeout)
{
	struct fusb300_chip *chip;
	int ret, s_comp, s_bclvl;
	unsigned int val, stat_reg;

	if (!phy) {
		cc_psy->v_rd = -1;
		return -ENODEV;
	}

	chip = dev_get_drvdata(phy->dev);
	timeout = msecs_to_jiffies(150);

	pm_runtime_get_sync(chip->dev);

	mutex_lock(&chip->lock);

	regmap_read(chip->map, FUSB300_SWITCH0_REG, &val);

	if (pin == TYPEC_PIN_CC1) {
		val |= FUSB300_SWITCH0_MEASURE_CC1;
		val &= ~FUSB300_SWITCH0_MEASURE_CC2;
		if (phy->state == TYPEC_STATE_UNATTACHED_DFP) {
			val &= ~FUSB300_SWITCH0_PU_CC2_EN;
			val |= FUSB300_SWITCH0_PU_CC1_EN;
		}
	} else {
		val |= FUSB300_SWITCH0_MEASURE_CC2;
		val &= ~FUSB300_SWITCH0_MEASURE_CC1;
		if (phy->state == TYPEC_STATE_UNATTACHED_DFP) {
			val &= ~FUSB300_SWITCH0_PU_CC1_EN;
			val |= FUSB300_SWITCH0_PU_CC2_EN;
		}
	}

	dev_dbg(phy->dev,
		"%s state %d unattached_dfp: %d switch0: %x val: %x\n",
		__func__, phy->state, TYPEC_STATE_UNATTACHED_DFP,
		FUSB300_SWITCH0_REG, val);
	reinit_completion(&chip->int_complete);
	ret = regmap_write(chip->map, FUSB300_SWITCH0_REG, val);
	if (ret < 0)
		goto err;
	mutex_unlock(&chip->lock);

	ret = wait_for_completion_timeout(&chip->int_complete, timeout);
	if (ret == 0) {
		ret = -ETIME;
		goto err_measure;
	}

	mutex_lock(&chip->lock);
	regmap_read(chip->map, FUSB300_STAT0_REG, &stat_reg);
	dev_dbg(chip->dev, "STAT0_REG = %x\n",
		stat_reg);
	if ((stat_reg & FUSB300_STAT0_VBUS_OK) &&
	    phy->state == TYPEC_STATE_UNATTACHED_DFP) {
		ret = -EPROTO;
		goto err;
	}
	s_comp = stat_reg & FUSB300_STAT0_COMP;
	s_bclvl = stat_reg & FUSB300_STAT0_BC_LVL_MASK;
	mutex_unlock(&chip->lock);

	if (!s_comp) {
		switch (s_bclvl) {
		case FUSB300_BC_LVL_VRA:
			cc_psy->v_rd = USB_TYPEC_CC_VRA;
			cc_psy->cur = 0;
			break;
		case FUSB300_BC_LVL_USB:
			cc_psy->v_rd = USB_TYPEC_CC_VRD_USB;
			cc_psy->cur = host_cur[1];
			break;
		case FUSB300_BC_LVL_1500:
			cc_psy->v_rd = USB_TYPEC_CC_VRD_1500;
			cc_psy->cur = host_cur[2];
			break;
		case FUSB300_BC_LVL_3000:
			cc_psy->v_rd = USB_TYPEC_CC_VRD_3000;
			cc_psy->cur = host_cur[3];
			break;
		}
	} else {
		dev_dbg(phy->dev, "chip->stat = %x s_comp %x",
			stat_reg, s_comp);
		cc_psy->v_rd = USB_TYPEC_CC_VRD_UNKNOWN; /* illegal */
		cc_psy->cur = TYPEC_CURRENT_UNKNOWN; /* illegal */
	}
	pm_runtime_put_sync(chip->dev);
	return 0;
err:
	mutex_unlock(&chip->lock);
err_measure:
	cc_psy->cur = TYPEC_CURRENT_UNKNOWN;
	cc_psy->v_rd = USB_TYPEC_CC_VRD_UNKNOWN;
	pm_runtime_put_sync(chip->dev);
	return ret;
}

static bool fusb300_pd_capable(struct typec_phy *phy)
{
	if (phy->type == USB_TYPE_C)
		return true;
	else
		return false;
}

static int fusb300_pd_version(struct typec_phy *phy)
{
	if (phy->type == USB_TYPE_C)
		return USB_TYPEC_PD_VERSION;
	else
		return 0;
}

static int fusb300_get_irq(struct i2c_client *client)
{
	/*
	  struct gpio_desc *gpio_desc;
	  int irq;
	  struct device *dev = &client->dev;

	  if (client->irq > 0)
	  return client->irq;

	  gpio_desc = devm_gpiod_get_index(dev, "fusb300", 0);

	  if (IS_ERR(gpio_desc))
	  return client->irq;

	  irq = gpiod_to_irq(gpio_desc);

	  devm_gpiod_put(&client->dev, gpio_desc);

	  return irq;
	*/
	pr_info("%s:%d. BUG call to unimplement foo\n", __func__, __LINE__);
	return 0;
}
static inline bool fusb300_is_iwake(struct fusb300_chip *chip)
{
	int val;

	mutex_lock(&chip->lock);
	regmap_read(chip->map, FUSB300_SWITCH0_REG, &val);
	mutex_unlock(&chip->lock);
	DBG(DBG_SM, "%s: SWITCH0=%02x \n", __func__, val);
	return (val == (FUSB300_SWITCH0_MEASURE_CC1 | FUSB300_SWITCH0_MEASURE_CC2));
}

static int fusb300_wake_on_cc_change(struct fusb300_chip *chip)
{
	int val;

	val = FUSB300_SWITCH0_MEASURE_CC1 | FUSB300_SWITCH0_MEASURE_CC2;

	mutex_lock(&chip->lock);
	regmap_write(chip->map, FUSB300_SWITCH0_REG, val);
	chip->phy.state = TYPEC_STATE_UNATTACHED_UFP;
	if (chip->time_divison_mode == typec_time_divison_none ||
	    chip->time_divison_mode >= typec_time_divison_invalid) {
		mutex_unlock(&chip->lock);
		return 0;
	}
	mutex_unlock(&chip->lock);

	DBG(DBG_SM, "%s: schedule TDM evt\n", __func__);
	cancel_delayed_work(&chip->tdm_work);
	queue_delayed_work(system_nrt_wq, &chip->tdm_work, HZ);

	return 0;
}

static struct regmap_config fusb300_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.val_format_endian = REGMAP_ENDIAN_NATIVE,
};
#ifdef ASUS_ATD_INTERFACE
static ssize_t i2c_status(struct device *dev,
                          struct device_attribute *attr, char *buf)
{
	/* Since we have verify the I2C functionality,
	 * just report pass here. */
	return snprintf(buf, PAGE_SIZE, "1\n");
}

static DEVICE_ATTR(i2c_status, S_IRUGO, i2c_status, NULL);
#endif

static ssize_t show_tdm(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct fusb300_chip *chip = dev_get_drvdata(dev);
	return snprintf(buf, PAGE_SIZE, "%d\n", chip->time_divison_mode);
}

static ssize_t write_tdm(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct fusb300_chip *chip = dev_get_drvdata(dev);
	int number = 0;
	int res;
	enum typec_time_divison_mode previous_tdm;

	res = sscanf(buf, "%d", &number);
	if (res != 1 || number >= typec_time_divison_invalid)
		return -EINVAL;

	if (chip->time_divison_mode == number)
		return count;

	mutex_lock(&chip->lock);
	previous_tdm = chip->time_divison_mode;
	chip->time_divison_mode = number;
	mutex_unlock(&chip->lock);

	DBG(DBG_SM, "%s: time divison mode: %d\n", __func__, number);
	if (number == typec_time_divison_none)
		return count;
	if (previous_tdm != typec_time_divison_none)
		return count;

	/* Active TDM work */
	cancel_delayed_work_sync(&chip->tdm_work);
	queue_delayed_work(system_nrt_wq, &chip->tdm_work,
			   msecs_to_jiffies(10));

	return count;
}
static DEVICE_ATTR(tdm, 0660, show_tdm, write_tdm);

static struct attribute *dev_attrs[] = {
	&dev_attr_tdm,
#ifdef ASUS_ATD_INTERFACE
	&dev_attr_i2c_status,
#endif
	NULL,
};
static struct attribute_group dev_attr_grp = {
	.attrs = dev_attrs,
};

static void tdm_evt_work(struct work_struct *w)
{
	struct fusb300_chip *chip = container_of(w, struct fusb300_chip,
						 tdm_work.work);
	struct typec_phy *phy = &chip->phy;
	enum typec_time_divison_mode tdm;

	if (!fusb300_is_iwake(chip)) {
		DBG(DBG_ERR, "%s: expired, not in iwake!!!\n", __func__);
		return;
	}
	mutex_lock(&chip->lock);
	tdm = chip->time_divison_mode;
	mutex_unlock(&chip->lock);

	DBG(DBG_SM, "%s: expired, Time divison mode = %d\n", __func__, tdm);
	switch (tdm) {
	case typec_time_divison_ufp:
		atomic_notifier_call_chain(&phy->notifier,
					   TYPEC_EVENT_TDM_UFP, (void*)225);
		break;
	case typec_time_divison_dfp:
		atomic_notifier_call_chain(&phy->notifier,
					   TYPEC_EVENT_TDM_DFP, (void*)225);
		break;
	case typec_time_divison_drp:
		atomic_notifier_call_chain(&phy->notifier,
					   TYPEC_EVENT_TDM_DRP, (void*)125);
		break;
	default:;
	}
}

static int fusb300_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct fusb300_chip *chip;
	int ret;
	unsigned int val;
	struct device *dev = &client->dev;
	struct fusb300_platform_data *pdata = dev->platform_data;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_WORD_DATA)) {
		DBG(DBG_INT, "i2c is not workable\n");
		return -EIO;
	}

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	dev_dbg(&client->dev, "chip addr = %x", client->addr);
	chip->client = client;
	chip->dev = &client->dev;
	chip->map = devm_regmap_init_i2c(client, &fusb300_regmap_config);
	if (IS_ERR(chip->map)) {
		dev_err(&client->dev, "Failed to initialize regmap\n");
		return -EINVAL;
	}
	chip->pdata = pdata;

	spin_lock_init(&chip->irqlock);
	chip->phy.dev = &client->dev;
	chip->phy.label = "fusb300";
	chip->phy.ops.measure_cc = fusb300_measure_cc;
	chip->phy.ops.set_host_current = fusb300_set_host_current;
	chip->phy.ops.get_host_current = fusb300_get_host_current;
	chip->phy.ops.switch_mode = fusb300_switch_mode;
	chip->phy.ops.setup_cc = fusb300_setup_cc;
	chip->phy.ops.wait_for_vbus = fusb300_wait_for_vbus;
	chip->phy.ops.mask_irq = fusb300_mask_irq;

	chip->phy.get_pd_version = fusb300_pd_version;
	chip->phy.is_pd_capable = fusb300_pd_capable;

	if (IS_ENABLED(CONFIG_ACPI))
		client->irq = fusb300_get_irq(client);
	else if (pdata) {
		/* we have platform data */
		ret = gpio_request_one(pdata->gpio_ccint, GPIOF_IN,
				       "fusb300_ccint");
		if (ret < 0) {
			DBG(DBG_ERR, "unable to get gpio - fusb300_ccint(%d), err=%d\n",
			    pdata->gpio_ccint, ret);
			goto skip_ccint;
		}

		client->irq = gpio_to_irq(pdata->gpio_ccint);
		if (client->irq < 0) {
			DBG(DBG_ERR, "unable to get irq for fusb300_ccint, err=%d\n", client->irq);
			client->irq = 0;
		}
		DBG(DBG_INT, "got irq(%d) for ccint\n", client->irq);

		ret = gpio_request_one(pdata->gpio_usb3_mux_vdd_en, GPIOF_OUT_INIT_LOW,
				       "usb3_mux_vdd_en");
		if (ret < 0) {
			DBG(DBG_ERR, "unable to get usb3_mux_vdd_en(%d), err=%d\n",
			    pdata->gpio_usb3_mux_vdd_en, ret);
			pdata->gpio_usb3_mux_vdd_en = -1;
			goto skip_ccint;
		}
		DBG(DBG_INT, "got gpio(%d) for usb3_mux_vdd_en\n", pdata->gpio_usb3_mux_vdd_en);

		ret = gpio_request_one(pdata->gpio_usb3_mux_sel, GPIOF_OUT_INIT_LOW,
				       "usb3_mux_sel");
		if (ret < 0) {
			DBG(DBG_ERR, "unable to get gpio_usb3_mux_sel(%d), err=%d\n",
			    pdata->gpio_usb3_mux_sel, ret);
			pdata->gpio_usb3_mux_sel = -1;
			goto skip_ccint;
		}
		DBG(DBG_INT, "got gpio(%d) for gpio_usb3_mux_sel\n", pdata->gpio_usb3_mux_sel);

		ret = gpio_request_one(pdata->gpio_usb_otg_plug, GPIOF_OUT_INIT_HIGH,
				       "usb_otg_plug");
		if (ret < 0) {
			DBG(DBG_ERR, "unable to get gpio_usb_otg_plug(%d), err=%d\n",
			    pdata->gpio_usb_otg_plug, ret);
			pdata->gpio_usb_otg_plug = -1;
			goto skip_ccint;
		}
		DBG(DBG_INT, "got gpio(%d) for usb_otg_plug\n", pdata->gpio_usb_otg_plug);

		INIT_DELAYED_WORK(&chip->tdm_work, tdm_evt_work);
	}
skip_ccint:
	mutex_init(&chip->lock);
	init_completion(&chip->int_complete);
	i2c_set_clientdata(client, chip);
	if (typec_add_phy(&chip->phy))
		dev_err(&client->dev, "error in typec_add_phy\n");
	chip->phy.id_gpio = pdata->gpio_usb_otg_plug;
	/* typec detect binding */
	typec_bind_detect(&chip->phy);
	fusb300_init_chip(chip);

	if (client->irq > 0) {
		ret = devm_request_threaded_irq(&client->dev, client->irq,
						NULL, fusb300_interrupt,
						IRQF_ONESHOT | IRQF_TRIGGER_LOW |
						IRQF_NO_SUSPEND,
						"fusb_ccint", chip);
		if (ret < 0) {
			dev_err(&client->dev,
				"error registering interrupt %d", ret);
			return -EIO;
		}
		regmap_write(chip->map, FUSB300_INT_MASK_REG, FUSB300_INT_MASK_ACTIVITY |
			     FUSB300_INT_MASK_CRCCHK | FUSB300_INT_MASK_ALERT |
			     FUSB300_INT_MASK_COLLISION);
	} else
		dev_warn(&client->dev,
			 "irq-no invalid: %d\n", client->irq);

	regmap_read(chip->map, FUSB300_CONTROL0_REG, &val);
	val &= ~FUSB300_CONTROL0_MASK_INT;
	regmap_write(chip->map, FUSB300_CONTROL0_REG, val);

	if (!chip->i_vbus)
		fusb300_wake_on_cc_change(chip);
	else
		atomic_notifier_call_chain(&chip->phy.notifier,
					   TYPEC_EVENT_VBUS, &chip->phy);

	sysfs_create_group(&dev->kobj, &dev_attr_grp);

	dev_set_drvdata(dev, chip);
	return 0;
}

static int fusb300_remove(struct i2c_client *client)
{
	struct fusb300_chip *chip = i2c_get_clientdata(client);
	struct typec_phy *phy = &chip->phy;

	sysfs_remove_group(&client->dev.kobj, &dev_attr_grp);

	typec_unbind_detect(&chip->phy);
	typec_remove_phy(phy);
	return 0;
}

static void fusb300_shutdown(struct i2c_client *client)
{
	struct fusb300_chip *chip = i2c_get_clientdata(client);
	int gpio_otg_plug;

	gpio_otg_plug = chip->pdata->gpio_usb_otg_plug;
	if (gpio_is_valid(gpio_otg_plug))
		gpio_set_value(gpio_otg_plug, 1);
}

static int fusb300_suspend(struct device *dev)
{
	struct fusb300_chip *chip = dev_get_drvdata(dev);
	struct typec_phy *phy;
	int gpio_mux_en, gpio_mux_sel;
	unsigned int val;

	if (!chip)
		return 0;

	regmap_read(chip->map, FUSB300_SWITCH0_REG, &val);
	if (!chip->i_vbus && (val & FUSB300_SWITCH0_PD_EN))
		/* Refuse suspend in case: DRP toggle, and UFP is actived,
		 * because we will not able to wakeup if OTG insert.
		 * But not for connecting with PC or AC adaptor. */
		return -EAGAIN;

	gpio_mux_en = chip->pdata->gpio_usb3_mux_vdd_en;
	gpio_mux_sel = chip->pdata->gpio_usb3_mux_sel;
	if (!gpio_is_valid(gpio_mux_en) || !gpio_is_valid(gpio_mux_sel))
		return 0;

	phy = &chip->phy;
	switch (phy->state) {
        case TYPEC_STATE_UNATTACHED_UFP:
        case TYPEC_STATE_UNATTACHED_DFP:
        case TYPEC_STATE_POWERED:
		gpio_set_value(gpio_mux_en, 0);
		gpio_set_value(gpio_mux_sel, 0);
		break;
        default:
		DBG(DBG_ERR, "call to %s with wrong state: %s\n",
		    __func__, typec_detect_state_str(phy->state));
		/* do nothing */
	}
	return 0;
}

static int fusb300_resume(struct device *dev)
{
	struct fusb300_chip *chip = dev_get_drvdata(dev);
	int gpio_mux_en;

	if (!chip)
		return 0;

	gpio_mux_en = chip->pdata->gpio_usb3_mux_vdd_en;
	if (!gpio_is_valid(gpio_mux_en))
		return 0;

	gpio_set_value(gpio_mux_en, 1);
	return 0;
}

static int fusb300_runtime_suspend(struct device *dev)
{
	return 0;
}

static int fusb300_runtime_idle(struct device *dev)
{
	return 0;
}

static int fusb300_runtime_resume(struct device *dev)
{
	return 0;
}

static const struct dev_pm_ops fusb300_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(fusb300_suspend,
				fusb300_resume)
	SET_RUNTIME_PM_OPS(fusb300_runtime_suspend,
			   fusb300_runtime_resume,
			   fusb300_runtime_idle)
};


#ifdef CONFIG_ACPI
static struct acpi_device_id fusb300_acpi_match[] = {
	{"FUSB0300", 0},
};
MODULE_DEVICE_TABLE(acpi, fusb300_acpi_match);
#endif

static const struct i2c_device_id fusb300_id[] = {
	{ FUSB_DEV_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, fusb300_id);

static struct i2c_driver fusb300_i2c_driver = {
	.driver	= {
		.name	= FUSB_DEV_NAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_ACPI
		.acpi_match_table = ACPI_PTR(fusb300_acpi_match),
#endif
		.pm	= &fusb300_pm_ops,
	},
	.probe	  = fusb300_probe,
	.remove	  = fusb300_remove,
	.shutdown = fusb300_shutdown,
	.id_table = fusb300_id,
};

static int __init fusb_i2c_init(void)
{
	int ret = 0;

	if (PROJ_ID_Z580CA == Read_PROJ_ID())
		ret = i2c_add_driver(&fusb300_i2c_driver);
	else {
		DBG(DBG_INT, "skip adding i2c driver, since PROJ_ID(%2x) is not matched\n",
		    Read_PROJ_ID());
	}

	return ret;
}

static void __exit fusb_i2c_exit(void)
{
	return;
}

module_init(fusb_i2c_init);
module_exit(fusb_i2c_exit);
//module_i2c_driver(fusb300_i2c_driver);

MODULE_AUTHOR("Kannappan, R r.kannappan@intel.com");
MODULE_DESCRIPTION("FUSB300 usb phy for TYPE-C & PD");
MODULE_LICENSE("GPL v2");
//MODULE_ALIAS("i2c:fusb300");
