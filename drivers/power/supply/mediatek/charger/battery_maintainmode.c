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
#include "battery_maintainmode.h"

//#define PCBASN_BUF "androidboot.pcbaserialno=":

/*
struct my_time{
	int year;
	int month;
	int day;
};
*/


int time_change(int time)
{
	if(time<64)
		time=time-48;
	if(time>64)
		time=time-55;
	return time;
}

int get_used_month(struct my_time factory_time,struct my_time now_time)
{

	struct rtc_time tm;
	struct timeval tv = { 0 };
	/* android time */
	struct rtc_time tm_android;
	struct timeval tv_android = { 0 };
	char *buf;
	char time_buf[64];
	int month_used=0;
	int i_year = 0;
	int i_month = 0;

//factory_time
	buf = strnstr(saved_command_line,"androidboot.pcbaserialno=",strlen(saved_command_line));
	if(buf)
	{
		sscanf(buf,"%s",time_buf);
		chr_err("fenglin %s\n",time_buf);
		
	}

	factory_time.year = time_change(time_buf[41]);
	factory_time.month = time_change(time_buf[42]);
	factory_time.day = time_change(time_buf[43]);
	if(factory_time.year>=8)
		factory_time.year = factory_time.year + 2010;
	if(factory_time.year < 8)
		factory_time.year = factory_time.year + 2020;
	chr_err("factory time : %d,%d,%d\n",factory_time.year,factory_time.month,factory_time.day);

//mow_time
	
	do_gettimeofday(&tv);
	tv_android = tv;
	rtc_time_to_tm(tv.tv_sec, &tm);
	tv_android.tv_sec -= sys_tz.tz_minuteswest * 60;
	rtc_time_to_tm(tv_android.tv_sec, &tm_android);
	now_time.year = tm_android.tm_year + 1900;
	now_time.month = tm_android.tm_mon + 1;
	now_time.day = tm_android.tm_mday;
	chr_err("UTC year=%d,month=%d,day=%d\n",now_time.year,now_time.month,now_time.day);	
	
//month_used
	
	
	i_month = now_time.month - factory_time.month;

	i_year = now_time.year - factory_time.year;

	if(i_year >= 0)
	{
		month_used = month_used + i_year*12;
		month_used = month_used + i_month;
	}
	else 
		return -1;

	chr_err("month_used : %d\n",month_used);
	return month_used;
}

int get_battery_maintain_mode(int month_used)
{
	int BATTERY_MAINTAIN_MODE;	
	
	if(month_used<30)
		BATTERY_MAINTAIN_MODE = 0;
	if(month_used >29 && month_used<54)
		BATTERY_MAINTAIN_MODE = 1;
	if(month_used >53 && month_used<56)
		BATTERY_MAINTAIN_MODE = 2;
	if(month_used >55)
		BATTERY_MAINTAIN_MODE = 3;

	chr_err("BATTERY_MAINTAIN_MODE: %d \n",BATTERY_MAINTAIN_MODE);
	return BATTERY_MAINTAIN_MODE;
}











