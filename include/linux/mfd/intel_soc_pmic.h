/*
 * intel_soc_pmic.h - Intel SoC PMIC Driver
 *
 * Copyright (C) 2012-2014 Intel Corporation. All rights reserved.
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
 * Author: Yang, Bin <bin.yang@intel.com>
 * Author: Zhu, Lejun <lejun.zhu@linux.intel.com>
 */

#ifndef __INTEL_SOC_PMIC_H__
#define __INTEL_SOC_PMIC_H__

#include <linux/regmap.h>

struct trip_config_map {
	u16 irq_reg;
	u16 irq_en;
	u16 evt_stat;
	u8 irq_mask;
	u8 irq_en_mask;
	u8 evt_mask;
	u8 trip_num;
};

struct thermal_irq_map {
	char handle[20];
	int num_trips;
	struct trip_config_map *trip_config;
};

struct pmic_thermal_data {
	struct thermal_irq_map *maps;
	int num_maps;
};

struct intel_soc_pmic {
	int irq;
	struct regmap *regmap;
	struct regmap_irq_chip_data *irq_chip_data;
	struct regmap_irq_chip_data *irq_chip_data_level2;
	struct device *dev;
};

#endif	/* __INTEL_SOC_PMIC_H__ */
