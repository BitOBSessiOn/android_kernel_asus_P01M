/*
 * rt5512.c  --  ALC5512 Smart DMIC bridge driver
 *
 * Copyright 2014 Realtek Semiconductor Corp.
 * Author: Oder Chiou <oder_chiou@realtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/firmware.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/debugfs.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <linux/gpio.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "rt5512.h"

//#define RT5512_USE_CUSTOM_FIRMWARE
#define RT5512_USE_CUSTOM_TRAINING_DATA
//#define RT5512_USE_NATIVE
//#define RT5512_DUMP_MEM
//#define RT5512_DUMP_BUF

#define VERSION "0.0.3"

static struct i2c_client *rt5512_i2c;
static const struct firmware *rt5512_fw = NULL;
static struct delayed_work reload_work;
static bool rt5512_start_rest = false;
static bool rt5512_use_amic = false;

static int rt5512_write(struct i2c_client *i2c, unsigned int reg,
	unsigned int value)
{
	u8 data[3];
	int ret;

	data[0] = reg & 0xff;
	data[1] = (0xff00 & value) >> 8;
	data[2] = value & 0xff;

	dev_dbg(&i2c->dev, "write %02x = %04x\n", reg, value);

	ret = i2c_master_send(i2c, data, 3);
	if (ret == 3)
		return 0;
	if (ret < 0)
		return ret;
	else
		return -EIO;
}

static int rt5512_read(struct i2c_client *i2c, unsigned int r)
{
	struct i2c_msg xfer[2];
	u8 reg[1];
	u8 data[2];
	int value = 0x0;
	int ret;

	/* Write register */
	reg[0] = r & 0xff;
	xfer[0].addr = i2c->addr;
	xfer[0].flags = 0;
	xfer[0].len = 1;
	xfer[0].buf = &reg[0];

	/* Read data */
	xfer[1].addr = i2c->addr;
	xfer[1].flags = I2C_M_RD;
	xfer[1].len = 2;
	xfer[1].buf = data;

	ret = i2c_transfer(i2c->adapter, xfer, 2);
	if (ret != 2) {
		dev_err(&i2c->dev, "i2c_transfer() returned %d\n", ret);
		return -EIO;
	}

	value = (data[0] << 8) | data[1];

	dev_dbg(&i2c->dev, "read %02x => %04x\n", r, value);

	return value;
}

static int rt5512_update_bits(struct i2c_client *i2c, unsigned int reg,
	unsigned int mask, unsigned int value)
{
	bool change;
	unsigned int old, new;
	int ret;

	ret = rt5512_read(i2c, reg);
	if (ret < 0)
		return ret;

	old = ret;
	new = (old & ~mask) | (value & mask);
	change = old != new;
	if (change)
		ret = rt5512_write(i2c, reg, new);

	if (ret < 0)
		return ret;

	return change;
}

static int rt5512_dsp_burst_write(struct i2c_client *i2c,
	unsigned int addr, const u8 *buf, size_t size)
{
	u8 data[5];
	unsigned int pos = 0;
	int ret;
	
	rt5512_write(i2c, RT5512_DSP_MEM_CTRL1, addr & 0xffff);
	rt5512_write(i2c, RT5512_DSP_MEM_CTRL2, (addr & 0xffff0000) >> 16);

	while (size > pos) {
		data[0] = RT5512_DSP_MEM_CTRL7;
		data[1] = buf[pos + 1];
		data[2] = buf[pos + 0];
		data[3] = buf[pos + 3];
		data[4] = buf[pos + 2];

		ret = i2c_master_send(i2c, data, ARRAY_SIZE(data));
		if (ret < 0)
			return ret;

		pos += 4;
	}

	return 0;
}

static unsigned int rt5512_dsp_read(struct i2c_client *i2c,
	unsigned int addr)
{
	unsigned int ret;
	int value;

	rt5512_write(i2c, RT5512_DSP_MEM_CTRL1, addr & 0xffff);
	rt5512_write(i2c, RT5512_DSP_MEM_CTRL2, (addr & 0xffff0000) >> 16);
	rt5512_write(i2c, RT5512_DSP_MEM_CTRL5, 0x02);

	value = rt5512_read(i2c, RT5512_DSP_MEM_CTRL3);
	ret = value;

	value= rt5512_read(i2c, RT5512_DSP_MEM_CTRL4);
	ret |= value << 16;

	dev_dbg(&i2c->dev, "%08x: %08x\n", addr, ret);

	return ret;
}

void rt5512_dsp_one_shot(void) {
	/* M1/N1/P1/Q1 Option */
	printk("%s()\n", __func__);
	rt5512_write(rt5512_i2c, RT5512_PLL_CLOCK_CTRL3, 0x0701);
	rt5512_write(rt5512_i2c, RT5512_UPFILTER_CTRL1, 0x6aaf);
	rt5512_write(rt5512_i2c, RT5512_UPFILTER_CTRL1, 0x6eaf);
	rt5512_write(rt5512_i2c, RT5512_PWR_DIG, 0x0003);
	rt5512_write(rt5512_i2c, RT5512_DMIC_DATA_CTRL, 0x0091);
	rt5512_write(rt5512_i2c, RT5512_DSP_CTRL2, 0x0000);
}
EXPORT_SYMBOL_GPL(rt5512_dsp_one_shot);

void rt5512_enable_dsp_clock(void) {
	if (rt5512_use_amic) {
		rt5512_write(rt5512_i2c, RT5512_AUTO_MODE_CTRL, 0x00c0);
		rt5512_write(rt5512_i2c, RT5512_PWR_ANLG1, 0x0bbf);
		rt5512_write(rt5512_i2c, RT5512_PWR_DSP, 0x0005);
		rt5512_write(rt5512_i2c, RT5512_DIG_PAD_CTRL2, 0x0400);
		rt5512_write(rt5512_i2c, RT5512_ANA_CIRCUIT_CTRL_MICBST, 0x2000);
		rt5512_write(rt5512_i2c, RT5512_ANA_CIRCUIT_CTRL_ADCFED, 0x0800);
		rt5512_write(rt5512_i2c, RT5512_ADC_EXT_CTRL1, 0x0045);
		rt5512_write(rt5512_i2c, RT5512_UPFILTER_CTRL2, 0x0000);
		rt5512_write(rt5512_i2c, RT5512_ANA_CIRCUIT_CTRL_INPUTBUF, 0x0083);
		rt5512_write(rt5512_i2c, RT5512_AD_DIG_FILTER_CTRL1, 0x8a37);
		rt5512_write(rt5512_i2c, RT5512_DUMMY_RTK4, 0x0000);
	} else {
		rt5512_write(rt5512_i2c, RT5512_AUTO_MODE_CTRL, 0x00c0);
		rt5512_write(rt5512_i2c, RT5512_PWR_ANLG1, 0x8ba1);
		rt5512_write(rt5512_i2c, RT5512_ANA_CIRCUIT_CTRL_LDO2, 0xa342);
		rt5512_write(rt5512_i2c, RT5512_PWR_DSP, 0x0005);
		rt5512_write(rt5512_i2c, RT5512_DIG_PAD_CTRL2, 0x0400);
		rt5512_write(rt5512_i2c, RT5512_DUMMY_RTK3, 0x6395);
		rt5512_write(rt5512_i2c, RT5512_DUMMY_RTK4, 0x0000);
	}
}

void rt5512_set_clock_cal_pll(void) {
	/* M1 Option */
	rt5512_write(rt5512_i2c, RT5512_PLL_CAL_CTRL8, 0x0022);
	if (rt5512_use_amic) {
		rt5512_write(rt5512_i2c, RT5512_PLL_CLK_EXT_CTRL1, 0x0008);
		rt5512_write(rt5512_i2c, RT5512_PLL_CLK_EXT_CTRL2, 0x4202);
	} else {
		rt5512_write(rt5512_i2c, RT5512_PLL_CLK_EXT_CTRL1, 0x0040);
		rt5512_write(rt5512_i2c, RT5512_PLL_CLK_EXT_CTRL2, 0x2202);
	}
	rt5512_write(rt5512_i2c, RT5512_PLL_CLOCK_CTRL1, 0x0e06);
	rt5512_write(rt5512_i2c, RT5512_PLL_CLOCK_CTRL2, 0x00d3);
	rt5512_write(rt5512_i2c, RT5512_PLL_CLOCK_CTRL3, 0x0704);
	rt5512_write(rt5512_i2c, RT5512_BUF_MODE_CTRL_PLL_CAL1, 0x8040);
	rt5512_write(rt5512_i2c, RT5512_BUF_MODE_CTRL_PLL_CAL2, 0x0200);
	rt5512_write(rt5512_i2c, RT5512_PLL_CAL_CTRL1, 0x0000);
	rt5512_write(rt5512_i2c, RT5512_PLL_CAL_CTRL2, 0x01f4);
	rt5512_write(rt5512_i2c, RT5512_PLL_CAL_CTRL8, 0x8022);
	rt5512_write(rt5512_i2c, RT5512_PLL_CAL_CTRL8, 0xc022);
}

void rt5512_dsp_start_first(void) {
	printk("%s()\n", __func__);
	rt5512_write(rt5512_i2c, RT5512_PWR_ANLG1, 0x8ba1);
	rt5512_write(rt5512_i2c, RT5512_AUTO_MODE_CTRL, 0x00c0);
	rt5512_write(rt5512_i2c, RT5512_DSP_CTRL1, 0x0000);
	rt5512_write(rt5512_i2c, RT5512_GPIO_CTRL1, 0xa800);
	rt5512_write(rt5512_i2c, RT5512_PWR_DSP, 0x0005);
	rt5512_write(rt5512_i2c, RT5512_PWR_DSP, 0x0007);
	rt5512_write(rt5512_i2c, RT5512_PWR_DSP, 0x0005);
	rt5512_write(rt5512_i2c, RT5512_PWR_DSP, 0x0004);
	rt5512_write(rt5512_i2c, RT5512_BUF_SRAM_CTRL7, 0x8000);
	rt5512_write(rt5512_i2c, RT5512_AD_DIG_FILTER_CTRL2, 0x0022);
	rt5512_write(rt5512_i2c, RT5512_PWR_DSP, 0x0001);
	rt5512_write(rt5512_i2c, RT5512_PWR_DSP, 0x0019);
	rt5512_write(rt5512_i2c, RT5512_PWR_DSP, 0x0018);
	rt5512_write(rt5512_i2c, RT5512_PWR_DIG, 0x0001);
	rt5512_write(rt5512_i2c, RT5512_BUF_SRAM_CTRL6, 0x003a);
	rt5512_write(rt5512_i2c, RT5512_BUF_SRAM_CTRL7, 0x0000);
	rt5512_write(rt5512_i2c, RT5512_VAD_CTRL1, 0x11c0);
	rt5512_write(rt5512_i2c, RT5512_VAD_STATUS1, 0x8000);
//	rt5512_write(rt5512_i2c, RT5512_AD_DIG_FILTER_CTRL1, 0x8a2f);	//boost = 24dB
	rt5512_write(rt5512_i2c, RT5512_VAD_CTRL2, 0x0000);		//VAD sensitivity 
	rt5512_write(rt5512_i2c, RT5512_VAD_CTRL3, 0x0001);		//VAD sensitivity 
	rt5512_write(rt5512_i2c, RT5512_VAD_CTRL4, 0x7c0a);
	rt5512_write(rt5512_i2c, RT5512_VAD_STATUS1, 0x0000);
	rt5512_write(rt5512_i2c, RT5512_DMIC_DATA_CTRL, 0x0090);
	rt5512_write(rt5512_i2c, RT5512_PLL_CAL_CTRL8, 0x8022);
	rt5512_write(rt5512_i2c, RT5512_PLL_CAL_CTRL8, 0xc022);
}

void rt5512_dsp_start_rest(void) {
	printk("%s()\n", __func__);
	if (rt5512_use_amic) {
		/* M10 Option */
		rt5512_write(rt5512_i2c, RT5512_AD_DIG_FILTER_CTRL1, 0x8a37);
		rt5512_write(rt5512_i2c, RT5512_DSP_CTRL2, 0x0000);
		rt5512_write(rt5512_i2c, RT5512_AUTO_MODE_CTRL, 0x00c0);
		rt5512_write(rt5512_i2c, RT5512_PWR_ANLG1, 0x0bbf);
		rt5512_write(rt5512_i2c, RT5512_PWR_ANLG2, 0x3f00);
		rt5512_write(rt5512_i2c, RT5512_ANA_CIRCUIT_CTRL_VREF, 0x8e50);
		rt5512_write(rt5512_i2c, RT5512_PWR_DSP, 0x0005);
		rt5512_write(rt5512_i2c, RT5512_PWR_DSP, 0x0004);
		rt5512_write(rt5512_i2c, RT5512_BUF_SRAM_CTRL1, 0x0fff);
		rt5512_write(rt5512_i2c, RT5512_BUF_SRAM_CTRL7, 0x8000);
		rt5512_write(rt5512_i2c, RT5512_PLL_CLOCK_CTRL1, 0x0e06);
		rt5512_write(rt5512_i2c, RT5512_PLL_CLOCK_CTRL2, 0x00d3);
		rt5512_write(rt5512_i2c, RT5512_PLL_CLOCK_CTRL3, 0x0704);
		rt5512_write(rt5512_i2c, RT5512_PLL_CLK_EXT_CTRL1, 0x0008);
		rt5512_write(rt5512_i2c, RT5512_PLL_CLK_EXT_CTRL2, 0x4202);
		rt5512_write(rt5512_i2c, RT5512_AD_DIG_FILTER_CTRL2, 0x0021);
		rt5512_write(rt5512_i2c, RT5512_PWR_DSP, 0x0001);
		rt5512_write(rt5512_i2c, RT5512_PWR_DSP, 0x0019);
		rt5512_write(rt5512_i2c, RT5512_PWR_DSP, 0x0018);
		rt5512_write(rt5512_i2c, RT5512_PWR_DIG, 0x0001);
		rt5512_write(rt5512_i2c, RT5512_BUF_SRAM_CTRL6, 0x003a);
		rt5512_write(rt5512_i2c, RT5512_BUF_SRAM_CTRL7, 0x0000);
		rt5512_write(rt5512_i2c, RT5512_VAD_CTRL1, 0x21c0);
		rt5512_write(rt5512_i2c, RT5512_VAD_STATUS1, 0x8000);
		rt5512_write(rt5512_i2c, RT5512_VAD_STATUS1, 0x0000);
		rt5512_write(rt5512_i2c, RT5512_DMIC_DATA_CTRL, 0x0090);
		rt5512_write(rt5512_i2c, RT5512_PLL_CAL_CTRL8, 0x8022);
		rt5512_write(rt5512_i2c, RT5512_PLL_CAL_CTRL8, 0xc022);
	} else {
		/* M1 Option */
		rt5512_write(rt5512_i2c, RT5512_DSP_CTRL2, 0x0000);
		rt5512_write(rt5512_i2c, RT5512_PWR_ANLG1, 0x8ba1);
		rt5512_write(rt5512_i2c, RT5512_AUTO_MODE_CTRL, 0x00c0);
		rt5512_write(rt5512_i2c, RT5512_DSP_CTRL1, 0x0000);
		rt5512_write(rt5512_i2c, RT5512_PWR_DSP, 0x0005);
		rt5512_write(rt5512_i2c, RT5512_PWR_DSP, 0x0004);
		rt5512_write(rt5512_i2c, RT5512_BUF_SRAM_CTRL1, 0x0fff);
		rt5512_write(rt5512_i2c, RT5512_BUF_SRAM_CTRL7, 0x8000);
		rt5512_write(rt5512_i2c, RT5512_PLL_CLOCK_CTRL3, 0x0704);
		rt5512_write(rt5512_i2c, RT5512_PLL_CLK_EXT_CTRL2, 0x2202);
		rt5512_write(rt5512_i2c, RT5512_PWR_DSP, 0x0001);
		rt5512_write(rt5512_i2c, RT5512_PWR_DSP, 0x0019);
		rt5512_write(rt5512_i2c, RT5512_PWR_DSP, 0x0018);
		rt5512_write(rt5512_i2c, RT5512_PWR_DIG, 0x0001);
		rt5512_write(rt5512_i2c, RT5512_BUF_SRAM_CTRL6, 0x003a);
		rt5512_write(rt5512_i2c, RT5512_BUF_SRAM_CTRL7, 0x0000);
		rt5512_write(rt5512_i2c, RT5512_VAD_CTRL1, 0x11c0);
		rt5512_write(rt5512_i2c, RT5512_VAD_STATUS1, 0x8000);
		rt5512_write(rt5512_i2c, RT5512_VAD_STATUS1, 0x0000);
		rt5512_write(rt5512_i2c, RT5512_DMIC_DATA_CTRL, 0x0090);
		rt5512_write(rt5512_i2c, RT5512_AD_DIG_FILTER_CTRL1, 0x8a2f);	//boost = 24dB
		rt5512_write(rt5512_i2c, RT5512_PLL_CAL_CTRL8, 0x8022);
		rt5512_write(rt5512_i2c, RT5512_PLL_CAL_CTRL8, 0xc022);
	}
}

void rt5512_dsp_start(void) {
	if (rt5512_start_rest) {
		rt5512_dsp_start_rest();
	} else {
		rt5512_dsp_start_first();
		rt5512_start_rest = true;
	}
}
EXPORT_SYMBOL_GPL(rt5512_dsp_start);

void rt5512_dsp_stop(void) {
	printk("%s()\n", __func__);
	if (rt5512_use_amic) {
		/* T10 Option */
		rt5512_write(rt5512_i2c, RT5512_AUTO_MODE_CTRL, 0x03c1);
		rt5512_write(rt5512_i2c, RT5512_PWR_ANLG1, 0x0bbf);
		rt5512_write(rt5512_i2c, RT5512_PWR_ANLG2, 0xfcff);
		rt5512_write(rt5512_i2c, RT5512_ANA_CIRCUIT_CTRL_VREF, 0x8f50);
		rt5512_write(rt5512_i2c, RT5512_VAD_STATUS1, 0x8000);
		rt5512_write(rt5512_i2c, RT5512_DSP_CTRL2, 0x0000);
		rt5512_write(rt5512_i2c, RT5512_PWR_DSP, 0x0019);
		rt5512_write(rt5512_i2c, RT5512_VAD_CTRL1, 0x1040);
		rt5512_write(rt5512_i2c, RT5512_BUF_SRAM_CTRL6, 0x002a);
		rt5512_write(rt5512_i2c, RT5512_PWR_DIG, 0x0000);
		rt5512_write(rt5512_i2c, RT5512_PLL_CLOCK_CTRL1, 0x0e06);
		rt5512_write(rt5512_i2c, RT5512_PLL_CLOCK_CTRL2, 0x8011);
		rt5512_write(rt5512_i2c, RT5512_PLL_CLK_EXT_CTRL1, 0x0000);
		rt5512_write(rt5512_i2c, RT5512_PLL_CLK_EXT_CTRL2, 0x9208);
		rt5512_write(rt5512_i2c, RT5512_PLL_CAL_CTRL8, 0x8022);
		rt5512_write(rt5512_i2c, RT5512_PLL_CAL_CTRL8, 0xc022);
		rt5512_write(rt5512_i2c, RT5512_DMIC_DATA_CTRL, 0x009f);
	} else {
		rt5512_write(rt5512_i2c, RT5512_VAD_STATUS1, 0x8000);
		rt5512_write(rt5512_i2c, RT5512_DSP_CTRL2, 0x0000);
		rt5512_write(rt5512_i2c, RT5512_PWR_DSP, 0x0019);
		rt5512_write(rt5512_i2c, RT5512_AUTO_MODE_CTRL, 0x03c1);
		rt5512_write(rt5512_i2c, RT5512_PLL_CLK_EXT_CTRL2, 0x2008);
		rt5512_write(rt5512_i2c, RT5512_DMIC_DATA_CTRL, 0x0092);
		rt5512_write(rt5512_i2c, RT5512_PWR_DIG, 0x0000);
		rt5512_write(rt5512_i2c, RT5512_VAD_CTRL1, 0x1040);
		rt5512_write(rt5512_i2c, RT5512_BUF_SRAM_CTRL6, 0x002a);
		rt5512_write(rt5512_i2c, RT5512_PWR_ANLG1, 0x88a0);
	}
}
EXPORT_SYMBOL_GPL(rt5512_dsp_stop);

static size_t rt5512_read_file(char *file_path, const u8 **buf)
{
	loff_t pos = 0;
	unsigned int file_size = 0;
	struct file *fp;

	fp = filp_open(file_path, O_RDONLY, 0);
	if (!IS_ERR(fp)) {
		file_size = vfs_llseek(fp, pos, SEEK_END);

		*buf = kzalloc(file_size, GFP_KERNEL);
		if (*buf == NULL) {
			filp_close(fp, 0);
			return 0;
		}

		kernel_read(fp, pos, (char *)*buf, file_size);
		filp_close(fp, 0);

		return file_size;
	}

	return 0;
}

static void rt5512_write_file(char *file_path, const u8 *buf, size_t size,
	loff_t pos)
{
	struct file *fp;
	ssize_t res;

	fp = filp_open(file_path, O_CREAT | O_WRONLY, 0);
	if (!IS_ERR(fp)) {
		res = kernel_write(fp, (char *)buf, size, pos);
		filp_close(fp, 0);
	}
}

static unsigned int rt5512_4byte_le_to_uint(const u8 *data)
{
	return data[0] | data[1] << 8 | data[2] << 16 | data[3] << 24;
}

static void rt5512_uint_to_4byte_le(unsigned int value, u8 *data)
{
		data[0] = (value & 0xff);
		data[1] = ((value & 0xff00) >> 8);
		data[2] = ((value & 0xff0000) >> 16);
		data[3] = ((value & 0xff000000) >> 24);
}

void rt5512_parse_header(const u8 *buf)
{
	SMicFWHeader sMicFWHeader;
	SMicFWSubHeader sMicFWSubHeader;

	int i, offset = 0;
	const u8 *data;
	char file_path[32];
	size_t size;
	int count = 0;

	sMicFWHeader.Sync = rt5512_4byte_le_to_uint(buf);
	dev_dbg(&rt5512_i2c->dev, "sMicFWHeader.Sync = %08x\n", sMicFWHeader.Sync);

	offset += 4;
	sMicFWHeader.Version =  rt5512_4byte_le_to_uint(buf + offset);
	dev_dbg(&rt5512_i2c->dev, "sMicFWHeader.Version = %08x\n", sMicFWHeader.Version);

	offset += 4;
	sMicFWHeader.NumBin = rt5512_4byte_le_to_uint(buf + offset);
	dev_dbg(&rt5512_i2c->dev, "sMicFWHeader.NumBin = %08x\n", sMicFWHeader.NumBin);

	sMicFWHeader.BinArray =
		kzalloc(sizeof(SMicBinInfo) * sMicFWHeader.NumBin, GFP_KERNEL);

	for (i = 0 ; i < sMicFWHeader.NumBin; i++) {
		offset += 4;
		sMicFWHeader.BinArray[i].Offset = rt5512_4byte_le_to_uint(buf + offset);
		dev_dbg(&rt5512_i2c->dev, "sMicFWHeader.BinArray[%d].Offset = %08x\n", i, sMicFWHeader.BinArray[i].Offset);

		offset += 4;
		sMicFWHeader.BinArray[i].Size = rt5512_4byte_le_to_uint(buf + offset);
		dev_dbg(&rt5512_i2c->dev, "sMicFWHeader.BinArray[%d].Size = %08x\n", i, sMicFWHeader.BinArray[i].Size);

		offset += 4;
		sMicFWHeader.BinArray[i].Addr = rt5512_4byte_le_to_uint(buf + offset);
		dev_dbg(&rt5512_i2c->dev, "sMicFWHeader.BinArray[%d].Addr = %08x\n", i, sMicFWHeader.BinArray[i].Addr);

		rt5512_dsp_burst_write(rt5512_i2c, sMicFWHeader.BinArray[i].Addr,
			buf + sMicFWHeader.BinArray[i].Offset, sMicFWHeader.BinArray[i].Size);

#ifdef RT5512_DUMP_BUF
		if ((sMicFWHeader.BinArray[i].Addr & 0xffff0000) == 0x0fff0000)
			rt5512_write_file("/sdcard/rt5512/0x0fff0000_fw", buf + sMicFWHeader.BinArray[i].Offset, sMicFWHeader.BinArray[i].Size, sMicFWHeader.BinArray[i].Addr - 0x0fff0000);
		else
			rt5512_write_file("/sdcard/rt5512/0x0ffe0000_fw", buf + sMicFWHeader.BinArray[i].Offset, sMicFWHeader.BinArray[i].Size, sMicFWHeader.BinArray[i].Addr - 0x0ffe0000);
#endif
	}

	offset += 4;
	sMicFWSubHeader.NumTD = rt5512_4byte_le_to_uint(buf + offset);
	dev_dbg(&rt5512_i2c->dev, "sMicFWSubHeader.NumTD = %08x\n", sMicFWSubHeader.NumTD);

	sMicFWSubHeader.TDArray = 
		kzalloc(sizeof(SMicTDInfo) * sMicFWSubHeader.NumTD, GFP_KERNEL);

	for (i = 0 ; i < sMicFWSubHeader.NumTD; i++) {
		offset += 4;
		sMicFWSubHeader.TDArray[i].ID = rt5512_4byte_le_to_uint(buf + offset);
		dev_dbg(&rt5512_i2c->dev, "sMicFWSubHeader.TDArray[%d].ID = %08x\n", i, sMicFWSubHeader.TDArray[i].ID);

		offset += 4;
		sMicFWSubHeader.TDArray[i].Addr = rt5512_4byte_le_to_uint(buf + offset);
		dev_dbg(&rt5512_i2c->dev, "sMicFWSubHeader.TDArray[%d].Addr = %08x\n", i, sMicFWSubHeader.TDArray[i].Addr);

#ifdef RT5512_USE_CUSTOM_TRAINING_DATA
		sprintf(file_path, RT5512_CUSTOM_FIRMWARE "SMicTD%u.dat",
			sMicFWSubHeader.TDArray[i].ID);

		while (true) {
			size = rt5512_read_file(file_path, &data);
			dev_dbg(&rt5512_i2c->dev, "training firmware path = %s (%u)\n", file_path, size);

			if (size || count >= 5)
				break;

			msleep(1000);
			count++;
		}

		if (size) {
			rt5512_dsp_burst_write(rt5512_i2c,
				sMicFWSubHeader.TDArray[i].Addr, data, size);
#ifdef RT5512_DUMP_BUF
			if ((sMicFWSubHeader.TDArray[i].Addr & 0xffff0000) == 0x0fff0000)
				rt5512_write_file("/sdcard/rt5512/0x0fff0000_fw", data, size, sMicFWSubHeader.TDArray[i].Addr - 0x0fff0000);
			else
				rt5512_write_file("/sdcard/rt5512/0x0ffe0000_fw", data, size, sMicFWSubHeader.TDArray[i].Addr - 0x0ffe0000);
#endif
			kfree(data);
			continue;
		}
#endif
		sprintf(file_path, RT5512_FIRMWARE "SMicTD%u.dat",
			sMicFWSubHeader.TDArray[i].ID);

		size = rt5512_read_file(file_path, &data);

		if (size) {
			rt5512_dsp_burst_write(rt5512_i2c,
				sMicFWSubHeader.TDArray[i].Addr, data, size);
#ifdef RT5512_DUMP_BUF
			if ((sMicFWSubHeader.TDArray[i].Addr & 0xffff0000) == 0x0fff0000)
				rt5512_write_file("/sdcard/rt5512/0x0fff0000_fw", data, size, sMicFWSubHeader.TDArray[i].Addr - 0x0fff0000);
			else
				rt5512_write_file("/sdcard/rt5512/0x0ffe0000_fw", data, size, sMicFWSubHeader.TDArray[i].Addr - 0x0ffe0000);
#endif
			kfree(data);
		}
	}

	if (sMicFWHeader.BinArray)
		kfree(sMicFWHeader.BinArray);

	if (sMicFWSubHeader.TDArray)
		kfree(sMicFWSubHeader.TDArray);
}

static void rt5512_reset(struct i2c_client *i2c)
{
	rt5512_write(i2c, RT5512_RESET, 0x10ec);
}

static void rt5512_dump_mem(struct i2c_client *i2c)
{
	unsigned int i, value;
	char data[4];

	for (i = 0 ; i < 0xc000; i += 4) {
		value = rt5512_dsp_read(i2c, 0x0ffe0000 + i);
		rt5512_uint_to_4byte_le(value, (u8 *)&data);
		rt5512_write_file("/sdcard/rt5512/0x0ffe0000", data, 4, i);
	}

	for (i = 0 ; i < 0x6000; i += 4) {
		value = rt5512_dsp_read(i2c, 0x0fff0000 + i);
		rt5512_uint_to_4byte_le(value, (u8 *)&data);
		rt5512_write_file("/sdcard/rt5512/0x0fff0000", data, 4, i);
	}
}

static void rt5512_dsp_load_fw(void) {
#ifdef RT5512_USE_NATIVE
	const struct firmware *fw = NULL;
#else
	const u8 *data;
	size_t size = 0;
#ifdef RT5512_USE_CUSTOM_FIRMWARE
	char file_path[32];
	int count = 0;
#endif
	if (rt5512_fw == NULL)
		request_firmware(&rt5512_fw, "SMicBin.dat", &rt5512_i2c->dev);
#endif

	rt5512_reset(rt5512_i2c);

	rt5512_enable_dsp_clock();
#ifdef RT5512_USE_NATIVE
	request_firmware(&fw, RT5512_FIRMWARE1, &rt5512_i2c->dev);
	if (fw) {
		rt5512_dsp_burst_write(rt5512_i2c, 0x0fff0000, fw->data, fw->size);
		release_firmware(fw);
		fw = NULL;
	}

	request_firmware(&fw, RT5512_FIRMWARE2, &rt5512_i2c->dev);
	if (fw) {
		rt5512_dsp_burst_write(rt5512_i2c, 0x0ffe0000, fw->data, fw->size);
		release_firmware(fw);
		fw = NULL;
	}
#else
#ifdef RT5512_USE_CUSTOM_FIRMWARE
	sprintf(file_path, RT5512_CUSTOM_FIRMWARE "SMicBin.dat");

	while (true) {
		size = rt5512_read_file(file_path, &data);
		dev_dbg(&rt5512_i2c->dev, "firmware path = %s (%u)\n", file_path, size);

		if (size || count >= 5)
			break;

		msleep(1000);
		count++;
	}
#endif
	if (size) {
		rt5512_parse_header(data);
		kfree(data);
	} else {
		if (rt5512_fw)
			rt5512_parse_header(rt5512_fw->data);
	}
#endif
#ifdef RT5512_DUMP_MEM
	rt5512_dump_mem(rt5512_i2c);
#endif
	rt5512_set_clock_cal_pll();
	rt5512_dsp_start();
	rt5512_dsp_stop();

	release_firmware(rt5512_fw);
	rt5512_fw = NULL;

	dev_dbg(&rt5512_i2c->dev, "firmware loading end\n");
}

static void rt5512_fw_loaded(const struct firmware *fw, void *context)
{
	if (fw) {
		rt5512_fw = fw;
		rt5512_dsp_load_fw();
	}

}

static int rt5512_readable_register(unsigned int reg)
{
	switch (reg) {
	case RT5512_RESET:
	case RT5512_ANA_CIRCUIT_CTRL_LDO1:
	case RT5512_ANA_CIRCUIT_CTRL_LDO2:
	case RT5512_ANA_CIRCUIT_CTRL_LDO3:
	case RT5512_ANA_CIRCUIT_CTRL_ADC1_1:
	case RT5512_ANA_CIRCUIT_CTRL_ADC1_2:
	case RT5512_ANA_CIRCUIT_CTRL_ADC2_1:
	case RT5512_ANA_CIRCUIT_CTRL_ADC2_2:
	case RT5512_ANA_CIRCUIT_CTRL_ADC2_3:
	case RT5512_ANA_CIRCUIT_CTRL_MICBST:
	case RT5512_ANA_CIRCUIT_CTRL_ADCFED:
	case RT5512_ANA_CIRCUIT_CTRL_INPUTBUF:
	case RT5512_ANA_CIRCUIT_CTRL_VREF:
	case RT5512_ANA_CIRCUIT_CTRL_MBIAS:
	case RT5512_AD_DIG_FILTER_CTRL1:
	case RT5512_AD_DIG_FILTER_CTRL2:
	case RT5512_DFT_BIST_SCAN:
	case RT5512_UPFILTER_CTRL1:
	case RT5512_UPFILTER_CTRL2:
	case RT5512_GPIO_CTRL1:
	case RT5512_GPIO_CTRL2:
	case RT5512_GPIO_CTRL3:
	case RT5512_GPIO_STATUS:
	case RT5512_DIG_PAD_CTRL1:
	case RT5512_DIG_PAD_CTRL2:
	case RT5512_DMIC_DATA_CTRL:
	case RT5512_TEST_MODE_CTRL1:
	case RT5512_TEST_MODE_CTRL2:
	case RT5512_TEST_MODE_CTRL3:
	case RT5512_VAD_CTRL1:
	case RT5512_VAD_CTRL2:
	case RT5512_VAD_CTRL3:
	case RT5512_VAD_CTRL4:
	case RT5512_VAD_STATUS1:
	case RT5512_VAD_STATUS2:
	case RT5512_BUF_SRAM_CTRL1:
	case RT5512_BUF_SRAM_CTRL2:
	case RT5512_BUF_SRAM_CTRL3:
	case RT5512_BUF_SRAM_CTRL4:
	case RT5512_BUF_SRAM_CTRL5:
	case RT5512_BUF_SRAM_CTRL6:
	case RT5512_BUF_SRAM_CTRL7:
	case RT5512_AUTO_MODE_CTRL:
	case RT5512_PWR_ANLG1:
	case RT5512_PWR_ANLG2:
	case RT5512_PWR_DIG:
	case RT5512_PWR_DSP:
	case RT5512_PRIV_INDEX:
	case RT5512_PRIV_DATA:
	case RT5512_BUF_MODE_CTRL_PLL_CAL1:
	case RT5512_BUF_MODE_CTRL_PLL_CAL2:
	case RT5512_BUF_MODE_CTRL_PLL_CAL3:
	case RT5512_BUF_MODE_CTRL_PLL_CAL4:
	case RT5512_BUF_MODE_CTRL_PLL_CAL5:
	case RT5512_BUF_MODE_CTRL_PLL_CAL6:
	case RT5512_KEY_FHRASE_CTRL_AVD:
	case RT5512_AUTO_CLK_SEL_STATUS1:
	case RT5512_AUTO_CLK_SEL_STATUS2:
	case RT5512_AUTO_CLK_SEL_STATUS3:
	case RT5512_AUTO_CLK_SEL_STATUS4:
	case RT5512_PLL_CLOCK_CTRL1:
	case RT5512_PLL_CLOCK_CTRL2:
	case RT5512_PLL_CLOCK_CTRL3:
	case RT5512_PLL_CAL_CTRL1:
	case RT5512_PLL_CAL_CTRL2:
	case RT5512_PLL_CAL_CTRL3:
	case RT5512_PLL_CAL_CTRL4:
	case RT5512_PLL_CAL_CTRL5:
	case RT5512_PLL_CAL_CTRL6:
	case RT5512_PLL_CAL_CTRL7:
	case RT5512_PLL_CAL_CTRL8:
	case RT5512_PLL_CAL_CTRL9:
	case RT5512_PLL_CAL_STATUS1:
	case RT5512_PLL_CAL_STATUS2:
	case RT5512_PLL_CAL_STATUS3:
	case RT5512_DSP_CTRL1:
	case RT5512_DSP_CTRL2:
	case RT5512_DSP_CTRL3:
	case RT5512_DSP_CTRL4:
	case RT5512_DSP_CTRL5:
	case RT5512_DSP_CTRL6:
	case RT5512_DSP_CTRL7:
	case RT5512_DSP_CTRL8:
	case RT5512_DSP_CTRL9:
	case RT5512_DSP_CTRL10:
	case RT5512_DSP_CTRL11:
	case RT5512_DSP_CTRL12:
	case RT5512_DSP_CTRL13:
	case RT5512_DSP_CTRL14:
	case RT5512_DSP_CTRL15:
	case RT5512_PLL_CLK_EXT_CTRL1:
	case RT5512_PLL_CLK_EXT_CTRL2:
	case RT5512_ADC_EXT_CTRL1:
	case RT5512_DUMMY_RTK1:
	case RT5512_DUMMY_RTK2:
	case RT5512_DUMMY_RTK3:
	case RT5512_DUMMY_RTK4:
	case RT5512_DUMMY_RTK5:
	case RT5512_DUMMY_RTK6:
	case RT5512_DUMMY_RTK7:
	case RT5512_DUMMY_RTK8:
	case RT5512_DUMMY_RTK9:
	case RT5512_DUMMY_RTK10:
	case RT5512_DUMMY_RTK11:
	case RT5512_DUMMY_RTK12:
	case RT5512_DUMMY_RTK13:
	case RT5512_DUMMY_RTK14:
	case RT5512_DUMMY_RTK15:
	case RT5512_DUMMY_RTK16:
	case RT5512_DUMMY_CUSTOMER1:
	case RT5512_DUMMY_CUSTOMER2:
	case RT5512_DUMMY_CUSTOMER3:
	case RT5512_DUMMY_CUSTOMER4:
	case RT5512_DUMMY_CUSTOMER5:
	case RT5512_DUMMY_CUSTOMER6:
	case RT5512_DUMMY_CUSTOMER7:
	case RT5512_DUMMY_CUSTOMER8:
	case RT5512_DUMMY_CUSTOMER9:
	case RT5512_DUMMY_CUSTOMER10:
	case RT5512_DUMMY_CUSTOMER11:
	case RT5512_DUMMY_CUSTOMER12:
	case RT5512_DUMMY_CUSTOMER13:
	case RT5512_DUMMY_CUSTOMER14:
	case RT5512_DUMMY_CUSTOMER15:
	case RT5512_DUMMY_CUSTOMER16:
	case RT5512_DSP_MEM_CTRL1:
	case RT5512_DSP_MEM_CTRL2:
	case RT5512_DSP_MEM_CTRL3:
	case RT5512_DSP_MEM_CTRL4:
	case RT5512_DSP_MEM_CTRL5:
	case RT5512_DSP_MEM_CTRL6:
	case RT5512_DSP_MEM_CTRL7:
	case RT5512_DUMMY1:
	case RT5512_DUMMY2:
	case RT5512_DUMMY3:
	case RT5512_VENDOR_ID:
	case RT5512_VENDOR_ID1:
	case RT5512_VENDOR_ID2:
		return true;
	default:
		return false;
	}
}

static ssize_t rt5512_reg_show(struct device *dev, 
	struct device_attribute *attr, char *buf)
{
	int count = 0;
	int i;
	int value;

	for (i = RT5512_RESET; i <= RT5512_VENDOR_ID2; i++) {
		if (rt5512_readable_register(i)) {
			value = rt5512_read(rt5512_i2c, i);
			if (value < 0)
				count += sprintf(buf + count, "%02x: XXXX\n",
					i);
			else
				count += sprintf(buf + count, "%02x: %04x\n", i,
					value);

			if (count >= PAGE_SIZE - 1)
				break;
		}
	}

	return count;
}

static ssize_t rt5512_reg_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int val = 0, addr = 0;
	int i;

	for (i = 0; i < count; i++) {
		if (*(buf + i) <= '9' && *(buf + i) >= '0')
			addr = (addr << 4) | (*(buf + i)-'0');
		else if (*(buf + i) <= 'f' && *(buf + i) >= 'a')
			addr = (addr << 4) | ((*(buf + i) - 'a') + 0xa);
		else if (*(buf + i) <= 'F' && *(buf + i) >= 'A')
			addr = (addr << 4) | ((*(buf + i)-'A') + 0xa);
		else
			break;
	}

	for (i = i + 1 ; i < count; i++) {
		if (*(buf + i) <= '9' && *(buf + i) >= '0')
			val = (val << 4) | (*(buf + i) - '0');
		else if (*(buf + i) <= 'f' && *(buf + i) >= 'a')
			val = (val << 4) | ((*(buf + i) - 'a') + 0xa);
		else if (*(buf + i) <= 'F' && *(buf + i) >= 'A')
			val = (val << 4) | ((*(buf + i) - 'A') + 0xa);
		else
			break;
	}

	if (addr > RT5512_VENDOR_ID2 || val > 0xffff || val < 0)
		return count;

	if (i == count)
		pr_info("0x%02x = 0x%04x\n", addr,
			rt5512_read(rt5512_i2c, addr));
	else
		rt5512_write(rt5512_i2c, addr, val);

	return count;
}
static DEVICE_ATTR(rt5512_reg, 0666, rt5512_reg_show, rt5512_reg_store);

static ssize_t device_read(struct file *file, char __user * buffer,
	size_t length, loff_t * offset)
{
	char fc[5];
	size_t ret;

	if (*offset > 0)
		return 0;

	ret = sprintf(fc, "%d", rt5512_read(rt5512_i2c, RT5512_DUMMY3));

	if (copy_to_user(buffer, fc, ret))
		return -EFAULT;

	*offset += ret;

	return ret;
}

static ssize_t device_write(struct file *file, const char __user * buffer,
	size_t length, loff_t * offset)
{
	char mode;

	if (copy_from_user(&mode, buffer, 1))
		goto out;

	switch(mode - '0') {
	case RT5512_DSP:
		rt5512_dsp_start();
		break;
	case RT5512_DSP_ONESHOT:
		rt5512_dsp_one_shot();
		break;
	case RT5512_DSP_RELOAD:
	case RT5512_USE_AMIC:
	case RT5512_USE_DMIC:
		if ((mode - '0') != RT5512_DSP_RELOAD)
			rt5512_use_amic = ((mode - '0') == RT5512_USE_AMIC);
		schedule_delayed_work(&reload_work, msecs_to_jiffies(50));
		break;
	case RT5512_NORMAL:
	default:
		rt5512_dsp_stop();
	}

out:
	return length;
}

struct file_operations fops = {
	.owner = THIS_MODULE,
	.read = device_read,
	.write = device_write,
};

static struct miscdevice rt5512_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "rt5512",
	.fops = &fops
};

static void rt5512_reload_work(struct work_struct *work)
{
	rt5512_start_rest = false;
	rt5512_dsp_load_fw();
}

static irqreturn_t rt5512_irq(int irq, void *data)
{
	//char name[20] = "NAME=RT5512_VAD";
	//char state[20] = "STATE=KeyPhraseDetected";
	char *envp[2] = { "STATE=KeyPhraseDetected", NULL };
	int ret;

	ret = kobject_uevent_env(&rt5512_dev.this_device->kobj, KOBJ_CHANGE, envp);
	printk("%s : got vad interrupt %d, ret %d\n", __func__, irq, ret);

	return IRQ_HANDLED;
}

static int request_vad_interrupt(unsigned int gpio)
{
	int ret, irq;
	ret = gpio_request(gpio, "vad_int"); // Voice activity detection interrupt
	if (ret) {
		pr_err("%s : request gpio %d failed, ret = %d\n", __func__, gpio, ret);
		return ret;
	}
	ret = gpio_direction_input(gpio);
	if (ret) {
		pr_err("%s : set %d failed, ret = %d\n", __func__, gpio, ret);
		return ret;
	}
	rt5512_i2c->irq = gpio_to_irq(gpio);

	if (rt5512_i2c->irq) {
		ret = request_threaded_irq(rt5512_i2c->irq,
				NULL,
				rt5512_irq,
				IRQF_TRIGGER_RISING | IRQF_ONESHOT,
				"rt5512",
				NULL);
		if (ret) {
			free_irq(rt5512_i2c->irq, NULL);
			gpio_free(gpio);
			pr_err("%s : request thread irq failed %d\n", __func__, ret);
			return ret;
		} else {
			enable_irq_wake(rt5512_i2c->irq);
			pr_info("%s : request gpio %d to irq %d for vad\n", __func__, gpio, rt5512_i2c->irq);
			return rt5512_i2c->irq;
		}
	}
	return 0;
}


static int rt5512_i2c_probe(struct i2c_client *i2c,
				      const struct i2c_device_id *i2c_id)
{
	int ret;

	pr_info("RT5512 Driver Version %s\n", VERSION);

	ret = device_create_file(&i2c->dev, &dev_attr_rt5512_reg);
	if (ret < 0)
		printk("failed to add rt5512_reg sysfs files\n");

	rt5512_reset(i2c);
	
	rt5512_i2c = i2c;

#ifdef RT5512_USE_NATIVE
	request_firmware_nowait(THIS_MODULE, FW_ACTION_HOTPLUG,
		RT5512_FIRMWARE1, &i2c->dev, GFP_KERNEL, i2c,
		rt5512_fw_loaded);
#else
	request_firmware_nowait(THIS_MODULE, FW_ACTION_HOTPLUG,
		"SMicBin.dat", &i2c->dev, GFP_KERNEL, i2c,
		rt5512_fw_loaded);
#endif

	ret = misc_register(&rt5512_dev);
	if (ret)
		dev_err(&i2c->dev, "Couldn't register control device\n");

	INIT_DELAYED_WORK(&reload_work, rt5512_reload_work);

	// joe_cheng : change DSP_INT from 60(EVB) to 176(EVB2)
        request_vad_interrupt(176);

	return 0;
}

static int rt5512_i2c_remove(struct i2c_client *i2c)
{
	rt5512_dsp_stop();

	rt5512_i2c = NULL;

	if (rt5512_dev.minor)
		misc_deregister(&rt5512_dev);
	if (i2c->irq)
		free_irq(i2c->irq, NULL);

	return 0;
}

static const struct i2c_device_id rt5512_i2c_id[] = {
	{ "rt5512", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rt5512_i2c_id);

static struct i2c_driver rt5512_i2c_driver = {
	.driver = {
		.name = "rt5512",
		.owner = THIS_MODULE,
	},
	.probe = rt5512_i2c_probe,
	.remove = rt5512_i2c_remove,
	.id_table = rt5512_i2c_id,
};
module_i2c_driver(rt5512_i2c_driver);

MODULE_DESCRIPTION("ALC5512 Smart DMIC Bridge Driver");
MODULE_AUTHOR("Oder Chiou <oder_chiou@realtek.com>");
MODULE_LICENSE("GPL v2");
