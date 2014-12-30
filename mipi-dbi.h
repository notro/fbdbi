/*
 * MIPI Display Bus Interface (DBI) LCD controller support
 *
 * Copyright 2014 Noralf Tronnes
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __LINUX_MIPI_DBI_H
#define __LINUX_MIPI_DBI_H

#include "core/lcdreg.h"
#include "core/fbdbi.h"


struct mipi_dbi_config {
	unsigned regwidth;
	u32 xres;
	u32 yres;
	enum fbdbi_format format;
	unsigned addr_mode0;
	unsigned addr_mode90;
	unsigned addr_mode180;
	unsigned addr_mode270;
	bool bgr;
};

struct fbdbi_display *devm_mipi_dbi_init(struct lcdreg *lcdreg,
					 struct mipi_dbi_config *config);

bool mipi_dbi_check_diagnostics(struct lcdreg *reg);

#endif /* __LINUX_MIPI_DBI_H */
