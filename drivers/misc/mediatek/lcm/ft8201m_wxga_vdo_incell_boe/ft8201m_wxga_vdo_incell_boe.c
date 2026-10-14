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

extern bool mtp_gesture_mode;

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
	{0x00, 1, {0x00} },
	{0xFF, 3, {0x82,0x01,0x01} },

	{0x00, 1, {0x80} },
	{0xFF, 2, {0x82,0x01} },
	//======voltage set==========

	{0x00, 1, {0x93} }, //VGH_N 20V
	{0xC5, 1, {0x84} },

	{0x00, 1, {0x97} }, //VGH_I 20V
	{0xC5, 1, {0x84} },

	{0x00, 1, {0x9E} },  //VGH Ratio 2AVDD-2AVEE
	{0xC5, 1, {0x0A} },

	{0x00, 1, {0x9A} },  //VGL_N -12V 2AVEE-AVDD
	{0xC5, 1, {0xC3} },

	{0x00, 1, {0x9C} },  //VGL_I -12V 2AVEE-AVDD
	{0xC5, 1, {0xC3} },

	{0x00, 1, {0xB6} },
	{0xC5, 2, {0x75,0x75} },   //VGHO1_N_I 19V

	{0x00, 1, {0xB8} },
	{0xC5, 2, {0x39,0x39} },  //VGLO1_N_I -11V

	{0x00, 1, {0x00} },
	{0xD8, 2, {0xD2,0xD2} },  //GVDDP/N 5.6V  -5.6V

	//{0x00,0x00} },
	//{0xD9,0x00,0x82,0x82} },  //VCOM(-1V)

	{0x00, 1, {0x82} }, 
	{0xC5, 1, {0x95} },  //LVD

	{0x00, 1, {0x83} }, 
	{0xC5, 1, {0x07} },  //LVD Enable


	//==========gamma==============
	//Analog Gamma 2.2 #1
	{0x00, 1, {0x00} },
	{0xE1, 16, {0x05,0x0A,0x15,0x22,0x2A,0x33,0x42,0x4E,0x4E,0x5A,0x5B,0x6E,0x96,0x85,0x85,0x79} },
	{0x00, 1, {0x10} },
	{0xE1, 8, {0x70,0x62,0x4F,0x43,0x38,0x25,0x0B,0x00} },

	{0x00, 1, {0x00} },
	{0xE2, 16, {0x05,0x0A,0x15,0x22,0x2A,0x33,0x42,0x4E,0x4E,0x5A,0x5B,0x6E,0x96,0x85,0x85,0x79} },
	{0x00, 1, {0x10} },
	{0xE2, 8, {0x70,0x62,0x4F,0x43,0x38,0x25,0x0B,0x00} },


	//reg_sd_sap
	{0x00, 1, {0x80} },
	{0xA4, 1, {0x8C} },

	//OSC Auto calibration
	{0x00, 1, {0xA0} },
	{0xF3, 1, {0x10} },

//############################################################################################################
//===FT8201AB===
// B3A1~B3A4 ³]©wžÑªR«×
// B3A5=0x00 Source Resolution B3A5[7:5]
//B3A6=0x13  [4]:reg_panel_sd_resol, [1]:reg_panel_size, [0]:reg_panel_zigzag
	{0x00, 1, {0xA1} },
	{0xB3, 2, {0x03,0x20} },
	{0x00, 1, {0xA3} },
	{0xB3, 2, {0x05,0x00} },
	{0x00, 1, {0xA5} },
	{0xB3, 2, {0x00,0x13} },

	// DMA Setting C1D0=0x30 (Single Chip) / 0xb0 (Multiple Drop)
	{0x00, 1, {0xD0} },
	{0xC1, 1, {0x30} },

	// ## GOA Timing ##

	//SKIP LVD Power-OFF0
	{0x00, 1, {0x80} },
	{0xCB, 7, {0x33,0x33,0x30,0x33,0x30,0x33,0x30} },
	{0x00, 1, {0x87} },
	{0xCB, 1, {0x33} },
	{0x00, 1, {0x88} },
	{0xCB, 8, {0x33,0x33,0x33,0x33,0x33,0x30,0x33,0x33} },
	{0x00, 1, {0x90} },
	{0xCB, 7, {0x30,0x33,0x33,0x33,0x30,0x30,0x33} },
	{0x00, 1, {0x97} },
	{0xCB, 1, {0x33} },

	//Power-OFF NORM BOE8"_M8
	{0x00, 1, {0x98} },
	{0xCB, 8, {0x00,0x00,0x00,0x04,0x00,0x05,0x00,0x00} },
	{0x00, 1, {0xA0} },
	{0xCB, 8, {0x04,0x04,0x04,0x04,0x00,0x00,0x04,0x08} },
	{0x00, 1, {0xA8} },
	{0xCB, 8, {0x00,0x07,0x04,0x00,0x00,0x00,0x00,0x00} },

	//Power-On BOE8"_M8
	{0x00, 1, {0xB0} },
	{0xCB, 7, {0x00,0x00,0x00,0x00,0x00,0x00,0x00} },
	{0x00, 1, {0xB7} },
	{0xCB, 1, {0x00} },
	{0x00, 1, {0xB8} },
	{0xCB, 8, {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00} },
	{0x00, 1, {0xC0} },
	{0xCB, 7, {0x00,0xFF,0x00,0x00,0x00,0x00,0x00} },
	{0x00, 1, {0xC7} },
	{0xCB, 1, {0x00} },

	//U2D_BOE8"_M8_20201201
	{0x00, 1, {0x80} },
	{0xCC, 8, {0x29,0x29,0x08,0x08,0x20,0x20,0x2A,0x2A} },
	{0x00, 1, {0x88} },
	{0xCC, 8, {0x2B,0x2B,0x2C,0x2C,0x1A,0x1A,0x19,0x19} },
	{0x00, 1, {0x90} },
	{0xCC, 6, {0x18,0x18,0x17,0x17,0x00,0x00} },
	{0x00, 1, {0x80} },
	{0xCD, 8, {0x29,0x29,0x07,0x07,0x1F,0x1F,0x2A,0x2A} },
	{0x00, 1, {0x88} },
	{0xCD, 8, {0x2B,0x2B,0x2C,0x2C,0x16,0x16,0x15,0x15} },
	{0x00, 1, {0x90} },
	{0xCD, 6, {0x14,0x14,0x13,0x13,0x00,0x00} },

	{0x00, 1, {0xA0} },
	{0xCC, 8, {0x29,0x29,0x07,0x07,0x1F,0x1F,0x2A,0x2A} },
	{0x00, 1, {0xA8} },
	{0xCC, 8, {0x2B,0x2B,0x2C,0x2C,0x13,0x13,0x14,0x14} },
	{0x00, 1, {0xB0} },
	{0xCC, 6, {0x15,0x15,0x16,0x16,0x00,0x00} },
	{0x00, 1, {0xA0} },
	{0xCD, 8, {0x29,0x29,0x08,0x08,0x20,0x20,0x2A,0x2A} },
	{0x00, 1, {0xA8} },
	{0xCD, 8, {0x2B,0x2B,0x2C,0x2C,0x17,0x17,0x18,0x18} },
	{0x00, 1, {0xB0} },
	{0xCD, 6, {0x19,0x19,0x1A,0x1A,0x00,0x00} },


	//goa_vstx_shift_cnt_sel (20200812)
	{0x00, 1, {0x81} },
	{0xC2, 1, {0x40} },

	{0x00, 1, {0x90} },
	{0xC2, 4, {0x88,0x02,0xBD,0xA0} },

	//VST6	 BOE 8" STV3
	{0x00, 1, {0x94} },
	{0xC2, 4, {0x87,0x02,0xBD,0xA0} },

	//VEND5	 BOE 8" STV2
	{0x00, 1, {0xB0} },
	{0xC2, 4, {0x13,0x02,0xBD,0xA0} },

	//VEND6	 BOE 8" STV4
	{0x00, 1, {0xB4} },
	{0xC2, 4, {0x14,0x02,0xBD,0xA0} },

	//CLKC_BOE8" Gout_R	side_CLK1357
	{0x00, 1, {0x80} },
	{0xC3, 8, {0x82,0x09,0x02,0xFF,0xA0,0x00,0x02,0x07} },
	{0x00, 1, {0x88} },
	{0xC3, 8, {0x00,0x0B,0x02,0xFF,0xA0,0x00,0x02,0x07} },
	{0x00, 1, {0x90} },
	{0xC3, 8, {0x86,0x0D,0x02,0xFF,0xA0,0x00,0x02,0x07} },
	{0x00, 1, {0x98} },
	{0xC3, 8, {0x84,0x0F,0x02,0xFF,0xA0,0x00,0x02,0x07} },

	//CLKD_BOE8" Gout_L	side_CLK2468
	{0x00, 1, {0xC0} },
	{0xCD, 8, {0x81,0x0A,0x02,0xFF,0xA0,0x00,0x02,0x07} },
	{0x00, 1, {0xC8} },
	{0xCD, 8, {0x01,0x0C,0x02,0xFF,0xA0,0x00,0x02,0x07} },
	{0x00, 1, {0xD0} },
	{0xCD, 8, {0x85,0x0E,0x02,0xFF,0xA0,0x00,0x02,0x07} },
	{0x00, 1, {0xD8} },
	{0xCD, 8, {0x83,0x10,0x02,0xFF,0xA0,0x00,0x02,0x07} },

	//GOFF1	BOE8" GCL
	{0x00, 1, {0xE0} },
	{0xC3, 4, {0x26,0xA0,0x01,0x00} },
	//GOFF1	BOE8" GCH
	{0x00, 1, {0xE4} },
	{0xC3, 4, {0x26,0xA0,0x01,0x00} },
		
	//ECLK (20200812)
	{0x00, 1, {0xF0} },
	{0xCC, 4, {0x3D,0x88,0x88,0xC2,0x90} },

	//CLK EQ
	{0x00, 1, {0xC8} },
	{0xC3, 1, {0x0F} },
	//=============== ## TCON Timing START ##	===============//
	{0x00, 1, {0x80} },
	{0xC0, 6, {0x00 ,0xD6 ,0x00 ,0xC1 ,0x00 ,0x10} },

	{0x00, 1, {0x90} },
	{0xC0, 6, {0x00 ,0xD6 ,0x00 ,0xC1 ,0x00 ,0x10} },

	{0x00, 1, {0xA0} },
	{0xC0, 6, {0x01 ,0xAC ,0x00 ,0xC1 ,0x00 ,0x10} },

	{0x00, 1, {0xB0} },
	{0xC0, 5, {0x00 ,0xD6 ,0x00 ,0xC1 ,0x10} },

	{0x00, 1, {0xA3} },
	{0xC1, 3, {0x24, 0x1A, 0x04} },

	{0x00, 1, {0x80} },
	{0xCE, 1, {0x00 } },

	{0x00, 1, {0x90} },
	{0xCE, 1, {0x00 } },

	{0x00, 1, {0xD0} },
	{0xCE, 8, {0x01 ,0x00 ,0x1E ,0x01 ,0x01 ,0x00 ,0xA1 ,0x00} },

	{0x00, 1, {0xE0} },
	{0xCE, 1, {0x00} },

	{0x00, 1, {0xF0} },
	{0xCE, 1, {0x00} },

	{0x00, 1, {0xB0} },
	{0xCF, 4, {0x00 ,0x00 ,0x4B ,0x4F} },

	{0x00, 1, {0xB5} },
	{0xCF, 4, {0x02 ,0x02 ,0x7A ,0x7E} },

	{0x00, 1, {0xC0} },
	{0xCF, 4, {0x04 ,0x04 ,0xA0 ,0xA4} },

	{0x00, 1, {0xC5} },
	{0xCF, 4, {0x00 ,0x05 ,0x08 ,0x00} },

	//=============== ## TCON Timing END ##	===============//

	//CLK Source delay	(20200812)
	{0x00, 1, {0x90} },
	{0xC4, 1, {0x88} },

	//CLK allon
	{0x00, 1, {0x92} },
	{0xC4, 1, {0xC0} },

	//VGHO1 sink OFF (20210125)
	{0x00, 1, {0xA8} },
	{0xC5, 1, {0x09} }, //sink mode select 9

	//VGLO1 sink ON
	{0x00, 1, {0xCB} },
	{0xC5, 1, {0x01} },

	//reg_goa_select_dg_rtn
	{0x00, 1, {0xFD} },
	{0xCB, 1, {0x82} },

	//pump VGH afd off (20210125)(ACmode off)
	{0x00, 1, {0x9F} },
	{0xC5, 1, {0x00} },

	//PUMP  VGH CLK all on disable
	{0x00, 1, {0x91} },
	{0xC5, 1, {0x4C} },

	//tcon_tp_term2_vb_ln
	{0x00, 1, {0xD7} },
	{0xCE, 1, {0x01} },

	//VGH CLK Line Rate normal:5(4line)
	{0x00, 1, {0x94} },
	{0xC5, 1, {0x46} },

	//tp term VGH pump off CLK gating L (20210125)
	{0x00, 1, {0x98} },
	{0xC5, 1, {0x64} }, //VGH CLK Line Rate Idle 5(4line)

	//VGL CLK Line Rate normal:5(4line)
	{0x00, 1, {0x9B} },
	{0xC5, 1, {0x65} },

	//VGL CLK Line Rate Idle:5(4line)
	{0x00, 1, {0x9D} },
	{0xC5, 1, {0x65} },


	//VGH pump AFD sequence
	{0x00, 1, {0x8C} },
	{0xCF, 2, {0x40,0x40} },

	//en_Vcom can't be gating by LVD
	{0x00, 1, {0x9A} },
	{0xF5, 1, {0x35} },

	//Source pull low setting
	{0x00, 1, {0xA2} },
	{0xF5, 1, {0x1F} },

	//Vsync sync with mipi
	{0x00, 1, {0xA0} },
	{0xC1, 1, {0xE0} },

	//FIFO
	{0x1C, 1, {0x00} },

	//tp_term_sync_vb
	{0x00, 1, {0xC9} },
	{0xCE, 1, {0x00} },
	//============CMD WR disable=============
	{0x00, 1, {0x00} },
	{0xFF, 3, {0x00,0x00,0x00} },

	{0x00, 1, {0x80} },
	{0xFF, 2, {0x00,0x00} },

	{0x11,0x01,{0x00}},
	{REGFLAG_DELAY,120,{}},

	{0x29,0x01,{0x00}},
	{0x35, 1, {0x00} },
	{REGFLAG_DELAY,100,{}}

//	{REGFLAG_END_OF_TABLE, 0x00, {}}
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

	params->dsi.vertical_sync_active = 8;
	params->dsi.vertical_backporch = 8;
	params->dsi.vertical_frontporch = 193;
	//params->dsi.vertical_frontporch_for_low_power = 540;/*disable dynamic frame rate*/
	params->dsi.vertical_active_line = FRAME_HEIGHT;

	params->dsi.horizontal_sync_active = 6;
	params->dsi.horizontal_backporch = 5;
	params->dsi.horizontal_frontporch = 6;
	params->dsi.horizontal_active_pixel = FRAME_WIDTH;
	params->dsi.ssc_range = 4;
	params->dsi.ssc_disable = 1;
	/*params->dsi.ssc_disable = 0;*/
#ifndef CONFIG_FPGA_EARLY_PORTING
#if (LCM_DSI_CMD_MODE)
	params->dsi.PLL_CLOCK = 270;	/* this value must be in MTK suggested table */
#else
	params->dsi.PLL_CLOCK = 231;//283;	/* this value must be in MTK suggested table */
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
	params->backlight_cust[0].max_bl_lvl = 1010;	//818 20mA, 859 21mA, 982 24mA, 1010 24.7mA
	params->backlight_cust[0].min_bl_lvl = 32;

#ifdef CONFIG_MTK_ROUND_CORNER_SUPPORT
	params->round_corner_en = 0;
	params->corner_pattern_width = 720;
	params->corner_pattern_height = 32;
#endif
	//LCM_LOGI("ft8201m_boe----lcm mode = vdo mode second11 ----\n");
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
	if(!mtp_gesture_mode) {
		pr_err("[LCM][GPIO]lcm_suspend_power !\n");
		ctp_reset_pin(0);
		MDELAY(2);
		lcm_reset_pin(0);
		MDELAY(10);
		lcm_power_disable();
	}
}

static void lcm_resume_power(void)
{
	/*if(!tp_gesture_mode) {
		pr_err("[LCM][GPIO]lcm_resume_power !\n");
		lcm_power_enable();
	}*/
	lcm_power_enable();
	MDELAY(3);

	pr_err("[LCM][GPIO]lcm_resume_power !\n");

	lcm_reset_pin(1);
	MDELAY(7);
	if(!mtp_gesture_mode) {
		ctp_reset_pin(0);
		MDELAY(5);
	}
/*
	if(mtp_gesture_mode) {
		lcm_power_disable();
		MDELAY(5);
	}
*/
	lcm_reset_pin(0);
	MDELAY(5);



	if(!mtp_gesture_mode) {
		ctp_reset_pin(1);
		MDELAY(5);
	}

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
	LCM_LOGI("ft8201m_boe----lcm mode = vdo mode second ----\n");

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
		LCM_LOGI("ft8201m_boe----lcm mode = vdo mode ----\n");
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
struct LCM_DRIVER ft8201m_wxga_cmd_boe_lcm_drv = {
	.name = "ft8201m_wxga_cmd_boe",
#else

struct LCM_DRIVER ft8201m_wxga_vdo_incell_boe_lcm_drv = {
	.name = "ft8201m_wxga_vdo_incell_boe",
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
