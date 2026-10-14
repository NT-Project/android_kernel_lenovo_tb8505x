/*
 * Copyright (C) 2015 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#define LOG_TAG "LCM"

#ifndef BUILD_LK
#include <linux/string.h>
#include <linux/kernel.h>
#endif

#include "lcm_drv.h"

#ifdef BUILD_LK
#include <platform/upmu_common.h>
#include <platform/mt_gpio.h>
#include <platform/mt_i2c.h>
#include <platform/mt_pmic.h>
#include <string.h>
#elif defined(BUILD_UBOOT)
#include <asm/arch/mt_gpio.h>
#else
#include "disp_dts_gpio.h"
#endif

#ifndef MACH_FPGA
#include <lcm_pmic.h>
#endif

#ifdef BUILD_LK
#define LCM_LOGI(string, args...)  dprintf(0, "[LK/"LOG_TAG"]"string, ##args)
#define LCM_LOGD(string, args...)  dprintf(1, "[LK/"LOG_TAG"]"string, ##args)
#else
#define LCM_LOGI(fmt, args...)  pr_debug("[KERNEL/"LOG_TAG"]"fmt, ##args)
#define LCM_LOGD(fmt, args...)  pr_debug("[KERNEL/"LOG_TAG"]"fmt, ##args)
#endif

#define LCM_ID (0x98)

static const unsigned int BL_MIN_LEVEL = 20;
static struct LCM_UTIL_FUNCS lcm_util;

#define SET_RESET_PIN(v)	(lcm_util.set_reset_pin((v)))
#define MDELAY(n)		(lcm_util.mdelay(n))
#define UDELAY(n)		(lcm_util.udelay(n))


#define dsi_set_cmdq_V22(cmdq, cmd, count, ppara, force_update) \
	lcm_util.dsi_set_cmdq_V22(cmdq, cmd, count, ppara, force_update)
#define dsi_set_cmdq_V2(cmd, count, ppara, force_update) \
	lcm_util.dsi_set_cmdq_V2(cmd, count, ppara, force_update)
#define dsi_set_cmdq(pdata, queue_size, force_update) \
		lcm_util.dsi_set_cmdq(pdata, queue_size, force_update)
#define wrtie_cmd(cmd) lcm_util.dsi_write_cmd(cmd)
#define write_regs(addr, pdata, byte_nums) \
		lcm_util.dsi_write_regs(addr, pdata, byte_nums)
#define read_reg(cmd) \
	  lcm_util.dsi_dcs_read_lcm_reg(cmd)
#define read_reg_v2(cmd, buffer, buffer_size) \
		lcm_util.dsi_dcs_read_lcm_reg_v2(cmd, buffer, buffer_size)

#ifndef BUILD_LK
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/i2c.h>
#include <linux/irq.h>
/* #include <linux/jiffies.h> */
/* #include <linux/delay.h> */
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#endif

extern bool ntp_gesture_mode;

/* static unsigned char lcd_id_pins_value = 0xFF; */
static const unsigned char LCD_MODULE_ID = 0x01;
#define LCM_DSI_CMD_MODE									0
#define FRAME_WIDTH										(800)
#define FRAME_HEIGHT										(1280)

#define LCM_PHYSICAL_WIDTH									(107640)
#define LCM_PHYSICAL_HEIGHT									(172224)

#define REGFLAG_DELAY		0xFFFC
#define REGFLAG_UDELAY		0xFFFB
#define REGFLAG_END_OF_TABLE	0xFFFD
#define REGFLAG_RESET_LOW	0xFFFE
#define REGFLAG_RESET_HIGH	0xFFFF

static struct LCM_DSI_MODE_SWITCH_CMD lcm_switch_mode_cmd;

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#define GPIO_LCM_BIAS_EN       (GPIO167 | 0x80000000)
#define GPIO_LCM_BIAS_EN2      (GPIO168 | 0x80000000)

#define GPIO_CTP_RST (GPIO172 | 0x80000000)

#define GPIO_LCD_RST (GPIO45 | 0x80000000)

struct LCM_setting_table {
	unsigned int cmd;
	unsigned char count;
	unsigned char para_list[64];
};

static struct LCM_setting_table lcm_sleep_suspend_setting[] = {
	{0x28, 0, {} },
	{REGFLAG_DELAY, 50, {} },
	{0x10, 0, {} },
	{REGFLAG_DELAY, 100, {} },
	{0x04, 0x01, {0x5A}},
	{0x05, 0x01, {0x5A}},
	{REGFLAG_DELAY, 120, {} }
};

/*static struct LCM_setting_table lcm_suspend_setting[] = {
	{0x28, 0, {} },
	{REGFLAG_DELAY, 50, {} },
	{0x10, 0, {} },
	{REGFLAG_DELAY, 120, {} }
};*/

static struct LCM_setting_table init_setting_vdo[] = {
	{0xFF,1,{0x26}},
	{0xFB,1,{0x01}},
	{0xA7,1,{0x0A}},
	{0xFF,1,{0x20}},
	{0xFB,1,{0x01}},
	{0x05,1,{0xB9}},
	{0x07,1,{0x73}},
	{0x08,1,{0x5F}},
	{0x0E,1,{0x7D}},
	{0x0F,1,{0x7D}},
	{0x94,1,{0xC0}},
	{0x95,1,{0x13}},
	{0x96,1,{0x13}},
	{0x9D,1,{0x00}},
	{0x9E,1,{0x00}},
	{0x69,1,{0x98}},
	{0x75,1,{0xA2}},
	{0x77,1,{0xB3}},
	{0x0D,1,{0x63}},
	{0xFF,1,{0x20}},
	{0xFB,1,{0x01}},
	{0xB0,16,{0x00,0x00,0x00,0x15,0x00,0x3C,0x00,0x5A,0x00,0x73,0x00,0x89,0x00,0x9C,0x00,0xAD}},
	{0xB1,16,{0x00,0xBD,0x00,0xF0,0x01,0x1B,0x01,0x5A,0x01,0x89,0x01,0xD6,0x02,0x13,0x02,0x15}},
	{0xB2,16,{0x02,0x50,0x02,0x93,0x02,0xBC,0x02,0xF4,0x03,0x18,0x03,0x47,0x03,0x53,0x03,0x61}},
	{0xB3,12,{0x03,0x70,0x03,0x80,0x03,0x91,0x03,0x93,0x03,0x94,0x03,0x95}},
	{0xB4,16,{0x00,0x00,0x00,0x15,0x00,0x3C,0x00,0x5A,0x00,0x73,0x00,0x89,0x00,0x9C,0x00,0xAD}},
	{0xB5,16,{0x00,0xBD,0x00,0xF0,0x01,0x1B,0x01,0x5A,0x01,0x89,0x01,0xD6,0x02,0x13,0x02,0x15}},
	{0xB6,16,{0x02,0x50,0x02,0x93,0x02,0xBC,0x02,0xF4,0x03,0x18,0x03,0x47,0x03,0x53,0x03,0x61}},
	{0xB7,12,{0x03,0x70,0x03,0x80,0x03,0x91,0x03,0x93,0x03,0x94,0x03,0x95}},
	{0xB8,16,{0x00,0x00,0x00,0x15,0x00,0x3C,0x00,0x5A,0x00,0x73,0x00,0x89,0x00,0x9C,0x00,0xAD}},
	{0xB9,16,{0x00,0xBD,0x00,0xF0,0x01,0x1B,0x01,0x5A,0x01,0x89,0x01,0xD6,0x02,0x13,0x02,0x15}},
	{0xBA,16,{0x02,0x50,0x02,0x93,0x02,0xBC,0x02,0xF4,0x03,0x18,0x03,0x47,0x03,0x53,0x03,0x61}},
	{0xBB,12,{0x03,0x70,0x03,0x80,0x03,0x91,0x03,0x93,0x03,0x94,0x03,0x95}},
	{0xFF,1,{0x21}},
	{0xFB,1,{0x01}},
	{0xB0,16,{0x00,0x00,0x00,0x15,0x00,0x3C,0x00,0x5A,0x00,0x73,0x00,0x89,0x00,0x9C,0x00,0xAD}},
	{0xB1,16,{0x00,0xBD,0x00,0xF0,0x01,0x1B,0x01,0x5A,0x01,0x89,0x01,0xD6,0x02,0x13,0x02,0x15}},
	{0xB2,16,{0x02,0x50,0x02,0x93,0x02,0xBC,0x02,0xF4,0x03,0x18,0x03,0x47,0x03,0x53,0x03,0x61}},
	{0xB3,12,{0x03,0x70,0x03,0x80,0x03,0x91,0x03,0xAE,0x03,0xD5,0x03,0xD6}},
	{0xB4,16,{0x00,0x00,0x00,0x15,0x00,0x3C,0x00,0x5A,0x00,0x73,0x00,0x89,0x00,0x9C,0x00,0xAD}},
	{0xB5,16,{0x00,0xBD,0x00,0xF0,0x01,0x1B,0x01,0x5A,0x01,0x89,0x01,0xD6,0x02,0x13,0x02,0x15}},
	{0xB6,16,{0x02,0x50,0x02,0x93,0x02,0xBC,0x02,0xF4,0x03,0x18,0x03,0x47,0x03,0x53,0x03,0x61}},
	{0xB7,12,{0x03,0x70,0x03,0x80,0x03,0x91,0x03,0xAE,0x03,0xD5,0x03,0xD6}},
	{0xB8,16,{0x00,0x00,0x00,0x15,0x00,0x3C,0x00,0x5A,0x00,0x73,0x00,0x89,0x00,0x9C,0x00,0xAD}},
	{0xB9,16,{0x00,0xBD,0x00,0xF0,0x01,0x1B,0x01,0x5A,0x01,0x89,0x01,0xD6,0x02,0x13,0x02,0x15}},
	{0xBA,16,{0x02,0x50,0x02,0x93,0x02,0xBC,0x02,0xF4,0x03,0x18,0x03,0x47,0x03,0x53,0x03,0x61}},
	{0xBB,12,{0x03,0x70,0x03,0x80,0x03,0x91,0x03,0xAE,0x03,0xD5,0x03,0xD6}},
	{0xFF,1,{0x24}},
	{0xFB,1,{0x01}},
	{0x91,1,{0x44}},
	{0x92,1,{0xD0}},
	{0x93,1,{0xF5}},
	{0x94,1,{0x08}},
	{0x60,1,{0xC8}},
	{0x61,1,{0x00}},
	{0x63,1,{0x50}},
	{0x00,1,{0x03}},
	{0x01,1,{0x03}},
	{0x02,1,{0x03}},
	{0x03,1,{0x03}},
	{0x04,1,{0x03}},
	{0x05,1,{0x03}},
	{0x06,1,{0x03}},
	{0x07,1,{0x03}},
	{0x08,1,{0x05}},
	{0x09,1,{0x22}},
	{0x0A,1,{0x06}},
	{0x0B,1,{0x1D}},
	{0x0C,1,{0x1C}},
	{0x0D,1,{0x11}},
	{0x0E,1,{0x10}},
	{0x0F,1,{0x0F}},
	{0x10,1,{0x0E}},
	{0x11,1,{0x0D}},
	{0x12,1,{0x0C}},
	{0x13,1,{0x04}},
	{0x14,1,{0x03}},
	{0x15,1,{0x03}},
	{0x16,1,{0x03}},
	{0x17,1,{0x03}},
	{0x18,1,{0x03}},
	{0x19,1,{0x03}},
	{0x1A,1,{0x03}},
	{0x1B,1,{0x03}},
	{0x1C,1,{0x03}},
	{0x1D,1,{0x03}},
	{0x1E,1,{0x05}},
	{0x1F,1,{0x22}},
	{0x20,1,{0x06}},
	{0x21,1,{0x1D}},
	{0x22,1,{0x1C}},
	{0x23,1,{0x11}},
	{0x24,1,{0x10}},
	{0x25,1,{0x0F}},
	{0x26,1,{0x0E}},
	{0x27,1,{0x0D}},
	{0x28,1,{0x0C}},
	{0x29,1,{0x04}},
	{0x2A,1,{0x03}},
	{0x2B,1,{0x03}},
	{0x2F,1,{0x04}},
	{0x30,1,{0x32}},
	{0x31,1,{0x41}},
	{0x33,1,{0x32}},
	{0x34,1,{0x04}},
	{0x35,1,{0x41}},
	{0x37,1,{0x44}},
	{0x38,1,{0x40}},
	{0x39,1,{0x00}},
	{0x3A,1,{0x01}},
	{0x3B,1,{0x97}},
	{0x3D,1,{0x53}},
	{0xAB,1,{0x44}},
	{0xAC,1,{0x40}},
	{0x4D,1,{0x21}},
	{0x4E,1,{0x43}},
	{0x4F,1,{0x65}},
	{0x51,1,{0x34}},
	{0x52,1,{0x12}},
	{0x53,1,{0x56}},
	{0x55,2,{0x43,0x03}},
	{0x56,1,{0x06}},
	{0x58,1,{0x21}},
	{0x59,1,{0x40}},
	{0x5A,1,{0x01}},
	{0x5B,1,{0x97}},
	{0x5E,2,{0x00,0x0C}},
	{0x5F,1,{0x00}},
	{0x7A,1,{0x00}},
	{0x7B,1,{0x00}},
	{0x7C,1,{0x00}},
	{0x7D,1,{0x00}},
	{0x7E,1,{0x20}},
	{0x7F,1,{0x3C}},
	{0x80,1,{0x00}},
	{0x81,1,{0x00}},
	{0x82,1,{0x08}},
	{0x97,1,{0x02}},
	{0xC5,1,{0x10}},
	{0xD7,1,{0x55}},
	{0xD8,1,{0x55}},
	{0xD9,1,{0x23}},
	{0xDA,1,{0x05}},
	{0xDB,1,{0x01}},
	{0xDC,1,{0xD0}},
	{0xDE,1,{0x27}},
	{0xB5,1,{0x90}},
	{0xFF,1,{0x25}},
	{0xFB,1,{0x01}},
	{0x05,1,{0x04}},
	{0x13,1,{0x04}},
	{0x14,1,{0x14}},
	{0xFF,1,{0x26}},
	{0xFB,1,{0x01}},
	{0x00,1,{0xA0}},
	{0x02,1,{0x31}},
	{0xCA,1,{0xCE}},
	{0xFF,1,{0x27}},
	{0xFB,1,{0x01}},
	{0xD1,1,{0x24}},
	{0xD2,1,{0x30}},
	{0xC0,1,{0x18}},
	{0xC1,1,{0x00}},
	{0xC2,1,{0x00}},
	{0x58,1,{0x80}},
	{0x59,1,{0x00}},
	{0x5B,1,{0xF4}},
	{0x5C,1,{0x00}},
	{0x5D,1,{0x0C}},
	{0x5E,1,{0x80}},
	{0x5F,1,{0x10}},
	{0x60,1,{0x00}},
	{0x98,1,{0x00}},
	{0xB4,1,{0x03}},
	{0x9B,1,{0xBD}},
	{0xA0,1,{0x90}},
	{0xAB,1,{0x14}},
	{0xAC,1,{0x00}},
	{0xB0,1,{0x83}},
	{0xBC,1,{0x0C}},
	{0xBD,1,{0x28}},
	{0xFF,1,{0x2A}},
	{0xFB,1,{0x01}},
	{0x24,1,{0x00}},
	{0x25,1,{0xD0}},
	{0x26,1,{0xF8}},
	{0x27,1,{0x00}},
	{0x28,1,{0xF5}},
	{0x29,1,{0x00}},
	{0x2A,1,{0xF5}},
	{0x2B,1,{0x00}},
	{0x2D,1,{0xF5}},
	{0x64,1,{0x96}},
	{0x65,1,{0x00}},
	{0x66,1,{0x00}},
	{0x6A,1,{0x96}},
	{0x6B,1,{0x00}},
	{0x6C,1,{0x00}},
	{0x70,1,{0x92}},
	{0x71,1,{0x00}},
	{0x72,1,{0x00}},
	{0xA2,1,{0x33}},
	{0xA3,1,{0x30}},
	{0xA4,1,{0xC0}},
	{0x97,1,{0x3C}},
	{0x98,1,{0x02}},
	{0x99,1,{0x95}},
	{0x9A,1,{0x06}},
	{0x9B,1,{0x00}},
	{0x9C,1,{0x0B}},
	{0x9D,1,{0x0A}},
	{0x9E,1,{0x90}},
	{0xFF,1,{0x23}},
	{0xFB,1,{0x01}},
	{0x00,1,{0x00}},
	{0x07,1,{0x20}},
	{0x08,1,{0x03}},
	{0x09,1,{0x59}},
	{0x11,1,{0x01}},
	{0x12,1,{0x77}},
	{0x15,1,{0x07}},
	{0x16,1,{0x07}},
	{0xFF,1,{0xD0}},
	{0xFB,1,{0x01}},
	{0x02,1,{0x77}},
	{0x09,1,{0xBF}},
	{0xFF,1,{0x25}},
	{0xFB,1,{0x01}},
	{0x17,1,{0xC2}},
	{0x19,1,{0x0F}},
	{0x1B,1,{0x5B}},
	{0x1D,1,{0x00}},
	{0xFF,1,{0x27}},
	{0xFB,1,{0x01}},
	{0xDE,1,{0xFF}},
	{0xDF,1,{0xFF}},
	{0xFF,1,{0x24}},
	{0xFB,1,{0x01}},
	{0xC2,1,{0xCF}},
	{0xFF,1,{0xF0}},
	{0xFB,1,{0x01}},
	{0x84,1,{0x08}},
	{0x85,1,{0x0C}},
	{0xFF,1,{0x20}},
	{0xFB,1,{0x01}},
	{0x30,1,{0x00}},
	{0x51,1,{0x00}},
	{0xFF,1,{0x25}},
	{0xFB,1,{0x01}},
	{0x91,1,{0x1F}},
	{0x92,1,{0x0F}},
	{0x93,1,{0x01}},
	{0x94,1,{0x18}},
	{0x95,1,{0x03}},
	{0x96,1,{0x01}},
	{0xFF,1,{0x10}},
	{0xFB,1,{0x01}},
	{0xB0,1,{0x01}},
	{0x53,1,{0x2C}},
	{0x68,2,{0x02,0x01}},
	{0xBB,1,{0x13}},
	{0x3B,5,{0x03,0x08,0xF5,0x04,0x04}},
	{0x35,1,{0x00}},

	{0x11,0x01,{0x00}},
	{REGFLAG_DELAY,100,{}},
	{0x29,0x01,{0x00}},
	{REGFLAG_DELAY,40,{}},

	//{0x50, 2, {0x5a,0x0c} },
	//{0x80, 1, {0xfd} },
	{REGFLAG_DELAY,20,{}},

	{0x50, 1, {0x00} },

	{REGFLAG_END_OF_TABLE, 0x00, {}}

};

static void push_table(void *cmdq, struct LCM_setting_table *table,
	unsigned int count, unsigned char force_update)
{
	unsigned int i;
	unsigned cmd;

	for (i = 0; i < count; i++) {
		cmd = table[i].cmd;

		switch (cmd) {

		case REGFLAG_DELAY:
			if (table[i].count <= 10)
				MDELAY(table[i].count);
			else
				MDELAY(table[i].count);
			break;

		case REGFLAG_UDELAY:
			UDELAY(table[i].count);
			break;

		case REGFLAG_END_OF_TABLE:
			break;

		default:
			dsi_set_cmdq_V22(cmdq, cmd, table[i].count, table[i].para_list, force_update);
		}
	}
}


static void lcm_set_util_funcs(const struct LCM_UTIL_FUNCS *util)
{
	memcpy(&lcm_util, util, sizeof(struct LCM_UTIL_FUNCS));
}


static void lcm_get_params(struct LCM_PARAMS *params)
{
	memset(params, 0, sizeof(struct LCM_PARAMS));

	params->type = LCM_TYPE_DSI;

	params->width = FRAME_WIDTH;
	params->height = FRAME_HEIGHT;
	params->physical_width = LCM_PHYSICAL_WIDTH/1000;
	params->physical_height = LCM_PHYSICAL_HEIGHT/1000;
	//params->physical_width_um = LCM_PHYSICAL_WIDTH;
	//params->physical_height_um = LCM_PHYSICAL_HEIGHT;

#if (LCM_DSI_CMD_MODE)
	params->dsi.mode = CMD_MODE;
	params->dsi.switch_mode = SYNC_PULSE_VDO_MODE;
	//lcm_dsi_mode = CMD_MODE;
#else
	params->dsi.mode = SYNC_PULSE_VDO_MODE;
	//params->dsi.switch_mode = CMD_MODE;
	//lcm_dsi_mode = SYNC_PULSE_VDO_MODE;
	//params->dsi.mode   = SYNC_PULSE_VDO_MODE;	//SYNC_EVENT_VDO_MODE
#endif
	//LCM_LOGI("lcm_get_params lcm_dsi_mode %d\n", lcm_dsi_mode);
	params->dsi.switch_mode_enable = 0;

	/* DSI */
	/* Command mode setting */
	params->dsi.LANE_NUM = LCM_FOUR_LANE;
	/* The following defined the fomat for data coming from LCD engine. */
	params->dsi.data_format.color_order = LCM_COLOR_ORDER_RGB;
	params->dsi.data_format.trans_seq = LCM_DSI_TRANS_SEQ_MSB_FIRST;
	params->dsi.data_format.padding = LCM_DSI_PADDING_ON_LSB;
	params->dsi.data_format.format = LCM_DSI_FORMAT_RGB888;

	/* Highly depends on LCD driver capability. */
	params->dsi.packet_size = 256;
	/* video mode timing */

	params->dsi.PS = LCM_PACKED_PS_24BIT_RGB888;

	params->dsi.vertical_sync_active = 2;
	params->dsi.vertical_backporch = 6;
	params->dsi.vertical_frontporch = 245;
	//params->dsi.vertical_frontporch_for_low_power = 540;/*disable dynamic frame rate*/
	params->dsi.vertical_active_line = FRAME_HEIGHT;

	params->dsi.horizontal_sync_active = 14;
	params->dsi.horizontal_backporch = 25;
	params->dsi.horizontal_frontporch = 25;
	params->dsi.horizontal_active_pixel = FRAME_WIDTH;
	params->dsi.ssc_range = 4;
	params->dsi.ssc_disable = 1;
	/*params->dsi.ssc_disable = 0;*/
#ifndef CONFIG_FPGA_EARLY_PORTING
#if (LCM_DSI_CMD_MODE)
	params->dsi.PLL_CLOCK = 270;	/* this value must be in MTK suggested table */
#else
	params->dsi.PLL_CLOCK = 305;//283;	/* this value must be in MTK suggested table */
#endif
	//params->dsi.PLL_CK_CMD = 220;
	//params->dsi.PLL_CK_VDO = 255;
#else
	params->dsi.pll_div1 = 0;
	params->dsi.pll_div2 = 0;
	params->dsi.fbk_div = 0x1;
#endif
	params->dsi.clk_lp_per_line_enable = 0;
	params->dsi.esd_check_enable = 1;
	params->dsi.customization_esd_check_enable = 1;
	params->dsi.lcm_esd_check_table[0].cmd = 0x0A;
	params->dsi.lcm_esd_check_table[0].count = 1;
	params->dsi.lcm_esd_check_table[0].para_list[0] = 0x9C;

	params->backlight_cust_count=1;
	params->backlight_cust[0].max_brightness = 255;
	params->backlight_cust[0].min_brightness = 10;
	params->backlight_cust[0].max_bl_lvl = 859;	//818 20mA, 859 21mA
	params->backlight_cust[0].min_bl_lvl = 32;

#ifdef CONFIG_MTK_ROUND_CORNER_SUPPORT
	params->round_corner_en = 0;
	params->corner_pattern_width = 720;
	params->corner_pattern_height = 32;
#endif

//	params->use_gpioID = 1;
//	params->gpioID_value = 0;
}

static void lcm_init_power(void)
{
	//if(!tp_gesture_mode) {
		pr_err("[LCM][GPIO]lcm_init_power !\n");
		lcm_power_enable();
	//}
}

static void lcm_suspend_power(void)
{
	if(!ntp_gesture_mode) {
		pr_err("[LCM][GPIO]lcm_suspend_power !\n");
		lcm_reset_pin(0);
		MDELAY(2);
		lcm_power_disable();
	}
}

static void lcm_resume_power(void)
{
	/*if(!tp_gesture_mode) {
		pr_err("[LCM][GPIO]lcm_resume_power !\n");
		lcm_power_enable();
	}*/

	pr_err("[LCM][GPIO]lcm_resume_power !\n");

	MDELAY(7);
	ctp_reset_pin(0);
	MDELAY(5);
	lcm_reset_pin(1);
	MDELAY(5);

	if(ntp_gesture_mode) {
		lcm_power_disable();
		MDELAY(5);
	}

	lcm_reset_pin(0);
	MDELAY(5);

	lcm_power_enable();
	MDELAY(5);

	ctp_reset_pin(1);
	MDELAY(5);
	lcm_reset_pin(1);
	MDELAY(35);

}

#ifdef BUILD_LK
static void lcm_set_gpio_output(unsigned int GPIO, unsigned int output)
{
   mt_set_gpio_mode(GPIO, GPIO_MODE_00);
   mt_set_gpio_dir(GPIO, GPIO_DIR_OUT);
   mt_set_gpio_out(GPIO, (output>0)? GPIO_OUT_ONE: GPIO_OUT_ZERO);
}
#endif
static void lcm_init(void)
{
	//int ret = 0;
	pr_debug("[LCM] lcm_init\n");
	lcm_reset_pin(1);
	MDELAY(10);
	//MDELAY(4);
	lcm_reset_pin(0);
	MDELAY(5);
	lcm_reset_pin(1);
	MDELAY(50);

	push_table(NULL, init_setting_vdo, sizeof(init_setting_vdo) / sizeof(struct LCM_setting_table), 1);
	LCM_LOGI("nt36523b_inx----lcm mode = vdo mode ----\n");
}

static void lcm_suspend(void)
{
	//int ret = 0;
	pr_debug("[LCM] lcm_suspend\n");

	//if(!tp_gesture_mode)
		push_table(NULL, lcm_sleep_suspend_setting, sizeof(lcm_sleep_suspend_setting) / sizeof(struct LCM_setting_table), 1);
	//else
	//	push_table(NULL, lcm_suspend_setting, sizeof(lcm_suspend_setting) / sizeof(struct LCM_setting_table), 1);

	MDELAY(10);

}

static void lcm_resume(void)
{
	pr_debug("[LCM] lcm_resume\n");

	//if(!tp_gesture_mode) {
		/*lcm_reset_pin(1);
		MDELAY(10);*/

		MDELAY(50);

		push_table(NULL, init_setting_vdo, sizeof(init_setting_vdo) / sizeof(struct LCM_setting_table), 1);
		LCM_LOGI("nt36523b_inx----lcm mode = vdo mode ----\n");
	//}
	//else {
	//	lcm_init();
	//}
}


#if 1
static unsigned int lcm_compare_id(void)
{
	return 1;
}
#endif


/* return TRUE: need recovery */
/* return FALSE: No need recovery */
static unsigned int lcm_esd_check(void)
{
	return FALSE;
}

static void lcm_setbacklight(unsigned int level)
{
}

static unsigned int lcm_ata_check(unsigned char *buffer)
{
#ifndef BUILD_LK
	unsigned int ret = 0;
	unsigned int x0 = FRAME_WIDTH / 4;
	unsigned int x1 = FRAME_WIDTH * 3 / 4;

	unsigned char x0_MSB = ((x0 >> 8) & 0xFF);
	unsigned char x0_LSB = (x0 & 0xFF);
	unsigned char x1_MSB = ((x1 >> 8) & 0xFF);
	unsigned char x1_LSB = (x1 & 0xFF);

	unsigned int data_array[3];
	unsigned char read_buf[4];

	LCM_LOGI("ATA check size = 0x%x,0x%x,0x%x,0x%x\n", x0_MSB, x0_LSB, x1_MSB, x1_LSB);
	data_array[0] = 0x0005390A;	/* HS packet */
	data_array[1] = (x1_MSB << 24) | (x0_LSB << 16) | (x0_MSB << 8) | 0x2a;
	data_array[2] = (x1_LSB);
	dsi_set_cmdq(data_array, 3, 1);

	data_array[0] = 0x00043700;	/* read id return two byte,version and id */
	dsi_set_cmdq(data_array, 1, 1);

	read_reg_v2(0x2A, read_buf, 4);

	if ((read_buf[0] == x0_MSB) && (read_buf[1] == x0_LSB)
	    && (read_buf[2] == x1_MSB) && (read_buf[3] == x1_LSB))
		ret = 1;
	else
		ret = 0;

	x0 = 0;
	x1 = FRAME_WIDTH - 1;

	x0_MSB = ((x0 >> 8) & 0xFF);
	x0_LSB = (x0 & 0xFF);
	x1_MSB = ((x1 >> 8) & 0xFF);
	x1_LSB = (x1 & 0xFF);

	data_array[0] = 0x0005390A;	/* HS packet */
	data_array[1] = (x1_MSB << 24) | (x0_LSB << 16) | (x0_MSB << 8) | 0x2a;
	data_array[2] = (x1_LSB);
	dsi_set_cmdq(data_array, 3, 1);
	return ret;
#else
	return 0;
#endif
}

static void *lcm_switch_mode(int mode)
{
#ifndef BUILD_LK
/* customization: 1. V2C config 2 values, C2V config 1 value; 2. config mode control register */
	if (mode == 0) {	/* V2C */
		lcm_switch_mode_cmd.mode = CMD_MODE;
		lcm_switch_mode_cmd.addr = 0xBB;	/* mode control addr */
		lcm_switch_mode_cmd.val[0] = 0x13;	/* enabel GRAM firstly, ensure writing one frame to GRAM */
		lcm_switch_mode_cmd.val[1] = 0x10;	/* disable video mode secondly */
	} else {		/* C2V */
		lcm_switch_mode_cmd.mode = SYNC_PULSE_VDO_MODE;
		lcm_switch_mode_cmd.addr = 0xBB;
		lcm_switch_mode_cmd.val[0] = 0x03;	/* disable GRAM and enable video mode */
	}
	return (void *)(&lcm_switch_mode_cmd);
#else
	return NULL;
#endif
}

#if (LCM_DSI_CMD_MODE)

/* partial update restrictions:
 * 1. roi width must be 1080 (full lcm width)
 * 2. vertical start (y) must be multiple of 16
 * 3. vertical height (h) must be multiple of 16
 */
static void lcm_validate_roi(int *x, int *y, int *width, int *height)
{
	unsigned int y1 = *y;
	unsigned int y2 = *height + y1 - 1;
	unsigned int x1, w, h;

	x1 = 0;
	w = FRAME_WIDTH;

	y1 = round_down(y1, 16);
	h = y2 - y1 + 1;

	/* in some cases, roi maybe empty. In this case we need to use minimu roi */
	if (h < 16)
		h = 16;

	h = round_up(h, 16);

	/* check height again */
	if (y1 >= FRAME_HEIGHT || y1 + h > FRAME_HEIGHT) {
		/* assign full screen roi */
		LCM_LOGD("%s calc error,assign full roi:y=%d,h=%d\n", __func__, *y, *height);
		y1 = 0;
		h = FRAME_HEIGHT;
	}

	/*LCM_LOGD("lcm_validate_roi (%d,%d,%d,%d) to (%d,%d,%d,%d)\n",*/
	/*	*x, *y, *width, *height, x1, y1, w, h);*/

	*x = x1;
	*width = w;
	*y = y1;
	*height = h;
}
#endif


#if (LCM_DSI_CMD_MODE)
struct LCM_DRIVER nt36523b_wxga_cmd_incell_inx_lcm_drv = {
	.name = "nt36523b_wxga_cmd_incell_inx",
#else

struct LCM_DRIVER nt36523b_wxga_vdo_incell_inx_lcm_drv = {
	.name = "nt36523b_wxga_vdo_incell_inx",
#endif
	.set_util_funcs = lcm_set_util_funcs,
	.get_params = lcm_get_params,
	.init = lcm_init,
	.suspend = lcm_suspend,
	.resume = lcm_resume,
	.compare_id = lcm_compare_id,
	.init_power = lcm_init_power,
	.resume_power = lcm_resume_power,
	.suspend_power = lcm_suspend_power,
	.esd_check = lcm_esd_check,
	.set_backlight = lcm_setbacklight,
	.ata_check = lcm_ata_check,
	.switch_mode = lcm_switch_mode,
#if (LCM_DSI_CMD_MODE)
	.validate_roi = lcm_validate_roi,
#endif

};
