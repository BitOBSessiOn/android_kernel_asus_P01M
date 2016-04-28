/*
 * Copyright (c) 2014, ASUSTek, Inc. All Rights Reserved.
 * Written by chris chang chris1_chang@asus.com
 */
#include <asm/types.h>

#define SDCV_SWCONTROL_ENABLE           (0x01 << 3)
#define SDCV_CCSM_OFF_ENABLE            (0x01 << 5)

#define SDCV_CHGRCTRL0_SWCONTROL_MASK   (0x01 << 3)
#define SDCV_CHGRCTRL0_CCSM_OFF_MASK    (0x01 << 5)

#define SDCV_CHGRCTRL0_ADDR             0x4B
#define SDCV_BATTIDRSLTH_REG            0xEC
#define SDCV_VBATRSLTH_REG              0xE9

#define SDCV_BATTID_CHANNEL_NAME        "BATID"
#define SDCV_VBAT_CHANNEL_NAME          "VBAT"

#ifdef CONFIG_PMIC_CCSM
int sd_cv_sw_take_control(void);
int sd_cv_batt_id_resistor(void);
int sd_cv_batt_vol(void);
#else
int sd_cv_sw_take_control(void) { return 0; }
int sd_cv_batt_id_resistor(void) { return -1; }
int sd_cv_batt_vol(void) { return -1; }
#endif
