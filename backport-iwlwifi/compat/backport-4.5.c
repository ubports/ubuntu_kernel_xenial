/*
 * Copyright(c) 2015 Hauke Mehrtens <hauke@hauke-m.de>
 *
 * Backport functionality introduced in Linux 4.5.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/leds.h>
#include <linux/export.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <asm/uaccess.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,19,0)
int led_set_brightness_sync(struct led_classdev *led_cdev,
			    enum led_brightness value)
{
	if (led_cdev->blink_delay_on || led_cdev->blink_delay_off)
		return -EBUSY;

	led_cdev->brightness = min(value, led_cdev->max_brightness);

	if (led_cdev->flags & LED_SUSPENDED)
		return 0;

	if (led_cdev->brightness_set_sync)
		return led_cdev->brightness_set_sync(led_cdev,
							 led_cdev->brightness);
	return -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(led_set_brightness_sync);
#endif /* >= 3.19 */

/**
 * memdup_user_nul - duplicate memory region from user space and NUL-terminate
 *
 * @src: source address in user space
 * @len: number of bytes to copy
 *
 * Returns an ERR_PTR() on failure.
 */
void *memdup_user_nul(const void __user *src, size_t len)
{
	char *p;

	/*
	 * Always use GFP_KERNEL, since copy_from_user() can sleep and
	 * cause pagefault, which makes it pointless to use GFP_NOFS
	 * or GFP_ATOMIC.
	 */
	p = kmalloc(len + 1, GFP_KERNEL);
	if (!p)
		return ERR_PTR(-ENOMEM);

	if (copy_from_user(p, src, len)) {
		kfree(p);
		return ERR_PTR(-EFAULT);
	}
	p[len] = '\0';

	return p;
}
EXPORT_SYMBOL_GPL(memdup_user_nul);
