/*
 * Copyright (C) 2017-2019  Minnesota Department of Transportation
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

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <gst/gst.h>
#include "elog.h"
#include "nstr.h"
#include "stream.h"

static const char *PATH = "/etc/%s";

/* ASCII separators */
static const char RECORD_SEP = '\n';
static const char UNIT_SEP = '\t';

static nstr_t config_load(const char *name, nstr_t str) {
	char path[64];
	int fd;

	if (snprintf(path, sizeof(path), PATH, name) < 0) {
		elog_err("Error: %s\n", strerror(errno));
		goto err;
	}
	fd = open(path, O_RDONLY | O_NOFOLLOW, 0);
	if (fd >= 0) {
		ssize_t n_bytes = read(fd, str.buf, str.buf_len);
		if (n_bytes < 0) {
			elog_err("Read %s: %s\n", path, strerror(errno));
			goto err;
		}
		str.len = n_bytes;
		close(fd);
		return str;
	} else {
		elog_err("Open %s: %s\n", path, strerror(errno));
		goto err;
	}
err:
	str.len = 0;
	return str;
}

static gboolean do_restart(gpointer data) {
	struct stream *st = (struct stream *) data;
	stream_start_pipeline(st);
	return FALSE;
}

static void stop_stream_cb(struct stream *st) {
	elog_err("Restarting stream %s\n", st->location);
	stream_stop_pipeline(st);
	g_timeout_add(5000, do_restart, st);
}

static void start_stream(nstr_t cmd, uint32_t idx) {
	nstr_t str = nstr_dup(cmd);
	nstr_t p1 = nstr_split(&str, UNIT_SEP);
	nstr_t p2 = nstr_split(&str, UNIT_SEP);
	nstr_t p3 = nstr_split(&str, UNIT_SEP);
	nstr_t p4 = nstr_split(&str, UNIT_SEP);
	nstr_t p5 = nstr_split(&str, UNIT_SEP);
	char uri[128];
	char encoding[16];
	char host[128];
	int port;
	char config_int[20];
	bool enable;
	struct stream stream;
	GMainLoop *loop;

	gst_init(NULL, NULL);
	stream_init(&stream, idx);
	stream.stop = stop_stream_cb;
	nstr_wrap(uri, sizeof(uri), p1);
	nstr_wrap(encoding, sizeof(encoding), p2);
	nstr_wrap(host, sizeof(host), p3);
	port = nstr_parse_u32(p4);
	nstr_wrap(config_int, sizeof(config_int), p5);
	stream_set_location(&stream, uri);
	stream_set_encoding(&stream, encoding);
	stream_set_host(&stream, host);
	stream_set_port(&stream, port);
	enable = (strcasecmp("no-config-interval", config_int) != 0);
	stream_set_config_interval(&stream, enable);
	elog_err("Starting stream %s\n", stream.location);
	stream_start_pipeline(&stream);

	loop = g_main_loop_new(NULL, TRUE);
	g_main_loop_run(loop);

	stream_destroy(&stream);
}

uint32_t load_config(void) {
	char buf[65536];
	uint32_t n_streams = 0;
	nstr_t str = config_load("polystream.conf", nstr_make(buf, sizeof(buf),
		0));
	while (nstr_len(str)) {
		nstr_t cmd = nstr_split(&str, RECORD_SEP);
		if (fork() == 0) {
			start_stream(cmd, n_streams);
			exit(0);
		} else
			n_streams++;
	}
	return n_streams;
}
