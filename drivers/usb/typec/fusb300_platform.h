#ifndef _FUSB300_PLATFORM_H
#define _FUSB300_PLATFORM_H

struct fusb300_platform_data
{
    int gpio_usb3_mux_sel;      /* USB3.0 DE-MUX sel pin, 0: CH1, 1: CH2 */
    int gpio_usb3_mux_vdd_en;   /* High active for USB3.0 DE-MUX power enable */
    int gpio_ccint;
    int gpio_usb_otg_plug;
};


#endif
