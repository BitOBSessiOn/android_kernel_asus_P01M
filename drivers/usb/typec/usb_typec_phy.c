/*
 * usb_typec_phy.c: usb phy driver for type-c cable connector
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

#include <linux/slab.h>
#include <linux/export.h>
#include <linux/sched.h>
#include <linux/usb_typec_phy.h>

static LIST_HEAD(typec_phy_list);
static spinlock_t typec_irq_lock;

const char* typec_phy_state_str(enum typec_state s)
{
    static char* typec_state_str[]   = {
        [TYPEC_STATE_UNKNOWN]        = "UNKNOWN",
        [TYPEC_STATE_POWERED]        = "POWERED",
        [TYPEC_STATE_UNATTACHED_UFP] = "UNATTACHED_UFP",
        [TYPEC_STATE_UNATTACHED_DFP] = "UNATTACHED_DFP",
        [TYPEC_STATE_ATTACHED_UFP]   = "ATTACHED_UFP",
        [TYPEC_STATE_ATTACHED_DFP]   = "ATTACHED_DFP",
    };
    BUG_ON(s >= TYPEC_STATE_INVALID);
    return typec_state_str[s];
}
EXPORT_SYMBOL_GPL(typec_phy_state_str);

const char* typec_event_str(enum typec_event s)
{
    static char* typec_event_str[] = {
        [TYPEC_EVENT_UNKNOWN]      = "EVENT_UNKNOWN",
        [TYPEC_EVENT_VBUS]         = "EVENT_VBUS",
        [TYPEC_EVENT_DRP]          = "EVENT_DRP",
        [TYPEC_EVENT_TIMER]        = "EVENT_TIMER",
        [TYPEC_EVENT_NONE]         = "EVENT_NONE",
        [TYPEC_EVENT_DEV_REMOVE]   = "EVENT_DEV_REMOVE",
        [TYPEC_EVENT_STOP_DRP]     = "EVENT_STOP_DRP",
	[TYPEC_EVENT_TDM_UFP]	   = "EVENT_TDM_UFP",
	[TYPEC_EVENT_TDM_DFP]	   = "EVENT_TDM_DFP",
	[TYPEC_EVENT_TDM_DRP]	   = "EVENT_TDM_DRP",
    };
    BUG_ON(s >= TYPEC_EVENT_INVALID);
    return typec_event_str[s];
}
EXPORT_SYMBOL_GPL(typec_event_str);

const char* typec_cc_level_str(enum typec_cc_level s)
{
    static char* typec_cc_level_str[] = {
        [USB_TYPEC_CC_VRA]            = "RA",
        [USB_TYPEC_CC_VRD_USB]        = "RD_USB",
        [USB_TYPEC_CC_VRD_1500]       = "RD_1500",
        [USB_TYPEC_CC_VRD_3000]       = "RD_3000",
    };
    if (s < 0 || s >= ARRAY_SIZE(typec_cc_level_str))
        return "RD_UNKNOWN";
    return typec_cc_level_str[s];
}
EXPORT_SYMBOL_GPL(typec_cc_level_str);

struct typec_phy *typec_get_phy(int type)
{
	struct typec_phy *phy;
	unsigned long flags;

	spin_lock_irqsave(&typec_irq_lock, flags);
	list_for_each_entry(phy, &typec_phy_list, list) {
		if (phy->type != type)
			continue;
		spin_unlock_irqrestore(&typec_irq_lock, flags);
		return phy;
	}
	spin_unlock_irqrestore(&typec_irq_lock, flags);

	return ERR_PTR(-ENODEV);
}
EXPORT_SYMBOL_GPL(typec_get_phy);

int typec_get_cc_orientation(struct typec_phy *x)
{
	struct typec_phy *phy;
	unsigned long flags;

	if (x) {
		spin_lock_irqsave(&typec_irq_lock, flags);
		list_for_each_entry(phy, &typec_phy_list, list) {
			if (phy == x) {
				spin_unlock_irqrestore(&typec_irq_lock, flags);
				switch (phy->valid_cc) {
				case TYPEC_PIN_CC1:
					return TYPEC_POS_NORMAL;
				case TYPEC_PIN_CC2:
					return TYPEC_POS_SWAP;
				default:
					return TYPEC_POS_DISCONNECT;
				}
			}
		}
		spin_unlock_irqrestore(&typec_irq_lock, flags);
	}

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(typec_get_cc_orientation);

int typec_add_phy(struct typec_phy *phy)
{
	if (phy) {
		phy->type = USB_TYPE_C;
		phy->state = TYPEC_STATE_UNKNOWN;
		ATOMIC_INIT_NOTIFIER_HEAD(&phy->notifier);
		ATOMIC_INIT_NOTIFIER_HEAD(&phy->prot_notifier);
		list_add_tail(&phy->list, &typec_phy_list);
		return 0;
	}

	return -EINVAL;
}

int typec_remove_phy(struct typec_phy *x)
{
	struct typec_phy *phy;
	unsigned long flags;

	if (x) {
		spin_lock_irqsave(&typec_irq_lock, flags);
		list_for_each_entry(phy, &typec_phy_list, list) {
			if (phy == x)
				list_del(&phy->list);
		}
		spin_unlock_irqrestore(&typec_irq_lock, flags);
		return 0;
	}

	return -EINVAL;
}

static int __init phy_init(void)
{
	spin_lock_init(&typec_irq_lock);
	return 0;
}
arch_initcall(phy_init);
