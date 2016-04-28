/*
 * rt5648_ioctl.h  --  RT5648 ALSA SoC audio driver IO control
 *
 * Copyright 2012 Realtek Microelectronics
 * Author: Bard <bardliao@realtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __RT5648_IOCTL_H__
#define __RT5648_IOCTL_H__

#include <sound/hwdep.h>
#include <linux/ioctl.h>

enum {
        PhoneCall_Default,
        RingTone_Coming,
        PhoneCall_Connected,
};

enum {
	Record_Default,
	Record_Headset_mic,
	Record_Front_mic,
};
#ifdef CONFIG_FE380CG
enum {
	NORMAL_DAC = 0,
	NORMAL_ADC,
	CLUB,
        SPK_WITHOUT_EFFECT,
	SPK_WITH_EFFECT,
	VOICE_RCV,
	VOICE_SPK,
	HP,
	VOICE_HP,
	BOARD_MIC_RECORD,
	HEADSET_MIC_RECORD,
	VOICE_BOARD_MIC_RECORD,
	MODE_NUM,
};

enum {
        NONE,
        PLAYBACK_SPK_WITHOUT_EFFECT,
        PLAYBACK_SPK_WITH_EFFECT,
        PLAYBACK_HS,
        PLAYBACK_RCV,
        RECORD,
        VOIP,
        RINGTONE_WITHOUT_EFFECT,
        RINGTONE_WITH_EFFECT,
        VR,
	VOICE,
	VOICE_HS,
	VOICE_HF,
        BT,
        USE_MODE_NUM,
};
#endif 

enum {
	EQ_CH_DAC = 0,
	EQ_CH_ADC,
	EQ_CH_NUM,
};

#if defined(CONFIG_FE171MG) || defined(CONFIG_Z580C)
enum {
	NORMAL_DAC = 0,
	NORMAL_ADC,
	CLUB,
	SPK,
	VOICE_RCV,
	VOICE_SPK,
	HP,
	VOICE_HP,
	BOARD_MIC_RECORD,
	HEADSET_MIC_RECORD,
	VOICE_BOARD_MIC_RECORD,
	MODE_NUM,
};

enum {
        NONE,
        PLAYBACK_SPK,
        PLAYBACK_HS,
        PLAYBACK_RCV,
        RECORD,
        VOIP,
        RINGTONE,
        VR,
	VOICE,
	VOICE_HS,
	VOICE_HF,
        BT,
        USE_MODE_NUM,
};
#endif

#define EQ_REG_NUM 56
typedef struct  hweq_s {
	unsigned int reg[EQ_REG_NUM];
	unsigned int value[EQ_REG_NUM];
	unsigned int ctrl;
} hweq_t;

int rt5648_ioctl_common(struct snd_hwdep *hw, struct file *file,
			unsigned int cmd, unsigned long arg);
int rt5648_update_eqmode(
	struct snd_soc_codec *codec, int channel, int mode);

int rt5648_update_drc_agc(
        struct snd_soc_codec *codec, int mode);

#endif /* __RT5648_IOCTL_H__ */
