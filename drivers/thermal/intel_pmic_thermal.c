/*
 * pmic_thermal.c - PMIC thermal driver
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
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/thermal.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/mfd/intel_soc_pmic.h>

static irqreturn_t pmic_thermal_irq_handler(int irq, void *data)
{
	struct platform_device *pdev = data;
	struct pmic_thermal_data *td =
			(struct pmic_thermal_data *)pdev->dev.platform_data;
	struct intel_soc_pmic *pmic = dev_get_drvdata(pdev->dev.parent);
	struct regmap *regmap = pmic->regmap;
	struct thermal_zone_device *tzd;
	int i, j, ret;
	u16 reg, evt_stat_reg;
	u8 reg_val, mask, irq_stat, trip;

	/* Resolve thermal irqs */
	for (i = 0; i < td->num_maps; i++) {
		for (j = 0; j < td->maps[i].num_trips; j++) {
			reg = td->maps[i].trip_config[j].irq_reg;
			mask = td->maps[i].trip_config[j].irq_mask;
			/*
			 * Read the irq register to resolve whether the
			 * interrupt was triggered for this sensor
			 */
			if (regmap_read(regmap, reg, &ret))
				return IRQ_HANDLED;

			reg_val = (u8)ret;

			irq_stat = ((u8)ret & mask);
			if (irq_stat) {
				/*
				 * Read the status register to find out what
				 * event occurred i.e a high or a low
				 */
				evt_stat_reg =
					td->maps[i].trip_config[j].evt_stat;
				if (regmap_read(regmap, evt_stat_reg, &ret))
					return IRQ_HANDLED;

				trip = td->maps[i].trip_config[j].trip_num;
				tzd = thermal_zone_get_zone_by_name(
						td->maps[i].handle);
				if (!IS_ERR(tzd))
					thermal_zone_device_update(tzd);

				/* Clear the appropriate irq */
				regmap_write(regmap, reg, reg_val & mask);
			}
		}
	}

	return IRQ_HANDLED;
}

static int pmic_thermal_probe(struct platform_device *pdev)
{
	struct intel_soc_pmic *pmic = dev_get_drvdata(pdev->dev.parent);
	struct regmap *regmap = pmic->regmap;
	struct pmic_thermal_data *thermal_data;
	struct regmap_irq_chip_data *regmap_irq_chip;
	int ret, irq, virq, i, j, pmic_irq_count = 0;
	u16 reg;
	u8 mask;

	thermal_data = (struct pmic_thermal_data *)pdev->dev.platform_data;
	if (thermal_data == NULL) {
		dev_err(&pdev->dev, "No thermal data initialized!!\n");
		return -ENODEV;
	}

	regmap_irq_chip = pmic->irq_chip_data_level2;
	while ((irq = platform_get_irq(pdev, pmic_irq_count)) != -ENXIO) {
		virq = regmap_irq_get_virq(regmap_irq_chip, irq);
		if (virq < 0) {
			dev_err(&pdev->dev,
				"failed to get virtual interrupt=%d\n", irq);
			return virq;
		}

		ret = devm_request_threaded_irq(&pdev->dev, virq,
				NULL, pmic_thermal_irq_handler,
				IRQF_ONESHOT, "pmic_thermal", pdev);

		if (ret) {
			dev_err(&pdev->dev, "request irq failed: %d,virq: %d\n",
								ret, virq);
			return ret;
		}
		pmic_irq_count++;
	}

	/* Enable thermal interrupts */
	for (i = 0; i < thermal_data->num_maps; i++) {
		for (j = 0; j < thermal_data->maps[i].num_trips; j++) {
			reg = thermal_data->maps[i].trip_config[j].irq_en;
			mask = thermal_data->maps[i].trip_config[j].irq_en_mask;
			if (regmap_update_bits(regmap, reg, mask, 0x00))
				return ret;
		}
	}

	return 0;
}

static int pmic_thermal_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_device_id pmic_thermal_id_table[] = {
	{ .name = "whiskey_cove_thermal" },
	{ .name = "bxt_wcove_thermal" },
	{},
};

static struct platform_driver pmic_thermal_driver = {
	.probe = pmic_thermal_probe,
	.remove = pmic_thermal_remove,
	.driver = {
		.name = "pmic_thermal",
	},
	.id_table = pmic_thermal_id_table,
};

MODULE_DEVICE_TABLE(platform, pmic_thermal_id_table);
module_platform_driver(pmic_thermal_driver);

MODULE_AUTHOR("Yegnesh S Iyer <yegnesh.s.iyer@intel.com>");
MODULE_DESCRIPTION("PMIC thermal Driver");
MODULE_LICENSE("GPL");
