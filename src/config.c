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

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
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

static struct stream streams[256];

static void stop_stream_cb(struct stream *st) {
	elog_err("Restarting stream %s\n", st->location);
	stream_stop_pipeline(st);
	stream_start_pipeline(st);
}

static bool start_stream(nstr_t cmd, uint32_t idx) {
	nstr_t str = nstr_dup(cmd);
	nstr_t p1 = nstr_split(&str, UNIT_SEP);
	nstr_t p2 = nstr_split(&str, UNIT_SEP);
	nstr_t p3 = nstr_split(&str, UNIT_SEP);
	nstr_t p4 = nstr_split(&str, UNIT_SEP);
	char uri[128];
	char encoding[16];
	char host[128];
	int port;

	struct stream *st = streams + idx;

	stream_init(st, idx);
	st->stop = stop_stream_cb;
	nstr_wrap(uri, sizeof(uri), p1);
	nstr_wrap(encoding, sizeof(encoding), p2);
	nstr_wrap(host, sizeof(host), p3);
	port = nstr_parse_u32(p4);
	stream_set_location(st, uri);
	stream_set_encoding(st, encoding);
	stream_set_host(st, host);
	stream_set_port(st, port);
	elog_err("Starting stream %s\n", st->location);
	stream_start_pipeline(st);

	return true;
}

uint32_t load_config(void) {
	char buf[16384];
	uint32_t n_streams = 0;
	nstr_t str = config_load("polystream.conf", nstr_make(buf, sizeof(buf),
		0));
	while (nstr_len(str)) {
		nstr_t cmd = nstr_split(&str, RECORD_SEP);
		if (start_stream(cmd, n_streams))
			n_streams++;
		if (n_streams > 255)
			break;
	}
	return n_streams;
}
