/*
 * SGM41542 battery charging driver
 *
 * Copyright (C) 2013 Texas Instruments
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#define pr_fmt(fmt)	"[sgm41542]:%s: " fmt, __func__

#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/err.h>
#include <linux/bitops.h>
#include <linux/math64.h>
#include <mt-plat/charger_type.h>
#include <linux/hardware_info.h>

#include "mtk_charger_intf.h"
#include "sgm41542_reg.h"
#include "sgm41542.h"

enum sgm41542_part_no {

	PN_SGM41542,
};

static int pn_data[] = {

	[PN_SGM41542] = 0x0D,
};

static char *pn_str[] = {

	[PN_SGM41542] = "sgm41542",
};

struct sgm41542 {
	struct device *dev;
	struct i2c_client *client;

	enum sgm41542_part_no part_no;
	int revision;

	const char *chg_dev_name;
	const char *eint_name;

	bool chg_det_enable;

	enum charger_type chg_type;

	int status;
	int irq;

	struct mutex i2c_rw_lock;

	bool charge_enabled;	/* Register bit status */
	bool power_good;
	bool hz_mode;

	struct sgm41542_platform_data *platform_data;
	struct charger_device *chg_dev;

	struct power_supply *psy;
#if 0
	struct pinctrl* pogo_otg_ctrl;
	struct pinctrl_state* pogo_otg_default;
	struct pinctrl_state* pogo_otg_on;
	struct pinctrl_state* pogo_otg_off;
#endif
};

static const struct charger_properties sgm41542_chg_props = {
	.alias_name = "sgm41542",
};

static int __sgm41542_read_reg(struct sgm41542 *bq, u8 reg, u8 *data)
{
	s32 ret;

	ret = i2c_smbus_read_byte_data(bq->client, reg);
	if (ret < 0) {
		pr_err("i2c read fail: can't read from reg 0x%02X\n", reg);
		return ret;
	}

	*data = (u8) ret;

	return 0;
}

static int __sgm41542_write_reg(struct sgm41542 *bq, int reg, u8 val)
{
	s32 ret;

	ret = i2c_smbus_write_byte_data(bq->client, reg, val);
	if (ret < 0) {
		pr_err("i2c write fail: can't write 0x%02X to reg 0x%02X: %d\n",
		       val, reg, ret);
		return ret;
	}
	return 0;
}

static int sgm41542_read_byte(struct sgm41542 *bq, u8 reg, u8 *data)
{
	int ret;

	mutex_lock(&bq->i2c_rw_lock);
	ret = __sgm41542_read_reg(bq, reg, data);
	mutex_unlock(&bq->i2c_rw_lock);

	return ret;
}

static int sgm41542_write_byte(struct sgm41542 *bq, u8 reg, u8 data)
{
	int ret;

	mutex_lock(&bq->i2c_rw_lock);
	ret = __sgm41542_write_reg(bq, reg, data);
	mutex_unlock(&bq->i2c_rw_lock);

	if (ret)
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);

	return ret;
}

static int sgm41542_update_bits(struct sgm41542 *bq, u8 reg, u8 mask, u8 data)
{
	int ret;
	u8 tmp;

	mutex_lock(&bq->i2c_rw_lock);
	ret = __sgm41542_read_reg(bq, reg, &tmp);
	if (ret) {
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);
		goto out;
	}

	tmp &= ~mask;
	tmp |= data & mask;

	ret = __sgm41542_write_reg(bq, reg, tmp);
	if (ret)
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);

out:
	mutex_unlock(&bq->i2c_rw_lock);
	return ret;
}

int sgm41542_enter_hiz_mode(struct sgm41542 *bq)
{
	u8 val = REG00_HIZ_ENABLE << REG00_ENHIZ_SHIFT;

	return sgm41542_update_bits(bq, SGM41542_REG_00, REG00_ENHIZ_MASK, val);

}
EXPORT_SYMBOL_GPL(sgm41542_enter_hiz_mode);

int sgm41542_exit_hiz_mode(struct sgm41542 *bq)
{

	u8 val = REG00_HIZ_DISABLE << REG00_ENHIZ_SHIFT;

	return sgm41542_update_bits(bq, SGM41542_REG_00, REG00_ENHIZ_MASK, val);

}
EXPORT_SYMBOL_GPL(sgm41542_exit_hiz_mode);

int sgm41542_set_hz_mode(struct charger_device *chg_dev, bool en)
{
	struct sgm41542 *bq = dev_get_drvdata(&chg_dev->dev);

	bq->hz_mode = en;
	if(en){
		sgm41542_enter_hiz_mode(bq);
	}else{
		sgm41542_exit_hiz_mode(bq);
	}

	return 0;
}

int sgm41542_get_hiz_mode(struct sgm41542 *bq, u8 *state)
{
	u8 val;
	int ret;

	ret = sgm41542_read_byte(bq, SGM41542_REG_00, &val);
	if (ret)
		return ret;
	*state = (val & REG00_ENHIZ_MASK) >> REG00_ENHIZ_SHIFT;

	return 0;
}
EXPORT_SYMBOL_GPL(sgm41542_get_hiz_mode);

static int sgm41542_enable_otg(struct sgm41542 *bq)
{
	u8 val = REG01_OTG_ENABLE << REG01_OTG_CONFIG_SHIFT;
#if 0
	if((!IS_ERR(bq->pogo_otg_ctrl))&&(!IS_ERR(bq->pogo_otg_on))){
		pr_err("sgm41542_enable_otg pogo\n");
		pinctrl_select_state(bq->pogo_otg_ctrl, bq->pogo_otg_on);
	}
#endif

	return sgm41542_update_bits(bq, SGM41542_REG_01, REG01_OTG_CONFIG_MASK,
				   val);

}

static int sgm41542_disable_otg(struct sgm41542 *bq)
{
	u8 val = REG01_OTG_DISABLE << REG01_OTG_CONFIG_SHIFT;
#if 0
	if((!IS_ERR(bq->pogo_otg_ctrl))&&(!IS_ERR(bq->pogo_otg_off))){
		pr_err("sgm41542_disable_otg pogo\n");
		pinctrl_select_state(bq->pogo_otg_ctrl, bq->pogo_otg_off);
	}
#endif
	return sgm41542_update_bits(bq, SGM41542_REG_01, REG01_OTG_CONFIG_MASK,
				   val);

}

static int sgm41542_enable_charger(struct sgm41542 *bq)
{
	int ret;
	u8 val = REG01_CHG_ENABLE << REG01_CHG_CONFIG_SHIFT;

	ret =
	    sgm41542_update_bits(bq, SGM41542_REG_01, REG01_CHG_CONFIG_MASK, val);

	if(bq->hz_mode)
		sgm41542_exit_hiz_mode(bq);

	return ret;
}

static int sgm41542_disable_charger(struct sgm41542 *bq)
{
	int ret;
	u8 val = REG01_CHG_DISABLE << REG01_CHG_CONFIG_SHIFT;

	ret =
	    sgm41542_update_bits(bq, SGM41542_REG_01, REG01_CHG_CONFIG_MASK, val);

	if(bq->hz_mode)
		sgm41542_enter_hiz_mode(bq);

	return ret;
}

int sgm41542_set_chargecurrent(struct sgm41542 *bq, int curr)
{
	u8 ichg;

	if (curr < REG02_ICHG_BASE)
		curr = REG02_ICHG_BASE;

	ichg = (curr - REG02_ICHG_BASE) / REG02_ICHG_LSB;
	return sgm41542_update_bits(bq, SGM41542_REG_02, REG02_ICHG_MASK,
				   ichg << REG02_ICHG_SHIFT);

}

int sgm41542_set_term_current(struct sgm41542 *bq, int curr)
{
	u8 iterm;

	if (curr < REG03_ITERM_BASE)
		curr = REG03_ITERM_BASE;

	iterm = (curr - REG03_ITERM_BASE) / REG03_ITERM_LSB;

	return sgm41542_update_bits(bq, SGM41542_REG_03, REG03_ITERM_MASK,
				   iterm << REG03_ITERM_SHIFT);
}
EXPORT_SYMBOL_GPL(sgm41542_set_term_current);

int sgm41542_set_prechg_current(struct sgm41542 *bq, int curr)
{
	u8 iprechg;

	if (curr < REG03_IPRECHG_BASE)
		curr = REG03_IPRECHG_BASE;

	iprechg = (curr - REG03_IPRECHG_BASE) / REG03_IPRECHG_LSB;

	return sgm41542_update_bits(bq, SGM41542_REG_03, REG03_IPRECHG_MASK,
				   iprechg << REG03_IPRECHG_SHIFT);
}
EXPORT_SYMBOL_GPL(sgm41542_set_prechg_current);

int sgm41542_set_chargevolt(struct sgm41542 *bq, int volt)
{
	u8 val;

	if (volt < REG04_VREG_BASE)
		volt = REG04_VREG_BASE;

	val = (volt - REG04_VREG_BASE) / REG04_VREG_LSB;
	return sgm41542_update_bits(bq, SGM41542_REG_04, REG04_VREG_MASK,
				   val << REG04_VREG_SHIFT);
}

int sgm41542_set_input_volt_limit(struct sgm41542 *bq, int volt)
{
	u8 val;

	if (volt < REG06_VINDPM_BASE)
		volt = REG06_VINDPM_BASE;

	val = (volt - REG06_VINDPM_BASE) / REG06_VINDPM_LSB;
	return sgm41542_update_bits(bq, SGM41542_REG_06, REG06_VINDPM_MASK,
				   val << REG06_VINDPM_SHIFT);
}

int sgm41542_set_input_current_limit(struct sgm41542 *bq, int curr)
{
	u8 val;

	if (curr < REG00_IINLIM_BASE)
		curr = REG00_IINLIM_BASE;

	val = (curr - REG00_IINLIM_BASE) / REG00_IINLIM_LSB;
	return sgm41542_update_bits(bq, SGM41542_REG_00, REG00_IINLIM_MASK,
				   val << REG00_IINLIM_SHIFT);
}

int sgm41542_set_watchdog_timer(struct sgm41542 *bq, u8 timeout)
{
	u8 temp;

	temp = (u8) (((timeout -
		       REG05_WDT_BASE) / REG05_WDT_LSB) << REG05_WDT_SHIFT);

	return sgm41542_update_bits(bq, SGM41542_REG_05, REG05_WDT_MASK, temp);
}
EXPORT_SYMBOL_GPL(sgm41542_set_watchdog_timer);

int sgm41542_disable_watchdog_timer(struct sgm41542 *bq)
{
	u8 val = REG05_WDT_DISABLE << REG05_WDT_SHIFT;

	return sgm41542_update_bits(bq, SGM41542_REG_05, REG05_WDT_MASK, val);
}
EXPORT_SYMBOL_GPL(sgm41542_disable_watchdog_timer);

int sgm41542_reset_watchdog_timer(struct sgm41542 *bq)
{
	u8 val = REG01_WDT_RESET << REG01_WDT_RESET_SHIFT;

	return sgm41542_update_bits(bq, SGM41542_REG_01, REG01_WDT_RESET_MASK,
				   val);
}
EXPORT_SYMBOL_GPL(sgm41542_reset_watchdog_timer);

int sgm41542_reset_chip(struct sgm41542 *bq)
{
	int ret;
	u8 val = REG0B_REG_RESET << REG0B_REG_RESET_SHIFT;

	ret =
	    sgm41542_update_bits(bq, SGM41542_REG_0B, REG0B_REG_RESET_MASK, val);
	return ret;
}
EXPORT_SYMBOL_GPL(sgm41542_reset_chip);

static int sgm41542_enable_term(struct sgm41542 *bq, bool enable)
{
	u8 val;
	int ret;

	if (enable)
		val = REG05_TERM_ENABLE << REG05_EN_TERM_SHIFT;
	else
		val = REG05_TERM_DISABLE << REG05_EN_TERM_SHIFT;

	ret = sgm41542_update_bits(bq, SGM41542_REG_05, REG05_EN_TERM_MASK, val);

	return ret;
}
EXPORT_SYMBOL_GPL(sgm41542_enable_term);

int sgm41542_set_boost_current(struct sgm41542 *bq, int curr)
{
	u8 val;

	val = REG02_BOOST_LIM_0P5A;
	if (curr >= BOOSTI_1200)
		val = REG02_BOOST_LIM_1P2A;

	return sgm41542_update_bits(bq, SGM41542_REG_02, REG02_BOOST_LIM_MASK,
				   val << REG02_BOOST_LIM_SHIFT);
}

int sgm41542_set_boost_voltage(struct sgm41542 *bq, int volt)
{
	u8 val;

	if (volt == BOOSTV_4850)
		val = REG06_BOOSTV_4P85V;
	else if (volt == BOOSTV_5150)
		val = REG06_BOOSTV_5P15V;
	else if (volt == BOOSTV_5300)
		val = REG06_BOOSTV_5P3V;
	else
		val = REG06_BOOSTV_5V;

	return sgm41542_update_bits(bq, SGM41542_REG_06, REG06_BOOSTV_MASK,
				   val << REG06_BOOSTV_SHIFT);
}
EXPORT_SYMBOL_GPL(sgm41542_set_boost_voltage);

static int sgm41542_set_acovp_threshold(struct sgm41542 *bq, int volt)
{
	u8 val;

	if (volt == VAC_OVP_14000)
		val = REG06_OVP_14P0V;
	else if (volt == VAC_OVP_10500)
		val = REG06_OVP_10P5V;
	else if (volt == VAC_OVP_6500)
		val = REG06_OVP_6P5V;
	else
		val = REG06_OVP_5P5V;

	return sgm41542_update_bits(bq, SGM41542_REG_06, REG06_OVP_MASK,
				   val << REG06_OVP_SHIFT);
}
EXPORT_SYMBOL_GPL(sgm41542_set_acovp_threshold);

static int sgm41542_set_stat_ctrl(struct sgm41542 *bq, int ctrl)
{
	u8 val;

	val = ctrl;

	return sgm41542_update_bits(bq, SGM41542_REG_00, REG00_STAT_CTRL_MASK,
				   val << REG00_STAT_CTRL_SHIFT);
}

static int sgm41542_set_int_mask(struct sgm41542 *bq, int mask)
{
	u8 val;

	val = mask;

	return sgm41542_update_bits(bq, SGM41542_REG_0A, REG0A_INT_MASK_MASK,
				   val << REG0A_INT_MASK_SHIFT);
}

static int sgm41542_set_vdpm_bat_trace(struct sgm41542 *bq, int mask)
{
	u8 val;

	val = mask;

	return sgm41542_update_bits(bq,  SGM41542_REG_07, REG07_VDPM_BAT_TRACK_MASK,
				   val << REG07_VDPM_BAT_TRACK_SHIFT);
}


static int sgm41542_enable_batfet(struct sgm41542 *bq)
{
	const u8 val = REG07_BATFET_ON << REG07_BATFET_DIS_SHIFT;

	return sgm41542_update_bits(bq, SGM41542_REG_07, REG07_BATFET_DIS_MASK,
				   val);
}
EXPORT_SYMBOL_GPL(sgm41542_enable_batfet);

static int sgm41542_disable_batfet(struct sgm41542 *bq)
{
	const u8 val = REG07_BATFET_OFF << REG07_BATFET_DIS_SHIFT;

	return sgm41542_update_bits(bq, SGM41542_REG_07, REG07_BATFET_DIS_MASK,
				   val);
}
EXPORT_SYMBOL_GPL(sgm41542_disable_batfet);

static int sgm41542_set_batfet_delay(struct sgm41542 *bq, uint8_t delay)
{
	u8 val;

	if (delay == 0)
		val = REG07_BATFET_DLY_0S;
	else
		val = REG07_BATFET_DLY_10S;

	val <<= REG07_BATFET_DLY_SHIFT;

	return sgm41542_update_bits(bq, SGM41542_REG_07, REG07_BATFET_DLY_MASK,
				   val);
}
EXPORT_SYMBOL_GPL(sgm41542_set_batfet_delay);

static int sgm41542_enable_safety_timer(struct sgm41542 *bq)
{
	const u8 val = REG05_CHG_TIMER_ENABLE << REG05_EN_TIMER_SHIFT;

	return sgm41542_update_bits(bq, SGM41542_REG_05, REG05_EN_TIMER_MASK,
				   val);
}
EXPORT_SYMBOL_GPL(sgm41542_enable_safety_timer);

static int sgm41542_disable_safety_timer(struct sgm41542 *bq)
{
	const u8 val = REG05_CHG_TIMER_DISABLE << REG05_EN_TIMER_SHIFT;

	return sgm41542_update_bits(bq, SGM41542_REG_05, REG05_EN_TIMER_MASK,
				   val);
}
EXPORT_SYMBOL_GPL(sgm41542_disable_safety_timer);

static struct sgm41542_platform_data *sgm41542_parse_dt(struct device_node *np,
						      struct sgm41542 *bq)
{
	int ret;
	struct sgm41542_platform_data *pdata;

	pdata = devm_kzalloc(bq->dev, sizeof(struct sgm41542_platform_data),
			     GFP_KERNEL);
	if (!pdata)
		return NULL;
#if 0
	bq->pogo_otg_ctrl = devm_pinctrl_get(bq->dev);
	
	if (IS_ERR(bq->pogo_otg_ctrl)) {
		pr_warn("cannot find pogo_otgctrl\n");	
	}else{
		bq->pogo_otg_default = pinctrl_lookup_state(bq->pogo_otg_ctrl, "default");
		if (IS_ERR(bq->pogo_otg_default)) {
			pr_warn("cannot find  pogo_otg_default\n");
		}
		bq->pogo_otg_on = pinctrl_lookup_state(bq->pogo_otg_ctrl, "pogo_otg_on");
		if (IS_ERR(bq->pogo_otg_on)) {
			pr_warn("cannot find pogo_otg_on\n");
		}
		bq->pogo_otg_off = pinctrl_lookup_state(bq->pogo_otg_ctrl, "pogo_otg_off");
		if (IS_ERR(bq->pogo_otg_on)) {
			pr_warn("cannot find pogo_otg_off\n");
		}	
	}
#endif
	if (of_property_read_string(np, "charger_name", &bq->chg_dev_name) < 0) {
		bq->chg_dev_name = "primary_chg";
		pr_warn("no charger name\n");
	}

	if (of_property_read_string(np, "eint_name", &bq->eint_name) < 0) {
		bq->eint_name = "chr_stat";
		pr_warn("no eint name\n");
	}

	bq->chg_det_enable =
	    of_property_read_bool(np, "ti,sgm41542,charge-detect-enable");

	ret = of_property_read_u32(np, "ti,sgm41542,usb-vlim", &pdata->usb.vlim);
	if (ret) {
		pdata->usb.vlim = 4500;
		pr_err("Failed to read node of ti,sgm41542,usb-vlim\n");
	}

	ret = of_property_read_u32(np, "ti,sgm41542,usb-ilim", &pdata->usb.ilim);
	if (ret) {
		pdata->usb.ilim = 2000;
		pr_err("Failed to read node of ti,sgm41542,usb-ilim\n");
	}

	ret = of_property_read_u32(np, "ti,sgm41542,usb-vreg", &pdata->usb.vreg);
	if (ret) {
		pdata->usb.vreg = 4200;
		pr_err("Failed to read node of ti,sgm41542,usb-vreg\n");
	}

	ret = of_property_read_u32(np, "ti,sgm41542,usb-ichg", &pdata->usb.ichg);
	if (ret) {
		pdata->usb.ichg = 2000;
		pr_err("Failed to read node of ti,sgm41542,usb-ichg\n");
	}

	ret = of_property_read_u32(np, "ti,sgm41542,stat-pin-ctrl",
				   &pdata->statctrl);
	if (ret) {
		pdata->statctrl = 0;
		pr_err("Failed to read node of ti,sgm41542,stat-pin-ctrl\n");
	}

	ret = of_property_read_u32(np, "ti,sgm41542,precharge-current",
				   &pdata->iprechg);
	if (ret) {
		pdata->iprechg = 180;
		pr_err("Failed to read node of ti,sgm41542,precharge-current\n");
	}

	ret = of_property_read_u32(np, "ti,sgm41542,termination-current",
				   &pdata->iterm);
	if (ret) {
		pdata->iterm = 180;
		pr_err
		    ("Failed to read node of ti,sgm41542,termination-current\n");
	}

	ret =
	    of_property_read_u32(np, "ti,sgm41542,boost-voltage",
				 &pdata->boostv);
	if (ret) {
		pdata->boostv = 5000;
		pr_err("Failed to read node of ti,sgm41542,boost-voltage\n");
	}

	ret =
	    of_property_read_u32(np, "ti,sgm41542,boost-current",
				 &pdata->boosti);
	if (ret) {
		pdata->boosti = 1200;
		pr_err("Failed to read node of ti,sgm41542,boost-current\n");
	}

	ret = of_property_read_u32(np, "ti,sgm41542,vac-ovp-threshold",
				   &pdata->vac_ovp);
	if (ret) {
		pdata->vac_ovp = 6500;
		pr_err("Failed to read node of ti,sgm41542,vac-ovp-threshold\n");
	}

	return pdata;
}

#if 0
static int sgm41542_get_charger_type(struct sgm41542 *bq, enum charger_type *type)
{
	int ret;

	u8 reg_val = 0;
	int vbus_stat = 0;
	enum charger_type chg_type = CHARGER_UNKNOWN;

	ret = sgm41542_read_byte(bq, SGM41542_REG_08, &reg_val);

	if (ret)
		return ret;

	vbus_stat = (reg_val & REG08_VBUS_STAT_MASK);
	vbus_stat >>= REG08_VBUS_STAT_SHIFT;

	switch (vbus_stat) {

	case REG08_VBUS_TYPE_NONE:
		chg_type = CHARGER_UNKNOWN;
		break;
	case REG08_VBUS_TYPE_SDP:
		chg_type = STANDARD_HOST;
		break;
	case REG08_VBUS_TYPE_CDP:
		chg_type = CHARGING_HOST;
		break;
	case REG08_VBUS_TYPE_DCP:
		chg_type = STANDARD_CHARGER;
		break;
	case REG08_VBUS_TYPE_UNKNOWN:
		chg_type = NONSTANDARD_CHARGER;
		break;
	case REG08_VBUS_TYPE_NON_STD:
		chg_type = NONSTANDARD_CHARGER;
		break;
	default:
		chg_type = NONSTANDARD_CHARGER;
		break;
	}

	*type = chg_type;

	return 0;
}

static int sgm41542_inform_charger_type(struct sgm41542 *bq)
{
	int ret = 0;
	union power_supply_propval propval;

	if (!bq->psy) {
		bq->psy = power_supply_get_by_name("charger");
		if (!bq->psy)
			return -ENODEV;
	}

	if (bq->chg_type != CHARGER_UNKNOWN)
		propval.intval = 1;
	else
		propval.intval = 0;

	ret = power_supply_set_property(bq->psy, POWER_SUPPLY_PROP_ONLINE,
					&propval);

	if (ret < 0)
		pr_notice("inform power supply online failed:%d\n", ret);

	propval.intval = bq->chg_type;

	ret = power_supply_set_property(bq->psy,
					POWER_SUPPLY_PROP_CHARGE_TYPE,
					&propval);

	if (ret < 0)
		pr_notice("inform power supply charge type failed:%d\n", ret);

	return ret;
}

static irqreturn_t sgm41542_irq_handler(int irq, void *data)
{
	int ret;
	u8 reg_val;
	bool prev_pg;
	enum charger_type prev_chg_type;

	ret = sgm41542_read_byte(bq, SGM41542_REG_08, &reg_val);
	if (ret)
		return IRQ_HANDLED;

	prev_pg = bq->power_good;

	bq->power_good = !!(reg_val & REG08_PG_STAT_MASK);

	if (!prev_pg && bq->power_good)
		pr_notice("adapter/usb inserted\n");
	else if (prev_pg && !bq->power_good)
		pr_notice("adapter/usb removed\n");

	prev_chg_type = bq->chg_type;

	ret = sgm41542_get_charger_type(bq, &bq->chg_type);
	if (!ret && prev_chg_type != bq->chg_type && bq->chg_det_enable)
		sgm41542_inform_charger_type(bq);

	return IRQ_HANDLED;
}

static int sgm41542_register_interrupt(struct sgm41542 *bq)
{
	int ret = 0;
	struct device_node *np;

	np = of_find_node_by_name(NULL, bq->eint_name);
	if (np) {
		bq->irq = irq_of_parse_and_map(np, 0);
	} else {
		pr_err("couldn't get irq node\n");
		return -ENODEV;
	}

	pr_info("irq = %d\n", bq->irq);

	ret = devm_request_threaded_irq(bq->dev, bq->irq, NULL,
					sgm41542_irq_handler,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					bq->eint_name, bq);
	if (ret < 0) {
		pr_err("request thread irq failed:%d\n", ret);
		return ret;
	}

	enable_irq_wake(bq->irq);

	return 0;
}
#endif

static int sgm41542_init_device(struct sgm41542 *bq)
{
	int ret;

	sgm41542_disable_watchdog_timer(bq);

	ret = sgm41542_set_stat_ctrl(bq, bq->platform_data->statctrl);
	if (ret)
		pr_err("Failed to set stat pin control mode, ret = %d\n", ret);

	ret = sgm41542_set_prechg_current(bq, bq->platform_data->iprechg);
	if (ret)
		pr_err("Failed to set prechg current, ret = %d\n", ret);

	ret = sgm41542_set_term_current(bq, bq->platform_data->iterm);
	if (ret)
		pr_err("Failed to set termination current, ret = %d\n", ret);

	ret = sgm41542_set_boost_voltage(bq, bq->platform_data->boostv);
	if (ret)
		pr_err("Failed to set boost voltage, ret = %d\n", ret);

	ret = sgm41542_set_boost_current(bq, bq->platform_data->boosti);
	if (ret)
		pr_err("Failed to set boost current, ret = %d\n", ret);

	ret = sgm41542_set_acovp_threshold(bq, bq->platform_data->vac_ovp);
	if (ret)
		pr_err("Failed to set acovp threshold, ret = %d\n", ret);

	ret = sgm41542_set_int_mask(bq,
				   REG0A_IINDPM_INT_MASK |
				   REG0A_VINDPM_INT_MASK);
	if (ret)
		pr_err("Failed to set vindpm and iindpm int mask\n");

	ret = sgm41542_set_vdpm_bat_trace(bq, REG07_VDPM_BAT_TRACK_300MV);
	if (ret)
		pr_err("Failed to set vdpm bat trace,ret = %d\n", ret);



	return 0;
}

#if 0
static void determine_initial_status(struct sgm41542 *bq)
{
	sgm41542_irq_handler(bq->irq, (void *) bq);
}
#endif

static int sgm41542_detect_device(struct sgm41542 *bq)
{
	int ret;
	u8 data;

	ret = sgm41542_read_byte(bq, SGM41542_REG_0B, &data);
	if (!ret) {
		bq->part_no = (data & REG0B_PN_MASK) >> REG0B_PN_SHIFT;
		bq->revision =
		    (data & REG0B_DEV_REV_MASK) >> REG0B_DEV_REV_SHIFT;
	}

	return ret;
}

static void sgm41542_dump_regs(struct sgm41542 *bq)
{
	int addr;
	u8 val;
	int ret;

	for (addr = 0x0; addr <= 0x0B; addr++) {
		msleep(2);
		ret = sgm41542_read_byte(bq, addr, &val);
		if (!ret)
	        	pr_err("Reg[%.2x] = 0x%.2x\n", addr, val);
	}
}

static ssize_t
sgm41542_show_registers(struct device *dev, struct device_attribute *attr,
		       char *buf)
{
	struct sgm41542 *bq = dev_get_drvdata(dev);
	u8 addr;
	u8 val;
	u8 tmpbuf[200];
	int len;
	int idx = 0;
	int ret;

	idx = snprintf(buf, PAGE_SIZE, "%s:\n", "sgm41542 Reg");
	for (addr = 0x0; addr <= 0x0B; addr++) {
		msleep(2);
		ret = sgm41542_read_byte(bq, addr, &val);
		if (ret == 0) {
			len = snprintf(tmpbuf, PAGE_SIZE - idx,
				       "Reg[%.2x] = 0x%.2x\n", addr, val);
			memcpy(&buf[idx], tmpbuf, len);
			idx += len;
		}
	}

	return idx;
}

static ssize_t
sgm41542_store_registers(struct device *dev,
			struct device_attribute *attr, const char *buf,
			size_t count)
{
	struct sgm41542 *bq = dev_get_drvdata(dev);
	int ret;
	unsigned int reg;
	unsigned int val;

	ret = sscanf(buf, "%x %x", &reg, &val);
	if (ret == 2 && reg < 0x0B) {
		sgm41542_write_byte(bq, (unsigned char) reg,
				   (unsigned char) val);
	}

	return count;
}

static DEVICE_ATTR(registers, S_IRUGO | S_IWUSR, sgm41542_show_registers,
		   sgm41542_store_registers);

static struct attribute *sgm41542_attributes[] = {
	&dev_attr_registers.attr,
	NULL,
};

static const struct attribute_group sgm41542_attr_group = {
	.attrs = sgm41542_attributes,
};

static int sgm41542_charging(struct charger_device *chg_dev, bool enable)
{

	struct sgm41542 *bq = dev_get_drvdata(&chg_dev->dev);
	int ret = 0;
	u8 val;

	if (enable)
		ret = sgm41542_enable_charger(bq);
	else
		ret = sgm41542_disable_charger(bq);

	pr_err("%s charger %s\n", enable ? "enable" : "disable",
	       !ret ? "successfully" : "failed");

	ret = sgm41542_read_byte(bq, SGM41542_REG_01, &val);

	if (!ret)
		bq->charge_enabled = !!(val & REG01_CHG_CONFIG_MASK);

	return ret;
}

static int sgm41542_plug_in(struct charger_device *chg_dev)
{

	int ret;

	ret = sgm41542_charging(chg_dev, true);

	if (ret)
		pr_err("Failed to enable charging:%d\n", ret);

	return ret;
}

static int sgm41542_plug_out(struct charger_device *chg_dev)
{
	int ret;

	ret = sgm41542_charging(chg_dev, false);

	if (ret)
		pr_err("Failed to disable charging:%d\n", ret);

	return ret;
}

static int sgm41542_dump_register(struct charger_device *chg_dev)
{
	struct sgm41542 *bq = dev_get_drvdata(&chg_dev->dev);

	sgm41542_dump_regs(bq);

	return 0;
}

static int sgm41542_is_charging_enable(struct charger_device *chg_dev, bool *en)
{
	struct sgm41542 *bq = dev_get_drvdata(&chg_dev->dev);

	*en = bq->charge_enabled;

	return 0;
}

static int sgm41542_is_charging_done(struct charger_device *chg_dev, bool *done)
{
	struct sgm41542 *bq = dev_get_drvdata(&chg_dev->dev);
	int ret;
	u8 val;

	ret = sgm41542_read_byte(bq, SGM41542_REG_08, &val);
	if (!ret) {
		val = val & REG08_CHRG_STAT_MASK;
		val = val >> REG08_CHRG_STAT_SHIFT;
		*done = (val == REG08_CHRG_STAT_CHGDONE);
	}

	return ret;
}

static int sgm41542_set_ichg(struct charger_device *chg_dev, u32 curr)
{
	struct sgm41542 *bq = dev_get_drvdata(&chg_dev->dev);

	pr_err("charge curr = %d\n", curr);

	return sgm41542_set_chargecurrent(bq, curr / 1000);
}

static int sgm41542_get_ichg(struct charger_device *chg_dev, u32 *curr)
{
	struct sgm41542 *bq = dev_get_drvdata(&chg_dev->dev);
	u8 reg_val;
	int ichg;
	int ret;

	ret = sgm41542_read_byte(bq, SGM41542_REG_02, &reg_val);
	if (!ret) {
		ichg = (reg_val & REG02_ICHG_MASK) >> REG02_ICHG_SHIFT;
		ichg = ichg * REG02_ICHG_LSB + REG02_ICHG_BASE;
		*curr = ichg * 1000;
	}

	return ret;
}

static int sgm41542_get_min_ichg(struct charger_device *chg_dev, u32 *curr)
{
	*curr = 60 * 1000;

	return 0;
}

static int sgm41542_set_vchg(struct charger_device *chg_dev, u32 volt)
{
	struct sgm41542 *bq = dev_get_drvdata(&chg_dev->dev);

	pr_err("charge volt = %d\n", volt);

	return sgm41542_set_chargevolt(bq, volt / 1000);
}

static int sgm41542_get_vchg(struct charger_device *chg_dev, u32 *volt)
{
	struct sgm41542 *bq = dev_get_drvdata(&chg_dev->dev);
	u8 reg_val;
	int vchg;
	int ret;

	ret = sgm41542_read_byte(bq, SGM41542_REG_04, &reg_val);
	if (!ret) {
		vchg = (reg_val & REG04_VREG_MASK) >> REG04_VREG_SHIFT;
		vchg = vchg * REG04_VREG_LSB + REG04_VREG_BASE;
		*volt = vchg * 1000;
	}

	return ret;
}

static int sgm41542_set_ivl(struct charger_device *chg_dev, u32 volt)
{
	struct sgm41542 *bq = dev_get_drvdata(&chg_dev->dev);

	pr_err("vindpm volt = %d\n", volt);

	return sgm41542_set_input_volt_limit(bq, volt / 1000);

}

static int sgm41542_set_icl(struct charger_device *chg_dev, u32 curr)
{
	struct sgm41542 *bq = dev_get_drvdata(&chg_dev->dev);

	pr_err("indpm curr = %d\n", curr);

	return sgm41542_set_input_current_limit(bq, curr / 1000);
}

static int sgm41542_get_icl(struct charger_device *chg_dev, u32 *curr)
{
	struct sgm41542 *bq = dev_get_drvdata(&chg_dev->dev);
	u8 reg_val;
	int icl;
	int ret;

	ret = sgm41542_read_byte(bq, SGM41542_REG_00, &reg_val);
	if (!ret) {
		icl = (reg_val & REG00_IINLIM_MASK) >> REG00_IINLIM_SHIFT;
		icl = icl * REG00_IINLIM_LSB + REG00_IINLIM_BASE;
		*curr = icl * 1000;
	}

	return ret;

}

static int sgm41542_kick_wdt(struct charger_device *chg_dev)
{
	struct sgm41542 *bq = dev_get_drvdata(&chg_dev->dev);

	return sgm41542_reset_watchdog_timer(bq);
}

static int sgm41542_set_otg(struct charger_device *chg_dev, bool en)
{
	int ret;
	struct sgm41542 *bq = dev_get_drvdata(&chg_dev->dev);

	if (en){
		if(bq->hz_mode)
			sgm41542_exit_hiz_mode(bq);

		ret = sgm41542_enable_otg(bq);
	}else{
		if(bq->hz_mode)
			sgm41542_enter_hiz_mode(bq);

		ret = sgm41542_disable_otg(bq);
	}

	pr_err("%s OTG %s\n", en ? "enable" : "disable",
	       !ret ? "successfully" : "failed");

	return ret;
}

static int sgm41542_set_safety_timer(struct charger_device *chg_dev, bool en)
{
	struct sgm41542 *bq = dev_get_drvdata(&chg_dev->dev);
	int ret;

	if (en)
		ret = sgm41542_enable_safety_timer(bq);
	else
		ret = sgm41542_disable_safety_timer(bq);

	return ret;
}

static int sgm41542_is_safety_timer_enabled(struct charger_device *chg_dev,
					   bool *en)
{
	struct sgm41542 *bq = dev_get_drvdata(&chg_dev->dev);
	int ret;
	u8 reg_val;

	ret = sgm41542_read_byte(bq, SGM41542_REG_05, &reg_val);

	if (!ret)
		*en = !!(reg_val & REG05_EN_TIMER_MASK);

	return ret;
}

static int sgm41542_set_boost_ilmt(struct charger_device *chg_dev, u32 curr)
{
	struct sgm41542 *bq = dev_get_drvdata(&chg_dev->dev);
	int ret;

	pr_err("otg curr = %d\n", curr);

	ret = sgm41542_set_boost_current(bq, curr / 1000);

	return ret;
}

static int sgm41542_do_event(struct charger_device *chg_dev, u32 event, u32 args)
{
	switch(event) {
	case EVENT_EOC:
		charger_dev_notify(chg_dev, CHARGER_DEV_NOTIFY_EOC);
		break;
	case EVENT_RECHARGE:
		charger_dev_notify(chg_dev, CHARGER_DEV_NOTIFY_RECHG);
		break;
	default:
		break;
	}
	return 0;
}

static struct charger_ops sgm41542_chg_ops = {
	/* Normal charging */
	.plug_in = sgm41542_plug_in,
	.plug_out = sgm41542_plug_out,
	.dump_registers = sgm41542_dump_register,
	.enable = sgm41542_charging,
	.is_enabled = sgm41542_is_charging_enable,
	.get_charging_current = sgm41542_get_ichg,
	.set_charging_current = sgm41542_set_ichg,
	.get_input_current = sgm41542_get_icl,
	.set_input_current = sgm41542_set_icl,
	.get_constant_voltage = sgm41542_get_vchg,
	.set_constant_voltage = sgm41542_set_vchg,
	.kick_wdt = sgm41542_kick_wdt,
	.set_mivr = sgm41542_set_ivl,
	.is_charging_done = sgm41542_is_charging_done,
	.get_min_charging_current = sgm41542_get_min_ichg,

	/* Safety timer */
	.enable_safety_timer = sgm41542_set_safety_timer,
	.is_safety_timer_enabled = sgm41542_is_safety_timer_enabled,

	/* Power path */
	.enable_powerpath = NULL,
	.is_powerpath_enabled = NULL,

	/* OTG */
	.enable_otg = sgm41542_set_otg,
	.set_boost_current_limit = sgm41542_set_boost_ilmt,
	.enable_discharge = NULL,

	/* PE+/PE+20 */
	.send_ta_current_pattern = NULL,
	.set_pe20_efficiency_table = NULL,
	.send_ta20_current_pattern = NULL,
	.enable_cable_drop_comp = NULL,

	/* ADC */
	.get_tchg_adc = NULL,
	.hz_mode = sgm41542_set_hz_mode,
	.event = sgm41542_do_event,
};

static struct of_device_id sgm41542_charger_match_table[] = {
	{
	 .compatible = "ti,sgm41542",
         .data = &pn_data[PN_SGM41542],
         },
	{},
};
//MODULE_DEVICE_TABLE(of, sgm41542_charger_match_table);


static int sgm41542_charger_probe(struct i2c_client *client,
				 const struct i2c_device_id *id)
{
	struct sgm41542 *bq;
	const struct of_device_id *match;
	struct device_node *node = client->dev.of_node;

	int ret = 0;

	pr_err("sgm41542 probe start\n");

	bq = devm_kzalloc(&client->dev, sizeof(struct sgm41542), GFP_KERNEL);
	if (!bq)
		return -ENOMEM;

	bq->dev = &client->dev;
	bq->client = client;

	i2c_set_clientdata(client, bq);

	mutex_init(&bq->i2c_rw_lock);

	ret = sgm41542_detect_device(bq);
	if (ret) {
		pr_err("No sgm41542 device found!\n");
		return -ENODEV;
	}

	match = of_match_node(sgm41542_charger_match_table, node);
	if (match == NULL) {
		pr_err("device tree match not found\n");
		return -EINVAL;
	}

	if (bq->part_no != *(int *)match->data)
		pr_info("part no mismatch, hw:%s, devicetree:%s\n",
			pn_str[bq->part_no], pn_str[*(int *) match->data]);

	bq->platform_data = sgm41542_parse_dt(node, bq);

	if (!bq->platform_data) {
		pr_err("No platform data provided.\n");
		return -EINVAL;
	}

	ret = sgm41542_init_device(bq);
	if (ret) {
		pr_err("Failed to init device\n");
		return ret;
	}
	
#if 0
	sgm41542_register_interrupt(bq);
#endif

	bq->chg_dev = charger_device_register(bq->chg_dev_name,
					      &client->dev, bq,
					      &sgm41542_chg_ops,
					      &sgm41542_chg_props);
	if (IS_ERR_OR_NULL(bq->chg_dev)) {
		ret = PTR_ERR(bq->chg_dev);
		return ret;
	}

	ret = sysfs_create_group(&bq->dev->kobj, &sgm41542_attr_group);
	if (ret)
		dev_err(bq->dev, "failed to register sysfs. err: %d\n", ret);

#if 0
	determine_initial_status(bq);
#endif

	if ( bq->part_no == 13 ){

	hardwareinfo_set_prop(HARDWARE_CHARGER_IC_INFO, "SGM41542");
	pr_err("SGM41542 probe successfully, Part Num:%d, Revision:%d\n!",
              bq->part_no, bq->revision);

	}
	return 0;
}

static int sgm41542_charger_remove(struct i2c_client *client)
{
	struct sgm41542 *bq = i2c_get_clientdata(client);

	mutex_destroy(&bq->i2c_rw_lock);

	sysfs_remove_group(&bq->dev->kobj, &sgm41542_attr_group);

	return 0;
}

static void sgm41542_charger_shutdown(struct i2c_client *client)
{

}

static const struct i2c_device_id sgm41542_charger_id[] = {
	{ "sgm41542", 0 },
	{},
};

MODULE_DEVICE_TABLE(i2c, sgm41542_charger_id);

static struct i2c_driver sgm41542_charger_driver = {
	.driver = {
		   .name = "sgm41542-charger",
		   .owner = THIS_MODULE,
		   .of_match_table = sgm41542_charger_match_table,
		   },

	.probe = sgm41542_charger_probe,
	.remove = sgm41542_charger_remove,
	.shutdown = sgm41542_charger_shutdown,
	.id_table = sgm41542_charger_id,

};

module_i2c_driver(sgm41542_charger_driver);

MODULE_DESCRIPTION("TI SGM41542 Charger Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Texas Instruments");
