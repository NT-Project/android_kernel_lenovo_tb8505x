#ifndef BATTERY_MAINTAINMODE_H
#define BATTERY_MAINTAINMODE_H


#include <linux/init.h>		
#include <linux/module.h>	
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/power_supply.h>
#include <linux/pm_wakeup.h>
#include <linux/time.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/proc_fs.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/scatterlist.h>
#include <linux/suspend.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/reboot.h>
#include <linux/slab.h>

#include <mt-plat/charger_type.h>
#include <mt-plat/mtk_battery.h>
#include <mt-plat/mtk_boot.h>
#include <pmic.h>
#include <mtk_gauge_time_service.h>
#include <linux/timer.h>
#include <linux/timex.h>
#include <linux/rtc.h>

#include <mt-plat/mtk_boot.h>
#include "mtk_charger_intf.h"
#include "mtk_charger_init.h"
#include "mtk_switch_charging.h"

#define PCBASN_BUF "androidboot.pcbaserialno=":

struct my_time{
	int year;
	int month;
	int day;
};


extern int time_change(int time);
extern int get_used_month(struct my_time factory_time,struct my_time now_time);
extern int get_battery_maintain_mode(int month_used);


#endif
