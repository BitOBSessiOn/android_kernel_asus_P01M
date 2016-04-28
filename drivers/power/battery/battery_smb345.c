/*
 * Copyright (c) 2013, ASUSTek, Inc. All Rights Reserved.
 * Written by chris chang chris1_chang@asus.com
 */
#include <linux/debugfs.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/power_supply.h>
#include <linux/power/smb345-me372cg-charger.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/usb/otg.h>
#include <linux/kernel.h>
#include <linux/wakelock.h>
#include <linux/ioport.h>
#include <linux/io.h>

#include "smb345_external_include.h"
#include "ti/asus_battery.h"
#include "sdcv.h"
#include <linux/proc_fs.h>
#include <linux/random.h>
#include <linux/usb/penwell_otg.h>
//#include "../intel_mdf_charger.h"
#include <linux/HWVersion.h>
#include <asm/intel-mid.h>
#include <asm/intel_scu_ipc.h>
#include <asm/intel_mid_gpadc.h>
#include <asm/intel_mid_thermal.h>
#include <linux/earlysuspend.h>
#ifdef CONFIG_EEPROM_PADSTATION
#include <linux/microp_api.h>
#endif

/* Used to manipulate GP152 directly. */
#define USB_ULPI_0_REFCLK_BASE 0xff0c052c
#define USB_ULPI_0_REFCLK_LEN 4
void __iomem *usb_ulpi_0_refclk;
static void set_gpio_value_usb_ulpi_0_refclk(int value)
{
    if (!usb_ulpi_0_refclk)
        return;

    if (value) {
        writel(0xb120, usb_ulpi_0_refclk);
    }
    else {
        writel(0xb220, usb_ulpi_0_refclk);
    }
}

static int charger_type_report(struct notifier_block *nb, unsigned long event, void *ptr);
static struct notifier_block charger_type_notifier = {
    .notifier_call = charger_type_report,
};
extern int cable_status_register_client(struct notifier_block *nb);
extern int cable_status_unregister_client(struct notifier_block *nb);

static int HW_ID;
extern int Read_HW_ID(void);
extern int Read_PROJ_ID(void);
extern int entry_mode;
#ifdef CONFIG_UPI_BATTERY
extern bool get_unknown_battery();
#endif
static bool is_pad_present();
static void smb345_config_max_current(int usb_state);
static bool g_disable_700 = true;
static bool g_init_disable_aicl = true;
static bool g_is_power_supply_registered = false;
#if defined(CONFIG_A400CG)
static int a400cg_1200_1600 = 1600;
static void check_battery_match_device();
#endif
#if defined(CONFIG_PF400CG) || defined(CONFIG_A400CG)
static void check_battery_id();
#endif

int smb345_get_soc_control_float_vol(void);
/* I2C communication related */
#define I2C_RETRY_COUNT 6
#define I2C_RETRY_DELAY 5

#define CFG_CHARGE_CURRENT            0x00
#define CFG_CHARGE_CURRENT_FCC_MASK        0xe0
#define CFG_CHARGE_CURRENT_FCC_SHIFT        5
#define CFG_CHARGE_CURRENT_PCC_MASK        0x18
#define CFG_CHARGE_CURRENT_PCC_SHIFT        3
#define CFG_CHARGE_CURRENT_TC_MASK        0x07
#define CFG_CHARGE_CURRENT_ALL        0x41

#define CFG_CURRENT_LIMIT            0x01
#define CFG_CURRENT_LIMIT_DC_MASK        0xf0
#define CFG_CURRENT_LIMIT_DC_SHIFT        4
#define CFG_CURRENT_LIMIT_USB_MASK        0x0f
#define CFG_CURRENT_LIMIT_SMB346_MASK   0xf0
#define CFG_CURRENT_LIMIT_SMB346_VALUE_1200 0x40
#define CFG_CURRENT_LIMIT_SMB346_VALUE_700 0x20
#define CFG_CURRENT_LIMIT_SMB346_VALUE_500 0x10
#define CFG_CURRENT_LIMIT_SMB358_CHARGER_INHIBIT_CONTROL_MASK   BIT(2) | BIT(3)
#define CFG_VARIOUS_FUNCS            0x02
#define CFG_VARIOUS_FUNCS_PRIORITY_USB        BIT(2)
#define CFG_VARIOUS_FUNCS_OPTICHARGE_TOGGLE    BIT(4)
#define CFG_VARIOUS_FUNCS_BATTERY_OV    BIT(1)
#define CFG_FLOAT_VOLTAGE            0x03
#define CFG_FLOAT_VOLTAGE_THRESHOLD_MASK    0xc0
#define CFG_FLOAT_VOLTAGE_THRESHOLD_SHIFT    6
#define CFG_STAT                0x05
#define CFG_STAT_DISABLED            BIT(5)
#define CFG_STAT_ACTIVE_HIGH            BIT(7)
#define CFG_PIN                    0x06
#define CFG_PIN_EN_CTRL_MASK            0x60
#define CFG_PIN_EN_CTRL_ACTIVE_HIGH        0x40
#define CFG_PIN_EN_CTRL_ACTIVE_LOW        0x60
#define CFG_PIN_EN_APSD_IRQ            BIT(1)
#define CFG_PIN_EN_CHARGER_ERROR        BIT(2)
#define CFG_THERM                0x07
#define CFG_THERM_SOFT_HOT_COMPENSATION_MASK    0x03
#define CFG_THERM_SOFT_HOT_COMPENSATION_SHIFT    0
#define CFG_THERM_SOFT_COLD_COMPENSATION_MASK    0x0c
#define CFG_THERM_SOFT_COLD_COMPENSATION_SHIFT    2
#define CFG_THERM_MONITOR_DISABLED        BIT(4)
#define CFG_SYSOK                0x08
#define CFG_SYSOK_SUSPEND_HARD_LIMIT_DISABLED    BIT(2)
#define CFG_OTHER                0x09
#define CFG_OTHER_RID_MASK            0xc0
#define CFG_OTHER_RID_DISABLED_OTG_PIN        0x40
#define CFG_OTHER_RID_ENABLED_OTG_I2C        0x80
#define CFG_OTHER_RID_ENABLED_AUTO_OTG        0xc0
#define CFG_OTHER_OTG_PIN_ACTIVE_LOW        BIT(5)
#define CFG_OTG                    0x0a
#define CFG_OTG_TEMP_THRESHOLD_MASK        0x30
#define CFG_OTG_TEMP_THRESHOLD_SHIFT        4
#define CFG_OTG_CC_COMPENSATION_MASK        0xc0
#define CFG_OTG_CC_COMPENSATION_SHIFT        6
#define CFG_OTG_BATTERY_UVLO_THRESHOLD_MASK    0x03
#define CFG_TEMP_LIMIT                0x0b
#define CFG_TEMP_LIMIT_SOFT_HOT_MASK        0x03
#define CFG_TEMP_LIMIT_SOFT_HOT_SHIFT        0
#define CFG_TEMP_LIMIT_SOFT_COLD_MASK        0x0c
#define CFG_TEMP_LIMIT_SOFT_COLD_SHIFT        2
#define CFG_TEMP_LIMIT_HARD_HOT_MASK        0x30
#define CFG_TEMP_LIMIT_HARD_HOT_SHIFT        4
#define CFG_TEMP_LIMIT_HARD_COLD_MASK        0xc0
#define CFG_TEMP_LIMIT_HARD_COLD_SHIFT        6
#define CFG_FAULT_IRQ                0x0c
#define CFG_FAULT_IRQ_DCIN_UV            BIT(2)
#define CFG_FAULT_IRQ_OTG_UV            BIT(5)
#define CFG_STATUS_IRQ                0x0d
#define CFG_STATUS_IRQ_CHARGE_TIMEOUT        BIT(7)
#define CFG_STATUS_IRQ_TERMINATION_OR_TAPER    BIT(4)
#define CFG_ADDRESS                0x0e

/* Command registers */
#define CMD_A                    0x30
#define CMD_A_CHG_ENABLED            BIT(1)
#define CMD_A_SUSPEND_ENABLED            BIT(2)
#define CMD_A_OTG_ENABLED            BIT(4)
#define CMD_A_ALLOW_WRITE            BIT(7)
#define CMD_B                    0x31
#define CMD_B_USB9_AND_HC_MODE    0x03
#define CMD_C                    0x33

/* Interrupt Status registers */
#define IRQSTAT_A                0x35
#define IRQSTAT_C                0x37
#define IRQSTAT_C_TERMINATION_STAT        BIT(0)
#define IRQSTAT_C_TERMINATION_IRQ        BIT(1)
#define IRQSTAT_C_TAPER_IRQ            BIT(3)
#define IRQSTAT_D                0x38
#define IRQSTAT_D_CHARGE_TIMEOUT_STAT        BIT(2)
#define IRQSTAT_D_CHARGE_TIMEOUT_IRQ        BIT(3)
#define IRQSTAT_E                0x39
#define IRQSTAT_E_USBIN_UV_STAT            BIT(0)
#define IRQSTAT_E_USBIN_UV_IRQ            BIT(1)
#define IRQSTAT_E_DCIN_UV_STAT            BIT(4)
#define IRQSTAT_E_DCIN_UV_IRQ            BIT(5)
#define IRQSTAT_F                0x3a
#define IRQSTAT_F_OTG_UV_IRQ            BIT(5)
#define IRQSTAT_F_OTG_UV_STAT            BIT(4)

/* Status registers */
#define STAT_A                    0x3b
#define STAT_A_FLOAT_VOLTAGE_MASK        0x3f
#define STAT_B                    0x3c
#define STAT_C                    0x3d
#define STAT_C_CHG_ENABLED            BIT(0)
#define STAT_C_HOLDOFF_STAT            BIT(3)
#define STAT_C_CHG_MASK                0x06
#define STAT_C_CHG_SHIFT            1
#define STAT_C_CHG_TERM                BIT(5)
#define STAT_C_CHARGER_ERROR            BIT(6)
#define STAT_E                    0x3f

#define STATUS_UPDATE_INTERVAL            (HZ * 60)

struct smb345_otg_event {
    struct list_head    node;
    bool            param;
};

struct smb345_charger {
    struct mutex        lock;
    struct i2c_client    *client;
    struct power_supply    mains;
    struct power_supply    usb;
    struct power_supply    battery;
    bool            mains_online;
    bool            usb_online;
    bool            charging_enabled;
    bool            running;
    struct dentry        *dentry;
    struct dentry        *dentry2;
    struct otg_transceiver    *otg;
    struct notifier_block    otg_nb;
    struct work_struct    otg_work;
    struct work_struct  chrgr_type_work;
    struct delayed_work aicl_dete_work;
    struct workqueue_struct *chrgr_work_queue;
    struct list_head    otg_queue;
    spinlock_t        otg_queue_lock;
    bool            otg_enabled;
    bool            otg_battery_uv;
    const struct smb345_charger_platform_data    *pdata;
    charger_jeita_state_t charger_jeita_state;
    charger_jeita_status_t charger_jeita_status;
    /* wake lock to prevent S3 during charging */
    struct wake_lock wakelock;
#ifdef CONFIG_HAS_EARLYSUSPEND
    struct early_suspend es;
#endif
    bool screen_off;
};

static bool pad_pwr_supply();
struct wake_lock wlock;
struct wake_lock wlock_t;
#ifdef ME372CG_ENG_BUILD
bool eng_charging_limit = true;
bool flag_eng_stop_charging = false;
int charging_limit_threshold = CHARGING_LIMIT_THRESHOLD;
int asus_charging_limit_toggle_read(char *page, char **start, off_t off,
                    int count, int *eof, void *date)
{
    int len = 0;

    BAT_DBG_E(" %s:\n", __func__);
    return len;
}

int asus_charging_limit_toggle_write(struct file *file, const char *buffer,
                    unsigned long count, void *data)
{
    BAT_DBG_E(" %s:\n", __func__);

    if (buffer[0] == '1') {
        /* turn on charging limit in eng mode */
        eng_charging_limit = true;
    }
    else if (buffer[0] == '0') {
        /* turn off charging limit in eng mode */
        eng_charging_limit = false;
    }

    aicl_dete_worker(NULL);

    return count;
}

#ifndef CONFIG_NEW_PROC_FS
int init_asus_charging_limit_toggle(void)
{
    struct proc_dir_entry *entry=NULL;

    entry = create_proc_entry("driver/charger_limit_enable", 0666, NULL);
    if (!entry) {
        BAT_DBG_E("Unable to create asus_charging_toggle\n");
        return -EINVAL;
    }
    entry->read_proc = asus_charging_limit_toggle_read;
    entry->write_proc = asus_charging_limit_toggle_write;

    return 0;
}
#else
static ssize_t nasus_charging_limit_toggle_write(struct file *file,
    const char __user *buf, size_t count, loff_t * ppos)
{
    char proc_buf[64] = {0};

    if (count > sizeof(proc_buf)) {
        BAT_DBG("%s: data error\n", __func__);
        return -EINVAL;
    }

    if (copy_from_user(proc_buf, buf, count)) {
        BAT_DBG("%s: read data from user space error\n", __func__);
        return -EFAULT;
    }

    BAT_DBG_E(" %s: %s", __func__, proc_buf);

    if (!strncmp("1", proc_buf, 1)) {
        /* turn on charging limit in eng mode */
        eng_charging_limit = true;
        BAT_DBG_E(" %s: turn on charging limit: %s", __func__, proc_buf);
    }
    else if (!strncmp("0", proc_buf, 1)) {
        /* turn off charging limit in eng mode */
        eng_charging_limit = false;
        BAT_DBG_E(" %s: turn off charging limit: %s", __func__, proc_buf);
    }

    aicl_dete_worker(NULL);
    request_power_supply_changed();

    return count;
}
static int nasus_charging_limit_toggle_read(struct seq_file *m, void *v)
{
    seq_printf(m, "%s: ENG charging limit %s\n", __func__, eng_charging_limit ? "On" : "Off");
    return 0;
}
static int nasus_charging_limit_toggle_open(struct inode *inode, struct file *file)
{
	return single_open(file, nasus_charging_limit_toggle_read, NULL);
}
//==============================================================================
static int nasus_charging_threshold_read(struct seq_file *m, void *v)
{
    seq_printf(m, "%d\n", charging_limit_threshold);
    BAT_DBG_E(" %s:%d\n", __func__, charging_limit_threshold);

    return 0;
}
static ssize_t nasus_charging_threshold_write(struct file *file,
    const char __user *buf, size_t count, loff_t * ppos)
{
    char proc_buf[64];
    char backup_fbuf[4];
    char default_charging_limit[4];
    static bool backup_file_ready = false;
    struct file *fp = NULL;
    mm_segment_t old_fs;
    int byte_count= 0;
    s32 res;

    if (count > sizeof(proc_buf)) {
        BAT_DBG("%s: data error\n", __func__);
        return -EINVAL;
    }

    if (copy_from_user(proc_buf, buf, count)) {
        BAT_DBG("%s: read data from user space error\n", __func__);
        return -EFAULT;
    }

    if (proc_buf[0] == 's') {

        BAT_DBG("************ CHARGER LIMIT THRESHOLD start ************\n",
            __func__);

        /* partition is ready for writing/reading */
        backup_file_ready = true;

        /* read value from file */
        fp = filp_open(CHARGING_LIMIT_THRESHOLD_FILE, O_RDWR, 0666);
        if (!IS_ERR_OR_NULL(fp) ) {
            /* Get current segment descriptor and Set segment
            *  descriptor associated to kernel space.
            */
            old_fs = get_fs();
            set_fs(KERNEL_DS);

            /* read file */
            byte_count= fp->f_op->read(fp, backup_fbuf, sizeof(backup_fbuf), &fp->f_pos);
            BAT_DBG(" %s: read data from file: %s\n", __func__, backup_fbuf);

            /* write default value to file if it's empty */
            if (!strncmp("", backup_fbuf, 1)) {
                BAT_DBG_E(" %s: file exist but is empty! Write default to file\n",
                    __func__);

                sprintf(default_charging_limit, "%d", charging_limit_threshold);
                fp->f_op->llseek(fp, 0, 0);
                fp->f_op->write(fp,
                    default_charging_limit,
                    sizeof(default_charging_limit),
                    &fp->f_pos);

                goto file_open_but_error;
            }

            /* give up if fail to transition */
            if (kstrtos32(backup_fbuf, 10, &res)) {
                BAT_DBG_E(" %s: kstrtos32 error!", __func__);

                goto file_open_but_error;
            }
            BAT_DBG(" %s: read data from file(int): %d\n", __func__, res);

            /* replace the charging limit threshold with new value */
            charging_limit_threshold = res;

            /* Restore segment descriptor
            */
            set_fs(old_fs);

            /* Close file operation
            */
            filp_close(fp, NULL);
        }
        else {
            BAT_DBG_E("%s: file open error (%s)\n",
                __func__,
                CHARGING_LIMIT_THRESHOLD_FILE);
            return -EFAULT;
        }
    }
    else {
        /* directly return if backup file is not ready */
        if (!backup_file_ready) {
            BAT_DBG_E("%s: backup file not ready!\n", __func__);
            return count;
        }

        /* give up if fail to transition */
        if (kstrtos32_from_user(buf, count, 10, &res)) {
            BAT_DBG_E(" %s: kstrtos32 error!", __func__);
            BAT_DBG_E(" %s: proc_buf is %s", __func__, proc_buf);
            return -EFAULT;
        }

        /* replace the charging limit threshold with new value */
        if (0 <= res && res <= 100) {

            /* backup new value to file */
            fp = filp_open(CHARGING_LIMIT_THRESHOLD_FILE,
                    O_WRONLY | O_CREAT | O_TRUNC,
                    0666);
            if (!IS_ERR_OR_NULL(fp) ) {
                /* Get current segment descriptor and Set segment
                *  descriptor associated to kernel space.
                */
                old_fs = get_fs();
                set_fs(KERNEL_DS);

                /* replace the charging limit threshold with new value */
                charging_limit_threshold = res;

                sprintf(default_charging_limit, "%d", charging_limit_threshold);
                fp->f_op->llseek(fp, 0, 0);
                fp->f_op->write(fp,
                    default_charging_limit,
                    sizeof(default_charging_limit),
                    &fp->f_pos);

                /* Restore segment descriptor
                */
                set_fs(old_fs);

                /* Close file operation
                */
                filp_close(fp, NULL);
            }
            else {
                BAT_DBG_E("%s: file open error (%s)\n",
                    __func__,
                    CHARGING_LIMIT_THRESHOLD_FILE);
                return -EFAULT;
            }
        }
        else {
            BAT_DBG_E(" %s: out of normal range, skip it!\n", __func__);
            return -EFAULT;
        }
    }

    aicl_dete_worker(NULL);

    return count;

file_open_but_error:
    /* Restore segment descriptor
    */
    set_fs(old_fs);

    /* Close file operation
    */
    filp_close(fp, NULL);
    return -EFAULT;
}
static int nasus_charging_threshold_open(struct inode *inode, struct file *file)
{
	return single_open(file, nasus_charging_threshold_read, NULL);
}

int init_asus_charging_limit_toggle(void)
{
    static const struct file_operations charging_limit_toggle_fops = {
        .owner = THIS_MODULE,
        .open = nasus_charging_limit_toggle_open,
        .read = seq_read,
        .write = nasus_charging_limit_toggle_write,
        .llseek = seq_lseek,
        .release = single_release,
    };
    struct proc_dir_entry *entry=NULL;

    entry = proc_create("driver/charger_limit_enable", 0666, NULL,
        &charging_limit_toggle_fops);
    if (!entry) {
        BAT_DBG_E("Unable to create asus_charging_toggle\n");
        return -EINVAL;
    }

    /* proc interface for charging limit threshold */
    static const struct file_operations asus_charging_threshold_fops = {
        .owner = THIS_MODULE,
        .open = nasus_charging_threshold_open,
        .read = seq_read,
        .write = nasus_charging_threshold_write,
        .llseek = seq_lseek,
        .release = single_release,
    };
    struct proc_dir_entry *entry2=NULL;

    entry2 = proc_create("driver/charging_limit_threshold", 0666, NULL,
        &asus_charging_threshold_fops);
    if (!entry2) {
        BAT_DBG_E("Unable to create /proc/driver/charging_limit_threshold\n");
        return -EINVAL;
    }

    return 0;
}
#endif
#else
int init_asus_charging_limit_toggle(void) { return 0; }
#endif

/* global charger type variable lock */
DEFINE_MUTEX(g_usb_state_lock);
static int g_usb_state = CABLE_OUT;
static int g_pad_cable_state = CABLE_OUT;
/* global software charging toggle lock */
DEFINE_MUTEX(g_charging_toggle_lock);
static bool g_charging_toggle = true;

static bool g_stop_charging_by_tempr_550 = false;

static struct smb345_charger *smb345_dev;
static struct chgr_dev_func smb345_tbl;

#if 0
/* Fast charge current in uA */
static const unsigned int fcc_tbl[] = {
    700000,
    900000,
    1200000,
    1500000,
    1800000,
    2000000,
    2200000,
    2500000,
};

/* Pre-charge current in uA */
static const unsigned int pcc_tbl[] = {
    100000,
    150000,
    200000,
    250000,
};

/* Termination current in uA */
static const unsigned int tc_tbl[] = {
    37500,
    50000,
    100000,
    150000,
    200000,
    250000,
    500000,
    600000,
};
#endif

/* Input current limit (mA) : 01h */
static const unsigned int icl_tbl[] = {
    300,
    500,
    700,
    900,
    1200,
    1500,
    1800,
    2000,
    2200,
    2500,
};

/* AICL Results Table (mA) : 3Fh */
#if defined(CONFIG_A400CG) || defined(CONFIG_ME175CG) || defined(CONFIG_FE380CG) || defined(CONFIG_Z580C) || defined(CONFIG_FE171MG)
/* smb358 */
static const unsigned int aicl_results[] = {
    300,
    500,
    700,
    1000,
    1200,
    1300,
    1800,
    2000
};
#else
/* PF400CG, A503ML adopted smb345, smb346, smb347 */
static const unsigned int aicl_results[] = {
    300,
    500,
    700,
    900,
    1200,
    1500,
    1800,
    2000,
    2200,
    2500,
};
#endif

/* Charge current compensation in uA */
static const unsigned int ccc_tbl[] = {
    250000,
    700000,
    900000,
    1200000,
};

#define EXPORT_CHARGER_OTG

#define DEBUG 1
#define DRIVER_VERSION            "1.1.0"

#define SMB345_MASK(BITS, POS)  ((unsigned char)(((1 << BITS) - 1) << POS))

/* Register definitions */
#define CHG_CURRENT_REG            0x00
#define INPUT_CURRENT_LIMIT_REG    0x01
#define VAR_FUNC_REG            0x02
#define FLOAT_VOLTAGE_REG        0x03
#define CHG_CTRL_REG            0x04
#define STAT_TIMER_REG            0x05
#define PIN_ENABLE_CTRL_REG        0x06
#define THERM_CTRL_A_REG        0x07
#define SYSOK_USB3_SELECT_REG    0x08
#define OTHER_CTRL_A_REG        0x09
#define OTG_TLIM_THERM_CNTRL_REG                0x0A

#define HARD_SOFT_LIMIT_CELL_TEMP_MONITOR_REG    0x0B
#define SOFT_LIMIT_HOT_CELL_TEMP_MASK            SMB345_MASK(2, 0)
#define SOFT_LIMIT_COLD_CELL_TEMP_MASK            SMB345_MASK(2, 2)
#define HARD_LIMIT_HOT_CELL_TEMP_MASK            SMB345_MASK(2, 4)
#define HARD_LIMIT_COLD_CELL_TEMP_MASK            SMB345_MASK(2, 6)

#define FAULT_INTERRUPT_REG        0x0C
#define STATUS_INTERRUPT_REG    0x0D
#define I2C_BUS_SLAVE_REG        0x0E //chris: add
#define CMD_A_REG        0x30
#define CMD_B_REG        0x31
#define CMD_C_REG        0x33
#define INTERRUPT_A_REG        0x35
#define INTERRUPT_B_REG        0x36
#define INTERRUPT_C_REG        0x37
#define INTERRUPT_D_REG        0x38
#define INTERRUPT_E_REG        0x39
#define INTERRUPT_F_REG        0x3A
#define STATUS_A_REG    0x3B
#define STATUS_B_REG    0x3C
#define STATUS_C_REG    0x3D
#define STATUS_D_REG    0x3E
#define STATUS_E_REG    0x3F

/* Status bits and masks */
#define CHG_STATUS_MASK        SMB345_MASK(2, 1)
#define CHG_ENABLE_STATUS_BIT        BIT(0)

/* Control bits and masks */
#define FAST_CHG_CURRENT_MASK            SMB345_MASK(4, 4)
#define AC_INPUT_CURRENT_LIMIT_MASK        SMB345_MASK(4, 0)
#define PRE_CHG_CURRENT_MASK            SMB345_MASK(3, 5)
#define TERMINATION_CURRENT_MASK        SMB345_MASK(3, 2)
#define PRE_CHG_TO_FAST_CHG_THRESH_MASK    SMB345_MASK(2, 6)
#define FLOAT_VOLTAGE_MASK                SMB345_MASK(6, 0)
#define CHG_ENABLE_BIT            BIT(1)
#define VOLATILE_W_PERM_BIT        BIT(7)
#define USB_SELECTION_BIT        BIT(1)
#define SYSTEM_FET_ENABLE_BIT    BIT(7)
#define AUTOMATIC_INPUT_CURR_LIMIT_BIT            BIT(4)
#define AUTOMATIC_POWER_SOURCE_DETECTION_BIT    BIT(2)
#define BATT_OV_END_CHG_BIT        BIT(1)
#define VCHG_FUNCTION            BIT(0)
#define CURR_TERM_END_CHG_BIT    BIT(6)

#define OTGID_PIN_CONTROL_MASK    SMB345_MASK(2, 6)
#define OTGID_PIN_CONTROL_BITS    BIT(6)

#define OTG_CURRENT_LIMIT_AT_USBIN_MASK    SMB345_MASK(2, 2)
#define OTG_CURRENT_LIMIT_750mA    (BIT(2) | BIT(3))
#define OTG_CURRENT_LIMIT_500mA    BIT(3)
#define OTG_CURRENT_LIMIT_250mA    BIT(2)
#define OTG_BATTERY_UVLO_THRESHOLD_MASK    SMB345_MASK(2, 0)

#define CHARGE_CURRENT_COMPENSATION         SMB345_MASK(2, 6)
#define CHARGE_CURRENT_COMPENSATION_VALUE   0x00

#define CREATE_DEBUGFS_INTERRUPT_STATUS_REGISTERS
#define SMB358_FAST_CHG_CURRENT_MASK            SMB345_MASK(3, 5)
#define SMB345_FAST_CHG_CURRENT_MASK            SMB345_MASK(3, 5)
#define SMB358_TERMINATION_CURRENT_MASK         SMB345_MASK(3, 0)
#define SMB358_TERMINATION_CURRENT_VALUE_200mA    BIT(0) | BIT(1) | BIT(2)
#define SMB358_TERMINATION_CURRENT_VALUE_80mA    BIT(1) | BIT(0)
#define SMB358_TERMINATION_CURRENT_VALUE_100mA   BIT(2)
#define SMB358_FAST_CHG_CURRENT_VALUE_450mA    BIT(5)
#define SMB358_FAST_CHG_CURRENT_VALUE_600mA    BIT(6)
#define SMB358_FAST_CHG_CURRENT_VALUE_900mA    BIT(5) | BIT(6)
#define SMB358_FAST_CHG_CURRENT_VALUE_2000mA    BIT(5) | BIT(6) | BIT(7)

static int smb346_soc_detect_batt_tempr(int usb_state);
static inline int get_battery_rsoc(int *rsoc);
static inline int get_battery_temperature(int *tempr);
static inline int get_battery_voltage(int *volt);

#ifdef CONFIG_LEDS_INTEL_KPD
extern void set_off();
extern void set_red();
extern void set_green();
extern void set_orange();
extern void set_blink();

void led_control()
{
    int usb_state;
    int rsoc;

    if (entry_mode == 1)
        return;
    if (is_pad_present())
        return;

    mutex_lock(&g_usb_state_lock);
    usb_state = g_usb_state;
    mutex_unlock(&g_usb_state_lock);

    if (usb_state != USB_IN && usb_state != AC_IN && usb_state != CABLE_OUT)
        return;

    /* acquire battery rsoc here */
    if (get_battery_rsoc(&rsoc)) {
        BAT_DBG_E(" %s: * fail to get battery rsoc *\n", __func__);
    }
    else {
        if (usb_state == CABLE_OUT) {
            if (rsoc < 15 && rsoc > -1) {
                BAT_DBG(" %s: ======== LED BLINK ========\n", __func__);
                set_blink();
            }
            else {
                BAT_DBG(" %s: ======== LED OFF ========\n", __func__);
                set_off();
            }
        }
        else {
            if (rsoc == 100) {
                BAT_DBG(" %s: ======== LED GREEN ========\n", __func__);
                set_green();
            }
            else {
                BAT_DBG(" %s: ======== LED ORANGE ========\n", __func__);
                set_orange();
            }
        }
    }
}
#else
void led_control() {}
#endif

static char *supply_list[] = {
    "battery",
};

static enum power_supply_property asus_power_properties[] = {
    POWER_SUPPLY_PROP_PRESENT,
    POWER_SUPPLY_PROP_ONLINE,
};

static int smb345_power_get_property(struct power_supply *psy,
                    enum power_supply_property psp,
                    union power_supply_propval *val);

static struct power_supply smb345_power_supplies[] = {
    {
        .name = "ac",
        .type = POWER_SUPPLY_TYPE_MAINS,
        .supplied_to = supply_list,
        .num_supplicants = ARRAY_SIZE(supply_list),
        .properties = asus_power_properties,
        .num_properties = ARRAY_SIZE(asus_power_properties),
        .get_property = smb345_power_get_property,
    },
    {
        .name = "usb",
        .type = POWER_SUPPLY_TYPE_USB,
        .supplied_to = supply_list,
        .num_supplicants = ARRAY_SIZE(supply_list),
        .properties = asus_power_properties,
        .num_properties = ARRAY_SIZE(asus_power_properties),
        .get_property = smb345_power_get_property,
    },
};

static int smb345_power_get_property(struct power_supply *psy,
                    enum power_supply_property psp,
                    union power_supply_propval *val)
{
    int ret=0;
    int usb_state;
    int chrg_status;

    mutex_lock(&g_usb_state_lock);
    usb_state = g_usb_state;
    mutex_unlock(&g_usb_state_lock);

    switch (psp) {
    case POWER_SUPPLY_PROP_ONLINE:
        if (psy->type == POWER_SUPPLY_TYPE_USB) {
            if (is_pad_present())
                val->intval = 0;
            else
                val->intval = (usb_state == USB_IN) ? 1 : 0;
        }
        else if (psy->type == POWER_SUPPLY_TYPE_MAINS) {
            if (is_pad_present()) {
#ifdef CONFIG_EEPROM_PADSTATION
                val->intval = (AX_MicroP_get_USBDetectStatus(Batt_P01) == P01_CABLE_CHARGER) ? 1 : 0;
#else
                val->intval = (g_pad_cable_state == AC_IN) ? 1 : 0;
#endif
            } else {
                val->intval = (g_pad_cable_state == AC_IN) ? 1 : 0;
            }
        }
        else {
            ret = -EINVAL;
        }
        break;
    case POWER_SUPPLY_PROP_PRESENT:
        if (psy->type == POWER_SUPPLY_TYPE_USB) {
            /* for ATD test to acquire the status about charger ic */
            if (!smb345_has_charger_error()) {
                val->intval = 1;
                return 0;
            }

            chrg_status = smb345_get_charging_status();
            if (chrg_status == POWER_SUPPLY_STATUS_CHARGING) {
                val->intval = 1;
                return 0;
            }
            else if (chrg_status == POWER_SUPPLY_STATUS_FULL) {
                val->intval = 1;
                return 0;
            }
            val->intval = 0;
        }
        else if (psy->type == POWER_SUPPLY_TYPE_MAINS) {
            if (!smb345_has_charger_error()) {
                val->intval = 1;
                return 0;
            }

            chrg_status = smb345_get_charging_status();
            if (chrg_status == POWER_SUPPLY_STATUS_CHARGING) {
                val->intval = 1;
                return 0;
            }
            else if (chrg_status == POWER_SUPPLY_STATUS_FULL) {
                val->intval = 1;
                return 0;
            }
            val->intval = 0;
        }
        else
            ret = -EINVAL;
        break;
    default:
        ret = -EINVAL;
    }

    return ret;
}

extern bool get_sw_charging_toggle()
{
    bool ret;

    if (!smb345_dev) {
        pr_err("Warning: smb345_dev is null "
            "due to probe function has error\n");
        return false;
    }

    mutex_lock(&g_charging_toggle_lock);
    ret = g_charging_toggle;
    mutex_unlock(&g_charging_toggle_lock);

    return ret;
}

extern int smb3xx_get_charger_type()
{
    int ret;

    if (!smb345_dev) {
        pr_err("Warning: smb345_dev is null "
            "due to probe function has error\n");
        return CABLE_OUT;
    }

    mutex_lock(&g_usb_state_lock);
    if (is_pad_present())
        ret = PAD_SUPPLY;
    else
        ret = g_usb_state;
    mutex_unlock(&g_usb_state_lock);

    return ret;
}

int request_power_supply_changed()
{
    int ret;

    if (!smb345_dev) {
        pr_err("Warning: smb345_dev is null "
            "due to probe function has error\n");
        return -1;
    }

    if (!g_is_power_supply_registered)
        return -1;

    power_supply_changed(&smb345_power_supplies[CHARGER_AC-1]);
    power_supply_changed(&smb345_power_supplies[CHARGER_USB-1]);

    return ret;
}

static int smb345_register_power_supply(struct device *dev)
{
    int ret;

    ret = power_supply_register(dev, &smb345_power_supplies[CHARGER_USB-1]);
    if (ret) {
        BAT_DBG_E("Fail to register power supply USB\n");
        goto batt_err_reg_fail_usb;
    }

    ret = power_supply_register(dev, &smb345_power_supplies[CHARGER_AC-1]);
    if (ret) {
        BAT_DBG_E("Fail to register power supply AC\n");
        goto batt_err_reg_fail_ac;
    }

    g_is_power_supply_registered = true;
    return 0;

batt_err_reg_fail_ac:
    power_supply_unregister(&smb345_power_supplies[CHARGER_USB-1]);
batt_err_reg_fail_usb:
    return ret;
}

static int smb345_read_reg(struct i2c_client *client, int reg,
                u8 *val, int ifDebug)
{
    s32 ret;
    struct smb345_charger *smb345_chg;

    smb345_chg = i2c_get_clientdata(client);
    ret = i2c_smbus_read_byte_data(smb345_chg->client, reg);
    if (ret < 0) {
        dev_err(&smb345_chg->client->dev,
            "i2c read fail: can't read from Reg%02Xh: %d\n", reg, ret);
        return ret;
    } else {
        *val = ret;
    }
    if (ifDebug)
        pr_info("Reg%02Xh = " BYTETOBINARYPATTERN
            "\n", reg, BYTETOBINARY(*val));

    return 0;
}

static int smb345_write_reg(struct i2c_client *client, int reg,
                        u8 val)
{
    s32 ret;
    struct smb345_charger *smb345_chg;

    smb345_chg = i2c_get_clientdata(client);

    ret = i2c_smbus_write_byte_data(smb345_chg->client, reg, val);
    if (ret < 0) {
        dev_err(&smb345_chg->client->dev,
            "i2c write fail: can't write %02X to %02X: %d\n",
            val, reg, ret);
        return ret;
    }
    return 0;
}

static int smb345_masked_read(struct i2c_client *client, int reg, u8 mask)
{
    s32 rc;
    u8 temp;
    int retry_count = I2C_RETRY_COUNT;

    do
    {
        rc = smb345_read_reg(client, reg, &temp, 0);
        if (rc) {
            retry_count--;
            pr_err("*smb345_read_reg failed: reg=%03X, rc=%d\n", reg, rc);
            msleep(I2C_RETRY_DELAY);
        }
    } while (rc && retry_count > 0);
    if (rc) {
        pr_err("smb345_read_reg failed: reg=%03X, rc=%d\n", reg, rc);
        return -1;
    }

    temp &= mask;
    return temp;
}

static int smb345_masked_write(struct i2c_client *client, int reg,
        u8 mask, u8 val)
{
    s32 rc;
    u8 temp;
    int retry_count = I2C_RETRY_COUNT;

    do
    {
    rc = smb345_read_reg(client, reg, &temp, 0);
    if (rc) {
        retry_count--;
        pr_err("*smb345_read_reg failed: reg=%03X, rc=%d\n", reg, rc);
        msleep(I2C_RETRY_DELAY);
    }
    } while (rc && retry_count > 0);
    if (rc) {
        pr_err("smb345_read_reg failed: reg=%03X, rc=%d\n", reg, rc);
        return rc;
    }

    temp &= ~mask;
    temp |= val & mask;

    retry_count = I2C_RETRY_COUNT;
    do
    {
    rc = smb345_write_reg(client, reg, temp);
    if (rc) {
        retry_count--;
        pr_err("*smb345_write failed: reg=%03X, rc=%d\n", reg, rc);
        msleep(I2C_RETRY_DELAY);
    }
    } while (rc && retry_count > 0);
    if (rc) {
        pr_err("smb345_write failed: reg=%03X, rc=%d\n", reg, rc);
        return rc;
    }

    return 0;
}

static int smb345_read(struct smb345_charger *smb, u8 reg)
{
    int ret;
    int retry_count = I2C_RETRY_COUNT;

    do
    {
        ret = i2c_smbus_read_byte_data(smb->client, reg);
        if (ret < 0) {
            retry_count--;
            dev_warn(&smb->client->dev, "fail to read reg %02xh: %d\n",
                reg, ret);
            msleep(I2C_RETRY_DELAY);
        }
    } while (ret < 0 && retry_count > 0);

    return ret;
}

static int smb345_write(struct smb345_charger *smb, u8 reg, u8 val)
{
    int ret;
    int retry_count = I2C_RETRY_COUNT;

    do
    {
        ret = i2c_smbus_write_byte_data(smb->client, reg, val);
        if (ret < 0) {
            retry_count--;
            dev_warn(&smb->client->dev, "fail to write reg %02xh: %d\n",
                reg, ret);
            msleep(I2C_RETRY_DELAY);
        }
    } while (ret < 0 && retry_count > 0);

    return ret;
}

static int smb345_set_writable(struct smb345_charger *smb, bool writable)
{
    int ret;

    ret = smb345_read(smb, CMD_A);
    if (ret < 0)
        return ret;

    if (writable)
        ret |= CMD_A_ALLOW_WRITE;
    else
        ret &= ~CMD_A_ALLOW_WRITE;

    return smb345_write(smb, CMD_A, ret);
}

static int cancel_soft_hot_temp_limit(bool cancel_it)
{
    int ret;

    /* ME371MG EVB/SR1 meet this problem that smb345 stop charging
        when IC temperature is high up to Soft Hot Limit. But Battery
        is not full charging. We disable this limitation.
    */

    ret = smb345_read(smb345_dev, CFG_THERM);
    if (ret < 0)
        return ret;

    ret &= ~CFG_THERM_SOFT_HOT_COMPENSATION_MASK;
    if (!cancel_it)
        ret |= 0x02;

    ret = smb345_write(smb345_dev, CFG_THERM, ret);
    if (ret < 0)
        return ret;
}

/*----------------------------------------------------------------------------*/
/* JEITA function for cell temperature control by SoC
 */

int smb345_recharge_func(int vbat)
{
    int ret;
    int Vflt_sub_100mV;

    if (!smb345_dev) {
        pr_err("Warning: smb345_dev is null "
            "due to probe function has error\n");
        return 1;
    }

    pr_debug("%s: vbat=%d\n", __func__, vbat);

    if (smb345_get_soc_control_float_vol() == 0x2A) // 4.34V
        Vflt_sub_100mV = 4340 - 100;
    else if (smb345_get_soc_control_float_vol() == 0x1E) // 4.11V
        Vflt_sub_100mV = 4110 - 100;
    else
        Vflt_sub_100mV = 0;

    if (Vflt_sub_100mV == 0) return 0;

    if (vbat < Vflt_sub_100mV) {
        /* write 01h[3:2]="10" */
        ret = smb345_masked_write(smb345_dev->client,
            0x01,
            BIT(3) | BIT(2),
            BIT(3));
        if (ret) {
            pr_err("fail to set Recharge Voltage = Vflt-200mV "
                "ret=%d\n", ret);
            return ret;
        }
    }
    else {
        /* write 01h[3:2]="00" */
        ret = smb345_masked_write(smb345_dev->client,
            0x01,
            BIT(3) | BIT(2),
            0);
        if (ret) {
            pr_err("fail to set Recharge Voltage = Vflt-50mV "
                "ret=%d\n", ret);
            return ret;
        }
    }

    return ret;
}

#if defined(CONFIG_PF400CG) || defined(CONFIG_ME175CG) || defined(CONFIG_A400CG) || defined(CONFIG_A450CG) || defined(CONFIG_ME372CG) || defined(CONFIG_ME372CL)
int smb345_soc_control_jeita(void)
{
    int ret;

    if (!smb345_dev) {
        pr_err("Warning: smb345_dev is null "
            "due to probe function has error\n");
        return 1;
    }

    pr_info("%s:", __func__);
    ret = smb345_set_writable(smb345_dev, true);
    if (ret < 0)
        return ret;

    /* write 0bh[5:4]="11" */
    ret = smb345_masked_write(smb345_dev->client,
        HARD_SOFT_LIMIT_CELL_TEMP_MONITOR_REG,
        HARD_LIMIT_HOT_CELL_TEMP_MASK,
        0xff);
    if (ret) {
        pr_err("fail to set HARD_LIMIT_HOT_CELL_TEMP_MASK "
            "ret=%d\n", ret);
        return ret;
    }

    #ifndef CONFIG_A450CG
    /* write 0bh[1:0]="11" */
    ret = smb345_masked_write(smb345_dev->client,
        HARD_SOFT_LIMIT_CELL_TEMP_MONITOR_REG,
        SOFT_LIMIT_HOT_CELL_TEMP_MASK,
        0xff);
    if (ret) {
        pr_err("fail to set SOFT_LIMIT_HOT_CELL_TEMP_MASK "
            "ret=%d\n", ret);
        return ret;
    }
    #else
    /* write 07h[3:2]="01" */
    ret = smb345_masked_write(smb345_dev->client,
        CFG_THERM,
        CFG_THERM_SOFT_COLD_COMPENSATION_MASK,
        BIT(2));
    if (ret) {
        pr_err("fail to set SOFT_COLD_TEMP_LIMIT_BEHAVIOR"
            "ret=%d\n", ret);
        return ret;
    }
    #endif

    #if defined(CONFIG_PF400CG) || defined(CONFIG_ME175CG) || defined(CONFIG_A400CG) || defined(CONFIG_A450CG)
    /* write 07h[1:0]="00" */
    ret = cancel_soft_hot_temp_limit(true);
    if (ret) {
        pr_err("fail to set Soft Hot Limit Behavior to No Response "
            "ret=%d\n", ret);
        return ret;
    }
    #endif

    return ret;
}

/* JEITA function for cell temperature control by Charger IC
 */
int smb345_charger_control_jeita(void)
{
    int ret;

    if (!smb345_dev) {
        pr_err("Warning: smb345_dev is null "
            "due to probe function has error\n");
        return 1;
    }

    pr_info("%s:", __func__);
    ret = smb345_set_writable(smb345_dev, true);
    if (ret < 0)
        return ret;

    /* write 0bh[5:4]= "01" or "00" */
    ret = smb345_masked_write(smb345_dev->client,
        HARD_SOFT_LIMIT_CELL_TEMP_MONITOR_REG,
        HARD_LIMIT_HOT_CELL_TEMP_MASK,
        #if defined(CONFIG_PF400CG)
        0x01);
        #elif defined(CONFIG_A400CG)
        0x01);
        #elif defined(CONFIG_A450CG)
        0x01);
        #else
        0x00);
        #endif
    if (ret) {
        pr_err("fail to set HARD_LIMIT_HOT_CELL_TEMP_MASK "
            "ret=%d\n", ret);
        return ret;
    }

    /* write 0bh[1:0]="01" */
    ret = smb345_masked_write(smb345_dev->client,
        HARD_SOFT_LIMIT_CELL_TEMP_MONITOR_REG,
        SOFT_LIMIT_HOT_CELL_TEMP_MASK,
        0x01);
    if (ret) {
        pr_err("fail to set SOFT_LIMIT_HOT_CELL_TEMP_MASK "
            "ret=%d\n", ret);
        return ret;
    }

    #if defined(CONFIG_PF400CG) || defined(CONFIG_ME175CG) || defined(CONFIG_A400CG) || defined(CONFIG_A450CG)
    /* write 07h[1:0]="10" */
    ret = cancel_soft_hot_temp_limit(false);
    if (ret) {
        pr_err("fail to set Soft Hot Limit Behavior to "
            "Float Voltage Compensation"
            "ret=%d\n", ret);
        return ret;
    }
    #endif

    #if defined(CONFIG_A450CG)
    /* write 07h[3:2]="01" */
    ret = smb345_masked_write(smb345_dev->client,
        CFG_THERM,
        CFG_THERM_SOFT_COLD_COMPENSATION_MASK,
        BIT(2));
    if (ret) {
        pr_err("fail to set SOFT_COLD_TEMP_LIMIT_BEHAVIOR"
            "ret=%d\n", ret);
        return ret;
    }
    #endif

    return ret;
}

/* new JEITA rule designed by Ming2 */
int smb3xx_soc_control_float_vol(float_volt_tempr_threshold_t threshold)
{
    int ret;

    ret = smb345_set_writable(smb345_dev, true);
    if (ret < 0)
        return ret;

    if (threshold == LOW_TEMPR) {
        /* write 03h[5:0]="101010" or "101011" or "100011" */
        ret = smb345_masked_write(smb345_dev->client,
            FLOAT_VOLTAGE_REG,
            FLOAT_VOLTAGE_MASK,
#if defined(CONFIG_PF400CG)
            0x2A);
#elif defined(CONFIG_A450CG)
            0x23);
#else
            0x2B);
#endif
    }
    else if (threshold == MIDDLE_TEMPR) {
        /* write 03h[5:0]="011001" */
        ret = smb345_masked_write(smb345_dev->client,
            FLOAT_VOLTAGE_REG,
            FLOAT_VOLTAGE_MASK,
            0x19);
    }
    else if (threshold == HIGH_TEMPR) {
        /* write 03h[5:0]="011110" or "011111"*/
        ret = smb345_masked_write(smb345_dev->client,
            FLOAT_VOLTAGE_REG,
            FLOAT_VOLTAGE_MASK,
#if defined(CONFIG_A450CG)
            0x23);
#else
            0x1E);
#endif
    }
    else {
        /* What the hell are you? */
    }

    return ret;
}

int smb345_get_soc_control_float_vol()
{
    int ret;

    /* read 03h[5:0]="011110" or "101010"*/
    ret = smb345_masked_read(smb345_dev->client,
                FLOAT_VOLTAGE_REG,
                FLOAT_VOLTAGE_MASK);

    return ret;
}

/* JEITA function for float voltage configuration by SoC
 */
int smb345_soc_control_float_vol(int bat_temp)
{
    int ret;
    int batt_volt;

    if (!smb345_dev) {
        pr_err("Warning: smb345_dev is null "
            "due to probe function has error\n");
        return 1;
    }

    ret = smb345_set_writable(smb345_dev, true);
    if (ret < 0)
        return ret;

    #if defined(CONFIG_PF400CG) || defined(CONFIG_A400CG) || defined(CONFIG_ME175CG)
    /* acquire battery voltage here */
    ret = get_battery_voltage(&batt_volt);
    if (ret) {
        BAT_DBG_E(" %s: fail to get battery voltage\n", __func__);
        return ret;
    }
    else
        BAT_DBG(" %s: get battery voltage(%d)\n", __func__, batt_volt);
    #endif

    /* the unit of battery temperature is 0.1C */
    if (bat_temp < FLOAT_VOLTAGE_TEMPERATURE_THRESHOLD) {
        /* write 03h[5:0]="101010" or "101011"*/
        ret = smb345_masked_write(smb345_dev->client,
            FLOAT_VOLTAGE_REG,
            FLOAT_VOLTAGE_MASK,
            #if defined(CONFIG_PF400CG) || defined(CONFIG_ME372CG) || defined(CONFIG_ME372CL)
            0x2A);
            #else
            0x2B);
            #endif
    }
    else {
        #if defined(CONFIG_PF400CG)
        if (bat_temp < 600) {
            if (batt_volt < 4110) {
                /* write 03h[5:0]="011110"*/
                ret = smb345_masked_write(smb345_dev->client,
                    FLOAT_VOLTAGE_REG,
                    FLOAT_VOLTAGE_MASK,
                    0x1E);
            }
            else {
                /* write 03h[5:0]="101010"*/
                ret = smb345_masked_write(smb345_dev->client,
                    FLOAT_VOLTAGE_REG,
                    FLOAT_VOLTAGE_MASK,
                    0x2A);
            }
        }
        else {
            /* write 03h[5:0]="101010"*/
            ret = smb345_masked_write(smb345_dev->client,
                FLOAT_VOLTAGE_REG,
                FLOAT_VOLTAGE_MASK,
                0x2A);
        }
        #elif defined(CONFIG_ME175CG)
        if (bat_temp < 550) {
            if (batt_volt < 4110) {
                /* write 03h[5:0]="011110"*/
                ret = smb345_masked_write(smb345_dev->client,
                    FLOAT_VOLTAGE_REG,
                    FLOAT_VOLTAGE_MASK,
                    0x1E);
            }
            else {
                /* write 03h[5:0]="101011"*/
                ret = smb345_masked_write(smb345_dev->client,
                    FLOAT_VOLTAGE_REG,
                    FLOAT_VOLTAGE_MASK,
                    0x2B);
            }
        }
        else {
            /* write 03h[5:0]="011110"*/
            ret = smb345_masked_write(smb345_dev->client,
                FLOAT_VOLTAGE_REG,
                FLOAT_VOLTAGE_MASK,
                0x1E);
        }
        #elif defined(CONFIG_A400CG)
        if (bat_temp < 450) {
            if (batt_volt < 4110) {
                /* write 03h[5:0]="011110"*/
                ret = smb345_masked_write(smb345_dev->client,
                    FLOAT_VOLTAGE_REG,
                    FLOAT_VOLTAGE_MASK,
                    0x1E);
            }
            else {
                /* write 03h[5:0]="101011"*/
                ret = smb345_masked_write(smb345_dev->client,
                    FLOAT_VOLTAGE_REG,
                    FLOAT_VOLTAGE_MASK,
                    0x2B);
            }
        }
        else {
            /* write 03h[5:0]="011110"*/
            ret = smb345_masked_write(smb345_dev->client,
                FLOAT_VOLTAGE_REG,
                FLOAT_VOLTAGE_MASK,
                0x1E);
        }
        #else
        /* ME372CG ME372CL: 50C <= tempr < 55C */
        if (bat_temp < 550) {
            if (0x2A == smb345_masked_read(smb345_dev->client,
                FLOAT_VOLTAGE_REG,
                FLOAT_VOLTAGE_MASK) && batt_volt >= 4100) {
                /* write 03h[5:0]="101010"*/
                ret = smb345_masked_write(smb345_dev->client,
                    FLOAT_VOLTAGE_REG,
                    FLOAT_VOLTAGE_MASK,
                    0x2A);
            }
            else {
                /* write 03h[5:0]="011110"*/
                ret = smb345_masked_write(smb345_dev->client,
                    FLOAT_VOLTAGE_REG,
                    FLOAT_VOLTAGE_MASK,
                    0x1E);
            }
        }
        else { /* tempr >= 55C */
            /* write 03h[5:0]="011110"*/
            ret = smb345_masked_write(smb345_dev->client,
                FLOAT_VOLTAGE_REG,
                FLOAT_VOLTAGE_MASK,
                0x1E);
        }
        #endif
    }
    if (ret)
        pr_err("fail to set FLOAT_VOLTAGE_REG ret=%d\n", ret);

    return ret;
}
#else
int smb345_get_soc_control_float_vol() { return 0; }
int smb345_soc_control_jeita(void) { return 0; }
int smb345_charger_control_jeita(void) { return 0; }
int smb345_soc_control_float_vol(int bat_temp) { return 0; }
int smb3xx_soc_control_float_vol(float_volt_tempr_threshold_t threshold) { return 0; }
#endif

#if defined(CONFIG_FE380CG) || defined(CONFIG_FE170CSG)
int smb3xx_soc_control_jeita()
{
    int ret;

    if (!smb345_dev) {
        pr_err("Warning: smb345_dev is null "
            "due to probe function has error\n");
        return 1;
    }

    pr_info("%s:", __func__);

    /* Set Hard Hot Limit as 72 Deg. C 0Bh[5:4]="11" */
    ret = smb345_masked_write(smb345_dev->client,
            0x0B,
            BIT(5) | BIT(4),
            BIT(5) | BIT(4));

    /* Set Soft Hot Limit Behavior as No Response 07h[1:0]="00" */
    ret = smb345_masked_write(smb345_dev->client,
            0x07,
            BIT(1) | BIT(0),
            0);

    /* Set Soft Cold Temp Limit as No Response 07h[3:2]="00" */
    ret = smb345_masked_write(smb345_dev->client,
            0x07,
            BIT(3) | BIT(2),
            0);

    if (ret)
        pr_err("%s: i2c communication failure!", __func__);

    return ret;
}
#elif defined(CONFIG_FE171MG)
int smb3xx_soc_control_jeita()
{
    int ret;

    if (!smb345_dev) {
        pr_err("Warning: smb345_dev is null "
            "due to probe function has error\n");
        return 1;
    }

    pr_info("%s:", __func__);

    /* Set Hard Hot Limit as 53 Deg. C 0Bh[5:4]="11" */
    ret = smb345_masked_write(smb345_dev->client,
            0x0B,
            BIT(5) | BIT(4),
            BIT(5) | BIT(4));

    /* Set Soft Hot Limit Behavior as Float Voltage Compensation 07h[1:0]="00" */
    ret = smb345_masked_write(smb345_dev->client,
            0x07,
            BIT(1) | BIT(0),
            0);

    /* Set Soft Cold Temp Limit as No Response 07h[3:2]="00" */
    ret = smb345_masked_write(smb345_dev->client,
            0x07,
            BIT(3) | BIT(2),
            0);

    if (ret)
        pr_err("%s: i2c communication failure!", __func__);

    return ret;
}
#elif defined(CONFIG_A503ML)
int smb3xx_soc_control_jeita()
{
    int ret;

    if (!smb345_dev) {
        pr_err("Warning: smb345_dev is null "
            "due to probe function has error\n");
        return 1;
    }

    pr_info("%s:", __func__);

    /* Set Hard Hot Limit as 72 Deg. C 0Bh[5:4]="11" */
    ret = smb345_masked_write(smb345_dev->client,
            0x0B,
            BIT(5) | BIT(4),
            BIT(5) | BIT(4));

    /* Set Soft Hot Limit Behavior as No Response 07h[1:0]="00" */
    ret = smb345_masked_write(smb345_dev->client,
            0x07,
            BIT(1) | BIT(0),
            0);

    if (ret)
        pr_err("%s: i2c communication failure!", __func__);

    return ret;
}
#elif defined(CONFIG_Z580C)
int smb3xx_soc_control_jeita()
{
    int ret;

    if (!smb345_dev) {
        pr_err("Warning: smb345_dev is null "
            "due to probe function has error\n");
        return 1;
    }

    pr_info("%s:", __func__);

    /* Set Hard Hot Limit as 72 Deg. C 0Bh[5:4]="11" */
    ret = smb345_masked_write(smb345_dev->client,
            0x0B,
            BIT(5) | BIT(4),
            BIT(5) | BIT(4));

    /* Set Soft Hot Limit Behavior as No Response 07h[1:0]="00" */
    ret = smb345_masked_write(smb345_dev->client,
            0x07,
            BIT(1) | BIT(0),
            0);

    if (HW_ID >= HW_ID_PR) { /* smb347 */
        /* Set Soft Cold temp limit as No Response 07h[3:2]="00" */
        ret = smb345_masked_write(smb345_dev->client,
                0x07,
                BIT(3) | BIT(2),
                0);
    }
    if (ret)
        pr_err("%s: i2c communication failure!", __func__);

    return ret;
}
#else
int smb3xx_soc_control_jeita() { return 0; }
#endif

#if defined(CONFIG_FE380CG) || defined(CONFIG_FE170CSG)
int smb3xx_charger_control_jeita()
{
    int ret;

    if (!smb345_dev) {
        pr_err("Warning: smb345_dev is null "
            "due to probe function has error\n");
        return 1;
    }

    pr_info("%s:", __func__);

    /* Set Hard Hot Limit as 53 Deg. C 0Bh[5:4]="00" */
    ret = smb345_masked_write(smb345_dev->client,
            0x0B,
            BIT(5) | BIT(4),
            0);

    /* Set Soft Hot Limit Behavior as Float Voltage Compensation 07h[1:0]="10" */
    ret = smb345_masked_write(smb345_dev->client,
            0x07,
            BIT(1) | BIT(0),
            BIT(1));

    /* Set Soft Cold Temp Limit as Charge Current Compensation 07h[3:2]="01" */
    ret = smb345_masked_write(smb345_dev->client,
            0x07,
            BIT(3) | BIT(2),
            BIT(2));

    if (HW_ID >= HW_ID_PR) /* smb347 */
        ret = smb345_charging_toggle(JEITA, true);
    if (ret)
        pr_err("%s: i2c communication failure!", __func__);

    return ret;
}
#elif defined(CONFIG_FE171MG)
int smb3xx_charger_control_jeita()
{
    int ret;

    if (!smb345_dev) {
        pr_err("Warning: smb345_dev is null "
            "due to probe function has error\n");
        return 1;
    }

    pr_info("%s:", __func__);

    /* Set Hard Hot Limit as 53 Deg. C 0Bh[5:4]="00" */
    ret = smb345_masked_write(smb345_dev->client,
            0x0B,
            BIT(5) | BIT(4),
            0);

    /* Set Soft Hot Limit as 50 Deg. C 0Bh[1:0]="01" */
    ret = smb345_masked_write(smb345_dev->client,
            0x0B,
            BIT(1) | BIT(0),
            BIT(0));

    /* Set Soft Hot Limit Behavior as Float Voltage Compensation 07h[1:0]="10" */
    ret = smb345_masked_write(smb345_dev->client,
            0x07,
            BIT(1) | BIT(0),
            BIT(1));

    /* Set Soft Cold Temp Limit as Charge Current Compensation 07h[3:2]="01" */
    ret = smb345_masked_write(smb345_dev->client,
            0x07,
            BIT(3) | BIT(2),
            BIT(2));

    if (ret)
        pr_err("%s: i2c communication failure!", __func__);

    return ret;
}
#elif defined(CONFIG_A503ML)
int smb3xx_charger_control_jeita()
{
    int ret;

    if (!smb345_dev) {
        pr_err("Warning: smb345_dev is null "
            "due to probe function has error\n");
        return 1;
    }

    pr_info("%s:", __func__);

    /* Set Hard Hot Limit as 59 Deg. C 0Bh[5:4]="01" */
    ret = smb345_masked_write(smb345_dev->client,
            0x0B,
            BIT(5) | BIT(4),
            BIT(4));

    /* Set Soft Hot Limit Behavior as Float Voltage Compensation 07h[1:0]="10" */
    ret = smb345_masked_write(smb345_dev->client,
            0x07,
            BIT(1) | BIT(0),
            BIT(1));

    if (ret)
        pr_err("%s: i2c communication failure!", __func__);

    return ret;
}
#elif defined(CONFIG_Z580C)
int smb3xx_charger_control_jeita()
{
    int ret;

    if (!smb345_dev) {
        pr_err("Warning: smb345_dev is null "
            "due to probe function has error\n");
        return 1;
    }

    BAT_DBG(" %s:", __func__);

    /* Set Hard Hot Limit as 59 Deg. C 0Bh[5:4]="01" */
    ret = smb345_masked_write(smb345_dev->client,
            0x0B,
            BIT(5) | BIT(4),
            BIT(4));

    /* Set Soft Hot Limit Behavior as Float Voltage Compensation 07h[1:0]="10" */
    ret = smb345_masked_write(smb345_dev->client,
            0x07,
            BIT(1) | BIT(0),
            BIT(1));

    /* Set Soft Cold temp limit as Charge Current Compensation  07h[3:2]="01" */
    ret = smb345_masked_write(smb345_dev->client,
            0x07,
            BIT(3) | BIT(2),
            BIT(2));

    /* Set Soft Hot Limit as 53 Deg. C 0Bh[1:0]="10" */
    ret = smb345_masked_write(smb345_dev->client,
            0x0B,
            BIT(1) | BIT(0),
            BIT(1));

    /* charging enable */
    ret = smb345_charging_toggle(JEITA, true);
    if (ret)
        BAT_DBG_E(" %s: i2c communication failure!", __func__);

    return ret;
}
#else
int smb3xx_charger_control_jeita() { return 0; }
#endif

#if defined(CONFIG_FE380CG) || defined(CONFIG_FE170CSG)
int smb3xx_jeita_control(int bat_tempr, int bat_volt)
{
    static charger_jeita_status_t ori_charger_jeita_status = ROOM;
    int ret;

    if (!smb345_dev) {
        pr_err("Warning: smb345_dev is null "
            "due to probe function has error\n");
        return POWER_SUPPLY_STATUS_DISCHARGING;
    }

    ori_charger_jeita_status = smb345_dev->charger_jeita_status;

    /* condition judgement 1: */
    if (ori_charger_jeita_status == FREEZE) {
        if (bat_tempr >= 30)
            /* control diagram II: */
            goto control_diagram_II;
        else
            /* control diagram I: */
            goto control_diagram_I;
    }

    /* condition judgement 2: */
    if (ori_charger_jeita_status == COLD) {
        if (bat_tempr > 130) {
            /* control diagram III: */
            goto control_diagram_III;
        }
        else if (bat_tempr >= 100) {
            /* control diagram II: */
            goto control_diagram_II;
        }
    }

    if (ori_charger_jeita_status == ROOM) {
        if (bat_tempr < 100) {
            /* control diagram II: */
            goto control_diagram_II;
        }
        else if (bat_tempr >= 500) {
            if (bat_volt >= 4110) {
                /* control diagram V: */
                goto control_diagram_V;
            }
            else {
                /* control diagram IV: */
                goto control_diagram_IV;
            }
        }
        else
            /* control diagram III: */
            goto control_diagram_III;
    }

    /* condition judgement 3: */
    if (ori_charger_jeita_status == HOT) {
        if (smb345_get_soc_control_float_vol() == 0x2B) {
            if (bat_tempr < 470) {
                /* control diagram III: */
                goto control_diagram_III;
            }
        }
        else if (smb345_get_soc_control_float_vol() == 0x1E) {
            if (bat_tempr < 500) {
                /* control diagram III: */
                goto control_diagram_III;
            }
        }
    }

    /* condition judgement 4: */
    if (ori_charger_jeita_status == OVERHEAT) {
        if (bat_tempr <= 520) {
            /* control diagram IV, V: */
            if (smb345_get_soc_control_float_vol() == 0x2B && bat_volt >= 4110)
                /* control diagram V: */
                goto control_diagram_V;
            else
                /* control diagram IV: */
                goto control_diagram_IV;
        }
        else
            /* control diagram VI: */
            goto control_diagram_VI;
    }

    /* condition judgement 5: */
    if (bat_tempr < 0) {
        /* control diagram I: */
        goto control_diagram_I;
    }

    /* condition judgement 6: */
    if (bat_tempr >= 550) {
        /* control diagram VI: */
        goto control_diagram_VI;
    }

    /* condition judgement 7: */
    if ((0 <= bat_tempr && bat_tempr < 100) &&
        (ori_charger_jeita_status == COLD || ori_charger_jeita_status == ROOM)) {
        /* control diagram II: */
        goto control_diagram_II;
    }

    /* condition judgement 8: */
    if ((100 <= bat_tempr && bat_tempr < 500) &&
        ori_charger_jeita_status == ROOM) {
        /* control diagram III: */
        goto control_diagram_III;
    }

    /* condition judgement 9: */
    if ((500 <= bat_tempr && bat_tempr < 550) &&
        (ori_charger_jeita_status == ROOM || ori_charger_jeita_status == HOT)) {
        /* control diagram IV, V: */
        if (smb345_get_soc_control_float_vol() == 0x2B && bat_volt >= 4110)
            /* control diagram V: */
            goto control_diagram_V;
        else
            /* control diagram IV: */
            goto control_diagram_IV;
    }

    BAT_DBG_E(" %s: *** ERROR ***\n", __func__);

control_diagram_I:
    ret = smb345_masked_write(smb345_dev->client,
            0x03,
            BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0),
            0x2B);
    ret = smb345_charging_toggle(JEITA, false);
    ret = smb345_masked_write(smb345_dev->client,
            0x01,
            BIT(3) | BIT(2),
            BIT(3));
    ret = smb345_masked_write(smb345_dev->client,
            0x00,
            BIT(7) | BIT(6) | BIT(5),
            BIT(7) | BIT(6) | BIT(5));
    smb345_dev->charger_jeita_status = FREEZE;
    if (ori_charger_jeita_status != smb345_dev->charger_jeita_status)
        BAT_DBG(" %s: ori_JEITA: %d, new_JEITA: %d, %d(0.1C), %dmV",
            __func__,
            ori_charger_jeita_status,
            smb345_dev->charger_jeita_status, bat_tempr, bat_volt);
    return POWER_SUPPLY_STATUS_DISCHARGING;

control_diagram_II:
    ret = smb345_masked_write(smb345_dev->client,
            0x03,
            BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0),
            0x2B);
    ret = smb345_charging_toggle(JEITA, true);
    ret = smb345_masked_write(smb345_dev->client,
            0x01,
            BIT(3) | BIT(2),
            BIT(3));
    ret = smb345_masked_write(smb345_dev->client,
            0x00,
            BIT(7) | BIT(6) | BIT(5),
            BIT(6));
    smb345_dev->charger_jeita_status = COLD;
    if (ori_charger_jeita_status != smb345_dev->charger_jeita_status)
        BAT_DBG(" %s: ori_JEITA: %d, new_JEITA: %d, %d(0.1C), %dmV",
            __func__,
            ori_charger_jeita_status,
            smb345_dev->charger_jeita_status, bat_tempr, bat_volt);
    return POWER_SUPPLY_STATUS_CHARGING;

control_diagram_III:
    ret = smb345_masked_write(smb345_dev->client,
            0x03,
            BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0),
            0x2B);
    ret = smb345_charging_toggle(JEITA, true);
    ret = smb345_masked_write(smb345_dev->client,
            0x01,
            BIT(3) | BIT(2),
            BIT(3));
    ret = smb345_masked_write(smb345_dev->client,
            0x00,
            BIT(7) | BIT(6) | BIT(5),
            BIT(7) | BIT(6) | BIT(5));
    smb345_dev->charger_jeita_status = ROOM;
    if (ori_charger_jeita_status != smb345_dev->charger_jeita_status)
        BAT_DBG(" %s: ori_JEITA: %d, new_JEITA: %d, %d(0.1C), %dmV",
            __func__,
            ori_charger_jeita_status,
            smb345_dev->charger_jeita_status, bat_tempr, bat_volt);
    return POWER_SUPPLY_STATUS_CHARGING;

control_diagram_IV:
    ret = smb345_masked_write(smb345_dev->client,
            0x03,
            BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0),
            0x1E);
    ret = smb345_charging_toggle(JEITA, true);
    ret = smb345_masked_write(smb345_dev->client,
            0x01,
            BIT(3) | BIT(2),
            BIT(2));
    ret = smb345_masked_write(smb345_dev->client,
            0x00,
            BIT(7) | BIT(6) | BIT(5),
            BIT(7) | BIT(6) | BIT(5));
    smb345_dev->charger_jeita_status = HOT;
    if (ori_charger_jeita_status != smb345_dev->charger_jeita_status)
        BAT_DBG(" %s: ori_JEITA: %d, new_JEITA: %d, %d(0.1C), %dmV",
            __func__,
            ori_charger_jeita_status,
            smb345_dev->charger_jeita_status, bat_tempr, bat_volt);
    return POWER_SUPPLY_STATUS_CHARGING;

control_diagram_V:
    ret = smb345_masked_write(smb345_dev->client,
            0x03,
            BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0),
            0x2B);
    ret = smb345_charging_toggle(JEITA, false);
    ret = smb345_masked_write(smb345_dev->client,
            0x01,
            BIT(3) | BIT(2),
            BIT(3));
    ret = smb345_masked_write(smb345_dev->client,
            0x00,
            BIT(7) | BIT(6) | BIT(5),
            BIT(7) | BIT(6) | BIT(5));
    smb345_dev->charger_jeita_status = HOT;
    if (ori_charger_jeita_status != smb345_dev->charger_jeita_status)
        BAT_DBG(" %s: ori_JEITA: %d, new_JEITA: %d, %d(0.1C), %dmV",
            __func__,
            ori_charger_jeita_status,
            smb345_dev->charger_jeita_status, bat_tempr, bat_volt);
    return POWER_SUPPLY_STATUS_DISCHARGING;

control_diagram_VI:
    ret = smb345_masked_write(smb345_dev->client,
            0x03,
            BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0),
            0x2B);
    ret = smb345_charging_toggle(JEITA, false);
    ret = smb345_masked_write(smb345_dev->client,
            0x01,
            BIT(3) | BIT(2),
            BIT(3));
    ret = smb345_masked_write(smb345_dev->client,
            0x00,
            BIT(7) | BIT(6) | BIT(5),
            BIT(7) | BIT(6) | BIT(5));
    smb345_dev->charger_jeita_status = OVERHEAT;
    if (ori_charger_jeita_status != smb345_dev->charger_jeita_status)
        BAT_DBG(" %s: ori_JEITA: %d, new_JEITA: %d, %d(0.1C), %dmV",
            __func__,
            ori_charger_jeita_status,
            smb345_dev->charger_jeita_status, bat_tempr, bat_volt);
    return POWER_SUPPLY_STATUS_DISCHARGING;

others:
    return POWER_SUPPLY_STATUS_CHARGING;
}
#elif defined(CONFIG_FE171MG)
int smb3xx_jeita_control(int bat_tempr, int bat_volt)
{
    static charger_jeita_status_t ori_charger_jeita_status = ROOM;
    int ret;

    if (!smb345_dev) {
        pr_err("Warning: smb345_dev is null "
            "due to probe function has error\n");
        return POWER_SUPPLY_STATUS_DISCHARGING;
    }

    ori_charger_jeita_status = smb345_dev->charger_jeita_status;

    /* condition judgement 1: */
    if (ori_charger_jeita_status == FREEZE) {
        if (bat_tempr >= 45)
            /* control diagram II: */
            goto control_diagram_II;
        else
            /* control diagram I: */
            goto control_diagram_I;
    }

    /* condition judgement 2: */
    if (ori_charger_jeita_status == COLD) {
        if (bat_tempr >= 130) {
            /* control diagram III: */
            goto control_diagram_III;
        }
        else if (bat_tempr >= 100) {
            /* control diagram II: */
            goto control_diagram_II;
        }
    }

    if (ori_charger_jeita_status == ROOM) {
        if (bat_tempr < 100) {
            /* control diagram II: */
            goto control_diagram_II;
        }
        else if (bat_tempr >= 500) {
            if (bat_volt > 4110) {
                /* control diagram V: */
                goto control_diagram_V;
            }
            else {
                /* control diagram IV: */
                goto control_diagram_IV;
            }
        }
        else
            /* control diagram III: */
            goto control_diagram_III;
    }

    /* condition judgement 3: */
    if (ori_charger_jeita_status == HOT) {
        // state before is "Charging Disable"
        if (smb345_get_soc_control_float_vol() == 0x1E) {
            if (bat_tempr <= 470) {
                /* control diagram III: */
                goto control_diagram_III;
            }
            else if (bat_tempr >=550) {
                /* control diagram VI: */
                goto control_diagram_VI;
            }
            else {
                if (bat_volt > 4110) {
                    /* control diagram V: */
                    goto control_diagram_V;
                }
                else {
                    /* control diagram IV: */
                    goto control_diagram_IV;
                }
            }
        }
        else { // state before is "Charging Enable"
            if (bat_tempr < 500) {
                /* control diagram III: */
                goto control_diagram_III;
            }
            else if (bat_tempr >= 550) {
                /* control diagram VI: */
                goto control_diagram_VI;
            }
            else {
                if (bat_volt > 4110) {
                    /* control diagram V: */
                    goto control_diagram_V;
                }
                else {
                    /* control diagram IV: */
                    goto control_diagram_IV;
                }
            }
        }
    }

    /* condition judgement 4: */
    if (ori_charger_jeita_status == OVERHEAT) {
        if (bat_tempr <= 520) {
        /* control diagram IV, V: */
        if (bat_volt > 4110)
            /* control diagram V: */
            goto control_diagram_V;
        else
            /* control diagram IV: */
            goto control_diagram_IV;
        }
        else
            /* control diagram VI: */
            goto control_diagram_VI;
    }

    /* condition judgement 5: */
    if (bat_tempr < 15) {
        /* control diagram I: */
        goto control_diagram_I;
    }

    /* condition judgement 6: */
    if (bat_tempr >= 550) {
        /* control diagram VI: */
        goto control_diagram_VI;
    }

    /* condition judgement 7: */
    if ((15 <= bat_tempr && bat_tempr < 100) &&
        (ori_charger_jeita_status == COLD || ori_charger_jeita_status == ROOM)) {
        /* control diagram II: */
        goto control_diagram_II;
    }

    /* condition judgement 8: */
    if ((100 <= bat_tempr && bat_tempr < 500) &&
        ori_charger_jeita_status == ROOM) {
        /* control diagram III: */
        goto control_diagram_III;
    }

    /* condition judgement 9: */
    if ((500 <= bat_tempr && bat_tempr < 550) &&
        (ori_charger_jeita_status == ROOM || ori_charger_jeita_status == HOT)) {
        /* control diagram IV, V: */
        if (bat_volt > 4110)
            /* control diagram V: */
            goto control_diagram_V;
        else
            /* control diagram IV: */
            goto control_diagram_IV;
    }

    BAT_DBG_E(" %s: *** ERROR ***\n", __func__);

control_diagram_I:
    ret = smb345_masked_write(smb345_dev->client,
            0x03,
            BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0),
            BIT(5) | BIT(3) | BIT(1));
    ret = smb345_charging_toggle(JEITA, false);
    smb345_dev->charger_jeita_status = FREEZE;
    if (ori_charger_jeita_status != smb345_dev->charger_jeita_status)
        BAT_DBG(" %s: ori_JEITA: %d, new_JEITA: %d, %d(0.1C), %dmV",
            __func__,
            ori_charger_jeita_status,
            smb345_dev->charger_jeita_status, bat_tempr, bat_volt);
    return POWER_SUPPLY_STATUS_DISCHARGING;

control_diagram_II:
    ret = smb345_masked_write(smb345_dev->client,
            0x03,
            BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0),
            BIT(5) | BIT(3) | BIT(1));
    ret = smb345_masked_write(smb345_dev->client,
            0x00,
            BIT(7) | BIT(6) | BIT(5),
            BIT(6));
    ret = smb345_charging_toggle(JEITA, true);
    ret = smb345_masked_write(smb345_dev->client,
            0x01,
            BIT(3) | BIT(2),
            BIT(2));
    smb345_dev->charger_jeita_status = COLD;
    if (ori_charger_jeita_status != smb345_dev->charger_jeita_status)
        BAT_DBG(" %s: ori_JEITA: %d, new_JEITA: %d, %d(0.1C), %dmV",
            __func__,
            ori_charger_jeita_status,
            smb345_dev->charger_jeita_status, bat_tempr, bat_volt);
    return POWER_SUPPLY_STATUS_CHARGING;

control_diagram_III:
    ret = smb345_masked_write(smb345_dev->client,
            0x03,
            BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0),
            BIT(5) | BIT(3) | BIT(1));
    ret = smb345_masked_write(smb345_dev->client,
            0x00,
            BIT(7) | BIT(6) | BIT(5),
            BIT(7) | BIT(5));
    ret = smb345_charging_toggle(JEITA, true);
    ret = smb345_masked_write(smb345_dev->client,
            0x01,
            BIT(3) | BIT(2),
            BIT(2));
    smb345_dev->charger_jeita_status = ROOM;
    if (ori_charger_jeita_status != smb345_dev->charger_jeita_status)
        BAT_DBG(" %s: ori_JEITA: %d, new_JEITA: %d, %d(0.1C), %dmV",
            __func__,
            ori_charger_jeita_status,
            smb345_dev->charger_jeita_status, bat_tempr, bat_volt);
    return POWER_SUPPLY_STATUS_CHARGING;

control_diagram_IV:
    ret = smb345_masked_write(smb345_dev->client,
            0x03,
            BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0),
            0x1E);
    ret = smb345_masked_write(smb345_dev->client,
            0x00,
            BIT(7) | BIT(6) | BIT(5),
            BIT(7) | BIT(5));
    ret = smb345_charging_toggle(JEITA, true);
    ret = smb345_masked_write(smb345_dev->client,
            0x01,
            BIT(3) | BIT(2),
            BIT(2));
    smb345_dev->charger_jeita_status = HOT;
    if (ori_charger_jeita_status != smb345_dev->charger_jeita_status)
        BAT_DBG(" %s: ori_JEITA: %d, new_JEITA: %d, %d(0.1C), %dmV",
            __func__,
            ori_charger_jeita_status,
            smb345_dev->charger_jeita_status, bat_tempr, bat_volt);
    return POWER_SUPPLY_STATUS_CHARGING;

control_diagram_V:
    ret = smb345_masked_write(smb345_dev->client,
            0x03,
            BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0),
            0x1E);
    ret = smb345_masked_write(smb345_dev->client,
            0x00,
            BIT(7) | BIT(6) | BIT(5),
            BIT(7) | BIT(5));
    ret = smb345_charging_toggle(JEITA, false);
    ret = smb345_masked_write(smb345_dev->client,
            0x01,
            BIT(3) | BIT(2),
            BIT(2));
    smb345_dev->charger_jeita_status = HOT;
    if (ori_charger_jeita_status != smb345_dev->charger_jeita_status)
        BAT_DBG(" %s: ori_JEITA: %d, new_JEITA: %d, %d(0.1C), %dmV",
            __func__,
            ori_charger_jeita_status,
            smb345_dev->charger_jeita_status, bat_tempr, bat_volt);
    return POWER_SUPPLY_STATUS_DISCHARGING;

control_diagram_VI:
    ret = smb345_masked_write(smb345_dev->client,
            0x03,
            BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0),
            0x1E);
    ret = smb345_charging_toggle(JEITA, false);
    smb345_dev->charger_jeita_status = OVERHEAT;
    if (ori_charger_jeita_status != smb345_dev->charger_jeita_status)
        BAT_DBG(" %s: ori_JEITA: %d, new_JEITA: %d, %d(0.1C), %dmV",
            __func__,
            ori_charger_jeita_status,
            smb345_dev->charger_jeita_status, bat_tempr, bat_volt);
    return POWER_SUPPLY_STATUS_DISCHARGING;

others:
    return POWER_SUPPLY_STATUS_CHARGING;
}
#elif defined(CONFIG_A503ML)
int smb3xx_jeita_control(int bat_tempr, int bat_volt)
{
    static charger_jeita_status_t ori_charger_jeita_status = ROOM;
    int ret;

    if (!smb345_dev) {
        pr_err("Warning: smb345_dev is null "
            "due to probe function has error\n");
        return POWER_SUPPLY_STATUS_DISCHARGING;
    }

    ori_charger_jeita_status = smb345_dev->charger_jeita_status;

    /* condition judgement 1: */
    if (ori_charger_jeita_status == FREEZE && bat_tempr > 30) {
        /* control diagram II: */
        goto control_diagram_II;
    }

    /* condition judgement 2: */
    if (ori_charger_jeita_status == COLD && bat_tempr > 130) {
        /* control diagram III: */
        goto control_diagram_III;
    }

    /* condition judgement 3: */
    if (ori_charger_jeita_status == HOT) {
        if (smb345_get_soc_control_float_vol() == 0x2A) {
            if (bat_tempr < 420) {
                /* control diagram III: */
                goto control_diagram_III;
            }
        }
        else if (smb345_get_soc_control_float_vol() == 0x1E) {
            if (bat_tempr < 450) {
                /* control diagram III: */
                goto control_diagram_III;
            }
        }
    }

    /* condition judgement 4: */
    if (ori_charger_jeita_status == OVERHEAT && bat_tempr < 570) {
        /* control diagram IV, V: */
        if (smb345_get_soc_control_float_vol() == 0x2B && bat_volt >= 4100)
            /* control diagram V: */
            goto control_diagram_V;
        else
            /* control diagram IV: */
            goto control_diagram_IV;
    }

    /* condition judgement 5: */
    if (bat_tempr < 0) {
        /* control diagram I: */
        goto control_diagram_I;
    }

    /* condition judgement 6: */
    if (bat_tempr >= 600) {
        /* control diagram VI: */
        goto control_diagram_VI;
    }

    /* condition judgement 7: */
    if ((0 <= bat_tempr && bat_tempr < 100) &&
        (ori_charger_jeita_status == COLD || ori_charger_jeita_status == ROOM)) {
        /* control diagram II: */
        goto control_diagram_II;
    }

    /* condition judgement 8: */
    if ((100 <= bat_tempr && bat_tempr < 450) &&
        ori_charger_jeita_status == ROOM) {
        /* control diagram III: */
        goto control_diagram_III;
    }

    /* condition judgement 9: */
    if ((450 <= bat_tempr && bat_tempr < 600) &&
        (ori_charger_jeita_status == ROOM || ori_charger_jeita_status == HOT)) {
        /* control diagram IV, V: */
        if (smb345_get_soc_control_float_vol() == 0x2A && bat_volt >= 4100)
            /* control diagram V: */
            goto control_diagram_V;
        else
            /* control diagram IV: */
            goto control_diagram_IV;
    }

control_diagram_I:
    ret = smb345_masked_write(smb345_dev->client,
            0x03,
            BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0),
            0x2A);
    ret = smb345_charging_toggle(JEITA, false);
    ret = smb345_masked_write(smb345_dev->client,
            0x00,
            BIT(7) | BIT(6) | BIT(5),
            BIT(5));
    smb345_dev->charger_jeita_status = FREEZE;
    if (ori_charger_jeita_status != smb345_dev->charger_jeita_status)
        BAT_DBG(" %s: ori_JEITA: %d, new_JEITA: %d",
            __func__,
            ori_charger_jeita_status,
            smb345_dev->charger_jeita_status);
    return POWER_SUPPLY_STATUS_DISCHARGING;

control_diagram_II:
    ret = smb345_masked_write(smb345_dev->client,
            0x03,
            BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0),
            0x2A);
    ret = smb345_charging_toggle(JEITA, true);
    ret = smb345_masked_write(smb345_dev->client,
            0x00,
            BIT(7) | BIT(6) | BIT(5),
            0);
    smb345_dev->charger_jeita_status = COLD;
    if (ori_charger_jeita_status != smb345_dev->charger_jeita_status)
        BAT_DBG(" %s: ori_JEITA: %d, new_JEITA: %d",
            __func__,
            ori_charger_jeita_status,
            smb345_dev->charger_jeita_status);
    return POWER_SUPPLY_STATUS_CHARGING;

control_diagram_III:
    ret = smb345_masked_write(smb345_dev->client,
            0x03,
            BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0),
            0x2A);
    ret = smb345_charging_toggle(JEITA, true);
    ret = smb345_masked_write(smb345_dev->client,
            0x00,
            BIT(7) | BIT(6) | BIT(5),
            BIT(5));
    smb345_dev->charger_jeita_status = ROOM;
    if (ori_charger_jeita_status != smb345_dev->charger_jeita_status)
        BAT_DBG(" %s: ori_JEITA: %d, new_JEITA: %d",
            __func__,
            ori_charger_jeita_status,
            smb345_dev->charger_jeita_status);
    return POWER_SUPPLY_STATUS_CHARGING;

control_diagram_IV:
    ret = smb345_masked_write(smb345_dev->client,
            0x03,
            BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0),
            0x1E);
    ret = smb345_charging_toggle(JEITA, true);
    ret = smb345_masked_write(smb345_dev->client,
            0x00,
            BIT(7) | BIT(6) | BIT(5),
            BIT(5));
    smb345_dev->charger_jeita_status = HOT;
    if (ori_charger_jeita_status != smb345_dev->charger_jeita_status)
        BAT_DBG(" %s: ori_JEITA: %d, new_JEITA: %d",
            __func__,
            ori_charger_jeita_status,
            smb345_dev->charger_jeita_status);
    return POWER_SUPPLY_STATUS_CHARGING;

control_diagram_V:
    ret = smb345_masked_write(smb345_dev->client,
            0x03,
            BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0),
            0x2A);
    ret = smb345_charging_toggle(JEITA, false);
    ret = smb345_masked_write(smb345_dev->client,
            0x00,
            BIT(7) | BIT(6) | BIT(5),
            BIT(5));
    smb345_dev->charger_jeita_status = HOT;
    if (ori_charger_jeita_status != smb345_dev->charger_jeita_status)
        BAT_DBG(" %s: ori_JEITA: %d, new_JEITA: %d",
            __func__,
            ori_charger_jeita_status,
            smb345_dev->charger_jeita_status);
    return POWER_SUPPLY_STATUS_DISCHARGING;

control_diagram_VI:
    ret = smb345_masked_write(smb345_dev->client,
            0x03,
            BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0),
            0x2A);
    ret = smb345_charging_toggle(JEITA, false);
    ret = smb345_masked_write(smb345_dev->client,
            0x00,
            BIT(7) | BIT(6) | BIT(5),
            BIT(5));
    smb345_dev->charger_jeita_status = OVERHEAT;
    if (ori_charger_jeita_status != smb345_dev->charger_jeita_status)
        BAT_DBG(" %s: ori_JEITA: %d, new_JEITA: %d",
            __func__,
            ori_charger_jeita_status,
            smb345_dev->charger_jeita_status);
    return POWER_SUPPLY_STATUS_DISCHARGING;

others:
    return POWER_SUPPLY_STATUS_CHARGING;
}
#elif defined(CONFIG_Z580C)
int smb3xx_jeita_control(int bat_tempr, int bat_volt)
{
    static charger_jeita_status_t ori_charger_jeita_status = ROOM;
    int ret;

    if (!smb345_dev) {
        pr_err("Warning: smb345_dev is null "
            "due to probe function has error\n");
        return POWER_SUPPLY_STATUS_DISCHARGING;
    }

    if (smb345_dev->screen_off) {
        smb3xx_charger_control_jeita();
        return POWER_SUPPLY_STATUS_CHARGING;
    }
    else
        smb3xx_soc_control_jeita();

    ori_charger_jeita_status = smb345_dev->charger_jeita_status;
    smb345_recharge_func(bat_volt);

    /* condition judgement 1: */
    if (ori_charger_jeita_status == FREEZE) {
        if (bat_tempr >= 45)
            /* control diagram II: */
            goto control_diagram_II;
        else
            /* control diagram I: */
            goto control_diagram_I;
    }

    /* condition judgement 2: */
    if (ori_charger_jeita_status == COLD) {
        if (bat_tempr >= 130) {
            /* control diagram III: */
            goto control_diagram_III;
        }
        else if (bat_tempr < 15) {
            /* control diagram I: */
            goto control_diagram_I;
        }
        else {
            /* control diagram II: */
            goto control_diagram_II;
        }
    }

    /* condition judgement 3: */
    if (ori_charger_jeita_status == ROOM) {
        if (bat_tempr < 100) {
            /* control diagram II: */
            goto control_diagram_II;
        }
        else if (bat_tempr >= 500) {
            if (bat_volt >= 4110) {
                /* control diagram V: */
                goto control_diagram_V;
            }
            else {
                /* control diagram IV: */
                goto control_diagram_IV;
            }
        }
        else
            /* control diagram III: */
            goto control_diagram_III;
    }

    /* condition judgement 4: */
    if (ori_charger_jeita_status == HOT) {
        // state before is "Charging Disable"
        if (smb345_get_soc_control_float_vol() == 0x2A) {
            if (bat_tempr <= 470) {
                /* control diagram III: */
                goto control_diagram_III;
            }
            else if (bat_tempr >=550) {
                /* control diagram VI: */
                goto control_diagram_VI;
            }
            else {
                if (bat_volt >= 4110) {
                    /* control diagram V: */
                    goto control_diagram_V;
                }
                else {
                    /* control diagram IV: */
                    goto control_diagram_IV;
                }
            }
        }
        else { // state before is "Charging Enable"
            if (bat_tempr < 500) {
                /* control diagram III: */
                goto control_diagram_III;
            }
            else if (bat_tempr >= 550) {
                /* control diagram VI: */
                goto control_diagram_VI;
            }
            else {
                    /* control diagram IV: */
                    goto control_diagram_IV;
            }
        }
    }

    /* condition judgement 5: */
    if (ori_charger_jeita_status == OVERHEAT) {
        if (bat_tempr < 520) {
            /* control diagram IV, V: */
            if (bat_volt >= 4110)
                /* control diagram V: */
                goto control_diagram_V;
            else
                /* control diagram IV: */
                goto control_diagram_IV;
        }
        else {
            /* control diagram VI: */
            goto control_diagram_VI;
        }
    }

    BAT_DBG_E(" %s: *** ERROR ***\n", __func__);
    goto others;

control_diagram_I:
    ret = smb345_masked_write(smb345_dev->client,
            0x03,
            BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0),
            BIT(5) | BIT(3) | BIT(1));
    ret = smb345_masked_write(smb345_dev->client,
            0x00,
            BIT(7) | BIT(6) | BIT(5),
            BIT(7) | BIT(6) | BIT(5));
    ret = smb345_charging_toggle(JEITA, false);
    smb345_dev->charger_jeita_status = FREEZE;
    if (ori_charger_jeita_status != smb345_dev->charger_jeita_status)
        BAT_DBG(" %s: ori_JEITA: %d, new_JEITA: %d, %d(0.1C), %dmV\n",
            __func__,
            ori_charger_jeita_status,
            smb345_dev->charger_jeita_status, bat_tempr, bat_volt);
    return POWER_SUPPLY_STATUS_DISCHARGING;

control_diagram_II:
    ret = smb345_masked_write(smb345_dev->client,
            0x03,
            BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0),
            BIT(5) | BIT(3) | BIT(1));
    ret = smb345_masked_write(smb345_dev->client,
            0x00,
            BIT(7) | BIT(6) | BIT(5),
            BIT(6));
    ret = smb345_charging_toggle(JEITA, true);
    smb345_dev->charger_jeita_status = COLD;
    if (ori_charger_jeita_status != smb345_dev->charger_jeita_status)
        BAT_DBG(" %s: ori_JEITA: %d, new_JEITA: %d, %d(0.1C), %dmV\n",
            __func__,
            ori_charger_jeita_status,
            smb345_dev->charger_jeita_status, bat_tempr, bat_volt);
    return POWER_SUPPLY_STATUS_CHARGING;

control_diagram_III:
    ret = smb345_masked_write(smb345_dev->client,
            0x03,
            BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0),
            BIT(5) | BIT(3) | BIT(1));
    ret = smb345_masked_write(smb345_dev->client,
            0x00,
            BIT(7) | BIT(6) | BIT(5),
            BIT(7) | BIT(6) | BIT(5));
    ret = smb345_charging_toggle(JEITA, true);
    smb345_dev->charger_jeita_status = ROOM;
    if (ori_charger_jeita_status != smb345_dev->charger_jeita_status)
        BAT_DBG(" %s: ori_JEITA: %d, new_JEITA: %d, %d(0.1C), %dmV\n",
            __func__,
            ori_charger_jeita_status,
            smb345_dev->charger_jeita_status, bat_tempr, bat_volt);
    return POWER_SUPPLY_STATUS_CHARGING;

control_diagram_IV:
    ret = smb345_masked_write(smb345_dev->client,
            0x03,
            BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0),
            BIT(4) | BIT(3) | BIT(2) | BIT(1));
    ret = smb345_masked_write(smb345_dev->client,
            0x00,
            BIT(7) | BIT(6) | BIT(5),
            BIT(7) | BIT(6) | BIT(5));
    ret = smb345_charging_toggle(JEITA, true);
    smb345_dev->charger_jeita_status = HOT;
    if (ori_charger_jeita_status != smb345_dev->charger_jeita_status)
        BAT_DBG(" %s: ori_JEITA: %d, new_JEITA: %d, %d(0.1C), %dmV\n",
            __func__,
            ori_charger_jeita_status,
            smb345_dev->charger_jeita_status, bat_tempr, bat_volt);
    return POWER_SUPPLY_STATUS_CHARGING;

control_diagram_V:
    ret = smb345_masked_write(smb345_dev->client,
            0x03,
            BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0),
            BIT(5) | BIT(3) | BIT(1));
    ret = smb345_masked_write(smb345_dev->client,
            0x00,
            BIT(7) | BIT(6) | BIT(5),
            BIT(7) | BIT(6) | BIT(5));
    ret = smb345_charging_toggle(JEITA, false);
    smb345_dev->charger_jeita_status = HOT;
    if (ori_charger_jeita_status != smb345_dev->charger_jeita_status)
        BAT_DBG(" %s: ori_JEITA: %d, new_JEITA: %d, %d(0.1C), %dmV\n",
            __func__,
            ori_charger_jeita_status,
            smb345_dev->charger_jeita_status, bat_tempr, bat_volt);
    return POWER_SUPPLY_STATUS_DISCHARGING;

control_diagram_VI:
    ret = smb345_masked_write(smb345_dev->client,
            0x03,
            BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0),
            BIT(5) | BIT(3) | BIT(1));
    ret = smb345_masked_write(smb345_dev->client,
            0x00,
            BIT(7) | BIT(6) | BIT(5),
            BIT(7) | BIT(6) | BIT(5));
    ret = smb345_charging_toggle(JEITA, false);
    smb345_dev->charger_jeita_status = OVERHEAT;
    if (ori_charger_jeita_status != smb345_dev->charger_jeita_status)
        BAT_DBG(" %s: ori_JEITA: %d, new_JEITA: %d, %d(0.1C), %dmV\n",
            __func__,
            ori_charger_jeita_status,
            smb345_dev->charger_jeita_status, bat_tempr, bat_volt);
    return POWER_SUPPLY_STATUS_DISCHARGING;

others:
    return POWER_SUPPLY_STATUS_CHARGING;
}
#else
int smb3xx_jeita_control(int bat_tempr, int bat_volt)
{
    return POWER_SUPPLY_STATUS_CHARGING;
}
#endif

#ifdef CONFIG_UPI_BATTERY
int smb3xx_jeita_control_for_upi(int usb_state)
{
    int ret;
    int batt_tempr = 250;/* unit: C  */
    int batt_volt = 4000;/* unit: mV */
    int rsoc;

    if (usb_state != AC_IN && usb_state != USB_IN && usb_state != PAD_SUPPLY)
        return 0;

    /* acquire battery temperature here */
    ret = get_battery_temperature(&batt_tempr);
    if (ret) {
        BAT_DBG_E(" %s: fail to get battery temperature\n", __func__);
        return ret;
    }

    /* acquire battery voltage here */
    ret = get_battery_voltage(&batt_volt);
    if (ret) {
        BAT_DBG_E(" %s: fail to get battery voltage\n", __func__);
        return ret;
    }

    /* exec jeita function */
    ret = smb3xx_jeita_control(batt_tempr, batt_volt);

    //request_power_supply_changed();
    return ret;
}
#else
int smb3xx_jeita_control_for_upi(int usb_state) { return 0; }
#endif
/*----------------------------------------------------------------------------*/

/* Convert register value to current using lookup table */
static int hw_to_current(const unsigned int *tbl,
                    size_t size, unsigned int val)
{
    if (val >= size)
        return tbl[size-1];;
    return tbl[val];
}

/* Acquire the value of AICL Results in Status Register E (3Fh)
   return the current value (unit: mA)
*/
static int get_aicl_results(void)
{
    int ret;

    ret = smb345_read(smb345_dev, STAT_E);
    if (ret < 0) {
        BAT_DBG_E(" %s: fail to read STAT_E reg\n", __func__);
        return ret;
    }

    ret &= 0x0F;
    return hw_to_current(aicl_results, ARRAY_SIZE(aicl_results), ret);
}

/* Acquire the value of AICL Results in Status Register E (3Fh) */
static ssize_t get_input_current(struct device *dev,
                    struct device_attribute *attr, char *buf)
{
    int ret;

    if (!smb345_dev) {
        pr_info("%s: ERROR: smb345_dev is null "
            "due to probe function has error\n",
            __func__);
        return sprintf(buf, "%d\n", -EINVAL);
    }

    ret = smb345_read(smb345_dev, STAT_E);
    if (ret<0) {
        pr_info("%s: ERROR: i2c read error\n", __func__);
        return sprintf(buf, "%d\n", -EIO);
    }

    ret &= 0x0F;
    return sprintf(buf, "%d\n",
            hw_to_current(aicl_results, ARRAY_SIZE(aicl_results), ret));
}

/* Acquire the charging status */
static ssize_t get_charge_status(struct device *dev,
                    struct device_attribute *attr, char *buf)
{
    int ret;

    ret = smb345_get_charging_status();
    if (ret == POWER_SUPPLY_STATUS_CHARGING || ret == POWER_SUPPLY_STATUS_FULL)
        ret = 1;
    else
        ret = 0;
    return sprintf(buf, "%d\n", ret);
}

static char gbuffer[64];
/* Generate UUID by invoking kernel library */
static void generate_key(void)
{
    char sysctl_bootid[16];

    generate_random_uuid(sysctl_bootid);
    sprintf(gbuffer, "%pU", sysctl_bootid);
}

/* Acquire the UUID */
static ssize_t get_charge_keys(struct device *dev,
                    struct device_attribute *attr, char *buf)
{
    generate_key();
    return sprintf(buf, "%s\n", gbuffer);
}

static DEVICE_ATTR(charge_keys, S_IRUGO, get_charge_keys, NULL);
static DEVICE_ATTR(input_current, S_IRUGO, get_input_current, NULL);
static DEVICE_ATTR(charge_status, S_IRUGO, get_charge_status, NULL);
static struct attribute *dev_attrs[] = {
    &dev_attr_input_current.attr,
    &dev_attr_charge_status.attr,
    &dev_attr_charge_keys.attr,
    NULL,
};
static struct attribute_group dev_attr_grp = {
    .attrs = dev_attrs,
};

#define SMB_DUMP(...) \
do { \
        local_len = sprintf(page, __VA_ARGS__); \
        len += local_len; \
        page += local_len; \
}while(0);

/*----------------------------------------------------------------------------*/

#if defined(CONFIG_ME372CG) || defined(CONFIG_ME372CL) || defined(CONFIG_A450CG)
static int config_otg_regs(int toggle)
{
    int ret;

    if (toggle) {
        /* I2C disable OTG function */
        ret = smb345_masked_write(smb345_dev->client, CMD_A_REG,
            BIT(4), 0);
        if (ret) {
            pr_err("fail to set OTG Disable bit ret=%d\n", ret);
            return ret;
        }

        /* Refer to SMB345 Application Note 72 to solve serious problems */
        ret = smb345_masked_write(smb345_dev->client,
                    OTG_TLIM_THERM_CNTRL_REG,
                    OTG_CURRENT_LIMIT_AT_USBIN_MASK,
                    OTG_CURRENT_LIMIT_250mA);
        if (ret) {
            pr_err("fail to set OTG current limit 250mA. "
                "ret=%d\n", ret);
            return ret;
        }

        /* I2C enable OTG function */
        ret = smb345_masked_write(smb345_dev->client, CMD_A_REG,
            BIT(4), BIT(4));
        if (ret) {
            pr_err("fail to set OTG Enable bit ret=%d\n", ret);
            return ret;
        }
        /* Refer to SMB345 Application Note 72 to solve serious problems */
        ret = smb345_masked_write(smb345_dev->client,
                    OTG_TLIM_THERM_CNTRL_REG,
                    OTG_CURRENT_LIMIT_AT_USBIN_MASK,
                    OTG_CURRENT_LIMIT_750mA);
        if (ret) {
            pr_err("fail to set OTG current limit 750mA. "
                "ret=%d\n", ret);
            return ret;
        }

        /* OTG 5V function set to PIN control due to HW will
            reset IC to default when external USB device is
            removed. This function support in SR2. And
            charger IC will be replaced with smb345
        */
        ret = smb345_masked_write(smb345_dev->client,
                    OTHER_CTRL_A_REG,
                    OTGID_PIN_CONTROL_MASK,
                    OTGID_PIN_CONTROL_BITS);
        if (ret) {
            pr_err("fail to set OTGID_PIN_CONTROL_BITS rc=%d\n", ret);
            return ret;
        }
    }
    else {

    }

    return ret;
}
#elif defined(CONFIG_PF400CG)
static int config_otg_regs(int toggle)
{
    int ret;

    if (toggle) {
        /* I2C disable OTG function */
        ret = smb345_masked_write(smb345_dev->client, CMD_A_REG,
            BIT(4), 0);
        if (ret) {
            pr_err("fail to set OTG Disable bit ret=%d\n", ret);
            return ret;
        }

        /* Set OTG current limit to 500mA: 0Ah[3:2]="10" */
        ret = smb345_masked_write(smb345_dev->client,
                    OTG_TLIM_THERM_CNTRL_REG,
                    OTG_CURRENT_LIMIT_AT_USBIN_MASK,
                    OTG_CURRENT_LIMIT_500mA);
        if (ret) {
            pr_err("fail to set OTG current limit 500mA ret=%d\n", ret);
            return ret;
        }

        /* Toggle to enable OTG function: output High */
        gpio_set_value(smb345_dev->pdata->gp_sdio_2_clk, 1);
    }
    else {
        /* Set OTG current limit to 250mA: 0Ah[3:2]="01" */
        ret = smb345_masked_write(smb345_dev->client,
                OTG_TLIM_THERM_CNTRL_REG,
                OTG_CURRENT_LIMIT_AT_USBIN_MASK,
                OTG_CURRENT_LIMIT_250mA);
        if (ret) {
            pr_err("fail to set OTG current limit 250mA ret=%d\n", ret);
            return ret;
        }

        /* Toggle to disable OTG function: output Low */
        gpio_set_value(smb345_dev->pdata->gp_sdio_2_clk, 0);
    }

    return ret;
}
#elif defined(CONFIG_ME175CG) || defined(CONFIG_A400CG)
static int config_otg_regs(int toggle)
{
    int ret;

    if (toggle) {
        /* I2C disable OTG function */
        ret = smb345_masked_write(smb345_dev->client, CMD_A_REG,
            BIT(4), 0);
        if (ret) {
            pr_err("fail to set OTG Disable bit ret=%d\n", ret);
            return ret;
        }

        /* Set OTG current limit to 250mA: 0Ah[3:2]="00" */
        ret = smb345_masked_write(smb345_dev->client,
                OTG_TLIM_THERM_CNTRL_REG,
                OTG_CURRENT_LIMIT_AT_USBIN_MASK,
                0);
        if (ret) {
            pr_err("fail to set OTG current limit 250mA ret=%d\n", ret);
            return ret;
        }

        /* Set OTG battery UVLO threshold to 2.7V: 0Ah[1:0]="00" */
        ret = smb345_masked_write(smb345_dev->client,
                OTG_TLIM_THERM_CNTRL_REG,
                OTG_BATTERY_UVLO_THRESHOLD_MASK,
                0);
        if (ret) {
            pr_err("fail to set OTG battery UVLO threshold to 2.7V ret=%d\n",
                ret);
            return ret;
        }

        /* Toggle to enable OTG function: output High */
        gpio_set_value(smb345_dev->pdata->gp_sdio_2_clk, 1);

        /* Set OTG current limit to 750mA: 0Ah[3:0]="1000" */
        ret = smb345_masked_write(smb345_dev->client,
                    OTG_TLIM_THERM_CNTRL_REG,
                    OTG_CURRENT_LIMIT_AT_USBIN_MASK,
                    BIT(3));
        if (ret) {
            pr_err("fail to set OTG current limit 750mA ret=%d\n", ret);
            return ret;
        }
    }
    else {
        #if 0
        /* Reset Charger IC: 31h[7]="1" */
        ret = smb345_masked_write(smb345_dev->client,
                    CMD_B,
                    BIT(7),
                    BIT(7));
        if (ret) {
            pr_err("fail to reset Charger IC ret=%d\n", ret);
            return ret;
        }
        #endif

        /* Set OTG current limit to 250mA: 0Ah[3:2]="00" */
        ret = smb345_masked_write(smb345_dev->client,
                OTG_TLIM_THERM_CNTRL_REG,
                OTG_CURRENT_LIMIT_AT_USBIN_MASK,
                0);
        if (ret) {
            pr_err("fail to set OTG current limit 250mA ret=%d\n", ret);
            return ret;
        }

        /* Toggle to disable OTG function: output Low */
        gpio_set_value(smb345_dev->pdata->gp_sdio_2_clk, 0);
    }

    return ret;
}
#elif defined(CONFIG_ME302C)
static int config_otg_regs(int toggle)
{
    int ret;

    if (toggle) {
        ret = smb345_masked_write(smb345_dev->client, CMD_A_REG,
            BIT(4), BIT(4));
        if (ret) {
            pr_err("Failed to set OTG Enable bit ret=%d\n", ret);
            return ret;
        }

        // Refer to SMB347 Application Note 72 to solve serious problems
        ret = smb345_masked_write(smb345_dev->client, OTG_TLIM_THERM_CNTRL_REG,
            OTG_CURRENT_LIMIT_AT_USBIN_MASK, OTG_CURRENT_LIMIT_750mA);
        if (ret) {
            pr_err("Failed to set OTG current limit 750mA. ret=%d\n", ret);
            return ret;
        }

        /* OTG 5V function set to PIN control due to HW will reset IC to default
        when external USB device is removed. This function support in SR2. And
        charger IC will be replaced with smb345 */
        ret = smb345_masked_write(smb345_dev->client, OTHER_CTRL_A_REG,
                OTGID_PIN_CONTROL_MASK, OTGID_PIN_CONTROL_BITS);
        if (ret) {
            pr_err("Failed to set OTGID_PIN_CONTROL_BITS rc=%d\n", ret);
            return ret;
        }
    } else {

    }

    return ret;
}
#elif defined(CONFIG_A503ML)
static int config_otg_regs(int toggle)
{
    int ret;

    if (toggle) {
        /* I2C disable OTG function */
        ret = smb345_masked_write(smb345_dev->client, CMD_A_REG,
            BIT(4), 0);
        if (ret) {
            pr_err("fail to set OTG Disable bit ret=%d\n", ret);
            return ret;
        }

        /* Refer to SMB345 Application Note 72 to solve serious problems */
        /* Set OTG current limit = 250mA 0A[3:2]="01" */
        ret = smb345_masked_write(smb345_dev->client,
                            OTG_TLIM_THERM_CNTRL_REG,
                            OTG_CURRENT_LIMIT_AT_USBIN_MASK,
                            OTG_CURRENT_LIMIT_250mA);

        /* OTG 5V function set to PIN control due to HW will
        reset IC to default when OTG device is removed.
        Set OTG to Pin control 09h[7:6]="01" */
        ret = smb345_masked_write(smb345_dev->client,
                OTHER_CTRL_A_REG,
                OTGID_PIN_CONTROL_MASK,
                OTGID_PIN_CONTROL_BITS);
        if (ret) {
            pr_err("Failed to set OTGID_PIN_CONTROL_BITS rc=%d\n", ret);
            return ret;
        }

        /* Toggle to enable OTG function: output High */
        gpio_set_value(smb345_dev->pdata->chg_otg, 1);

        // Refer to SMB347 Application Note 72 to solve serious problems
        /* Set OTG current limit = 500mA 0A[3:2]="10" */
        ret = smb345_masked_write(smb345_dev->client,
            OTG_TLIM_THERM_CNTRL_REG,
            OTG_CURRENT_LIMIT_AT_USBIN_MASK,
            OTG_CURRENT_LIMIT_500mA);
        if (ret) {
            pr_err("Failed to set OTG current limit 500mA. ret=%d\n", ret);
            return ret;
        }

    } else {
        /* Refer to SMB345 Application Note 72 to solve serious problems */
        /* Set OTG current limit = 250mA 0A[3:2]="01" */
        ret = smb345_masked_write(smb345_dev->client,
                            OTG_TLIM_THERM_CNTRL_REG,
                            OTG_CURRENT_LIMIT_AT_USBIN_MASK,
                            OTG_CURRENT_LIMIT_250mA);

        /* Toggle to disable OTG function: output High */
        gpio_set_value(smb345_dev->pdata->chg_otg, 0);

        /* IC reset to default 31h[7]="1" */
        ret = smb345_masked_write(smb345_dev->client,
            0x31,
            BIT(7),
            BIT(7));
        if (ret) {
            pr_err("fail to reset IC to default ret=%d\n", ret);
            return ret;
        }
    }

    return ret;
}
#elif defined(CONFIG_FE380CG)
static int config_otg_regs(int toggle)
{
    int ret;

    if (toggle) {
        /* I2C disable OTG function 09h[7:6]="00" */
        ret = smb345_masked_write(smb345_dev->client,
            0x09,
            BIT(7) | BIT(6),
            0);
        if (ret) {
            pr_err("fail to set OTG Disable bit 09h ret=%d\n", ret);
            return ret;
        }
        /* I2C disable OTG function 30h[4]="0" */
        ret = smb345_masked_write(smb345_dev->client,
            0x30,
            BIT(4),
            0);
        if (ret) {
            pr_err("fail to set OTG Disable bit ret=%d\n", ret);
            return ret;
        }

        /* Switching Frequency change to 1.5Mhz 07h[1]="1" */
        ret = smb345_masked_write(smb345_dev->client,
            0x07,
            BIT(7),
            BIT(7));
        if (ret) {
            pr_err("fail to set Switching Frequency ret=%d\n", ret);
            return ret;
        }

        /* Set OTG current limit to 250mA 0Ah[3:2]="00" */
        ret = smb345_masked_write(smb345_dev->client,
            0x0A,
            BIT(3) | BIT(2),
            0);
        if (ret) {
            pr_err("fail to set OTG current limit 250mA ret=%d\n", ret);
            return ret;
        }

        /* I2C enable OTG function 09h[7:6]="01" */
        ret = smb345_masked_write(smb345_dev->client,
            0x09,
            BIT(7) | BIT(6),
            BIT(6));
        if (ret) {
            pr_err("fail to set OTG enable bit 09h ret=%d\n", ret);
            return ret;
        }

        /* Set OTG current limit to 750mA 0Ah[3:2]="10" */
        ret = smb345_masked_write(smb345_dev->client,
            0x0A,
            BIT(3) | BIT(2),
            BIT(3));
        if (ret) {
            pr_err("fail to set OTG current limit 750mA ret=%d\n", ret);
            return ret;
        }
    }
    else {
        /* IC reset to default 31h[7]="1" */
        ret = smb345_masked_write(smb345_dev->client,
            0x31,
            BIT(7),
            BIT(7));
        if (ret) {
            pr_err("fail to reset IC to default ret=%d\n", ret);
            return ret;
        }
    }

    return ret;
}
#elif defined(CONFIG_FE171MG)
static int config_otg_regs(int toggle)
{
    int ret;

    if (toggle) {
        /* I2C disable OTG function 09h[7:6]="00" */
        ret = smb345_masked_write(smb345_dev->client,
            0x09,
            BIT(7) | BIT(6),
            0);
        if (ret) {
            pr_err("fail to set OTG Disable bit 09h ret=%d\n", ret);
            return ret;
        }

        /* I2C disable OTG function 30h[4]="0" */
        ret = smb345_masked_write(smb345_dev->client,
            0x30,
            BIT(4),
            0);
        if (ret) {
            pr_err("fail to set OTG Disable bit ret=%d\n", ret);
            return ret;
        }

        /* Switching Frequency change to 1.5Mhz 07h[1]="1" */
        ret = smb345_masked_write(smb345_dev->client,
            0x07,
            BIT(7),
            BIT(7));
        if (ret) {
            pr_err("fail to set Switching Frequency ret=%d\n", ret);
            return ret;
        }

        /* Set OTG current limit to 250mA 0Ah[3:2]="00" */
        ret = smb345_masked_write(smb345_dev->client,
            0x0A,
            BIT(3) | BIT(2),
            0);
        if (ret) {
            pr_err("fail to set OTG current limit 250mA ret=%d\n", ret);
            return ret;
        }

        /* I2C enable OTG function 09h[7:6]="01" */
        ret = smb345_masked_write(smb345_dev->client,
            0x09,
            BIT(7) | BIT(6),
            BIT(6));
        if (ret) {
            pr_err("fail to set OTG enable bit 09h ret=%d\n", ret);
            return ret;
        }

        if (HW_ID == HW_ID_SR1) {
            /* Toggle to disable OTG function: output High */
            gpio_set_value(smb345_dev->pdata->chg_otg, 1);
        }

        /* Set OTG current limit to 500mA 0Ah[3:2]="01" */
        ret = smb345_masked_write(smb345_dev->client,
            0x0A,
            BIT(3) | BIT(2),
            BIT(2));
        if (ret) {
            pr_err("fail to set OTG current limit 500mA ret=%d\n", ret);
            return ret;
        }
    }
    else {
        /* IC reset to default 31h[7]="1" */
        ret = smb345_masked_write(smb345_dev->client,
            0x31,
            BIT(7),
            BIT(7));
        if (ret) {
            pr_err("fail to reset IC to default ret=%d\n", ret);
            return ret;
        }

        /* Set OTG current limit to 250mA 0Ah[3:2]="00" */
        ret = smb345_masked_write(smb345_dev->client,
            0x0A,
            BIT(3) | BIT(2),
            0);
        if (ret) {
            pr_err("fail to set OTG current limit 250mA ret=%d\n", ret);
            return ret;
        }
    }

    return ret;
}
#elif defined(CONFIG_Z580C)
static int config_otg_regs(int toggle)
{
    int ret;

    if (HW_ID <= HW_ID_ER) {
        if (toggle) {
            /* I2C disable OTG function 30h[4]="0" */
            ret = smb345_masked_write(smb345_dev->client,
                0x30,
                BIT(4),
                0);
            if (ret) {
                pr_err("fail to set OTG Disable bit ret=%d\n", ret);
                return ret;
            }

            /* Set OTG current limit to 250mA 0Ah[3:2]="01" */
            ret = smb345_masked_write(smb345_dev->client,
                0x0A,
                BIT(3) | BIT(2),
                BIT(2));
            if (ret) {
                pr_err("fail to set OTG current limit 250mA ret=%d\n", ret);
                return ret;
            }

            /* I2C enable OTG function 30h[4]="1" */
            ret = smb345_masked_write(smb345_dev->client,
                0x30,
                BIT(4),
                BIT(4));
            if (ret) {
                pr_err("fail to set OTG Disable bit ret=%d\n", ret);
                return ret;
            }

            /* Set OTG current limit to 500mA 0Ah[3:2]="10" */
            ret = smb345_masked_write(smb345_dev->client,
                0x0A,
                BIT(3) | BIT(2),
                BIT(3));
            if (ret) {
                pr_err("fail to set OTG current limit 500mA ret=%d\n", ret);
                return ret;
            }

            /* set OTG to Pin Control 09h[7:6]="01" */
            ret = smb345_masked_write(smb345_dev->client,
                0x09,
                BIT(7) | BIT(6),
                BIT(6));
            if (ret) {
                pr_err("fail to set OTG enable bit 09h ret=%d\n", ret);
                return ret;
            }
        }
        else {
            /* do nothing due to HW auto-cut-off with pin control configuration */
        }
    }
    else {
        /* HW_ID > HW_ID_ER */
        if (HW_ID == HW_ID_PR) { /* smb347 */
            if (toggle) {
                /* Set OTG current limit to 250mA 0Ah[3:2]="01" */
                ret = smb345_masked_write(smb345_dev->client,
                    0x0A,
                    BIT(3) | BIT(2),
                    BIT(2));
                if (ret) {
                    pr_err("fail to set OTG current limit 250mA ret=%d\n", ret);
                    return ret;
                }

                /* set OTG to Pin Control 09h[7:6]="01" */
                ret = smb345_masked_write(smb345_dev->client,
                    0x09,
                    BIT(7) | BIT(6),
                    BIT(6));
                if (ret) {
                    pr_err("fail to set OTG enable bit 09h ret=%d\n", ret);
                    return ret;
                }

                /* Toggle to enable OTG function: output High */
                gpio_set_value(smb345_dev->pdata->chg_otg, 1);

                /* Set OTG current limit to 500mA 0Ah[3:2]="10" */
                ret = smb345_masked_write(smb345_dev->client,
                    0x0A,
                    BIT(3) | BIT(2),
                    BIT(3));
                if (ret) {
                    pr_err("fail to set OTG current limit 500mA ret=%d\n", ret);
                    return ret;
                }
            }
            else {
                /* Set OTG current limit to 250mA 0Ah[3:2]="01" */
                ret = smb345_masked_write(smb345_dev->client,
                    0x0A,
                    BIT(3) | BIT(2),
                    BIT(2));
                if (ret) {
                    pr_err("fail to set OTG current limit 250mA ret=%d\n", ret);
                    return ret;
                }

                /* Toggle to disable OTG function: output Low */
                gpio_set_value(smb345_dev->pdata->chg_otg, 0);
            }
        }
        else { /* smb358: HW_ID > HW_ID_PR */
            if (toggle) {
                /* Set OTG current limit to 250mA 0Ah[3:2]="00" */
                ret = smb345_masked_write(smb345_dev->client,
                    0x0A,
                    BIT(3) | BIT(2),
                    0);
                if (ret) {
                    pr_err("fail to set OTG current limit 250mA ret=%d\n", ret);
                    return ret;
                }

                if (Read_PROJ_ID() == PROJ_ID_Z580C) {
                    /* Toggle to enable OTG function: output High */
                    gpio_set_value(smb345_dev->pdata->chg_otg, 1);

                    /* Set OTG current limit to 500mA 0Ah[3:2]="01" */
                    ret = smb345_masked_write(smb345_dev->client,
                        0x0A,
                        BIT(3) | BIT(2),
                        BIT(2));
                }
                else { /* project id is PROJ_ID_Z580CA */
                    /* Toggle to enable OTG function: output Low */
                    gpio_set_value(152, 0);
                    set_gpio_value_usb_ulpi_0_refclk(0);
                    /* ++ Just for image verification, remove me in next release. */
                    gpio_set_value(87, 0);
                    /* -- */

                    /* Set OTG current limit to 900mA 0Ah[3:2]="11" */
                    ret = smb345_masked_write(smb345_dev->client,
                        0x0A,
                        BIT(3) | BIT(2),
                        BIT(3) | BIT(2));
                }
                if (ret) {
                    pr_err("fail to set OTG current limit 500mA ret=%d\n", ret);
                    return ret;
                }
            }
            else {
                /* Set OTG current limit to 250mA 0Ah[3:2]="00" */
                ret = smb345_masked_write(smb345_dev->client,
                    0x0A,
                    BIT(3) | BIT(2),
                    0);
                if (ret) {
                    pr_err("fail to set OTG current limit 250mA ret=%d\n", ret);
                    return ret;
                }

                if (Read_PROJ_ID() == PROJ_ID_Z580C) {
                /* Toggle to disable OTG function: output Low */
                gpio_set_value(smb345_dev->pdata->chg_otg, 0);
                }
                else { /* project id is PROJ_ID_Z580CA */
                /* Toggle to disable OTG function: output High */
                gpio_set_value(152, 1);
                set_gpio_value_usb_ulpi_0_refclk(1);
                /* ++ Just for image verification, remove me in next release. */
                gpio_set_value(87, 1);
                /* -- */
                }
            }
        }
    }

    return ret;
}
#else
static int config_otg_regs(int toggle) { return 0; }
#endif
/*----------------------------------------------------------------------------*/

static int otg(int toggle)
{
    int ret;

    if (!smb345_dev) {
        pr_info("Warning: smb345_dev is null "
            "due to probe function has error\n");
        return 1;
    }

    #if defined(CONFIG_FE171MG)
    if (!toggle) {
        if (HW_ID == HW_ID_SR1) {
            /* Toggle to disable OTG function: output Low */
            gpio_set_value(smb345_dev->pdata->chg_otg, 0);
        }
    }
    #endif

    ret = smb345_set_writable(smb345_dev, true);
    if (ret < 0)
        return ret;


    ret = config_otg_regs(toggle);
    if (ret < 0)
        return ret;

    smb345_dev->otg_enabled = (toggle > 0 ? true : false);
    return 0;
}

/* enable/disable AICL function */
static int smb345_OptiCharge_Toggle(bool on)
{
    int ret;

    if (!smb345_dev) {
        pr_info("%s: smb345_dev is null "
            "due to driver probed isn't ready\n",
            __func__);
        return -1;
    }

    ret = smb345_read(smb345_dev, CFG_VARIOUS_FUNCS);
    if (ret < 0)
        goto fail;

    if (on)
        ret |= CFG_VARIOUS_FUNCS_OPTICHARGE_TOGGLE;
    else
        ret &= ~CFG_VARIOUS_FUNCS_OPTICHARGE_TOGGLE;

    ret = smb345_write(smb345_dev, CFG_VARIOUS_FUNCS, ret);
    if (ret < 0)
        goto fail;

fail:
    return ret;
}

/* print the value in Various Functions Register */
static int smb345_get_AICL(void)
{
    int ret;

    if (!smb345_dev) {
        pr_info("%s: smb345_dev is null "
            "due to driver probed isn't ready\n",
            __func__);
        return -1;
    }

    ret = smb345_read(smb345_dev, CFG_VARIOUS_FUNCS);
    if (ret < 0)
        goto fail;
    else
        pr_info("Reg%02Xh = " BYTETOBINARYPATTERN
            "\n", CFG_VARIOUS_FUNCS, BYTETOBINARY(ret));
fail:
    return ret;
}

/* print the value in CMD_B */
static int smb345_get_USB9_HC_Toggle(void)
{
    int ret;

    if (!smb345_dev) {
        pr_info("%s: smb345_dev is null "
            "due to driver probed isn't ready\n",
            __func__);
        return -1;
    }

    ret = smb345_read(smb345_dev, CMD_B);
    if (ret < 0)
        goto fail;
    else
        pr_info("Reg%02Xh = " BYTETOBINARYPATTERN
            "\n", CMD_B, BYTETOBINARY(ret));
fail:
    return ret;
}

/* enable USB5 or USB9 and HC mode function */
#if !defined(CONFIG_PF400CG)
static int smb345_USB9_HC_Toggle(bool on)
{
    int ret;

    if (!smb345_dev) {
        pr_info("%s: smb345_dev is null "
            "due to driver probed isn't ready\n",
            __func__);
        return -1;
    }

    ret = smb345_read(smb345_dev, CMD_B);
    if (ret < 0)
        goto fail;

    if (on)
        ret |= CMD_B_USB9_AND_HC_MODE;
    else
        ret &= ~CMD_B_USB9_AND_HC_MODE;
    ret = smb345_write(smb345_dev, CMD_B, ret);

fail:
    return ret;
}
#else
static int smb345_USB9_HC_Toggle(bool on) { return 0; }
#endif

/* print the value in CFG_PIN */
static int smb345_get_USB9_HC_PIN_Control(void)
{
    int ret;

    if (!smb345_dev) {
        pr_info("%s: smb345_dev is null "
            "due to driver probed isn't ready\n",
            __func__);
        return -1;
    }

    ret = smb345_read(smb345_dev, CFG_PIN);
    if (ret < 0)
        goto fail;
    else
        pr_info("Reg%02Xh = " BYTETOBINARYPATTERN
            "\n", CFG_PIN, BYTETOBINARY(ret));
fail:
    return ret;
}

/* enable USB5 or USB9 and HC mode pin control function */
#if !defined(CONFIG_PF400CG) && !defined(CONFIG_ME175CG) && !defined(CONFIG_A400CG)
static int smb345_USB9_HC_PIN_Control(bool on)
{
    int ret;
    u8 b = BIT(4);

    if (!smb345_dev) {
        pr_info("%s: smb345_dev is null "
            "due to driver probed isn't ready\n",
            __func__);
        return -1;
    }

    ret = smb345_read(smb345_dev, CFG_PIN);
    if (ret < 0)
        goto fail;

    if (on)
        ret |= b;
    else
        ret &= ~b;
    ret = smb345_write(smb345_dev, CFG_PIN, ret);

fail:
    return ret;
}
#else
static int smb345_USB9_HC_PIN_Control(bool on) { return 0; }
#endif

/* Convert current to register value using lookup table */
static int current_to_hw(const unsigned int *tbl,
                    size_t size, unsigned int val)
{
    size_t i;

    for (i = 0; i < size; i++)
        if (val < tbl[i])
            break;
    return i > 0 ? i - 1 : -EINVAL;
}

/* print the value in CFG_CURRENT_LIMIT */
static int smb345_get_current_limits()
{
    int ret, index=-1;

    if (!smb345_dev) {
        pr_info("%s: smb345_dev is null "
            "due to driver probed isn't ready\n",
            __func__);
        return -1;
    }

    ret = smb345_read(smb345_dev, CFG_CURRENT_LIMIT);
    if (ret < 0)
        return ret;
    else
        pr_info("Reg%02Xh = " BYTETOBINARYPATTERN
            "\n", CFG_CURRENT_LIMIT, BYTETOBINARY(ret));
    return ret;
}

/* config DC_IN current limits according to BAT RSOC */
static int smb346_set_pad_supply_dc_in_limits(int rsoc)
{
    int ret;
    
    if(rsoc >= 20)
    	g_disable_700 = true;
    else if(rsoc <= 15)
    	g_disable_700 = false;

    //pr_info("%s: \n", __func__);
    if(entry_mode == 4)
    {
    	pr_info("%s: always set CFG_CURRENT_LIMIT_SMB346_VALUE_700 in COS \n", __func__);
    	
    	/* Disable AICL - Write 02h[4]="0" */
    	if (smb345_OptiCharge_Toggle(false) < 0) {
        	dev_err(&smb345_dev->client->dev,
        	"%s: fail to disable AICL\n", __func__);
        	return;
    	}
    	
        ret = smb345_masked_write(smb345_dev->client,
            CFG_CURRENT_LIMIT,
            CFG_CURRENT_LIMIT_SMB346_MASK,
            CFG_CURRENT_LIMIT_SMB346_VALUE_700);
            
        /* Enable AICL - Write 02h[4]="1" */
    	if (smb345_OptiCharge_Toggle(true) < 0) {
        	dev_err(&smb345_dev->client->dev,
        	"%s: fail to enable AICL\n", __func__);
        	return;
    	} 
	}
    else if ((rsoc > 15) && (g_disable_700))
    {
    	pr_info("%s: CFG_CURRENT_LIMIT_SMB346_VALUE_500(%d)\n", __func__, g_disable_700);
    	
   		/* Disable AICL - Write 02h[4]="0" */
    	if (smb345_OptiCharge_Toggle(false) < 0) {
        	dev_err(&smb345_dev->client->dev,
        	"%s: fail to disable AICL\n", __func__);
        	return;
    	}
        
        ret = smb345_masked_write(smb345_dev->client,
            CFG_CURRENT_LIMIT,
            CFG_CURRENT_LIMIT_SMB346_MASK,
            CFG_CURRENT_LIMIT_SMB346_VALUE_500);   

       	/* Enable AICL - Write 02h[4]="1" */
    	if (smb345_OptiCharge_Toggle(true) < 0) {
        	dev_err(&smb345_dev->client->dev,
        	"%s: fail to enable AICL\n", __func__);
        	return;
    	} 
	}   	
    else if (rsoc > -1)
    {
    	pr_info("%s: CFG_CURRENT_LIMIT_SMB346_VALUE_700(%d)\n", __func__, g_disable_700);
    	
    	/* Disable AICL - Write 02h[4]="0" */
    	if (smb345_OptiCharge_Toggle(false) < 0) {
        	dev_err(&smb345_dev->client->dev,
        	"%s: fail to disable AICL\n", __func__);
        	return;
    	}
    	
        ret = smb345_masked_write(smb345_dev->client,
            CFG_CURRENT_LIMIT,
            CFG_CURRENT_LIMIT_SMB346_MASK,
            CFG_CURRENT_LIMIT_SMB346_VALUE_700);
            
        /* Enable AICL - Write 02h[4]="1" */
    	if (smb345_OptiCharge_Toggle(true) < 0) {
        	dev_err(&smb345_dev->client->dev,
        	"%s: fail to enable AICL\n", __func__);
        	return;
    	} 
    }
    else
    {
    	pr_info("%s: smb346_set_pad_supply_dc_in_limits FAIL, rsoc = %d, g_disable_700 = %d !!!\n", __func__, rsoc, g_disable_700);
        ret = -1;
    }

    if (ret < 0)
        return ret;

    return ret;
}

#if !defined(CONFIG_ME175CG) && !defined(CONFIG_A400CG)
static int smb345_set_current_limits(int usb_state, bool is_twinsheaded)
{
    int ret, index=-1;

    ret = smb345_set_writable(smb345_dev, true);
    if (ret < 0)
        return ret;

    ret = smb345_read(smb345_dev, CFG_CURRENT_LIMIT);
    if (ret < 0)
        return ret;

    if (usb_state == AC_IN) {
        index = current_to_hw(icl_tbl, ARRAY_SIZE(icl_tbl),
                    smb345_dev->pdata->mains_current_limit);
    }
    else if (usb_state == USB_IN) {
        if (is_twinsheaded)
            index = current_to_hw(icl_tbl, ARRAY_SIZE(icl_tbl),
                    smb345_dev->pdata->twins_h_current_limit);
        else
            index = current_to_hw(icl_tbl, ARRAY_SIZE(icl_tbl),
                    smb345_dev->pdata->usb_hc_current_limit);
    }
    if (index < 0)
        return index;

    return smb345_write(smb345_dev, CFG_CURRENT_LIMIT, index);
}
#else
static int smb345_set_current_limits(int usb_state, bool is_twinsheaded)
{
    int ret;

    ret = smb345_set_writable(smb345_dev, true);
    if (ret < 0)
        return ret;

    if (usb_state == AC_IN) {
        return smb345_masked_write(smb345_dev->client,
            CFG_CURRENT_LIMIT,
            CFG_CURRENT_LIMIT_SMB346_MASK,
            CFG_CURRENT_LIMIT_SMB346_VALUE_1200);
    }
    else if (usb_state == USB_IN) {
    }

    return ret;
}
#endif

#if defined(CONFIG_A450CG) || defined(CONFIG_FE380CG) || defined(CONFIG_FE170CSG) || defined(CONFIG_FE171MG)
static void aicl_cur_control(int usb_state)
{
    int aicl_result;

    if (usb_state != AC_IN)
        return;

    aicl_result = get_aicl_results();
    if (aicl_result <= 500) {
        dev_err(&smb345_dev->client->dev,
            "%s: * re-config AC_IN when aicl result(%dmA) <= 500mA.\n",
            __func__,
            aicl_result);
        smb345_config_max_current(AC_IN);
    }
    else {
        dev_info(&smb345_dev->client->dev,
            "%s: execute AICL routine control work\n",
            __func__);
    }
}
#elif defined(CONFIG_Z580C)
static void aicl_cur_control(int usb_state)
{
    int aicl_result;
    static bool first_enter = true;

    /* ignore non-AC_IN */
    if (usb_state != AC_IN) {
        /* reset to default if non-AC_IN */
        first_enter = true;
        return;
    }
    else {
        /* AC_IN */
        if (first_enter) {
            /* don't AICL when first enter */
            first_enter = false;
            return;
        }
    }

    aicl_result = get_aicl_results();
    if (aicl_result <= 500) {
        dev_err(&smb345_dev->client->dev,
            "%s: * re-config AC_IN when aicl result(%dmA) <= 500mA.\n",
            __func__,
            aicl_result);
        smb345_config_max_current(AC_IN);
    }
    else {
        dev_info(&smb345_dev->client->dev,
            "%s: execute AICL routine control work\n",
            __func__);
    }
}
#else
static void aicl_cur_control(int usb_state) { return; }
#endif

void aicl_dete_worker(struct work_struct *dat)
{
    int usb_state;
    int rsoc;

    if (!smb345_dev) {
        pr_err("%s: smb345_dev is null "
            "due to driver probed isn't ready\n",
            __func__);
        return;
    }

    mutex_lock(&g_usb_state_lock);
    usb_state = g_usb_state;
    mutex_unlock(&g_usb_state_lock);

    aicl_cur_control(usb_state);
    smb346_soc_detect_batt_tempr(usb_state);
    smb3xx_jeita_control_for_upi(usb_state);

    /* acquire battery rsoc here */
    if (get_battery_rsoc(&rsoc)) {
        dev_err(&smb345_dev->client->dev,
        " %s: fail to get battery rsoc\n", __func__);
    }
    else {
        if (rsoc==5 || rsoc==1)
            smb345_dump_registers(NULL);
    }

    if (dat)
        queue_delayed_work(smb345_dev->chrgr_work_queue,
            &smb345_dev->aicl_dete_work,
            30*HZ);

    led_control();
}
EXPORT_SYMBOL(aicl_dete_worker);

static void verifyFW()
{
    if (!smb345_dev) {
        pr_err("%s: smb345_dev is null "
            "due to driver probed isn't ready\n",
            __func__);
        return;
    }

    /* USB Mode Detection (by SOC) */

    /* Get USB to HC mode */
    if (smb345_get_USB9_HC_Toggle() < 0) {
        dev_err(&smb345_dev->client->dev,
        "%s: fail to get USB9 and HC mode!\n", __func__);
        return;
    }

    /* Get USB5/1/HC to register control */
    if (smb345_get_USB9_HC_PIN_Control() < 0) {
        dev_err(&smb345_dev->client->dev,
        "%s: fail to get USB9 and HC mode pin control!\n", __func__);
        return;
    }

#if 0
    /* Get AICL value */
    if (smb345_get_AICL() < 0) {
        dev_err(&smb345_dev->client->dev,
        "%s: fail to get AICL mode!\n", __func__);
        return;
    }
#endif

    /* Get I_USB_IN value */
    if (smb345_get_current_limits() < 0) {
        dev_err(&smb345_dev->client->dev,
        "%s: fail to get max current limits!\n", __func__);
        return;
    }
}

static void smb345_config_max_current_twinheadeddragon()
{
    if (!smb345_dev) {
        pr_err("%s: smb345_dev is null "
            "due to driver probed isn't ready\n",
            __func__);
        return;
    }

    /* check if ACOK# = 0 */
    if (smb345_dev->pdata->inok_gpio >= 0) {
        if (gpio_get_value(smb345_dev->pdata->inok_gpio)) {
            dev_err(&smb345_dev->client->dev,
            "%s: system input voltage is not valid "
            ">>> INOK pin (HIGH) <<<\n",
            __func__);
            return;
        }
    }
    else {
        dev_err(&smb345_dev->client->dev,
        "%s: negative gpio number: INOK!\n", __func__);
        return;
    }

    /* Allow violate register can be written - Write 30h[7]="1" */
    if (smb345_set_writable(smb345_dev, true) < 0) {
        dev_err(&smb345_dev->client->dev,
        "%s: smb345_set_writable failed!\n", __func__);
        return;
    }

    /* Disable AICL - Write 02h[4]="0" */
    if (smb345_OptiCharge_Toggle(false) < 0) {
        dev_err(&smb345_dev->client->dev,
        "%s: fail to disable AICL\n", __func__);
        return;
    }

    /* Set I_USB_IN=1800mA - 01h[3:0]="0110" */
    if (smb345_set_current_limits(USB_IN, true) < 0) {
        dev_err(&smb345_dev->client->dev,
        "%s: fail to set max current limits for TWINSHEADED\n",
        __func__);
        return;
    }

    /* Set USB to HC mode - Write 31h[1:0]="11" */
    if (smb345_USB9_HC_Toggle(true) < 0) {
        dev_err(&smb345_dev->client->dev,
        "%s: fail to enable USB9 and HC mode!\n",
        __func__);
        return;
    }

    /* Set USB5/1/HC to register control - Write 06h[4]="0" */
    if (smb345_USB9_HC_PIN_Control(false) < 0) {
        dev_err(&smb345_dev->client->dev,
        "%s: fail to disable USB9 and HC mode pin control!\n",
        __func__);
        return;
    }

#if 0
    /* Enable AICL - Write 02h[4]="1" */
    if (smb345_OptiCharge_Toggle(true) < 0) {
        dev_err(&smb345_dev->client->dev,
        "%s: fail to enable AICL\n", __func__);
        return;
    }
#endif

    smb345_soc_control_jeita();

    /* check if ACOK# = 0 */
    if (gpio_get_value(smb345_dev->pdata->inok_gpio)) {
        dev_err(&smb345_dev->client->dev,
        "%s: system input voltage is not valid "
        "after charge current settings\n",
        __func__);
        return;
    }

    pr_info("%s: charger type: TWINHEADEDDRAGON done.\n",
        __func__);
}

#if defined(CONFIG_PF400CG)
/* enable/disable AICL function */
static int smb346_Battery_OV_Toggle(bool on)
{
    int ret;

    ret = smb345_read(smb345_dev, CFG_VARIOUS_FUNCS);
    if (ret < 0)
        goto fail;

    if (on)
        ret |= CFG_VARIOUS_FUNCS_BATTERY_OV;
    else
        ret &= ~CFG_VARIOUS_FUNCS_BATTERY_OV;

    ret = smb345_write(smb345_dev, CFG_VARIOUS_FUNCS, ret);
    if (ret < 0)
        goto fail;

fail:
    return ret;
}

static int smb346_config_charge_current(void)
{
    int ret;

    ret = smb345_write(smb345_dev,
        CFG_CHARGE_CURRENT,
        CFG_CHARGE_CURRENT_ALL);
    if (ret < 0)
        goto fail;

fail:
    return ret;
}

static int smb346_config_ccc(void)
{
    int ret;

    ret = smb345_masked_write(smb345_dev->client,
            OTG_TLIM_THERM_CNTRL_REG,
            CHARGE_CURRENT_COMPENSATION,
            CHARGE_CURRENT_COMPENSATION_VALUE);
    if (ret < 0)
        return ret;

    return ret;
}

static void smb346_pre_config(void)
{
    /* set battery OV does not end charge cycle */
    smb346_Battery_OV_Toggle(false);

    /* set fast charge current: 600mA
           pre-charge current: 50mA
           termination current: 50mA
    */
    smb346_config_charge_current();

    /* set cold soft limit current: 125mA */
    smb346_config_ccc();
    return;
}
#else
static void smb346_pre_config(void) { return; }
#endif

#if defined(CONFIG_ME175CG)
static int smb358_pre_config()
{
    int ret;

    /* set fast charge current: 2000mA */
    ret = smb345_masked_write(smb345_dev->client,
        CHG_CURRENT_REG,
        SMB358_FAST_CHG_CURRENT_MASK,
        SMB358_FAST_CHG_CURRENT_VALUE_2000mA);
    if (ret < 0)
        goto fail;

    /* set termination current: 200mA */
    ret = smb345_masked_write(smb345_dev->client,
        CHG_CURRENT_REG,
        SMB358_TERMINATION_CURRENT_MASK,
        SMB358_TERMINATION_CURRENT_VALUE_200mA);
    if (ret < 0)
        goto fail;

    /* set cold soft limit current: 600mA */
    ret = smb345_masked_write(smb345_dev->client,
            OTG_TLIM_THERM_CNTRL_REG,
            CHARGE_CURRENT_COMPENSATION,
            BIT(7));
    if (ret < 0)
        return ret;

fail:
    return ret;
}
#elif defined(CONFIG_A400CG)
static int smb358_pre_config()
{
    int ret;
    int term_curr = SMB358_TERMINATION_CURRENT_VALUE_80mA;
    int charging_curr = SMB358_FAST_CHG_CURRENT_VALUE_600mA;

    /* set fast charge current: 600mA or 900mA
     * according to 1200mAh battery or 1600mAh battery
    */
    if (is_battery_id_51k() || is_battery_id_100k()) {
        charging_curr = SMB358_FAST_CHG_CURRENT_VALUE_600mA;
    }
    else if (is_battery_id_10k()) {
        charging_curr = SMB358_FAST_CHG_CURRENT_VALUE_900mA;
    }
    else {
        charging_curr = SMB358_FAST_CHG_CURRENT_VALUE_600mA;
        dev_err(&smb345_dev->client->dev,
            "%s: * UNKNOWN Battery, use 600mA charging current *\n",
            __func__);
    }
    ret = smb345_masked_write(smb345_dev->client,
        CHG_CURRENT_REG,
        SMB358_FAST_CHG_CURRENT_MASK,
        charging_curr);
    if (ret < 0)
        goto fail;

    /* set cold soft limit current: 200mA */
    ret = smb345_masked_write(smb345_dev->client,
        OTG_TLIM_THERM_CNTRL_REG,
        CHARGE_CURRENT_COMPENSATION,
        0);
    if (ret < 0)
        return ret;

    /* set termination current: 80mA or 100mA */
    if (is_battery_id_10k())
        term_curr = SMB358_TERMINATION_CURRENT_VALUE_100mA;
    ret = smb345_masked_write(smb345_dev->client,
        CHG_CURRENT_REG,
        SMB358_TERMINATION_CURRENT_MASK,
        term_curr);
    if (ret < 0)
        goto fail;

fail:
    return ret;
}
#elif defined(CONFIG_FE380CG) || defined(CONFIG_FE170CSG)
static int smb358_pre_config()
{
    int ret;

    /* Set Fast Charge current to 2000mA 00h[7:5]="111" */
    ret = smb345_masked_write(smb345_dev->client,
        CHG_CURRENT_REG,
        BIT(7) | BIT(6) | BIT(5),
        BIT(7) | BIT(6) | BIT(5));
    if (ret < 0)
        goto fail;

    /* Set cold soft limit current to 600mA 0Ah[7:6]="10" */
    ret = smb345_masked_write(smb345_dev->client,
        OTG_TLIM_THERM_CNTRL_REG,
        BIT(7) | BIT(6),
        BIT(7));
    if (ret < 0)
        goto fail;

    /* Set Termination current to 200mA 00h[2:0]="111" */
    ret = smb345_masked_write(smb345_dev->client,
        CHG_CURRENT_REG,
        BIT(2) | BIT(1) | BIT(0),
        BIT(2) | BIT(1) | BIT(0));
    if (ret < 0)
        goto fail;

    /* Set Recharge Voltage to Vflt-100mV 01h[3:2]="01" */
    ret = smb345_masked_write(smb345_dev->client,
        CFG_CURRENT_LIMIT,
        BIT(3) | BIT(2),
        BIT(2));
    if (ret < 0)
        goto fail;

fail:
    return ret;
}
#elif defined(CONFIG_FE171MG)
static int smb358_pre_config()
{
    int ret;

    /* Set Fast Charge current to 1500mA 00h[7:5]="101" */
    ret = smb345_masked_write(smb345_dev->client,
        CHG_CURRENT_REG,
        BIT(7) | BIT(6) | BIT(5),
        BIT(7) | BIT(5));
    if (ret < 0)
        goto fail;

    /* Set cold soft limit current to 600mA 0Ah[7:6]="10" */
    ret = smb345_masked_write(smb345_dev->client,
        OTG_TLIM_THERM_CNTRL_REG,
        BIT(7) | BIT(6),
        BIT(7));
    if (ret < 0)
        goto fail;

    /* Set Pre-charge current to 150mA 00h[4:3]="00" */
    ret = smb345_masked_write(smb345_dev->client,
        CHG_CURRENT_REG,
        BIT(4) | BIT(3),
        0);
    if (ret < 0)
        goto fail;

    /* Set Termination current to 200mA 00h[2:0]="111" */
    ret = smb345_masked_write(smb345_dev->client,
        CHG_CURRENT_REG,
        BIT(2) | BIT(1) | BIT(0),
        BIT(2) | BIT(1) | BIT(0));
    if (ret < 0)
        goto fail;

    /* Set Recharge Voltage to Vflt-100mV 01h[3:2]="01" */
    ret = smb345_masked_write(smb345_dev->client,
        CFG_CURRENT_LIMIT,
        BIT(3) | BIT(2),
        BIT(2));
    if (ret < 0)
        goto fail;

    /* Disable charging */
    ret = smb345_charging_toggle(JEITA, false);
    ret = smb345_masked_write(smb345_dev->client,
        0x30,
        BIT(1),
        0);
    if (ret < 0)
        goto fail;

    /* Set float voltage 03h[5:0]="101010" */
    ret = smb345_masked_write(smb345_dev->client,
        0x03,
        BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0),
        BIT(5) | BIT(3) | BIT(1));
    if (ret < 0)
        goto fail;

    /* Enable charging */
    ret = smb345_charging_toggle(JEITA, true);

fail:
    return ret;
}
#elif defined(CONFIG_Z580Cx)
static int smb358_pre_config()
{
    int ret;

    /* set fast charge current: 2000mA 00h[7:5]="111"*/
    /* set termination current: 200mA 00h[2:0]="111"*/
    ret = smb345_masked_write(smb345_dev->client,
            0x00,
            BIT(7) | BIT(6) | BIT(5),
            BIT(7) | BIT(6) | BIT(5));
    if (ret < 0)
        goto fail;
    ret = smb345_masked_write(smb345_dev->client,
            0x00,
            BIT(2) | BIT(1) | BIT(0),
            BIT(2) | BIT(1) | BIT(0));
    if (ret < 0)
        goto fail;

    /* set cold soft limit current = 600mA 0Ah[7:6]="10" */
    ret = smb345_masked_write(smb345_dev->client,
            0x0A,
            BIT(7) | BIT(6),
            BIT(7));
    if (ret < 0)
        goto fail;

    /* Disable charging */
    ret = smb345_charging_toggle(JEITA, false);

    /* Set float voltage 03h[5:0]="101010" */
    ret = smb345_masked_write(smb345_dev->client,
            0x03,
            BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0),
            BIT(5) | BIT(3) | BIT(1));
    if (ret < 0)
        goto fail;

    /* Enable charging */
    ret = smb345_charging_toggle(JEITA, true);

fail:
    return ret;
}
#else
static int smb358_pre_config() { return 0; }
#endif

#if defined(CONFIG_A503ML)
static int smb347_pre_config()
{
    int ret;

    /* set fast charge current: 900mA 00h[7:5]="001"*/
    /* set pre charge current: 100mA 00h[4:3]="00"*/
    /* set termination current: 100mA 00h[2:0]="010"*/
    ret = smb345_write(smb345_dev,
        0x00,
        0x22);
    if (ret < 0)
        goto fail;

    /* set cold soft limit current 0Ah[7:6]="00" */
    ret = smb345_masked_write(smb345_dev->client,
        0x0A,
        BIT(7) | BIT(6),
        0x00);
    if (ret < 0)
        goto fail;

    /* set Battery OV does not end charge cycle 02h[1]="0" */
    ret = smb345_masked_write(smb345_dev->client,
        0x02,
        BIT(1),
        0x00);
    if (ret < 0)
        goto fail;

    /* set Float voltage as 4.34V 03h[5:0]="101010" */
    ret = smb345_masked_write(smb345_dev->client,
        0x03,
        BIT(0) | BIT(1) | BIT(2) | BIT(3) | BIT(4) | BIT(5),
        0x2A);
    if (ret < 0)
        goto fail;

fail:
    return ret;
}
#elif defined(CONFIG_Z580C)
static int smb347_pre_config()
{
    int ret;

    if (HW_ID <= HW_ID_PR) { /* smb347 */
    /* set fast charge current: 1800mA 00h[7:5]="100"*/
    /* set pre charge current: 150mA 00h[4:3]="01"*/
    /* set termination current: 200mA 00h[2:0]="100"*/
    ret = smb345_write(smb345_dev,
        0x00,
        0x8C);
    if (ret < 0)
        goto fail;

    /* set cold soft limit current = 700mA 0Ah[7:6]="01" */
    ret = smb345_masked_write(smb345_dev->client,
        0x0A,
        BIT(7) | BIT(6),
        BIT(6));
    if (ret < 0)
        goto fail;

    /* set Battery OV does not end charge cycle 02h[1]="0" */
    ret = smb345_masked_write(smb345_dev->client,
        0x02,
        BIT(1),
        0x00);
    if (ret < 0)
        goto fail;

    /* set Float voltage as 4.34V 03h[5:0]="101010" */
    ret = smb345_masked_write(smb345_dev->client,
        0x03,
        BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0),
        BIT(5) | BIT(3) | BIT(1));
    if (ret < 0)
        goto fail;
    }
    else {  /* smb358 */
        /* set fast charge current: 2000mA 00h[7:5]="111"*/
        /* set termination current: 200mA 00h[2:0]="111"*/
        ret = smb345_masked_write(smb345_dev->client,
            0x00,
            BIT(7) | BIT(6) | BIT(5),
            BIT(7) | BIT(6) | BIT(5));
        if (ret < 0)
            goto fail;
        ret = smb345_masked_write(smb345_dev->client,
            0x00,
            BIT(2) | BIT(1) | BIT(0),
            BIT(2) | BIT(1) | BIT(0));
        if (ret < 0)
            goto fail;

        /* set cold soft limit current = 600mA 0Ah[7:6]="10" */
        ret = smb345_masked_write(smb345_dev->client,
            0x0A,
            BIT(7) | BIT(6),
            BIT(7));
        if (ret < 0)
            goto fail;

        /* Disable charging */
        ret = smb345_charging_toggle(JEITA, false);

        /* Set float voltage 03h[5:0]="101010" */
        ret = smb345_masked_write(smb345_dev->client,
            0x03,
            BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0),
            BIT(5) | BIT(3) | BIT(1));
        if (ret < 0)
            goto fail;

        /* Enable charging */
        ret = smb345_charging_toggle(JEITA, true);
    }

fail:
    return ret;
}
#else
static int smb347_pre_config() { return 0; }
#endif

#if defined(CONFIG_A450CG)
static int smb345_pre_config()
{
    int ret;

    /* set fast charge current: 900mA 00h[7:5]="001"*/
    /* set pre charge current: 100mA 00h[4:3]="00"*/
    /* set termination current: 100mA 00h[2:0]="010"*/
    ret = smb345_write(smb345_dev,
        CHG_CURRENT_REG,
        0x22);
    if (ret < 0)
        goto fail;

    /* set battery OV does not end charge cycle 02h[1]="0" */
    ret = smb345_masked_write(smb345_dev->client,
        CFG_VARIOUS_FUNCS,
        CFG_VARIOUS_FUNCS_BATTERY_OV,
        0x00);
    if (ret < 0)
        goto fail;

    /* set soft hot temp limit behavior 07h[1:0]="10" */
    ret = cancel_soft_hot_temp_limit(false);
    if (ret < 0)
        goto fail;

    /* set soft cold temp limit current 250mA 0Ah[7:6]="00" */
    ret = smb345_masked_write(smb345_dev->client,
        OTG_TLIM_THERM_CNTRL_REG,
        CHARGE_CURRENT_COMPENSATION,
        CHARGE_CURRENT_COMPENSATION_VALUE);
    if (ret < 0)
        return ret;

fail:
    return ret;
}
#elif defined(CONFIG_ME372CG) || defined(CONFIG_ME372CL)
static int smb345_pre_config()
{
    int ret;

    /* set battery OV does not end charge cycle 02h[1]="0" */
    ret = smb345_masked_write(smb345_dev->client,
        CFG_VARIOUS_FUNCS,
        CFG_VARIOUS_FUNCS_BATTERY_OV,
        0x00);
    if (ret < 0)
        goto fail;

fail:
    return ret;
}
#else
static int smb345_pre_config() { return 0; }
#endif

#if defined(CONFIG_A503ML)
static void smb347_config_max_current(int usb_state)
{
    if (usb_state == AC_IN) {
        /* check IF Max USBIN input current limit is 1200mA 01h[3:0]="0100" */
        if (smb345_masked_read(smb345_dev->client, 0x01, 0x0f) != 0x04) {

            /* Disable AICL - Write 02h[4]="0" */
            if (smb345_masked_write(smb345_dev->client, 0x02, BIT(4), 0x00)) {
                dev_err(&smb345_dev->client->dev,
                "%s: fail to disable AICL\n", __func__);
                return;
            }

            /* Set I_USB_IN=1200mA - Write 01h[3:0]="0100" */
            if (smb345_masked_write(smb345_dev->client, 0x01, 0x0f, 0x04)) {
                dev_err(&smb345_dev->client->dev,
                "%s: fail to set max current limits for USB_IN\n",
                __func__);
                return;
            }

            /* Enable AICL - Write 02h[4]="1" */
            if (smb345_masked_write(smb345_dev->client, 0x02, BIT(4), 0x10)) {
                dev_err(&smb345_dev->client->dev,
                "%s: fail to enable AICL\n", __func__);
                return;
            }
        }
    }
    else if (usb_state == USB_IN) {

    }
    else if (usb_state == PAD_SUPPLY) {
        /* enable 1800mA charging as Phone plug into Pad */

        /* Disable AICL - Write 02h[4]="0" */
        if (smb345_masked_write(smb345_dev->client, 0x02, BIT(4), 0x00)) {
            dev_err(&smb345_dev->client->dev,
            "%s: fail to disable AICL\n", __func__);
            return;
        }

        /* Set I_DC_IN=1800mA - Write 01h[7:4]="0110" */
        if (smb345_masked_write(smb345_dev->client, 0x01, 0xf0, 0x60)) {
            dev_err(&smb345_dev->client->dev,
            "%s: fail to set max current limits for USB_IN\n",
            __func__);
            return;
        }

        /* Enable AICL - Write 02h[4]="1" */
        if (smb345_masked_write(smb345_dev->client, 0x02, BIT(4), 0x10)) {
            dev_err(&smb345_dev->client->dev,
            "%s: fail to enable AICL\n", __func__);
            return;
        }
    }
}
#elif defined(CONFIG_Z580C)
static void smb347_config_max_current(int usb_state)
{
    if (usb_state == AC_IN) {
        if (HW_ID <= HW_ID_PR) { /* smb347 */
        /* check if Max USBIN input current limit is 1200mA 01h[3:0]="0100" */
        if (smb345_masked_read(smb345_dev->client, 0x01, 0x0f) != 0x04) {

            /* Disable AICL - Write 02h[4]="0" */
            if (smb345_masked_write(smb345_dev->client, 0x02, BIT(4), 0x00)) {
                dev_err(&smb345_dev->client->dev,
                "%s: fail to disable AICL\n", __func__);
                return;
            }

            /* Set I_USB_IN=1200mA - Write 01h[3:0]="0100" */
            if (smb345_masked_write(smb345_dev->client, 0x01, 0x0f, 0x04)) {
                dev_err(&smb345_dev->client->dev,
                "%s: fail to set max current limits for USB_IN\n",
                __func__);
                return;
            }

            /* Enable AICL - Write 02h[4]="1" */
            if (smb345_masked_write(smb345_dev->client, 0x02, BIT(4), 0x10)) {
                dev_err(&smb345_dev->client->dev,
                "%s: fail to enable AICL\n", __func__);
                return;
            }
        }
        }
        else { /* smb358 */
            /* check if Max USBIN input current limit is 1200mA 3Fh[3:0]="0100" */
            if (smb345_masked_read(smb345_dev->client, 0x3f, 0x0f) != 0x04) {

                /* Disable AICL - Write 02h[4]="0" */
                if (smb345_masked_write(smb345_dev->client, 0x02, BIT(4), 0x00)) {
                    dev_err(&smb345_dev->client->dev,
                    "%s: fail to disable AICL\n", __func__);
                    return;
                }

                /* Set I_USB_IN=1200mA - Write 01h[7:4]="0100" */
                if (smb345_masked_write(smb345_dev->client, 0x01, 0xf0, 0x40)) {
                    dev_err(&smb345_dev->client->dev,
                    "%s: fail to set max current limits for USB_IN\n",
                    __func__);
                    return;
                }

                /* Enable AICL - Write 02h[4]="1" */
                if (smb345_masked_write(smb345_dev->client, 0x02, BIT(4), 0x10)) {
                    dev_err(&smb345_dev->client->dev,
                    "%s: fail to enable AICL\n", __func__);
                    return;
                }
            }
        }
    }
}
#else
static void smb347_config_max_current(int usb_state) {}
#endif

/*----------------------------------------------------------------------------*/
#if defined(CONFIG_PF400CG)
static void smb3xx_config_max_current(int usb_state)
{
    int rsoc;

    /* USB Mode Detection (by SOC) */
    if ( (usb_state == AC_IN) && (!g_init_disable_aicl) ) // Not call AICL when device init and AC-IN
    {
        /* Disable AICL - Write 02h[4]="0" */
        if (smb345_OptiCharge_Toggle(false) < 0) {
            dev_err(&smb345_dev->client->dev,
            "%s: fail to disable AICL\n", __func__);
            g_init_disable_aicl = false;
            return;
        }

        /* Set I_USB_IN=1200mA - Write 01h[3:0]="0100" */
        if (smb345_set_current_limits(AC_IN, false) < 0) {
            dev_err(&smb345_dev->client->dev,
            "%s: fail to set max current limits for USB_IN\n",
            __func__);
            g_init_disable_aicl = false;
            return;
        }

        /* Enable AICL - Write 02h[4]="1" */
        if (smb345_OptiCharge_Toggle(true) < 0) {
            dev_err(&smb345_dev->client->dev,
            "%s: fail to enable AICL\n", __func__);
            g_init_disable_aicl = false;
            return;
        }
    }
    else if (usb_state == USB_IN) {

    }
    else if (usb_state == PAD_SUPPLY) {
        /* acquire battery rsoc here */
        if (get_battery_rsoc(&rsoc)) {
            dev_err(&smb345_dev->client->dev,
            " %s: fail to get battery rsoc\n", __func__);
            g_init_disable_aicl = false;
            return;
        }
        else
            dev_err(&smb345_dev->client->dev,
            " %s: get battery rsoc(%d)\n", __func__, rsoc);

        if(!g_init_disable_aicl)  // Not call AICL when device init and PAD Mode
        {
        	if (smb346_set_pad_supply_dc_in_limits(rsoc) < 0) {
            	dev_err(&smb345_dev->client->dev,
            	"%s: fail to config DC_IN limits\n", __func__);
        	} 
        }
    }

    if(g_init_disable_aicl)
        dev_err(&smb345_dev->client->dev, "%s: not call disable/enable function when device init and AC-IN !!!\n", __func__);

    g_init_disable_aicl = false;
}
#elif defined(CONFIG_ME175CG) || defined(CONFIG_A400CG)
static void smb3xx_config_max_current(int usb_state)
{
    #if 0
    /* Set USB to HC mode - Write 31h[1:0]="11" */
    if (smb345_USB9_HC_Toggle(true) < 0) {
        dev_err(&smb345_dev->client->dev,
        "%s: fail to enable USB9 and HC mode!\n",
        __func__);
        return;
    }
    #endif

    /* USB Mode Detection (by SOC) */
     if ( (usb_state == AC_IN) && (!g_init_disable_aicl) ) {
        /* Disable AICL - Write 02h[4]="0" */
        if (smb345_OptiCharge_Toggle(false) < 0) {
            dev_err(&smb345_dev->client->dev,
            "%s: fail to disable AICL\n", __func__);
            g_init_disable_aicl = false;
            return;
        }

        /* Set I_USB_IN=1200mA - Write 01h[7:4]="0100" */
        if (smb345_set_current_limits(AC_IN, false) < 0) {
            dev_err(&smb345_dev->client->dev,
            "%s: fail to set max current limits for USB_IN\n",
            __func__);
            g_init_disable_aicl = false;
            return;
        }

        /* Enable AICL - Write 02h[4]="1" */
        if (smb345_OptiCharge_Toggle(true) < 0) {
            dev_err(&smb345_dev->client->dev,
            "%s: fail to enable AICL\n", __func__);
            g_init_disable_aicl = false;
            return;
        }
    }
    else if (usb_state == USB_IN) {

    }

    if(g_init_disable_aicl)
        dev_err(&smb345_dev->client->dev, "%s: not call disable/enable function when device init and AC-IN !!!\n", __func__);

    g_init_disable_aicl = false;
}
#elif defined(CONFIG_ME372CG) || defined(CONFIG_ME372CL) || defined(CONFIG_A450CG)
static void smb3xx_config_max_current(int usb_state)
{
    /* USB Mode Detection (by SOC) */
    if (usb_state == AC_IN) {

        #ifndef CONFIG_A450CG
        /* Disable AICL - Write 02h[4]="0" */
        if (smb345_OptiCharge_Toggle(false) < 0) {
            dev_err(&smb345_dev->client->dev,
            "%s: fail to disable AICL\n", __func__);
            return;
        }
        #endif

        /* Set I_USB_IN=1200mA - Write 01h[3:0]="0100" */
        if (smb345_set_current_limits(AC_IN, false) < 0) {
            dev_err(&smb345_dev->client->dev,
            "%s: fail to set max current limits for USB_IN\n", __func__);
            return;
        }

        #ifndef CONFIG_A450CG
        /* Enable AICL - Write 02h[4]="1" */
        if (smb345_OptiCharge_Toggle(true) < 0) {
            dev_err(&smb345_dev->client->dev,
            "%s: fail to enable AICL\n", __func__);
            return;
        }
        #endif

        /* Set USB to HC mode - Write 31h[1:0]="11" */
        if (smb345_USB9_HC_Toggle(true) < 0) {
            dev_err(&smb345_dev->client->dev,
            "%s: fail to enable USB9 and HC mode!\n", __func__);
            return;
        }

        /* Set USB5/1/HC to register control - Write 06h[4]="0" */
        if (smb345_USB9_HC_PIN_Control(false) < 0) {
            dev_err(&smb345_dev->client->dev,
            "%s: fail to disable USB9 and HC mode pin control!\n", __func__);
            return;
        }
    }
    else if (usb_state == USB_IN) {
        /* Set I_USB_IN=500mA - Write 01h[3:0]="0001" */
        if (smb345_set_current_limits(USB_IN, false) < 0) {
            dev_err(&smb345_dev->client->dev,
            "%s: fail to set max current limits for USB_IN\n", __func__);
            return;
        }
    }
}
#elif defined(CONFIG_ME302C)
static void smb3xx_config_max_current(int usb_state)
{
    /* USB Mode Detection (by SOC) */
    if (usb_state == AC_IN) {
        /* Set I_USB_IN=1800mA - Write 01h[3:0]="0110" */
        if (smb345_set_current_limits(AC_IN, false) < 0) {
            dev_err(&smb345_dev->client->dev,
                "%s: fail to set max current limits for USB_IN\n",
                __func__);
            return;
        }

        /* Set USB to HC mode - Write 31h[1:0]="11" */
        if (smb345_USB9_HC_Toggle(true) < 0) {
            dev_err(&smb345_dev->client->dev,
                "%s: fail to enable USB9 and HC mode!\n",
                __func__);
            return;
        }

        /* Set USB5/1/HC to register control - Write 06h[4]="0" */
        if (smb345_USB9_HC_PIN_Control(false) < 0) {
            dev_err(&smb345_dev->client->dev,
                "%s: fail to disable USB9 and HC mode pin control!\n",
                __func__);
            return;
        }
    }
    else if (usb_state == USB_IN) {
        /* Set I_USB_IN=500mA - Write 01h[3:0]="0001" */
        if (smb345_set_current_limits(USB_IN, false) < 0) {
            dev_err(&smb345_dev->client->dev,
                "%s: fail to set max current limits for USB_IN\n",
                __func__);
            return;
        }
    }
}
#elif defined(CONFIG_FE380CG) || defined(CONFIG_FE170CSG) || defined(CONFIG_FE171MG)
static void smb3xx_config_max_current(int usb_state)
{
    if (usb_state == AC_IN) {
        /* check IF Max USBIN input current limit is 1200mA 01h[7:4]="0100" */
        if (smb345_masked_read(smb345_dev->client, 0x01, 0xf0) != 0x40) {

            /* Disable AICL - Write 02h[4]="0" */
            if (smb345_masked_write(smb345_dev->client, 0x02, BIT(4), 0x00)) {
                dev_err(&smb345_dev->client->dev,
                "%s: fail to disable AICL\n", __func__);
                return;
            }

            /* Set I_USB_IN=1200mA - Write 01h[7:4]="0100" */
            if (smb345_masked_write(smb345_dev->client, 0x01, 0xf0, 0x40)) {
                dev_err(&smb345_dev->client->dev,
                "%s: fail to set max current limits for USB_IN\n",
                __func__);
                return;
            }

            /* Enable AICL - Write 02h[4]="1" */
            if (smb345_masked_write(smb345_dev->client, 0x02, BIT(4), 0x10)) {
                dev_err(&smb345_dev->client->dev,
                "%s: fail to enable AICL\n", __func__);
                return;
            }
        }
    }
    else if (usb_state == USB_IN) {

    }
}
#else
static void smb3xx_config_max_current(int usb_state)
{
}
#endif
/*----------------------------------------------------------------------------*/

static void smb345_config_max_current(int usb_state)
{
    if (usb_state != AC_IN && usb_state != USB_IN && usb_state != PAD_SUPPLY)
        return;

    if (!smb345_dev) {
        pr_err("%s: smb345_dev is null "
            "due to driver probed isn't ready\n",
            __func__);
        return;
    }

    /* check if ACOK# = 0 */
    if (smb345_dev->pdata->inok_gpio >= 0) {
        if (gpio_get_value(smb345_dev->pdata->inok_gpio)) {
            dev_err(&smb345_dev->client->dev,
            "%s: system input voltage is not valid "
            ">>> INOK pin (HIGH) <<<\n",
            __func__);
            return;
        }
    }
    else {
        dev_err(&smb345_dev->client->dev,
        "%s: negative gpio number: INOK!\n", __func__);
        return;
    }

    /* Allow violate register can be written - Write 30h[7]="1" */
    if (smb345_set_writable(smb345_dev, true) < 0) {
        dev_err(&smb345_dev->client->dev,
        "%s: smb345_set_writable failed!\n", __func__);
        return;
    }

    smb346_pre_config();
    smb358_pre_config();
    smb345_pre_config();
    smb347_pre_config();

    smb3xx_config_max_current(usb_state);
    smb347_config_max_current(usb_state);

    smb345_soc_control_jeita();
    smb3xx_soc_control_jeita();

    /* check if ACOK# = 0 */
    if (gpio_get_value(smb345_dev->pdata->inok_gpio)) {
        dev_err(&smb345_dev->client->dev,
        "%s: system input voltage is not valid "
        "after charge current settings\n",
        __func__);
        return;
    }

    pr_info("%s: charger type:%d done.\n", __func__, usb_state);
}

#ifdef CONFIG_PROC_FS
static int smb345_proc_read(char *page, char **start, off_t off,
                    int count, int *eof, void *date)
{
    int ret;

    if (!gbuffer)
        return count;

    /* print the key to screen for debugging */
    ret = sprintf(page, "%s\n", gbuffer);

    /* re-generate the key to clean the memory */
    generate_key();

    return ret;
}

static int smb345_proc_write(struct file *file, const char *buffer,
                    unsigned long count, void *data)
{
    char proc_buf[64];

    if (count > sizeof(gbuffer)) {
        BAT_DBG("%s: data error\n", __func__);
        return -EINVAL;
    }

    if (copy_from_user(proc_buf, buffer, count)) {
        BAT_DBG("%s: read data from user space error\n", __func__);
        return -EFAULT;
    }

    if (!memcmp(proc_buf, gbuffer, 36)) {
        BAT_DBG_E("%s: EQ\n", __func__);

        if (smb345_dev && !gpio_get_value(smb345_dev->pdata->inok_gpio)) {

            /* ME372CG charge current control algorithm:
               config the charge current only when
               Vbus is legal (a valid input voltage
               is present)
            */
            mutex_lock(&g_usb_state_lock);
            dev_warn(&smb345_dev->client->dev,
                "%s: config current when TWINHEADEDDRAGON present!\n",
                __func__);
            /* force max 1.8A charge current for ME372CG */
            smb345_config_max_current_twinheadeddragon();
            mutex_unlock(&g_usb_state_lock);
        }

    }
    else {
        BAT_DBG_E("%s: NOT EQ\n", __func__);

        /* re-generate the key to clean memory to avoid
           "brute-force attack". we do not allow someone
           who using "repetitive try-error" to find out
           the correct one.
        */
        generate_key();
    }

    return count;
}

#ifndef CONFIG_NEW_PROC_FS
int smb345_proc_fs_current_control(void)
{
    struct proc_dir_entry *entry=NULL;

    entry = create_proc_entry("twinheadeddragon", 0666, NULL);
    if (!entry) {
        BAT_DBG_E("Unable to create twinheadeddragon\n");
        return -EINVAL;
    }
    entry->read_proc = smb345_proc_read;
    entry->write_proc = smb345_proc_write;

    return 0;
}
#else
# if 0
static ssize_t nsmb345_proc_write(struct file *file,
    const char __user *buf, size_t count, loff_t * ppos)
{
    char proc_buf[64];

    if (count > sizeof(gbuffer)) {
        BAT_DBG("%s: data error\n", __func__);
        return -EINVAL;
    }

    if (copy_from_user(proc_buf, buf, count)) {
        BAT_DBG("%s: read data from user space error\n", __func__);
        return -EFAULT;
    }

    if (!memcmp(proc_buf, gbuffer, 36)) {
        BAT_DBG_E("%s: EQ\n", __func__);

        if (smb345_dev && !gpio_get_value(smb345_dev->pdata->inok_gpio)) {

            /* ME372CG charge current control algorithm:
               config the charge current only when
               Vbus is legal (a valid input voltage
               is present)
            */
            mutex_lock(&g_usb_state_lock);
            dev_warn(&smb345_dev->client->dev,
                "%s: config current when TWINHEADEDDRAGON present!\n",
                __func__);
            /* force max 1.8A charge current for ME372CG */
            smb345_config_max_current_twinheadeddragon();
            mutex_unlock(&g_usb_state_lock);
        }

    }
    else {
        BAT_DBG_E("%s: NOT EQ\n", __func__);

        /* re-generate the key to clean memory to avoid
           "brute-force attack". we do not allow someone
           who using "repetitive try-error" to find out
           the correct one.
        */
        generate_key();
    }

    return count;
}
static int nsmb345_proc_read(struct seq_file *m, void *v)
{
    if (!gbuffer)
        return 0;

    /* print the key to screen for debugging */
    seq_printf(m, "%s\n", gbuffer);

    /* re-generate the key to clean the memory */
    generate_key();

    return 0;
}
static int nsmb345_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, nsmb345_proc_read, NULL);
}
int smb345_proc_fs_current_control(void)
{
    static const struct file_operations proc_fs_current_control_fops = {
        .owner = THIS_MODULE,
        .open = nsmb345_proc_open,
        .read = seq_read,
        .write = nsmb345_proc_write,
        .llseek = seq_lseek,
        .release = single_release,
    };
    struct proc_dir_entry *entry=NULL;

    entry = proc_create("twinheadeddragon", 0666, NULL,
        &proc_fs_current_control_fops);
    if (!entry) {
        BAT_DBG_E("Unable to create twinheadeddragon\n");
        return -EINVAL;
    }

    return 0;
}
#endif
#endif
#else
int smb345_proc_fs_current_control(void) { return 0; }
#endif

#if defined(CONFIG_PF400CG)
int pad_in_otg(int otg_state)
{
    int ret;
    switch (otg_state)
    {
    case ENABLE_5V:
        BAT_DBG(" %s: ENABLE_5V\n", __func__);
        ret = otg(1);
        break;
    case DISABLE_5V:
        BAT_DBG(" %s: DISABLE_5V\n", __func__);
        ret = otg(0);
        break;
    }
    return ret;
}
#endif

int setSMB345Charger(int usb_state)
{
    int ret = 0;

    mutex_lock(&g_usb_state_lock);
    g_pad_cable_state = usb_state;

    if (is_pad_present())
    {
        BAT_DBG(" %s: * Ignore usb_state: %d as PAD_SUPPLY\n",
            __func__, usb_state);
        usb_state = PAD_SUPPLY;
        g_usb_state = usb_state;
    }
    else
        g_usb_state = usb_state;

    mutex_unlock(&g_usb_state_lock);
    if (smb345_dev && g_is_power_supply_registered) {
        switch (usb_state){
        case USB_IN:
            power_supply_changed(&smb345_power_supplies[CHARGER_USB-1]);
            break;
        case AC_IN:
            power_supply_changed(&smb345_power_supplies[CHARGER_AC-1]);
            break;
        case CABLE_OUT:
            power_supply_changed(&smb345_power_supplies[CHARGER_AC-1]);
            power_supply_changed(&smb345_power_supplies[CHARGER_USB-1]);
            break;
        case PAD_SUPPLY:
            if (g_pad_cable_state == AC_IN || g_pad_cable_state == CABLE_OUT)
                power_supply_changed(&smb345_power_supplies[CHARGER_AC-1]);
            break;
        }
    }

    switch (usb_state)
    {
    case USB_IN:
        BAT_DBG(" usb_state: USB_IN\n");
        usb_to_battery_callback(USB_PC);

        if (smb345_dev && !gpio_get_value(smb345_dev->pdata->inok_gpio)) {
            dev_warn(&smb345_dev->client->dev,
                "%s: >>> INOK pin (LOW) <<<\n", __func__);

            /* ME372CG charge current control algorithm:
               config the charge current only when
               Vbus is legal (a valid input voltage
               is present)
            */
            mutex_lock(&g_usb_state_lock);
            dev_warn(&smb345_dev->client->dev,
                "%s: config current when USB_IN\n", __func__);
            smb345_config_max_current(USB_IN);
            mutex_unlock(&g_usb_state_lock);
        }

        led_control();
        break;
    case AC_IN:
        BAT_DBG(" usb_state: AC_IN\n");
        usb_to_battery_callback(USB_ADAPTER);

        if (smb345_dev && !gpio_get_value(smb345_dev->pdata->inok_gpio)) {
            dev_warn(&smb345_dev->client->dev,
                "%s: >>> INOK pin (LOW) <<<\n", __func__);

            /* ME372CG charge current control algorithm:
               config the charge current only when
               Vbus is legal (a valid input voltage
               is present)
            */
            mutex_lock(&g_usb_state_lock);
            dev_warn(&smb345_dev->client->dev,
                "%s: config current when AC_IN\n", __func__);
            smb345_config_max_current(AC_IN);
            mutex_unlock(&g_usb_state_lock);
        }

#ifdef CONFIG_UPI_BATTERY
        if (smb345_dev) {
            if (entry_mode == 4 || entry_mode == 1) {
                if (!wake_lock_active(&wlock)) {
                    BAT_DBG(" %s: asus_battery_power_wakelock "
                        "-> wake lock\n",
                        __func__);
                    wake_lock(&wlock);
                }
            }
        }
#endif
        led_control();
        break;

    case CABLE_OUT:
        BAT_DBG(" usb_state: CABLE_OUT\n");
        usb_to_battery_callback(NO_CABLE);
#ifdef CONFIG_UPI_BATTERY
        if (smb345_dev) {
            if (entry_mode == 4 || entry_mode == 1) {
                if (wake_lock_active(&wlock)) {
                    BAT_DBG(" %s: asus_battery_power_wakelock "
                        "-> wake unlock\n",
                        __func__);
                    /* timeout value as same as the
                    <charger.exe>\asus_global.h
                    #define ASUS_UNPLUGGED_SHUTDOWN_TIME(3 sec)
                    */
                    wake_lock_timeout(&wlock_t, 3*HZ);
                    wake_unlock(&wlock);
                } else { // for PC case
                    wake_lock_timeout(&wlock_t, 3*HZ);
                }
            }
        }
#endif
        led_control();
#if defined(CONFIG_FE380CG)
        /* turn off OTG 5V in some condition.
           check if OTG current is 750mA 0Ah[3:2]="10".
           turn off OTG 5V if true.
        */
        ret = smb345_masked_read(smb345_dev->client,
                0x0A,
                BIT(3) | BIT(2));
        if (ret == BIT(3))
            ret = otg(0);
#endif
#if defined(CONFIG_A503ML)
        /* turn off OTG 5V in some condition.
           check if OTG current is 500mA 0Ah[3:2]="10".
           turn off OTG 5V if true.
        */
        ret = smb345_masked_read(smb345_dev->client,
                0x0A,
                BIT(3) | BIT(2));
        if (ret == BIT(3))
            ret = otg(0);
#endif
        break;

    case ENABLE_5V:
        BAT_DBG(" usb_state: ENABLE_5V\n");
        ret = otg(1);
        break;
    case DISABLE_5V:
        BAT_DBG(" usb_state: DISABLE_5V\n");
        ret = otg(0);
        break;
    case PAD_SUPPLY:
        BAT_DBG(" usb_state: PAD_SUPPLY\n");
        if (smb345_dev && !gpio_get_value(smb345_dev->pdata->inok_gpio)) {
            dev_warn(&smb345_dev->client->dev,
                "%s: >>> INOK pin (LOW) <<<\n", __func__);

            /* ME372CG charge current control algorithm:
               config the charge current only when
               Vbus is legal (a valid input voltage
               is present)
            */
            mutex_lock(&g_usb_state_lock);
            dev_warn(&smb345_dev->client->dev,
                "%s: config current when PAD_SUPPLY\n", __func__);
            smb345_config_max_current(PAD_SUPPLY);
            mutex_unlock(&g_usb_state_lock);
        }
        break;

    default:
        BAT_DBG(" ERROR: wrong usb state value = %d\n", usb_state);
        ret = 1;
    }

    return ret;
}
EXPORT_SYMBOL(setSMB345Charger);
//#endif

/* write 06h[6:5]="00" or "11" */
int smb345_charging_toggle(charging_toggle_level_t level, bool on)
{
    int ret = 0;
    int rsoc;
    int charging_toggle;
    static bool jeita_want_charging = true;
    static bool balan_want_charging = true;
    static charging_toggle_level_t old_lvl = JEITA;
    char *level_str[] = {
        "BALANCE",
        "JEITA",
        "FLAGS",
    };

    if (!smb345_dev) {
        pr_info("Warning: smb345_dev is null "
            "due to probe function has error\n");
        return 1;
    }

    mutex_lock(&g_charging_toggle_lock);
    charging_toggle = g_charging_toggle;
    mutex_unlock(&g_charging_toggle_lock);

    BAT_DBG(" %s: old_lvl:%s, charging_toggle:%s, level:%s, on:%s, Vpmic:%dmV\n",
        __func__,
        level_str[old_lvl],
        charging_toggle ? "YES" : "NO",
        level_str[level],
        on ? "YES" : "NO",
        sd_cv_batt_vol());

    /* reset to default if phone does not plugged into pad */
    if (!is_pad_present())
        balan_want_charging = true;

    /* do charging or not? */
    if (level != FLAGS) {
        if (on) {
            /* want to start charging? */
            if (level == JEITA) {
                jeita_want_charging = true;
                if (!charging_toggle) {
                    /* want to restart charging? */
                    if (old_lvl != JEITA) {
                        /* reject the request! someone stop charging before */
                        BAT_DBG_E("%s: * reject RESTART charging to phone! *"
                            "(old_lvl != JEITA)\n",
                            __func__);
                        return -1;
                    }
                    else if (!balan_want_charging) {
                        /* reject the request! someone stop charging before */
                        BAT_DBG_E("%s: * reject RESTART charging to phone! *"
                            "(!balan_want_charging)\n",
                            __func__);
                        return -1;
                    }
                }
            }
            else if (level == BALANCE) {
                balan_want_charging = true;
                if (!charging_toggle) {
                    /* want to restart charging? */
                    if (old_lvl != BALANCE) {
                        /* reject the request! someone stop charging before */
                        BAT_DBG_E("%s: * reject RESTART charging to phone! *"
                            "(old_lvl != BALANCE)\n",
                            __func__);
                        return -1;
                    }                    
                    else if (!jeita_want_charging) {
                        /* reject the request! someone stop charging before */
                        BAT_DBG_E("%s: * reject RESTART charging to phone! *"
                            "(!jeita_want_charging)\n",
                            __func__);
                        return -1;
                    }
                }
            }
            else {
                /* what the hell are you? */
            }
        }
        else {
            /* want to stop charging? just do it! */
            if (level == JEITA)
                jeita_want_charging = false;
            if (level == BALANCE)
                balan_want_charging = false;
        }
    }
    else {
        /* it's the highest level. just do it! */
    }

    /* level value assignment */
    old_lvl = level;

    if (!on)
        BAT_DBG_E(" %s: *** charging toggle: OFF ***\n",
            __func__);
    else
        BAT_DBG(" %s: --------------- charging toggle: ON ---------------\n",
            __func__);

    ret = smb345_set_writable(smb345_dev, true);
    if (ret < 0)
        return ret;

    /* Config CFG_PIN register */

    ret = smb345_read(smb345_dev, CFG_PIN);
    if (ret < 0)
        goto out;

#if defined(ME372CG_ENG_BUILD) && defined(CONFIG_UPI_BATTERY)
    /* acquire battery rsoc here */
    if (get_battery_rsoc(&rsoc)) {
        BAT_DBG(" %s: fail to get battery rsoc\n", __func__);
    }
    else {
        BAT_DBG(" %s: rsoc=%d\n", __func__, rsoc);
        if (rsoc >= 0 && rsoc <= 100) {
            if (eng_charging_limit) {
                if (rsoc > charging_limit_threshold) {
                    /* stop charging as phone battery rsoc >= 60% in ENG build */
                    on = false;
                    flag_eng_stop_charging = true;
                    BAT_DBG_E(" %s: *** I. ENG charging toggle: OFF *** -> rsoc=%d\n",
                        __func__, rsoc);
                }
                else if (rsoc >= (charging_limit_threshold - 15) && flag_eng_stop_charging && !gpio_get_value(smb345_dev->pdata->inok_gpio)) {
                    /* stop charging as flag is true */
                    on = false;
                    BAT_DBG_E(" %s: *** II. ENG charging toggle: OFF *** -> rsoc=%d\n",
                        __func__, rsoc);
                }
                else
                    flag_eng_stop_charging = false;
            }
            else
                flag_eng_stop_charging = false;
        }
        else
            flag_eng_stop_charging = false;
    }
#endif

#ifdef CONFIG_UPI_BATTERY
    if (get_unknown_battery()) {
        on = false;
        BAT_DBG_E(" %s: *** UNKNOWN BATTERY charging toggle: OFF ***\n",
            __func__);
    }
#endif
    /*
     * Make the charging functionality controllable by a write to the
     * command register unless pin control is specified in the platform
     * data.
     */
    ret &= ~CFG_PIN_EN_CTRL_MASK;
    if (on) {
        /* set Pin Controls - active low (ME371MG connect EN to GROUND) */
        ret |= CFG_PIN_EN_CTRL_ACTIVE_LOW;
    } else {
        /* Do nothing, 0 means i2c control
            . I2C Control - "0" in Command Register disables charger */
    }

    ret = smb345_write(smb345_dev, CFG_PIN, ret);
    if (ret < 0)
        goto out;

    mutex_lock(&g_charging_toggle_lock);
    g_charging_toggle = on;
    mutex_unlock(&g_charging_toggle_lock);

#if 0
    /* Config CMD_A register */

    ret = smb345_read(smb345_dev, CMD_A);
    if (ret < 0)
        goto out;

    //smb345_dev->charging_enabled = on;

    if (on)
        ret |= CMD_A_CHG_ENABLED;
    else
        ret &= ~CMD_A_CHG_ENABLED;

    ret = smb345_write(smb345_dev, CMD_A, ret);
#endif
out:
    return ret;
}

#if 0
static irqreturn_t smb345_interrupt(int irq, void *data)
{
    struct smb345_charger *smb = data;
    int stat_c, irqstat_c, irqstat_d, irqstat_e, irqstat_f;
    irqreturn_t ret = IRQ_NONE;

    pm_runtime_get_sync(&smb->client->dev);
    dev_warn(&smb->client->dev, "%s\n", __func__);

    stat_c = smb345_read(smb, STAT_C);
    if (stat_c < 0) {
        dev_warn(&smb->client->dev, "reading STAT_C failed\n");
        return IRQ_NONE;
    }

    irqstat_c = smb345_read(smb, IRQSTAT_C);
    if (irqstat_c < 0) {
        dev_warn(&smb->client->dev, "reading IRQSTAT_C failed\n");
        return IRQ_NONE;
    }

    irqstat_d = smb345_read(smb, IRQSTAT_D);
    if (irqstat_d < 0) {
        dev_warn(&smb->client->dev, "reading IRQSTAT_D failed\n");
        return IRQ_NONE;
    }

    irqstat_e = smb345_read(smb, IRQSTAT_E);
    if (irqstat_e < 0) {
        dev_warn(&smb->client->dev, "reading IRQSTAT_E failed\n");
        return IRQ_NONE;
    }

    irqstat_f = smb345_read(smb, IRQSTAT_F);
    if (irqstat_f < 0) {
        dev_warn(&smb->client->dev, "reading IRQSTAT_F failed\n");
        return IRQ_NONE;
    }

    /*
     * If we get charger error we report the error back to user.
     * If the error is recovered charging will resume again.
     */
    if (stat_c & STAT_C_CHARGER_ERROR) {
        dev_err(&smb->client->dev,
            "****** charging stopped due to charger error ******\n");

        ret = IRQ_HANDLED;
    }

    /*
     * If we reached the termination current the battery is charged and
     * we can update the status now. Charging is automatically
     * disabled by the hardware.
     */
    if (irqstat_c & (IRQSTAT_C_TERMINATION_IRQ | IRQSTAT_C_TAPER_IRQ)) {
        if ((irqstat_c & IRQSTAT_C_TERMINATION_STAT) &&
                        smb->pdata->show_battery)
        dev_info(&smb->client->dev,
            "[Charge Terminated] Going to HW Maintenance mode\n");
        ret = IRQ_HANDLED;
    }

    /*
     * If we got a complete charger timeout int that means the charge
     * full is not detected with in charge timeout value.
     */
    if (irqstat_d & IRQSTAT_D_CHARGE_TIMEOUT_IRQ) {
        dev_info(&smb->client->dev,
            "[Charge Timeout]:Total Charge Timeout INT recieved\n");
        if (irqstat_d & IRQSTAT_D_CHARGE_TIMEOUT_STAT)
            dev_info(&smb->client->dev,
                "[Charge Timeout]:charging stopped\n");
        ret = IRQ_HANDLED;
    }

    /*
     * If we got an under voltage interrupt it means that AC/USB input
     * was connected or disconnected.
     */
    if (irqstat_e & (IRQSTAT_E_USBIN_UV_IRQ | IRQSTAT_E_DCIN_UV_IRQ)) {
        if (smb345_update_status(smb) > 0) {
        }

        if (smb->mains_online || smb->usb_online)
            dev_info(&smb->client->dev, "Charger connected\n");
        else
            dev_info(&smb->client->dev, "Charger disconnected\n");

        ret = IRQ_HANDLED;
    }

    /*
     * If the battery voltage falls below OTG UVLO the VBUS is
     * automatically turned off but we must not enable it again unless
     * UVLO is cleared. It will be cleared when external power supply
     * is connected and the battery voltage goes over the UVLO
     * threshold.
     */
    if (irqstat_f & IRQSTAT_F_OTG_UV_IRQ) {
        smb->otg_battery_uv = !!(irqstat_f & IRQSTAT_F_OTG_UV_STAT);
        dev_info(&smb->client->dev, "Vbatt is below OTG UVLO\n");
        ret = IRQ_HANDLED;
    }

    pm_runtime_put_sync(&smb->client->dev);
    return ret;
}
#endif

static int smb345_irq_set(struct smb345_charger *smb, bool enable)
{
    int ret;

#if 0
    ret = smb345_set_writable(smb, true);
    if (ret < 0)
        return ret;
#endif

    /*
     * Enable/disable interrupts for:
     *    - under voltage
     *    - termination current reached
     *    - charger error
     */
    if (enable) {
#if 0
        int val = CFG_FAULT_IRQ_DCIN_UV;

        if (smb->otg)
            val |= CFG_FAULT_IRQ_OTG_UV;

        ret = smb345_write(smb, CFG_FAULT_IRQ, val);
        if (ret < 0)
            goto fail;

        val = CFG_STATUS_IRQ_CHARGE_TIMEOUT |
            CFG_STATUS_IRQ_TERMINATION_OR_TAPER;
        ret = smb345_write(smb, CFG_STATUS_IRQ, val);
        if (ret < 0)
            goto fail;
#endif
// Need charger error IRQ
        ret = smb345_read(smb, CFG_PIN);
        if (ret < 0)
            goto fail;

        ret |= CFG_PIN_EN_CHARGER_ERROR;

        ret = smb345_write(smb, CFG_PIN, ret);
    } else {
#if 0
        ret = smb345_write(smb, CFG_FAULT_IRQ, 0);
        if (ret < 0)
            goto fail;

        ret = smb345_write(smb, CFG_STATUS_IRQ, 0);
        if (ret < 0)
            goto fail;
#endif
// cancel charger error IRQ
        ret = smb345_read(smb, CFG_PIN);
        if (ret < 0)
            goto fail;

        ret &= ~CFG_PIN_EN_CHARGER_ERROR;

        ret = smb345_write(smb, CFG_PIN, ret);
    }

fail:
#if 0
    smb345_set_writable(smb, false);
#endif
    return ret;
}

bool external_power_source_present()
{
    return !gpio_get_value(get_gpio_by_name("CHG_INOK#"));
}
#if defined(CONFIG_PF400CG)
static bool is_pad_present()
{
    bool ret;

    ret = !gpio_get_value(get_gpio_by_name("PAD_PLUG_IN_N"));

    return ret;
}
static bool pad_pwr_supply()
{
    bool ret;

    /* return TRUE if gpio for pad insertion detected
       and charger received the valid external power
    */
    ret = !gpio_get_value(get_gpio_by_name("PAD_PLUG_IN_N")) &&
          !gpio_get_value(get_gpio_by_name("CHG_INOK#"));
    return ret;    
}
#else
static bool pad_pwr_supply() { return false; }
static bool is_pad_present() { return false; }
#endif

static irqreturn_t smb345_inok_interrupt(int irq, void *data)
{
    struct smb345_charger *smb = data;
    int stat_c, irqstat_c, irqstat_d, irqstat_e, irqstat_f;
    irqreturn_t ret = IRQ_NONE;
    int charger_type;

    /* wake lock to prevent system instantly
       enter S3 while it's in resuming flow */
    wake_lock_timeout(&smb->wakelock, HZ);

    pm_runtime_get_sync(&smb->client->dev);

    if (gpio_get_value(smb->pdata->inok_gpio)) {
        dev_warn(&smb->client->dev,
            "%s: >>> INOK pin (HIGH) <<<\n", __func__);

#if 0
        mutex_lock(&g_usb_state_lock);
        /* maybe CUT OFF pad power leads to gpio level HIGH */
        if (!is_pad_present())
            g_usb_state = CABLE_OUT;
        mutex_unlock(&g_usb_state_lock);
#endif

        /* reset to default as missing external power source */
        mutex_lock(&g_charging_toggle_lock);
        if (!is_pad_present())
            g_charging_toggle = true;
#ifdef ME372CG_ENG_BUILD
        flag_eng_stop_charging = false;
#endif
        mutex_unlock(&g_charging_toggle_lock);
    }
    else {
        dev_warn(&smb->client->dev,
            "%s: >>> INOK pin (LOW) <<<\n",    __func__);

        /* ME372CG charge current control algorithm:
           config the charge current only when
           Vbus is legal (a valid input voltage
           is present)
        */
        mutex_lock(&g_usb_state_lock);
        if (is_pad_present()) {
            g_usb_state = PAD_SUPPLY;
            charger_type = PAD_SUPPLY;
        }
        else
            charger_type = g_usb_state;
        dev_warn(&smb->client->dev,
            "%s: config current when inok interrupt\n", __func__);
        smb345_config_max_current(charger_type);
        mutex_unlock(&g_usb_state_lock);
    }

    pm_runtime_put_sync(&smb->client->dev);
    return IRQ_HANDLED;
}

#ifdef CONFIG_A400CG
static int pcb_id11_gpio_init(struct smb345_charger *smb)
{
    const struct smb345_charger_platform_data *pdata = smb->pdata;
    int ret;

    if (pdata->pcb_id11 < 0) {
        BAT_DBG_E("%s: fail to request PCB_ID11 gpio number\n", __func__);
        return -1;
    }

    ret = gpio_request_one(pdata->pcb_id11,
            GPIOF_IN,
            "PCB_ID11");
    if (ret < 0)
        BAT_DBG_E("%s: request PCB_ID11 gpio fail!\n", __func__);

    return ret;
}
bool is_A400CG_1200mA_device()
{
    if (!smb345_dev) {
        BAT_DBG_E("%s: smb345_dev is null "
            "due to driver probed isn't ready\n",
            __func__);
        return false;
    }

    /* check GPIO PCB_ID11 */
    if (smb345_dev->pdata->pcb_id11 >= 0)
        return gpio_get_value(smb345_dev->pdata->pcb_id11) ? false : true;
    return false;
}
EXPORT_SYMBOL(is_A400CG_1200mA_device);
bool is_A400CG_1600mA_device()
{
    if (!smb345_dev) {
        BAT_DBG_E("%s: smb345_dev is null "
            "due to driver probed isn't ready\n",
            __func__);
        return false;
    }

    /* check GPIO PCB_ID11 */
    if (smb345_dev->pdata->pcb_id11 >= 0)
        return gpio_get_value(smb345_dev->pdata->pcb_id11) ? true : false;
    return false;
}
EXPORT_SYMBOL(is_A400CG_1600mA_device);
#endif

#if defined(CONFIG_PF400CG) || defined(CONFIG_ME175CG) || defined(CONFIG_A400CG)
static int smb3xx_otg_gpio_init(struct smb345_charger *smb)
{
    const struct smb345_charger_platform_data *pdata = smb->pdata;
    int ret;

    if (pdata->gp_sdio_2_clk < 0) {
        BAT_DBG_E("%s: fail to request CHG_OTG gpio number\n", __func__);
        return -1;
    }

    ret = gpio_request_one(pdata->gp_sdio_2_clk,
            GPIOF_OUT_INIT_LOW,
            "smb346_otg");
    if (ret < 0)
        BAT_DBG_E("%s: request CHG_OTG gpio fail!\n", __func__);

    return ret;
}
#elif defined(CONFIG_Z580C)
static int smb3xx_otg_gpio_init(struct smb345_charger *smb)
{
    const struct smb345_charger_platform_data *pdata = smb->pdata;
    int ret;

    if (pdata->chg_otg < 0) {
        BAT_DBG_E("%s: fail to request chg_otg gpio number\n", __func__);
        return -1;
    }

    if ((Read_PROJ_ID() == PROJ_ID_Z580CA) && (HW_ID > HW_ID_PR)) {
        ret = gpio_request_one(152,
                GPIOF_OUT_INIT_HIGH,
                "smb346_otg");
        /* ++ Just for image verification, remove me in next release. */
        ret = gpio_request_one(87,
                GPIOF_OUT_INIT_HIGH,
                "smb346_otg_87");
        /* -- */
    }
    else {
    ret = gpio_request_one(pdata->chg_otg,
            GPIOF_OUT_INIT_LOW,
            "smb3xx_otg_en");
    }
    if (ret < 0)
        BAT_DBG_E("%s: request chg_otg gpio fail!\n", __func__);

    return ret;
}
#else
static int smb3xx_otg_gpio_init(struct smb345_charger *smb) { return 0; }
#endif

static int smb345_inok_gpio_init(struct smb345_charger *smb)
{
    const struct smb345_charger_platform_data *pdata = smb->pdata;
    int ret, irq = gpio_to_irq(pdata->inok_gpio);
    int gpio_usb0_id;

#ifdef CHRIS
    gpio_usb0_id = get_gpio_by_name("USB0_ID_LS");
    if (gpio_get_value(gpio_usb0_id))
        BAT_DBG(">>> USB0_ID (HIGH) <<<\n");
    else
        BAT_DBG(">>> USB0_ID (LOW) <<<\n");
#endif
    if (gpio_get_value(pdata->inok_gpio))
        BAT_DBG(">>> INOK (HIGH) <<<\n");
    else
        BAT_DBG(">>> INOK (LOW) <<<\n");

    ret = gpio_request_one(pdata->inok_gpio, GPIOF_IN, "smb345_inok");
    if (ret < 0) {
        BAT_DBG_E("%s: request INOK gpio fail!\n", __func__);
        goto fail;
    }

    ret = request_threaded_irq(irq, NULL, smb345_inok_interrupt,
                    IRQF_PERCPU | IRQF_NO_SUSPEND | IRQF_FORCE_RESUME |
                    IRQF_ONESHOT |
                    IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
                    smb->client->name,
                    smb);

    if (ret < 0) {
        BAT_DBG_E("%s: config INOK gpio as IRQ fail!\n", __func__);
        goto fail_gpio;
    }

    return 0;
fail_gpio:
    gpio_free(pdata->inok_gpio);
fail:
    smb->client->irq = 0;
    return ret;
}

static inline int smb345_irq_enable(struct smb345_charger *smb)
{
    return smb345_irq_set(smb, true);
}

static inline int smb345_irq_disable(struct smb345_charger *smb)
{
    return smb345_irq_set(smb, false);
}

static int smb345_irq_init(struct smb345_charger *smb)
{
    const struct smb345_charger_platform_data *pdata = smb->pdata;
    int ret;
#if 0
    int ret, irq = gpio_to_irq(pdata->irq_gpio);

    ret = gpio_request_one(pdata->irq_gpio, GPIOF_IN, smb->client->name);
    if (ret < 0)
        goto fail;

    ret = request_threaded_irq(irq, NULL, smb345_interrupt,
                   IRQF_TRIGGER_FALLING, smb->client->name,
                   smb);
    ret = request_threaded_irq(irq, NULL, smb345_interrupt,
                   IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
                   smb->client->name,
                   smb);
    if (ret < 0)
        goto fail_gpio;
#endif

    ret = smb345_set_writable(smb, true);
    if (ret < 0)
        goto fail;

    /*
     * Configure the STAT output to be suitable for interrupts: disable
     * all other output (except interrupts) and make it active low.
     */
    ret = smb345_read(smb, CFG_STAT);
    if (ret < 0)
        goto fail_readonly;

    ret &= ~CFG_STAT_ACTIVE_HIGH;
    ret |= CFG_STAT_DISABLED;

    ret = smb345_write(smb, CFG_STAT, ret);
    if (ret < 0)
        goto fail_readonly;

    ret = smb345_irq_enable(smb);
    if (ret < 0)
        goto fail_readonly;

#if 0
    smb345_set_writable(smb, false);
    smb->client->irq = irq;
    enable_irq_wake(smb->client->irq);
#endif
    return 0;

fail_readonly:
#if 0
    smb345_set_writable(smb, false);
fail_irq:
    free_irq(irq, smb);
fail_gpio:
    gpio_free(pdata->irq_gpio);
#endif
fail:
    smb->client->irq = 0;
    return ret;
}

bool smb345_has_charger_error(void)
{
    int ret, status;

    if (!smb345_dev)
        return -EINVAL;

    ret = smb345_read(smb345_dev, STAT_C);
    if (ret < 0)
        return true;

    if (ret & STAT_C_CHARGER_ERROR)
        return true;

    return false;
}

int smb345_get_charging_status(void)
{
    int ret, status;
    int irqstat_c;

    if (!smb345_dev)
        return -EINVAL;

    ret = smb345_read(smb345_dev, STAT_C);
    if (ret < 0)
        return ret;

    irqstat_c = smb345_read(smb345_dev, IRQSTAT_C);
    if (irqstat_c < 0)
        return irqstat_c;
#if 0
    dev_info(&smb345_dev->client->dev,
            "Charging Status: STAT_C:0x%x\n", ret);
#endif

    if ((ret & STAT_C_CHARGER_ERROR) ||
        (ret & STAT_C_HOLDOFF_STAT)) {
        /* set to NOT CHARGING upon charger error
         * or charging has stopped.
         */
        status = POWER_SUPPLY_STATUS_NOT_CHARGING;
    } else {
        if ((ret & STAT_C_CHG_MASK) >> STAT_C_CHG_SHIFT) {
            /* set to charging if battery is in pre-charge,
             * fast charge or taper charging mode.
             */
            status = POWER_SUPPLY_STATUS_CHARGING;
        } else if (ret & STAT_C_CHG_TERM) {
            /* set the status to FULL if battery is not in pre
             * charge, fast charge or taper charging mode AND
             * charging is terminated at least once.
             */
            status = POWER_SUPPLY_STATUS_FULL;
        } else {
            /* in this case no charger error or termination
             * occured but charging is not in progress!!!
             */
            status = POWER_SUPPLY_STATUS_NOT_CHARGING;
        }

        if (irqstat_c & IRQSTAT_C_TERMINATION_STAT)
            status = POWER_SUPPLY_STATUS_FULL;
    }

    return status;
}
//EXPORT_SYMBOL_GPL(smb345_get_charging_status);

#ifndef ME372CG_OTHER_BUILD
int smb345_dump_registers(struct seq_file *s)
{
    struct smb345_charger *smb;
    int ret;
    u8 reg;

    if (s) {
        smb = s->private;
    }
    else {
        if (!smb345_dev) {
            BAT_DBG(" %s: smb345_dev is null!\n",
                __func__);
            return -1;
        }
        else {
            smb = smb345_dev;
        }
    }

    BAT_DBG(" %s:\n", __func__);
    BAT_DBG(" Control registers:\n");
    BAT_DBG(" ==================\n");
    BAT_DBG(" #Addr\t#Value\n");

    for (reg = CFG_CHARGE_CURRENT; reg <= CFG_ADDRESS; reg++) {
        ret = smb345_read(smb, reg);
        BAT_DBG(" 0x%02x:\t" BYTETOBINARYPATTERN
            "\n", reg, BYTETOBINARY(ret));
        if (s)
            seq_printf(s, "0x%02x:\t" BYTETOBINARYPATTERN
                "\n", reg, BYTETOBINARY(ret));
    }
    BAT_DBG("\n");
    if (s)
        seq_printf(s, "\n");

    BAT_DBG(" Command registers:\n");
    BAT_DBG(" ==================\n");
    BAT_DBG(" #Addr\t#Value\n");
    if (s) {
        seq_printf(s, "Command registers:\n");
        seq_printf(s, "==================\n");
        seq_printf(s, "#Addr\t#Value\n");
    }

    ret = smb345_read(smb, CMD_A);
    BAT_DBG(" 0x%02x:\t" BYTETOBINARYPATTERN
        "\n", CMD_A, BYTETOBINARY(ret));
    if (s)
        seq_printf(s, "0x%02x:\t" BYTETOBINARYPATTERN
            "\n", CMD_A, BYTETOBINARY(ret));
    ret = smb345_read(smb, CMD_B);
    BAT_DBG(" 0x%02x:\t" BYTETOBINARYPATTERN
        "\n", CMD_B, BYTETOBINARY(ret));
    if (s)
        seq_printf(s, "0x%02x:\t" BYTETOBINARYPATTERN
            "\n", CMD_B, BYTETOBINARY(ret));
    ret = smb345_read(smb, CMD_C);
    BAT_DBG(" 0x%02x:\t" BYTETOBINARYPATTERN
        "\n", CMD_C, BYTETOBINARY(ret));
    if (s)
        seq_printf(s, "0x%02x:\t" BYTETOBINARYPATTERN
            "\n", CMD_C, BYTETOBINARY(ret));
    BAT_DBG("\n");
    if (s)
        seq_printf(s, "\n");

    BAT_DBG(" Interrupt status registers:\n");
    BAT_DBG(" ===========================\n");
    BAT_DBG(" #Addr\t#Value\n");
    if (s) {
        seq_printf(s, "Interrupt status registers:\n");
        seq_printf(s, "===========================\n");
        seq_printf(s, "#Addr\t#Value\n");
    }
    for (reg = IRQSTAT_A; reg <= IRQSTAT_F; reg++) {
        ret = smb345_read(smb, reg);
        BAT_DBG(" 0x%02x:\t" BYTETOBINARYPATTERN
            "\n", reg, BYTETOBINARY(ret));
        if (s)
            seq_printf(s, "0x%02x:\t" BYTETOBINARYPATTERN
                "\n", reg, BYTETOBINARY(ret));
    }
    BAT_DBG("\n");
    if (s)
        seq_printf(s, "\n");

    BAT_DBG(" Status registers:\n");
    BAT_DBG(" =================\n");
    BAT_DBG(" #Addr\t#Value\n");
    if (s) {
        seq_printf(s, "Status registers:\n");
        seq_printf(s, "=================\n");
        seq_printf(s, "#Addr\t#Value\n");
    }
    for (reg = STAT_A; reg <= STAT_E; reg++) {
        ret = smb345_read(smb, reg);
        BAT_DBG(" 0x%02x:\t" BYTETOBINARYPATTERN
            "\n", reg, BYTETOBINARY(ret));
        if (s)
            seq_printf(s, "0x%02x:\t" BYTETOBINARYPATTERN
                "\n", reg, BYTETOBINARY(ret));
    }

    return 0;
}
#else
int smb345_dump_registers(struct seq_file *s) { return 0; }
#endif

static int smb345_debugfs_show(struct seq_file *s, void *data)
{
    seq_printf(s, "Control registers:\n");
    seq_printf(s, "==================\n");
    seq_printf(s, "#Addr\t#Value\n");

    smb345_dump_registers(s);

    return 0;
}

static int smb345_debugfs_open(struct inode *inode, struct file *file)
{
    return single_open(file, smb345_debugfs_show, inode->i_private);
}

static const struct file_operations smb345_debugfs_fops = {
    .open        = smb345_debugfs_open,
    .read        = seq_read,
    .llseek        = seq_lseek,
    .release    = single_release,
};

#ifdef CONFIG_ME372CG_PMIC_SNB5072C1
static int smb345_debugfs_show_pmic(struct seq_file *s,
                    void *data)
{
    seq_printf(s, "PMIC battery-related registers:\n");
    seq_printf(s, "===============================\n");
    seq_printf(s, "#Addrs   #Value       #Name\n");

    pmic_dump_registers(MSIC_CHRG_REG_DUMP_INT |
            MSIC_CHRG_REG_DUMP_BOOT    |
            MSIC_CHRG_REG_DUMP_EVENT |
            MSIC_CHRG_REG_DUMP_OTHERS,
            s);

    return 0;
}

static int smb345_debugfs_open_pmic(struct inode *inode,
                    struct file *file)
{
    return single_open(file,
                    smb345_debugfs_show_pmic,
                    inode->i_private);
}

static const struct file_operations smb345_debugfs_fops2 = {
    .open        = smb345_debugfs_open_pmic,
    .read        = seq_read,
    .llseek        = seq_lseek,
    .release    = single_release,
};
#endif

static inline struct power_supply *get_psy_battery(void)
{
    struct class_dev_iter iter;
    struct device *dev;
    static struct power_supply *pst;

    class_dev_iter_init(&iter, power_supply_class, NULL, NULL);
    while ((dev = class_dev_iter_next(&iter))) {
        pst = (struct power_supply *)dev_get_drvdata(dev);
        if (pst->type == POWER_SUPPLY_TYPE_BATTERY) {
            class_dev_iter_exit(&iter);
            return pst;
        }
    }
    class_dev_iter_exit(&iter);

    return NULL;
}

static inline int get_battery_temperature(int *tempr)
{
    struct power_supply *psy;
    union power_supply_propval val;
    int ret;

    psy = get_psy_battery();
    if (!psy)
        return -EINVAL;

    ret = psy->get_property(psy, POWER_SUPPLY_PROP_TEMP_AMBIENT, &val);
    if (!ret)
        *tempr = val.intval;

    return ret;
}

static inline int get_battery_voltage(int *volt)
{
    struct power_supply *psy;
    union power_supply_propval val;
    int ret;

    psy = get_psy_battery();
    if (!psy)
        return -EINVAL;

    ret = psy->get_property(psy, POWER_SUPPLY_PROP_VOLTAGE_NOW, &val);
    if (!ret)
        *volt = val.intval / 1000;

    return ret;
}

static inline int get_battery_rsoc(int *rsoc)
{
    struct power_supply *psy;
    union power_supply_propval val;
    int ret;

    psy = get_psy_battery();
    if (!psy)
        return -EINVAL;

    ret = psy->get_property(psy, POWER_SUPPLY_PROP_CAPACITY, &val);
    if (!ret)
        *rsoc = val.intval;

    return ret;
}

#if defined(CONFIG_PF400CG) || defined(CONFIG_ME175CG) || defined(CONFIG_A400CG) || defined(CONFIG_A450CG)
/* only smb348/smb358 support it */
#if defined(CONFIG_ME175CG) || defined(CONFIG_A400CG)
static int smb345_recharge_voltage(charger_jeita_state_t charger_jeita_state)
{
    int ret;
    int VFLT_reg_value;

    #if defined(CONFIG_ME175CG)
    /* Write 01h[3:2]="10" or "01" */
    if (charger_jeita_state == OTS || charger_jeita_state == OTP)
        VFLT_reg_value = BIT(3);
    else //RTS or LTP
        VFLT_reg_value = BIT(2);
    #else //A400CG
        VFLT_reg_value = BIT(2);
    #endif

    ret = smb345_masked_write(smb345_dev->client,
            CFG_CURRENT_LIMIT,
            CFG_CURRENT_LIMIT_SMB358_CHARGER_INHIBIT_CONTROL_MASK,
            VFLT_reg_value);
    if (ret < 0)
        return ret;

    return 0;
}
#else
static int smb345_recharge_voltage(charger_jeita_state_t charger_jeita_state)
{ return 0; }
#endif

static int smb346_soc_detect_batt_tempr(int usb_state)
{
    int ret;
    int batt_tempr = 250;/* unit: C  */
    int batt_volt = 4000;/* unit: mV */
    int high_tempr_thrsd = STOP_CHARGING_HIGH_TEMPERATURE;
    int low_tempr_thrsd = STOP_CHARGING_LOW_TEMPERATURE;
    int rsoc;
    charger_jeita_state_t charger_jeita_state = smb345_dev->charger_jeita_state;
    int Vchg;

    if (usb_state != AC_IN && usb_state != USB_IN && usb_state != PAD_SUPPLY)
        return 0;

    /* acquire battery temperature here */
    ret = get_battery_temperature(&batt_tempr);
    if (ret) {
        BAT_DBG_E(" %s: fail to get battery temperature\n", __func__);
        return ret;
    }
    else
        BAT_DBG(" %s: get battery temperature(%d)\n", __func__, batt_tempr);

    /* acquire battery voltage here */
    ret = get_battery_voltage(&batt_volt);
    if (ret) {
        BAT_DBG_E(" %s: fail to get battery voltage\n", __func__);
        return ret;
    }
    else
        BAT_DBG(" %s: get battery voltage(%d)\n", __func__, batt_volt);
///////////////////////////////////////////////////////////////////////////////
    BAT_DBG(" %s: old charger_jeita_state = %d\n", __func__, charger_jeita_state);
    if (charger_jeita_state == OTP) {
        if (batt_tempr <= STOP_CHARGING_HIGH_TEMPERATURE - 30) { // 60 - 3 = 57
#if !defined(CONFIG_A450CG)
            /* Setup Charging Enable: 06h[6:5]="11" */
            smb345_charging_toggle(JEITA, true);
            /* set Bat_S = OTS */
            charger_jeita_state = OTS;
#else
            /* Check if Vchg == 4.2V "100011" && Vbat >= 4.0V */
            Vchg = smb345_get_soc_control_float_vol();
            if (Vchg == 0x23 && batt_volt >= 4000) {
                /* Setup Vchg=4.2V */
                smb3xx_soc_control_float_vol(HIGH_TEMPR);
                /* Setup Charging Disable: 06h[6:5]="00" */
                smb345_charging_toggle(JEITA, false);
                /* set Bat_S = OTP */
                charger_jeita_state = OTP;
            }
            else {
                /* Setup Vchg=4.0V */
                smb3xx_soc_control_float_vol(MIDDLE_TEMPR);
                /* Setup Charging Enable: 06h[6:5]="11" */
                smb345_charging_toggle(JEITA, true);
                /* set Bat_S = OTS */
                charger_jeita_state = OTS;
            }
#endif
        }
        else { /*Nothing to do*/ }
    }
    else { /* not OTP */
        if (charger_jeita_state == OTS) {
            if (batt_tempr <= STOP_CHARGING_HIGH_TEMPERATURE) {
                if (batt_tempr <= FLOAT_VOLTAGE_TEMPERATURE_THRESHOLD - 30) { // 45 - 3 = 42
                    /* Setup Vchg=4.35V */
                    smb3xx_soc_control_float_vol(LOW_TEMPR);
                    /* Setup Charging Enable: 06h[6:5]="11" */
                    smb345_charging_toggle(JEITA, true);
                    /* set Bat_S = RTS */
                    charger_jeita_state = RTS;
                    /* Recharge voltage : 4.15V */
                    smb345_recharge_voltage(charger_jeita_state);
                }
            }
            else {
                /* Setup Charging Disable: 06h[6:5]="00" */
                smb345_charging_toggle(JEITA, false);
                /* set Bat_S = OTP */
                charger_jeita_state = OTP;
            }
        }
        else { /* not OTS */
            if (charger_jeita_state == RTS) {
                if (batt_tempr <= FLOAT_VOLTAGE_TEMPERATURE_THRESHOLD) { // 45
                    if (batt_tempr <= STOP_CHARGING_LOW_TEMPERATURE) {
                        /* Setup Vchg=4.35V */
                        smb3xx_soc_control_float_vol(LOW_TEMPR);
                        /* Setup Charging Disable: 06h[6:5]="00" */
                        smb345_charging_toggle(JEITA, false);
                        /* set Bat_S = LTP */
                        charger_jeita_state = LTP;
                    }
                    else { /*Nothing to do*/ }
                }
                else {
#if !defined(CONFIG_A450CG)
                    /* Setup Vchg=4.11V */
                    smb3xx_soc_control_float_vol(HIGH_TEMPR);
                    /* Setup Charging Enable: 06h[6:5]="11" */
                    smb345_charging_toggle(JEITA, true);
                    /* set Bat_S = OTS */
                    charger_jeita_state = OTS;
                    /* Recharge voltage : 4.01V */
                    smb345_recharge_voltage(charger_jeita_state);
#else
                    /* Check if Vchg == 4.2V "100011" && Vbat >= 4.0V */
                    Vchg = smb345_get_soc_control_float_vol();
                    if (Vchg == 0x23 && batt_volt >= 4000) {
                        /* Setup Vchg=4.2V */
                        smb3xx_soc_control_float_vol(HIGH_TEMPR);
                        /* Setup Charging Disable: 06h[6:5]="00" */
                        smb345_charging_toggle(JEITA, false);
                        /* set Bat_S = OTP */
                        charger_jeita_state = OTP;
                    }
                    else {
                        /* Setup Vchg=4.0V "011001" */
                        smb3xx_soc_control_float_vol(MIDDLE_TEMPR);
                        /* Setup Charging Enable: 06h[6:5]="11" */
                        smb345_charging_toggle(JEITA, true);
                        /* set Bat_S = OTS */
                        charger_jeita_state = OTS;
                    }
#endif
                }
            }
            else { /* not RTS */
                if (batt_tempr <= STOP_CHARGING_LOW_TEMPERATURE + 30) { // 0 + 3 = 3
                    /* Setup Charging Disable: 06h[6:5]="00" */
                    smb345_charging_toggle(JEITA, false);
                    /* set Bat_S = LTP */
                    charger_jeita_state = LTP;
                }
                else {
                    /* Setup Vchg=4.35V */
                    smb3xx_soc_control_float_vol(LOW_TEMPR);
                    /* Setup Charging Enable: 06h[6:5]="11" */
                    smb345_charging_toggle(JEITA, true);
                    /* set Bat_S = RTS */
                    charger_jeita_state = RTS;
                    /* Recharge voltage : 4.15V */
                    smb345_recharge_voltage(charger_jeita_state);
                }
            }
        }
    }
    BAT_DBG(" %s: new charger_jeita_state = %d\n", __func__, charger_jeita_state);

#if defined(CONFIG_PF400CG)
    /* recovery to phone-only state */
    if (!is_pad_present())
        smb345_charging_toggle(BALANCE, true);
#endif

    /* confirm JEITA charging when not Low/High Temperature Protection */
    if (charger_jeita_state != LTP && charger_jeita_state != OTP)
        smb345_charging_toggle(JEITA, true);

    /* charger dump when JEITA status transistion */
    if (smb345_dev->charger_jeita_state != charger_jeita_state)
        smb345_dump_registers(NULL);

    smb345_dev->charger_jeita_state = charger_jeita_state;
    request_power_supply_changed();

    return ret;
}
#else
static int smb346_soc_detect_batt_tempr(int usb_state) { return 0; }
#endif

static int battery_id()
{
    /* read battery divided voltage */
    int bat_adc_value[1], bat_adc_channel[1];
    static int *bat_adc_handle;
    int ret, volt=0;

    bat_adc_channel[0] = 0x08 | CH_NEED_VREF | CH_NEED_VCALIB;
    if (!bat_adc_handle) {
        bat_adc_handle = gpadc_alloc_channels(1, bat_adc_channel);
        if (!bat_adc_handle) {
            BAT_DBG(" * fail to gpadc_alloc_channels *\n");
            return -1;
        }
    }

    ret = get_gpadc_sample(bat_adc_handle, 1, bat_adc_value);
    if (ret) {
        BAT_DBG(" * fail to get_gpadc_sample *\n");
        return -1;
    }

    volt = bat_adc_value[0];
    BAT_DBG("battery ID pin ADC value=0x%x, decimal value=%d, voltage=%dmV\n",
        bat_adc_value[0], volt, volt * 2400 / 1024);

    return (volt * 2400 / 1024);
}

void chrgr_type_work_func(struct work_struct *work)
{ setSMB345Charger(g_usb_state); }

#ifdef CONFIG_HAS_EARLYSUSPEND
static void smb3xx_early_suspend(struct early_suspend *h)
{
    struct smb345_charger *chip = container_of(h, struct smb345_charger, es);

    dev_info(&chip->client->dev, "enter %s\n", __func__);

    chip->screen_off = true;
    mutex_lock(&g_usb_state_lock);
    if (g_usb_state == AC_IN || g_usb_state == USB_IN || g_usb_state == PAD_SUPPLY)
    {
        smb345_charger_control_jeita();
        smb3xx_charger_control_jeita();
    }
    mutex_unlock(&g_usb_state_lock);
}

static void smb3xx_late_resume(struct early_suspend *h)
{
    struct smb345_charger *chip = container_of(h, struct smb345_charger, es);

    dev_info(&chip->client->dev, "enter %s\n", __func__);
    chip->screen_off = false;
    mutex_lock(&g_usb_state_lock);
    if (g_usb_state == AC_IN || g_usb_state == USB_IN || g_usb_state == PAD_SUPPLY)
    {
        smb345_soc_control_jeita();
        smb3xx_soc_control_jeita();
    }
    mutex_unlock(&g_usb_state_lock);
}

static void smb3xx_config_earlysuspend(struct smb345_charger *chip)
{
    chip->es.level = EARLY_SUSPEND_LEVEL_DISABLE_FB + 11;
    chip->es.suspend = smb3xx_early_suspend;
    chip->es.resume = smb3xx_late_resume;
    register_early_suspend(&chip->es);
}
#else
#define smb3xx_early_suspend NULL
#define smb3xx_late_resume NULL
static void smb3xx_config_earlysuspend(struct smb345_charger *chip) { return; }
#endif

static int smb345_probe(struct i2c_client *client,
            const struct i2c_device_id *id)
{
    const struct smb345_charger_platform_data *pdata;
    struct device *dev = &client->dev;
    struct smb345_charger *smb;
    int ret;
    int charger_type;

    BAT_DBG(" ++++++++++++++++ %s ++++++++++++++++\n", __func__);
    usb_ulpi_0_refclk = devm_ioremap_nocache(dev, USB_ULPI_0_REFCLK_BASE,
                                             USB_ULPI_0_REFCLK_LEN);
    if (!usb_ulpi_0_refclk) {
        BAT_DBG_E("%s: error in ioremap to get access to usb_ulpi_0_refclk\n", __func__);
    }
    set_gpio_value_usb_ulpi_0_refclk(1);

    pdata = dev->platform_data;
    if (!pdata)
        return -EINVAL;

    if (!pdata->use_mains && !pdata->use_usb)
        return -EINVAL;

    smb = devm_kzalloc(dev, sizeof(*smb), GFP_KERNEL);
    if (!smb)
        return -ENOMEM;

    i2c_set_clientdata(client, smb);

    smb->client = client;
    smb->pdata = pdata;

    /* init wake lock */
    wake_lock_init(&smb->wakelock,
        WAKE_LOCK_SUSPEND, "smb345_wakelock");

#ifdef CONFIG_UPI_BATTERY
    /* init wake lock in COS */
    if (entry_mode == 4 || entry_mode == 1) {
        BAT_DBG(" %s: wake lock init: asus_battery_power_wakelock\n",
            __func__);
        wake_lock_init(&wlock,
            WAKE_LOCK_SUSPEND,
            "asus_battery_power_wakelock");
        wake_lock_init(&wlock_t,
            WAKE_LOCK_SUSPEND,
            "asus_battery_power_wakelock_timeout");

        /* prevent system from entering s3 in COS
            while AC charger is connected
        */
        mutex_lock(&g_usb_state_lock);
        charger_type = g_usb_state;
        mutex_unlock(&g_usb_state_lock);

        if ((charger_type == AC_IN) || (is_pad_present())) {
            if (!wake_lock_active(&wlock)) {
                BAT_DBG(" %s: asus_battery_power_wakelock "
                    "-> wake lock\n", __func__);
                wake_lock(&wlock);
            }
        }
    }
#endif
    /* init charger jeita state by battery temperature to RTS */
    smb->charger_jeita_state = RTS;
    smb->charger_jeita_status = ROOM;

    /* enable register writing - chris */
    ret = smb345_set_writable(smb, true);
    if (ret < 0)
        return ret;

    smb345_dev = smb;
    smb345_dump_registers(NULL);

    /* Refer to SMB345 Application Note 72 to solve serious problems */
    ret = smb345_masked_write(smb->client,
            OTG_TLIM_THERM_CNTRL_REG,
            OTG_CURRENT_LIMIT_AT_USBIN_MASK,
            OTG_CURRENT_LIMIT_250mA);
    if (ret < 0)
        return ret;

    /* Init Runtime PM State */
    pm_runtime_put_noidle(&smb->client->dev);
    pm_schedule_suspend(&smb->client->dev, MSEC_PER_SEC);

//ME371MG only
#if 0
    /*
     * Interrupt pin is optional. If it is connected, we setup the
     * interrupt support here.
     */
    if (pdata->irq_gpio >= 0) {
        ret = smb345_irq_init(smb);
        if (ret < 0) {
            dev_warn(dev, "failed to initialize IRQ: %d\n", ret);
            dev_warn(dev, "disabling IRQ support\n");
        }
    }
#endif

    /* INOK pin configuration */
    if (pdata->inok_gpio >= 0) {
        ret = 0;//smb345_inok_gpio_init(smb);
        if (ret < 0) {
            dev_warn(dev,
                "fail to initialize INOK gpio: %d\n",
                ret);
        }
    }

    /* OTG pin configuration */
    ret = smb3xx_otg_gpio_init(smb);
    if (ret < 0)
        return ret;

    #ifdef CONFIG_A400CG
    /* A400CG GP_CORE_073 PCB_ID11 for 1200mA/1600mA board recognition */
    ret = pcb_id11_gpio_init(smb);
    if (ret < 0)
        return ret;
    a400cg_1200_1600 = is_A400CG_1200mA_device() ? 1200 : 1600;
    #endif

    smb->running = true;
    smb->dentry = debugfs_create_file("smb", S_IRUGO, NULL, smb,
                      &smb345_debugfs_fops);
    #ifdef CONFIG_ME372CG_PMIC_SNB5072C1
    smb->dentry = debugfs_create_file("pmic", S_IRUGO, NULL, smb,
                      &smb345_debugfs_fops2);
    #endif
    sysfs_create_group(&client->dev.kobj, &dev_attr_grp);

    ret = smb345_register_power_supply(&client->dev);
    if (ret < 0)
        return ret;

    BAT_DBG(" ++++++++++++++++ %s done ++++++++++++++++\n", __func__);

    /* Attention:
       A400CG need to decide the battery id. Then it could
       adjust the charging termination current by battery id.
       (set Termination Current to  80mA with the 1200mAh batttery
        set Termination Current to 100mA with the 1600mAh batttery)
       We must invoke check_battery_id() before smb345_config_max_current()
       is invoked.
    */
    #if defined(CONFIG_PF400CG) || defined(CONFIG_A400CG)
    check_battery_id();
    #endif
    /* Attention:
       A400CG need to recognize 1200/1600 device. So the PCB_ID11
       gpio initialization is necessary in early init. we invoke
       check_battery_match_device() after pcb_id11_gpio_init() and
       check_battery_id() are invoked.
    */
    #if defined(CONFIG_A400CG)
    check_battery_match_device();
    #endif
    /* ME372CG charge current control algorithm:
       config the charge current only when
       Vbus is legal (a valid input voltage
       is present)
    */
    mutex_lock(&g_usb_state_lock);
    charger_type = g_usb_state;
    verifyFW();
    dev_warn(&smb345_dev->client->dev,
        "%s: config current when probe\n", __func__);
    smb345_config_max_current(charger_type);
    mutex_unlock(&g_usb_state_lock);

    generate_key();
    //smb345_proc_fs_current_control();

#ifdef CONFIG_ME372CG_PMIC_SNB5072C1
    pmic_dump_registers(MSIC_CHRG_REG_DUMP_INT |
            MSIC_CHRG_REG_DUMP_BOOT |
            MSIC_CHRG_REG_DUMP_EVENT |
            MSIC_CHRG_REG_DUMP_OTHERS,
            NULL);
#endif

#ifndef CONFIG_A450CG
    smb345_tbl.soc_control_float_vol = smb345_soc_control_float_vol;
    smb345_tbl.get_soc_control_float_vol = smb345_get_soc_control_float_vol;
    smb345_tbl.charging_toggle = smb345_charging_toggle;
    smb345_tbl.dump_registers = smb345_dump_registers;
    smb345_tbl.jeita_control = smb3xx_jeita_control;
    smb345_tbl.aicl_control = aicl_cur_control;
    asus_register_power_supply_charger(dev, &smb345_tbl);
#endif

    #ifdef CONFIG_UPI_BATTERY
    ret = init_asus_charging_limit_toggle();
    if (ret) {
        BAT_DBG_E("Unable to create proc file\n");
        return ret;
    }
    #endif

    INIT_WORK(&smb345_dev->chrgr_type_work, chrgr_type_work_func);
    cable_status_register_client(&charger_type_notifier);

    smb3xx_config_earlysuspend(smb345_dev);
    smb345_dev->screen_off = false;
#ifdef CONFIG_ME372CG_PMIC_SNB5072C1
    BAT_DBG(" %s: battery id: %d\n", __func__, pmic_get_batt_id());
#endif
    return 0;
}

static bool loop_check_vbus_present()
{
    int LOOP = 10;
    int i;

    /* Check Vbus by check charger IC inok pin level */

    for (i=0; i<LOOP; i++) {
        if (gpio_get_value(smb345_dev->pdata->inok_gpio)) {
            BAT_DBG_E(" * Vbus is gone.\n");
            msleep(5);
        }
        else {
            BAT_DBG(" Vbus is present.\n");
            return true;
        }
    }

    return false;
}

static int charger_type_report(struct notifier_block *nb, unsigned long event, void *ptr)
{
    int ret;

    switch (event) {
    case POWER_SUPPLY_CHARGER_TYPE_NONE:
        BAT_DBG_E("-----------------------------------------------TYPE_NONE\n");
#ifdef ME372CG_ENG_BUILD
        flag_eng_stop_charging = false;
#endif
        /* wake lock to prevent system instantly
           enter S3 while it's in resuming flow */
        wake_lock_timeout(&smb345_dev->wakelock, HZ);
        g_usb_state = CABLE_OUT;
        schedule_work(&smb345_dev->chrgr_type_work);
        break;
    case POWER_SUPPLY_CHARGER_TYPE_USB_SDP:
        BAT_DBG_E("-----------------------------------------------USB_SDP\n");
        if (!loop_check_vbus_present()) break;
        g_usb_state = USB_IN;
        schedule_work(&smb345_dev->chrgr_type_work);
        break;
    case POWER_SUPPLY_CHARGER_TYPE_USB_DCP:
        BAT_DBG_E("-----------------------------------------------USB_DCP\n");
        if (!loop_check_vbus_present()) break;
        g_usb_state = AC_IN;
        schedule_work(&smb345_dev->chrgr_type_work);
        break;
    case POWER_SUPPLY_CHARGER_TYPE_USB_CDP:
        BAT_DBG_E("-----------------------------------------------USB_CDP\n");
        break;
    case POWER_SUPPLY_CHARGER_TYPE_USB_ACA:
        BAT_DBG_E("-----------------------------------------------USB_ACA\n");
        break;
    case POWER_SUPPLY_CHARGER_TYPE_AC:
        BAT_DBG_E("-----------------------------------------------AC\n");
        break;
    case POWER_SUPPLY_CHARGER_TYPE_ACA_DOCK:
        BAT_DBG_E("-----------------------------------------------ACA_DOCK\n");
        break;
    case POWER_SUPPLY_CHARGER_TYPE_ACA_A:
        BAT_DBG_E("-----------------------------------------------ACA_A\n");
        break;
    case POWER_SUPPLY_CHARGER_TYPE_ACA_B:
        BAT_DBG_E("-----------------------------------------------ACA_B\n");
        break;
    case POWER_SUPPLY_CHARGER_TYPE_ACA_C:
        BAT_DBG_E("-----------------------------------------------ACA_C\n");
        break;
    case POWER_SUPPLY_CHARGER_TYPE_SE1:
        BAT_DBG_E("-----------------------------------------------SE1\n");
        if (!loop_check_vbus_present()) break;
        g_usb_state = AC_IN;
        schedule_work(&smb345_dev->chrgr_type_work);
        break;
    case POWER_SUPPLY_CHARGER_TYPE_MHL:
        BAT_DBG_E("-----------------------------------------------MHL\n");
        break;
    case POWER_SUPPLY_CHARGER_TYPE_B_DEVICE:
        BAT_DBG_E("-----------------------------------------------B_DEVICE\n");
        g_usb_state = ENABLE_5V;
        schedule_work(&smb345_dev->chrgr_type_work);
        break;
    case POWER_SUPPLY_CHARGER_TYPE_OTG:
        BAT_DBG_E("-----------------------------------------------OTG\n");
        g_usb_state = ENABLE_5V;
        schedule_work(&smb345_dev->chrgr_type_work);
        break;
    case POWER_SUPPLY_CHARGER_TYPE_OTG_OUT:
        BAT_DBG_E("-----------------------------------------------OTG_OUT\n");
        g_usb_state = DISABLE_5V;
        schedule_work(&smb345_dev->chrgr_type_work);
        break;
    default:
        BAT_DBG_E("-----------------------------------------------FUCK *\n");
        break;
    }

    return NOTIFY_OK;
}

static bool check_valid_battery_id(valid_battery_id_t resistors)
{
    struct sfi_table_simple *sb;
    u8 batt_id_thres[6][2] = {0};
    int rett = 0;
    char *id_num_00;
    char *id_num_01;
    char *id_num_02;
    char *ID_STR[] = {
        "00ICDK",
        "01ICDK",
        "02ICDK"
    };
    int battery_id_resistor = resistors;

    sb = (struct sfi_table_simple *) get_oem0_table();
    if (!sb) {
        BAT_DBG_E("%s: *** UNKNOWN BATTERY: failed to get_oem0_table() ***\n",
            __func__);
        return false;
    }

    BAT_DBG("%s: Bid:%s Bsig:%s Blen:%d Brev:%d Bcsum:%d Boem_id:%s Boem_table_id:%s\n",
            __func__,
            (char *)sb->pentry,
            sb->header.sig,
            sb->header.len,
            sb->header.rev,
            sb->header.csum,
            sb->header.oem_id,
            sb->header.oem_table_id);

    id_num_00 = strstr(sb->header.oem_id, ID_STR[0]);
    if (id_num_00) {
        /* FW told that it match the threshold between B1IDMIN and B1IDMAX */
        rett = intel_scu_ipc_read_mip(batt_id_thres[0], ID_BYTES, B1IDMIN, 1);
        if (rett)
            BAT_DBG_E("%s: *** failed to read MIP at 0x%04X(%d) ***\n",
                __func__,
                B1IDMIN,
                rett);
        else
            BAT_DBG("%s: B1IDMIN=0x%X,0x%X\n", __func__,
                batt_id_thres[0][0], batt_id_thres[0][1]);

        rett = intel_scu_ipc_read_mip(batt_id_thres[1], ID_BYTES, B1IDMAX, 1);
        if (rett)
            BAT_DBG_E("%s: *** failed to read MIP 0x%04X(%d) ***\n",
                __func__,
                B1IDMAX,
                rett);
        else
            BAT_DBG("%s: B1IDMAX=0x%X,0x%X\n", __func__,
                batt_id_thres[1][0], batt_id_thres[1][1]);

        /* if battery_id_resistor satisfy the min/max range */
        if (battery_id_resistor >= batt_id_thres[0][0] &&
            battery_id_resistor <= batt_id_thres[1][0]) {
            BAT_DBG("%s: battery_id_resistor(%dK) is in range[0x%02X, 0x%02X]\n",
                __func__,
                battery_id_resistor,
                batt_id_thres[0][0],
                batt_id_thres[1][0]);
            return true;
        }
        else {
            BAT_DBG("%s: battery_id_resistor(%dK) is out of range[0x%02X, 0x%02X]\n",
                __func__,
                battery_id_resistor,
                batt_id_thres[0][0],
                batt_id_thres[1][0]);
            return false;
        }
    }

    id_num_01 = strstr(sb->header.oem_id, ID_STR[1]);
    if (id_num_01) {
        rett = intel_scu_ipc_read_mip(batt_id_thres[2], ID_BYTES, B2IDMIN, 1);
        if (rett)
            BAT_DBG_E("%s: *** failed to read MIP 0x%04X(%d) ***\n",
                __func__,
                B2IDMIN,
                rett);
        else
            BAT_DBG("%s: B2IDMIN=0x%X,0x%X\n", __func__,
                batt_id_thres[2][0], batt_id_thres[2][1]);

        rett = intel_scu_ipc_read_mip(batt_id_thres[3], ID_BYTES, B2IDMAX, 1);
        if (rett)
            BAT_DBG_E("%s: *** failed to read MIP 0x%04X(%d) ***\n",
                __func__,
                B2IDMAX,
                rett);
        else
            BAT_DBG("%s: B2IDMAX=0x%X,0x%X\n", __func__,
                batt_id_thres[3][0], batt_id_thres[3][1]);

        /* if battery_id_resistor satisfy the min/max range */
        if (battery_id_resistor >= batt_id_thres[2][0] &&
            battery_id_resistor <= batt_id_thres[3][0]) {
            BAT_DBG("%s: battery_id_resistor(%dK) is in range[0x%02X, 0x%02X]\n",
                __func__,
                battery_id_resistor,
                batt_id_thres[2][0],
                batt_id_thres[3][0]);
            return true;
        }
        else {
            BAT_DBG("%s: battery_id_resistor(%dK) is out of range[0x%02X, 0x%02X]\n",
                __func__,
                battery_id_resistor,
                batt_id_thres[2][0],
                batt_id_thres[3][0]);
            return false;
        }
    }

    id_num_02 = strstr(sb->header.oem_id, ID_STR[2]);
    if (id_num_02) {
        rett = intel_scu_ipc_read_mip(batt_id_thres[4], ID_BYTES, B3IDMIN, 1);
        if (rett)
            BAT_DBG_E("%s: *** failed to read MIP 0x%04X(%d) ***\n",
                __func__,
                B3IDMIN,
                rett);
        else
            BAT_DBG("%s: B3IDMIN=0x%X,0x%X\n", __func__,
                batt_id_thres[4][0], batt_id_thres[4][1]);

        rett = intel_scu_ipc_read_mip(batt_id_thres[5], ID_BYTES, B3IDMAX, 1);
        if (rett)
            BAT_DBG_E("%s: *** failed to read MIP 0x%04X(%d) ***\n",
                __func__,
                B3IDMAX,
                rett);
        else
            BAT_DBG("%s: B3IDMAX=0x%X,0x%X\n", __func__,
                batt_id_thres[5][0], batt_id_thres[5][1]);

        /* if battery_id_resistor satisfy the min/max range */
        if (battery_id_resistor >= batt_id_thres[4][0] &&
            battery_id_resistor <= batt_id_thres[5][0]) {
            BAT_DBG("%s: battery_id_resistor(%dK) is in range[0x%02X, 0x%02X]\n",
                __func__,
                battery_id_resistor,
                batt_id_thres[4][0],
                batt_id_thres[5][0]);
            return true;
        }
        else {
            BAT_DBG("%s: battery_id_resistor(%dK) is out of range[0x%02X, 0x%02X]\n",
                __func__,
                battery_id_resistor,
                batt_id_thres[4][0],
                batt_id_thres[5][0]);
            return false;
        }
    }

    BAT_DBG_E("%s: *** UNKNOWN BATTERY: what the hell are you? ***\n",
        __func__);

    return false;
}
static bool id_51k = false;
static bool id_100k = false;
static bool id_10k = false;
bool is_battery_id_51k() { return id_51k; }
bool is_battery_id_100k() { return id_100k; }
bool is_battery_id_10k() { return id_10k; }
static void check_battery_id()
{
#if defined(CONFIG_PF400CG)
    id_51k = check_valid_battery_id(ID51K);
    id_100k = check_valid_battery_id(ID100K);
#elif defined(CONFIG_A400CG)
    id_51k = check_valid_battery_id(ID51K);
    id_100k = check_valid_battery_id(ID100K);
    id_10k = check_valid_battery_id(ID10K);
#endif
}
#if defined(CONFIG_A400CG)
static char* battery_match_device;
static void check_battery_match_device()
{
    /* PASS if 1200mAh battery insert into 1200 device */
    if (is_A400CG_1200mA_device()) {
        if (id_51k || id_100k) {
            battery_match_device = "PASS";
            return;
        }
    }

    /* PASS if 1600mAh battery insert into 1600 device */
    if (is_A400CG_1600mA_device()) {
        if (id_10k) {
            battery_match_device = "PASS";
            return;
        }
    }

    /* battery does not match device */
    battery_match_device = "FAIL";
}
#endif

static int smb345_remove(struct i2c_client *client)
{
    struct smb345_charger *smb = i2c_get_clientdata(client);

    if (!IS_ERR_OR_NULL(smb->dentry))
        debugfs_remove(smb->dentry);

    smb->running = false;

#if 0
    if (client->irq) {
        smb345_irq_disable(smb);
        free_irq(client->irq, smb);
        gpio_free(smb->pdata->irq_gpio);
    }
#endif

    wake_lock_destroy(&smb->wakelock);
    pm_runtime_get_noresume(&smb->client->dev);

    return 0;
}

static int smb345_shutdown(struct i2c_client *client)
{
    dev_info(&client->dev, "%s\n", __func__);

#if defined(CONFIG_PF400CG) || defined(CONFIG_ME175CG) || defined(CONFIG_A400CG)
    //cancel_delayed_work(&smb345_dev->aicl_dete_work);
#endif

    /* JEITA function by charger protection */
    mutex_lock(&g_usb_state_lock);
    if (g_usb_state == AC_IN || g_usb_state == USB_IN || g_usb_state == PAD_SUPPLY)
    {
        smb345_charger_control_jeita();
        smb3xx_charger_control_jeita();
    }
    mutex_unlock(&g_usb_state_lock);

    /* registers dump */
    smb345_dump_registers(NULL);

    return 0;
}

#ifdef CONFIG_PM
static int smb345_prepare(struct device *dev)
{
    struct smb345_charger *smb = dev_get_drvdata(dev);

    dev_info(&smb->client->dev, "smb345 suspend\n");
    /*
     * disable irq here doesn't mean smb345 interrupt
     * can't wake up system. smb345 interrupt is triggered
     * by GPIO pin, which is always active.
     * When resume callback calls enable_irq, kernel
     * would deliver the buffered interrupt (if it has) to
     * driver.
     */
    if (smb->client->irq > 0)
        disable_irq(smb->client->irq);

#if 0
    /* JEITA function by charger protection */
    mutex_lock(&g_usb_state_lock);
    if (g_usb_state == AC_IN || g_usb_state == USB_IN || g_usb_state == PAD_SUPPLY)
    {
        smb345_charger_control_jeita();
        smb3xx_charger_control_jeita();
    }
    mutex_unlock(&g_usb_state_lock);
#endif

    return 0;
}

static void smb345_complete(struct device *dev)
{
    struct smb345_charger *smb = dev_get_drvdata(dev);

    dev_info(&smb->client->dev, "smb345 resume\n");

#if 0
    /* JEITA function by soc protection */
    mutex_lock(&g_usb_state_lock);
    if (!gpio_get_value(smb345_dev->pdata->inok_gpio) && (g_usb_state == AC_IN || g_usb_state == USB_IN))
    {
        smb345_soc_control_jeita();
        smb3xx_soc_control_jeita();
    }
    mutex_unlock(&g_usb_state_lock);
#endif
}
#else
#define smb345_prepare NULL
#define smb345_complete NULL
#endif

#ifdef CONFIG_PM_RUNTIME
static int smb345_runtime_suspend(struct device *dev)
{
    dev_info(dev, "%s called\n", __func__);
    return 0;
}

static int smb345_runtime_resume(struct device *dev)
{
    dev_info(dev, "%s called\n", __func__);
    return 0;
}

static int smb345_runtime_idle(struct device *dev)
{

    dev_info(dev, "%s called\n", __func__);
    return 0;
}
#else
#define smb345_runtime_suspend    NULL
#define smb345_runtime_resume    NULL
#define smb345_runtime_idle    NULL
#endif

static const struct i2c_device_id smb345_id[] = {
    {SMB345_DEV_NAME, 0},
    {},
};
MODULE_DEVICE_TABLE(i2c, smb345_id);

static const struct dev_pm_ops smb345_pm_ops = {
    .prepare        = smb345_prepare,
    .complete        = smb345_complete,
    .runtime_suspend    = smb345_runtime_suspend,
    .runtime_resume        = smb345_runtime_resume,
    .runtime_idle        = smb345_runtime_idle,
};

static struct i2c_driver smb345_driver = {
    .driver = {
        .name    = SMB345_DEV_NAME,
        .owner    = THIS_MODULE,
        .pm    = &smb345_pm_ops,
    },
    .probe        = smb345_probe,
    .remove        = smb345_remove,
    .shutdown    = smb345_shutdown,
    .id_table    = smb345_id,
};

static int __init smb345_init(void)
{
    BAT_DBG(" ++++++++++++++++ %s ++++++++++++++++\n", __func__);
    HW_ID = Read_HW_ID();

    /* check if cpu can take the control from sdcv pmic */
    if (sd_cv_sw_take_control())
        return -EAGAIN;

    return i2c_add_driver(&smb345_driver);
}
module_init(smb345_init);

static void __exit smb345_exit(void)
{
    i2c_del_driver(&smb345_driver);
}
module_exit(smb345_exit);

MODULE_AUTHOR("Bruce E. Robertson <bruce.e.robertson@intel.com>");
MODULE_AUTHOR("Mika Westerberg <mika.westerberg@linux.intel.com>");
MODULE_AUTHOR("Chris Chang <chris1_chang@asus.com>");
MODULE_DESCRIPTION("SMB345 battery charger driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("i2c:smb345");

#if defined(CONFIG_A400CG)
module_param(a400cg_1200_1600, int, 0444);
MODULE_PARM_DESC(a400cg_1200_1600, "Acquire the correct device for A400CG.");
module_param(battery_match_device, charp, 0444);
MODULE_PARM_DESC(battery_match_device, "Check battery match device for A400CG.");
#endif
