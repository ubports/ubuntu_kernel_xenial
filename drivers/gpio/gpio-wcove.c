/*
 * gpio-wcove.c - Intel Whiskey Cove GPIO Driver
 *
 * This driver is written based on gpio-crystalcove.c
 *
 * Copyright (C) 2015 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/seq_file.h>
#include <linux/bitops.h>
#include <linux/regmap.h>
#include <linux/mfd/intel_soc_pmic.h>

#define WCOVE_GPIO_NUM	13
#define WCOVE_VGPIO_NUM	94

#define UPDATE_IRQ_TYPE		BIT(0)
#define UPDATE_IRQ_MASK		BIT(1)

#define GPIOIRQ0		0x4e0b
#define GPIOIRQ1		0x4e0c
#define MGPIOIRQ0		0x4e19
#define MGPIOIRQ1		0x4e1a
#define GPIO0P0CTLO		0x4e44
#define GPIO0P0CTLI		0x4e51
#define GPIO1P0CTLO		0x4e4b
#define GPIO1P0CTLI		0x4e58
#define GPIO2P0CTLO		0x4e4f
#define GPIO2P0CTLI		0x4e5c

#define CTLI_INTCNT_DIS		(0)
#define CTLI_INTCNT_NE		(1 << 1)
#define CTLI_INTCNT_PE		(2 << 1)
#define CTLI_INTCNT_BE		(3 << 1)

#define CTLO_DIR_IN		(0)
#define CTLO_DIR_OUT		(1 << 5)

#define CTLO_DRV_CMOS		(0)
#define CTLO_DRV_OD		(1 << 4)

#define CTLO_DRV_REN		(1 << 3)

#define CTLO_RVAL_2KDW		(0)
#define CTLO_RVAL_2KUP		(1 << 1)
#define CTLO_RVAL_50KDW		(2 << 1)
#define CTLO_RVAL_50KUP		(3 << 1)

#define CTLO_INPUT_SET	(CTLO_DRV_CMOS | CTLO_DRV_REN | CTLO_RVAL_2KUP)
#define CTLO_OUTPUT_SET	(CTLO_DIR_OUT | CTLO_INPUT_SET)

enum ctrl_register {
	CTRL_IN,
	CTRL_OUT,
};

/*
 * struct wcove_gpio - Whiskey Cove GPIO controller
 * @buslock: for bus lock/sync and unlock.
 * @chip: the abstract gpio_chip structure.
 * @regmap: the regmap from the parent device.
 * @update: pending IRQ setting update, to be written to the chip upon unlock.
 * @intcnt_value: the Interrupt Detect value to be written.
 * @set_irq_mask: true if the IRQ mask needs to be set, false to clear.
 */
struct wcove_gpio {
	struct mutex buslock; /* irq_bus_lock */
	struct gpio_chip chip;
	struct regmap *regmap;
	struct regmap_irq_chip_data *regmap_irq_chip;
	int update;
	int intcnt_value;
	bool set_irq_mask;
};

static inline struct wcove_gpio *to_wg(struct gpio_chip *gc)
{
	return container_of(gc, struct wcove_gpio, chip);
}

static inline int to_reg(int gpio, enum ctrl_register reg_type)
{
	int reg;

	if (reg_type == CTRL_IN) {
		if (gpio < 7)
			reg = GPIO0P0CTLI + gpio;
		else if (gpio < 11)
			reg = GPIO1P0CTLI + (gpio % 7);
		else
			reg = GPIO2P0CTLI + (gpio % 11);
	} else {
		if (gpio < 7)
			reg = GPIO0P0CTLO + gpio;
		else if (gpio < 11)
			reg = GPIO1P0CTLO + (gpio % 7);
		else
			reg = GPIO2P0CTLO + (gpio % 11);
	}

	return reg;
}

static void wcove_update_irq_mask(struct wcove_gpio *wg,
					int gpio)
{
	u8 mirq = gpio < 7 ? MGPIOIRQ0 : MGPIOIRQ1;
	int mask = BIT(gpio % 7);

	if (wg->set_irq_mask)
		regmap_update_bits(wg->regmap, mirq, mask, mask);
	else
		regmap_update_bits(wg->regmap, mirq, mask, 0);
}

static void wcove_update_irq_ctrl(struct wcove_gpio *wg, int gpio)
{
	int reg = to_reg(gpio, CTRL_IN);

	regmap_update_bits(wg->regmap, reg, CTLI_INTCNT_BE, wg->intcnt_value);
}

static int wcove_gpio_dir_in(struct gpio_chip *chip, unsigned int gpio)
{
	struct wcove_gpio *wg = to_wg(chip);

	if (gpio > WCOVE_VGPIO_NUM)
		return 0;

	return regmap_write(wg->regmap, to_reg(gpio, CTRL_OUT),
			    CTLO_INPUT_SET);
}

static int wcove_gpio_dir_out(struct gpio_chip *chip, unsigned int gpio,
				    int value)
{
	struct wcove_gpio *wg = to_wg(chip);

	if (gpio > WCOVE_VGPIO_NUM)
		return 0;

	return regmap_write(wg->regmap, to_reg(gpio, CTRL_OUT),
			    CTLO_OUTPUT_SET | value);
}

static int wcove_gpio_get(struct gpio_chip *chip, unsigned int gpio)
{
	struct wcove_gpio *wg = to_wg(chip);
	int ret;
	unsigned int val;

	if (gpio > WCOVE_VGPIO_NUM)
		return 0;

	ret = regmap_read(wg->regmap, to_reg(gpio, CTRL_IN), &val);
	if (ret)
		return ret;

	return val & 0x1;
}

static void wcove_gpio_set(struct gpio_chip *chip,
				 unsigned int gpio, int value)
{
	struct wcove_gpio *wg = to_wg(chip);

	if (gpio > WCOVE_VGPIO_NUM)
		return;

	if (value)
		regmap_update_bits(wg->regmap, to_reg(gpio, CTRL_OUT), 1, 1);
	else
		regmap_update_bits(wg->regmap, to_reg(gpio, CTRL_OUT), 1, 0);
}

static int wcove_irq_type(struct irq_data *data, unsigned int type)
{
	struct wcove_gpio *wg = to_wg(irq_data_get_irq_chip_data(data));

	switch (type) {
	case IRQ_TYPE_NONE:
		wg->intcnt_value = CTLI_INTCNT_DIS;
		break;
	case IRQ_TYPE_EDGE_BOTH:
		wg->intcnt_value = CTLI_INTCNT_BE;
		break;
	case IRQ_TYPE_EDGE_RISING:
		wg->intcnt_value = CTLI_INTCNT_PE;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		wg->intcnt_value = CTLI_INTCNT_NE;
		break;
	default:
		return -EINVAL;
	}

	wg->update |= UPDATE_IRQ_TYPE;

	return 0;
}

static void wcove_bus_lock(struct irq_data *data)
{
	struct wcove_gpio *wg = to_wg(irq_data_get_irq_chip_data(data));

	mutex_lock(&wg->buslock);
}

static void wcove_bus_sync_unlock(struct irq_data *data)
{
	struct wcove_gpio *wg = to_wg(irq_data_get_irq_chip_data(data));
	int gpio = data->hwirq;

	if (wg->update & UPDATE_IRQ_TYPE)
		wcove_update_irq_ctrl(wg, gpio);
	if (wg->update & UPDATE_IRQ_MASK)
		wcove_update_irq_mask(wg, gpio);
	wg->update = 0;

	mutex_unlock(&wg->buslock);
}

static void wcove_irq_unmask(struct irq_data *data)
{
	struct wcove_gpio *wg = to_wg(irq_data_get_irq_chip_data(data));

	wg->set_irq_mask = false;
	wg->update |= UPDATE_IRQ_MASK;
}

static void wcove_irq_mask(struct irq_data *data)
{
	struct wcove_gpio *wg = to_wg(irq_data_get_irq_chip_data(data));

	wg->set_irq_mask = true;
	wg->update |= UPDATE_IRQ_MASK;
}

static struct irq_chip wcove_irqchip = {
	.name			= "Whiskey Cove",
	.irq_mask		= wcove_irq_mask,
	.irq_unmask		= wcove_irq_unmask,
	.irq_set_type		= wcove_irq_type,
	.irq_bus_lock		= wcove_bus_lock,
	.irq_bus_sync_unlock	= wcove_bus_sync_unlock,
};

static irqreturn_t wcove_gpio_irq_handler(int irq, void *data)
{
	struct wcove_gpio *wg = data;
	unsigned int p0, p1, virq;
	int pending, gpio;

	if (regmap_read(wg->regmap, GPIOIRQ0, &p0) ||
	    regmap_read(wg->regmap, GPIOIRQ1, &p1))
		return IRQ_NONE;

	pending = p0 | (p1 << 8);

	for (gpio = 0; gpio < WCOVE_GPIO_NUM; gpio++) {
		if (pending & BIT(gpio)) {
			virq = irq_find_mapping(wg->chip.irqdomain, gpio);
			handle_nested_irq(virq);
		}
	}

	regmap_write(wg->regmap, GPIOIRQ0, p0);
	regmap_write(wg->regmap, GPIOIRQ1, p1);

	return IRQ_HANDLED;
}

static void wcove_gpio_dbg_show(struct seq_file *s,
				      struct gpio_chip *chip)
{
	struct wcove_gpio *wg = to_wg(chip);
	int gpio, offset;
	unsigned int ctlo, ctli, mirq, irq;

	for (gpio = 0; gpio < WCOVE_GPIO_NUM; gpio++) {
		regmap_read(wg->regmap, to_reg(gpio, CTRL_OUT), &ctlo);
		regmap_read(wg->regmap, to_reg(gpio, CTRL_IN), &ctli);
		regmap_read(wg->regmap, gpio < 7 ? MGPIOIRQ0 : MGPIOIRQ1,
			    &mirq);
		regmap_read(wg->regmap, gpio < 7 ? GPIOIRQ0 : GPIOIRQ1,
			    &irq);

		offset = gpio % 8;
		seq_printf(s, " gpio-%-2d %s %s %s %s ctlo=%2x,%s %s\n",
			   gpio, ctlo & CTLO_DIR_OUT ? "out" : "in ",
			   ctli & 0x1 ? "hi" : "lo",
			   ctli & CTLI_INTCNT_NE ? "fall" : "    ",
			   ctli & CTLI_INTCNT_PE ? "rise" : "    ",
			   ctlo,
			   mirq & BIT(offset) ? "mask  " : "unmask",
			   irq & BIT(offset) ? "pending" : "       ");
	}
}

static int wcove_gpio_probe(struct platform_device *pdev)
{
	int virq, retval, irq = platform_get_irq(pdev, 0);
	struct wcove_gpio *wg;
	struct device *dev = pdev->dev.parent;
	struct intel_soc_pmic *pmic = dev_get_drvdata(dev);

	if (irq < 0)
		return irq;

	wg = devm_kzalloc(&pdev->dev, sizeof(*wg), GFP_KERNEL);
	if (!wg)
		return -ENOMEM;

	wg->regmap_irq_chip = pmic->irq_chip_data_level2;

	platform_set_drvdata(pdev, wg);

	mutex_init(&wg->buslock);
	wg->chip.label = KBUILD_MODNAME;
	wg->chip.direction_input = wcove_gpio_dir_in;
	wg->chip.direction_output = wcove_gpio_dir_out;
	wg->chip.get = wcove_gpio_get;
	wg->chip.set = wcove_gpio_set;
	wg->chip.base = -1;
	wg->chip.ngpio = WCOVE_VGPIO_NUM;
	wg->chip.can_sleep = true;
	wg->chip.parent = dev;
	wg->chip.dbg_show = wcove_gpio_dbg_show;
	wg->regmap = pmic->regmap;

	retval = gpiochip_add(&wg->chip);
	if (retval) {
		dev_warn(&pdev->dev, "add gpio chip error: %d\n", retval);
		return retval;
	}

	gpiochip_irqchip_add(&wg->chip, &wcove_irqchip, 0,
			     handle_simple_irq, IRQ_TYPE_NONE);

	virq = regmap_irq_get_virq(wg->regmap_irq_chip, irq);
	if (virq < 0) {
		dev_err(&pdev->dev,
				"failed to get virtual interrupt=%d\n", irq);
		retval = virq;
		goto out_remove_gpio;
	}

	retval = devm_request_threaded_irq(&pdev->dev, virq,
			NULL, wcove_gpio_irq_handler,
			IRQF_ONESHOT, pdev->name, wg);

	if (retval) {
		dev_warn(&pdev->dev, "request irq failed: %d, virq: %d\n",
							retval, virq);
		goto out_remove_gpio;
	}

	return 0;

out_remove_gpio:
	gpiochip_remove(&wg->chip);
	return retval;
}

static int wcove_gpio_remove(struct platform_device *pdev)
{
	struct wcove_gpio *wg = platform_get_drvdata(pdev);

	gpiochip_remove(&wg->chip);
	return 0;
}

static struct platform_driver wcove_gpio_driver = {
	.driver = {
		.name = "bxt_wcove_gpio",
	},
	.probe = wcove_gpio_probe,
	.remove = wcove_gpio_remove,
};

module_platform_driver(wcove_gpio_driver);

MODULE_AUTHOR("Ajay Thomas <ajay.thomas.david.rajamanickam@intel.com>");
MODULE_DESCRIPTION("Intel Whiskey Cove GPIO Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:bxt_wcove_gpio");
