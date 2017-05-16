/*
 * Copyright (C) 2017  Minnesota Department of Transportation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <gst/gst.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define VERSION "0.2"
#define BANNER "polystream: v" VERSION "  Copyright (C)  MnDOT\n"

uint32_t load_config(void);

int main(void) {
	GMainLoop *loop;
	printf(BANNER);
	gst_init(NULL, NULL);
	if (load_config()) {
		loop = g_main_loop_new(NULL, TRUE);
		g_main_loop_run(loop);
	}
	return 1;
}
