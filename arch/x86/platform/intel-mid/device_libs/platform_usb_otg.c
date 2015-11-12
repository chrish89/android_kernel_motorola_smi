/*
 * platform_usb_otg.c: usb_otg platform data initilization file
 *
 * (C) Copyright 2008 Intel Corporation
 * Author:
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#include <linux/gpio.h>
#include <linux/lnw_gpio.h>
#include <linux/usb/penwell_otg.h>
#include <asm/intel-mid.h>
#include "platform_usb_otg.h"

void *cloverview_usb_otg_get_pdata(void)
{
	struct cloverview_usb_otg_pdata *pdata;

	pdata = (struct cloverview_usb_otg_pdata *)
				kmalloc(sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		pr_err("%s: out of memory.\n", __func__);
		goto failed1;
	}
	pdata->gpio_cs = get_gpio_by_name("usb_otg_phy_cs");
	if (pdata->gpio_cs == -1) {
		pr_err("%s: No gpio pin for 'usb_otg_phy_cs'\n", __func__);
		goto failed2;
	}
	pdata->gpio_reset = get_gpio_by_name("usb_otg_phy_rst");
	if (pdata->gpio_reset == -1) {
		pr_err("%s: No gpio pin for 'usb_otg_phy_rst'\n", __func__);
		goto failed2;
	}
	pr_info("%s: CS pin: gpio %d, Reset pin: gpio %d\n", __func__,
			 pdata->gpio_cs, pdata->gpio_reset);
	return pdata;

failed2:
	kfree(pdata);
failed1:
	return NULL;
}
EXPORT_SYMBOL_GPL(cloverview_usb_otg_get_pdata);
