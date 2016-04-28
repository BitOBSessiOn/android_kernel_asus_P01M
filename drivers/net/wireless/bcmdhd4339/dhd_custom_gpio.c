/*
* Customer code to add GPIO control during WLAN start/stop
* $Copyright Open Broadcom Corporation$
*
* $Id: dhd_custom_gpio.c 493822 2014-07-29 13:20:26Z $
*/

#include <typedefs.h>
#include <linuxver.h>
#include <osl.h>
#include <bcmutils.h>
#include <dngl_stats.h>
#include <dhd.h>
#include <dhd_linux.h>
#include <linux/gpio.h>

#include <wlioctl.h>
#include <wl_iw.h>

#define WL_ERROR(x) printf x
#define WL_TRACE(x)

#if defined(CUSTOMER_HW2)

#if defined(PLATFORM_MPS)
int __attribute__ ((weak)) wifi_get_fw_nv_path(char *fw, char *nv) { return 0;};
#endif

#endif 

#if defined(OOB_INTR_ONLY)

#if defined(BCMLXSDMMC)
extern int sdioh_mmc_irq(int irq);
#endif /* (BCMLXSDMMC)  */

#if defined(CUSTOMER_HW3) || defined(PLATFORM_MPS)
#include <mach/gpio.h>
#endif

/* Customer specific Host GPIO defintion  */
static int dhd_oob_gpio_num = -1;

module_param(dhd_oob_gpio_num, int, 0644);
MODULE_PARM_DESC(dhd_oob_gpio_num, "DHD oob gpio number");

/* This function will return:
 *  1) return :  Host gpio interrupt number per customer platform
 *  2) irq_flags_ptr : Type of Host interrupt as Level or Edge
 *
 *  NOTE :
 *  Customer should check his platform definitions
 *  and his Host Interrupt spec
 *  to figure out the proper setting for his platform.
 *  Broadcom provides just reference settings as example.
 *
 */
int dhd_customer_oob_irq_map(void *adapter, unsigned long *irq_flags_ptr)
{
	int  host_oob_irq = 0;

#if defined(CUSTOMER_HW2) && !defined(PLATFORM_MPS)
	host_oob_irq = wifi_platform_get_irq_number(adapter, irq_flags_ptr);

#else
#if defined(CUSTOM_OOB_GPIO_NUM)
	if (dhd_oob_gpio_num < 0) {
		dhd_oob_gpio_num = CUSTOM_OOB_GPIO_NUM;
	}
#endif /* CUSTOMER_OOB_GPIO_NUM */

	if (dhd_oob_gpio_num < 0) {
		WL_ERROR(("%s: ERROR customer specific Host GPIO is NOT defined \n",
		__FUNCTION__));
		return (dhd_oob_gpio_num);
	}

	WL_ERROR(("%s: customer specific Host GPIO number is (%d)\n",
	         __FUNCTION__, dhd_oob_gpio_num));

#if defined CUSTOMER_HW3 || defined(PLATFORM_MPS)
	gpio_request(dhd_oob_gpio_num, "oob irq");
	host_oob_irq = gpio_to_irq(dhd_oob_gpio_num);
	gpio_direction_input(dhd_oob_gpio_num);
#endif /* defined CUSTOMER_HW3 || defined(PLATFORM_MPS) */
#endif 
	host_oob_irq = gpio_to_irq(dhd_oob_gpio_num);
	return (host_oob_irq);
}
#endif 

/* Customer function to control hw specific wlan gpios */
int
dhd_customer_gpio_wlan_ctrl(void *adapter, int onoff)
{
	int err = 0;

	return err;
}

#ifdef GET_CUSTOM_MAC_ENABLE
/* Function to get custom MAC address */
int
dhd_custom_get_mac_address(void *adapter, unsigned char *buf)
{
	int ret = 0;

	WL_TRACE(("%s Enter\n", __FUNCTION__));
	if (!buf)
		return -EINVAL;

	/* Customer access to MAC address stored outside of DHD driver */
#if (defined(CUSTOMER_HW2) || defined(CUSTOMER_HW10)) && (LINUX_VERSION_CODE >= \
	KERNEL_VERSION(2, 6, 35))
	ret = wifi_platform_get_mac_addr(adapter, buf);
#endif

#ifdef EXAMPLE_GET_MAC
	/* EXAMPLE code */
	{
		struct ether_addr ea_example = {{0x00, 0x11, 0x22, 0x33, 0x44, 0xFF}};
		bcopy((char *)&ea_example, buf, sizeof(struct ether_addr));
	}
#endif /* EXAMPLE_GET_MAC */

	return ret;
}
#endif /* GET_CUSTOM_MAC_ENABLE */

/* Customized Locale table : OPTIONAL feature */
const struct cntry_locales_custom translate_custom_table[] = {
/* Table should be filled out based on custom platform regulatory requirement */
//CY+ ASUS's translate_custom_table
//special country code
	{"XY", "XY", 7},        //for WW SKU(include US)
	{"TW", "TW", 16},       //for TW SKU
	{"JP", "JP", 3},        //for JP SKU
	{"CN", "CN", 11},       //for CN SKU
	{"HK", "HK", 0},        //for HK SIM
	{"SG", "SG", 0},        //for SG SIM

	{"XZ", "XZ", 1},
	{"US", "US", 109},
	{"CA", "US", 109},

	{"EU", "GB", 4},
	{"AT", "GB", 4},
	{"BE", "GB", 4},
	{"BG", "GB", 4},
	{"CY", "GB", 4},
	{"CZ", "GB", 4},
	{"DK", "GB", 4},
	{"EE", "GB", 4},
	{"FI", "GB", 4},
	{"FR", "GB", 4},
	{"DE", "GB", 4},
	{"GR", "GB", 4},
	{"HU", "GB", 4},
	{"IE", "GB", 4},
	{"IT", "GB", 4},
	{"LV", "GB", 4},
	{"LI", "GB", 4},
	{"LT", "GB", 4},
	{"LU", "GB", 4},
	{"MT", "GB", 4},
	{"NL", "GB", 4},
	{"PL", "GB", 4},
	{"PT", "GB", 4},
	{"RO", "GB", 4},
	{"SK", "GB", 4},
	{"SI", "GB", 4},
	{"ES", "GB", 4},
	{"SE", "GB", 4},
	{"GB", "GB", 4},

	{"AU", "AU", 0},
	{"AR", "AR", 0},
	{"MX", "MX", 0},
	{"IL", "IL", 0},
	{"CH", "CH", 0},
	{"NO", "NO", 0},
	{"KR", "KR", 25},
	{"BR", "KR", 25},

	{"AQ", "XY", 7},
	{"AX", "XY", 7},
	{"BL", "XY", 7},
	{"BV", "XY", 7},
	{"CC", "XY", 7},
	{"CU", "XY", 7},
	{"GL", "XY", 7},
	{"GS", "XY", 7},
	{"HM", "XY", 7},
	{"IR", "XY", 7},
	{"KP", "XY", 7},
	{"MH", "XY", 7},
	{"PN", "XY", 7},
	{"PS", "XY", 7},
	{"SD", "XY", 7},
	{"SH", "XY", 7},
	{"SJ", "XY", 7},
	{"SY", "XY", 7},
	{"TK", "XY", 7},
	{"TL", "XY", 7},

	{"ID", "ID", 1},
	{"RU", "XY", 7},       //for connecting to 802.11n
};

//CY+ ASUS BCM43362 country code translation table
const struct cntry_locales_custom translate_custom_table_bcm43362[] = {
//BCM43362 only support 2.4G, so only 3 choice(1-11/1-13/1-14)
	//XV/0(1-11 active, 12-14 passive)
	{"XY",   "XV", 0}, //for WW SKU

	{"US",   "US", 0}, //for US SIM
	{"ID",   "ID", 0}, //for ID SIM
	{"IL",   "IL", 0}, //for IL SIM
	{"TW",   "TW", 0}, //for TW SKU
	{"HK",   "HK", 0}, //for HK SIM
	{"SG",   "SG", 0}, //for SG SIM
	{"CN",   "CN", 0}, //for CN SKU

	//no available channel for these country code in 43362 FW, use default country XV/0
	{"AD",   "XV", 0},
	{"AQ",   "XV", 0},
	{"BV",   "XV", 0},
	{"CC",   "XV", 0},
	{"CK",   "XV", 0},
	{"GL",   "XV", 0},
	{"GS",   "XV", 0},
	{"HM",   "XV", 0},
	{"IR",   "XV", 0},
	{"KP",   "XV", 0},
	{"MH",   "XV", 0},
	{"PN",   "XV", 0},
	{"PS",   "XV", 0},
	{"SD",   "XV", 0},
	{"SH",   "XV", 0},
	{"SJ",   "XV", 0},
	{"SY",   "XV", 0},
	{"TK",   "XV", 0},
	{"TL",   "XV", 0},

	{"RU",   "XV", 0},      //for connecting to 802.11n

	//special case(rev!=0)
	{"JP",   "JP", 1}, //for JP SKU
};

//CY+ ASUS BCM4343s country code translation table
const struct cntry_locales_custom translate_custom_table_bcm43430[] = {
//BCM4343s only support 2.4G, so only 3 choice(1-11/1-13/1-14)
	//XV/0(1-11 active, 12-14 passive)
	{"XY",   "XV", 0}, //for WW SKU

	{"US",   "US", 0}, //for US SIM
	{"ID",   "ID", 0}, //for ID SIM
	{"IL",   "IL", 0}, //for IL SIM
	{"TW",   "TW", 0}, //for TW SKU
	{"HK",   "HK", 0}, //for HK SIM
	{"SG",   "SG", 0}, //for SG SIM
	{"CN",   "CN", 0}, //for CN SKU

	//no available channel for these country code in 4343s FW, use default country XV/0
	{"AQ",   "XV", 0},
	{"AX",   "XV", 0},
	{"BL",   "XV", 0},
	{"BV",   "XV", 0},
	{"CC",   "XV", 0},
	{"CU",   "XV", 0},
	{"GL",   "XV", 0},
	{"GS",   "XV", 0},
	{"HM",   "XV", 0},
	{"IR",   "XV", 0},
	{"KP",   "XV", 0},
	{"KR",   "XV", 0},
	{"MH",   "XV", 0},
	{"PN",   "XV", 0},
	{"PS",   "XV", 0},
	{"SD",   "XV", 0},
	{"SH",   "XV", 0},
	{"SJ",   "XV", 0},
	{"SY",   "XV", 0},
	{"TK",   "XV", 0},
	{"TL",   "XV", 0},

	//special case(rev!=0)
	{"JP",   "JP", 1}, //for JP SKU

	//for connecting to 802.11n
	{"RU",   "XV", 0},
};

const struct cntry_locales_custom translate_custom_table_bcm4339[] = {
/* Table should be filled out based on custom platform regulatory requirement */
//An+ ASUS's translate_custom_table
//special country code
	{"XY", "XV", 10},        //for WW SKU(include US)
	{"XV", "XV", 10},
	{"TW", "TW", 1},       //for TW SKU
	{"JP", "JP", 45},        //for JP SKU
	{"CN", "CN", 38},       //for CN SKU
	{"HK", "HK", 0},
	{"SG", "SG", 0},

	{"XZ", "XZ", 1},
	{"US", "US", 118},
	{"CA", "CA", 2},

	{"EU", "GB", 6},
	{"AT", "GB", 6},
	{"BE", "GB", 6},
	{"BG", "GB", 6},
	{"CY", "GB", 6},
	{"CZ", "GB", 6},
	{"DK", "GB", 6},
	{"EE", "GB", 6},
	{"FI", "GB", 6},
	{"FR", "GB", 6},
	{"DE", "GB", 6},
	{"GR", "GB", 6},
	{"HU", "GB", 6},
	{"IE", "GB", 6},
	{"IT", "GB", 6},
	{"LV", "GB", 6},
	{"LI", "GB", 6},
	{"LT", "GB", 6},
	{"LU", "GB", 6},
	{"MT", "GB", 6},
	{"NL", "GB", 6},
	{"PL", "GB", 6},
	{"PT", "GB", 6},
	{"RO", "GB", 6},
	{"SK", "GB", 6},
	{"SI", "GB", 6},
	{"ES", "GB", 6},
	{"SE", "GB", 6},
	{"GB", "GB", 6},

	{"AU", "AU", 0},
	{"AR", "AR", 0},
	{"MX", "MX", 0},
	{"IL", "IL", 0},
	{"CH", "CH", 0},
	{"NO", "NO", 0},
	{"KR", "KR", 25},
	{"BR", "KR", 25},

	{"AQ", "XY", 7},
	{"AX", "XY", 7},
	{"BL", "XY", 7},
	{"BV", "XY", 7},
	{"CC", "XY", 7},
	{"CU", "XY", 7},
	{"GL", "XY", 7},
	{"GS", "XY", 7},
	{"HM", "XY", 7},
	{"IR", "XY", 7},
	{"KP", "XY", 7},
	{"MH", "XY", 7},
	{"PN", "XY", 7},
	{"PS", "XY", 7},
	{"SD", "XY", 7},
	{"SH", "XY", 7},
	{"SJ", "XY", 7},
	{"SY", "XY", 7},
	{"TK", "XY", 7},
	{"TL", "XY", 7},

	{"ID", "ID", 1},
	{"RU", "XY", 7},       //for connecting to 802.11n
};

/* Customized Locale convertor
*  input : ISO 3166-1 country abbreviation
*  output: customized cspec
*/
#ifdef CHIPNUM
void get_customized_country_code(void *adapter, char *country_iso_code, wl_country_t *cspec, uint chipnum)
#else
void get_customized_country_code(void *adapter, char *country_iso_code, wl_country_t *cspec)
#endif
{
#if 0 && defined(CUSTOMER_HW2) && (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 39))
	struct cntry_locales_custom *cloc_ptr;

	if (!cspec)
		return;

	cloc_ptr = wifi_platform_get_country_code(adapter, country_iso_code);
	if (cloc_ptr) {
		strlcpy(cspec->ccode, cloc_ptr->custom_locale, WLC_CNTRY_BUF_SZ);
		cspec->rev = cloc_ptr->custom_locale_rev;
	}
	return;
#else
	int size, i;

	const struct cntry_locales_custom* translate_table;
	translate_table = translate_custom_table;
	//Cannot get array size from sizeof() for pointor!!!
	size = ARRAYSIZE(translate_custom_table);

#ifdef CHIPNUM
	//BCM4343s use it's own table
	if(chipnum == 43430){
		//printk("############get_customized_country_code: use translate_custom_table_bcm43430");
		translate_table = translate_custom_table_bcm43430;
		//Cannot get array size from sizeof() for pointor!!!
		size = ARRAYSIZE(translate_custom_table_bcm43430);
	}else if (chipnum == 43362){
		//printk("############get_customized_country_code: use translate_custom_table_bcm43362");
		translate_table = translate_custom_table_bcm43362;
		//Cannot get array size from sizeof() for pointor!!!
		size = ARRAYSIZE(translate_custom_table_bcm43362);
	}else if (chipnum == 0x4339) {
		//printk("############get_customized_country_code: use translate_custom_table_bcm43362");
                translate_table = translate_custom_table_bcm4339;
                //Cannot get array size from sizeof() for pointor!!!
                size = ARRAYSIZE(translate_custom_table_bcm4339);
	}
#endif

	if (cspec == 0)
		 return;

	if (size == 0)
		 return;

	for (i = 0; i < size; i++) {
		if (strcmp(country_iso_code, translate_table[i].iso_abbrev) == 0) {
			memcpy(cspec->ccode,
				translate_table[i].custom_locale, WLC_CNTRY_BUF_SZ);
			cspec->rev = translate_table[i].custom_locale_rev;
			return;
		}
	}

//CY+ always use translate_custom_table
//#ifdef EXAMPLE_TABLE
	/* if no country code matched return first universal code from translate_custom_table */
	memcpy(cspec->ccode, translate_table[0].custom_locale, WLC_CNTRY_BUF_SZ);
	cspec->rev = translate_table[0].custom_locale_rev;
//#endif /* EXMAPLE_TABLE */
//CY-


	return;
#endif /* defined(CUSTOMER_HW2) && (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36)) */
}
