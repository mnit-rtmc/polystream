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

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <gst/gst.h>
#include "elog.h"
#include "stream.h"

#define STREAM_NUM_VIDEO	(0)

static const uint32_t DEFAULT_LATENCY = 0;

static int stream_elem_next(const struct stream *st) {
	int i = 0;
	while (st->elem[i]) {
		i++;
		if (i >= MAX_ELEMS) {
			elog_err("Too many elements\n");
			return 0;
		}
	}
	return i;
}

static void pad_added_cb(GstElement *src, GstPad *pad, gpointer data) {
	GstElement *sink = (GstElement *) data;
	GstPad *p = gst_element_get_static_pad(sink, "sink");
	GstPadLinkReturn ret = gst_pad_link(pad, p);
	if (ret != GST_PAD_LINK_OK) {
		elog_err("Pad link error: %s\n", gst_pad_link_get_name(ret));
		elog_err("  src: %s\n", GST_ELEMENT_NAME(src));
		elog_err("  sink: %s\n", GST_ELEMENT_NAME(sink));
	}
	gst_object_unref(p);
}

static void stream_link(struct stream *st, int i) {
	GstElement *sink = st->elem[i - 1];
	GstElement *src = st->elem[i];
	if (!gst_element_link(src, sink)) {
		g_signal_connect(src, "pad-added", G_CALLBACK(pad_added_cb),
		                 sink);
	}
}

static void stream_add(struct stream *st, GstElement *elem) {
	if (gst_bin_add(GST_BIN(st->pipeline), elem)) {
		int i = stream_elem_next(st);
		st->elem[i] = elem;
		if (i > 0)
			stream_link(st, i);
	} else
		elog_err("Element not added to pipeline\n");
}

static void stream_add_sink(struct stream *st) {
	GstElement *sink = gst_element_factory_make("udpsink", NULL);
	g_object_set(G_OBJECT(sink), "host", st->host, NULL);
	g_object_set(G_OBJECT(sink), "port", st->port, NULL);
	g_object_set(G_OBJECT(sink), "ttl-mc", 15, NULL);
	stream_add(st, sink);
}

static gboolean select_stream_cb(GstElement *src, guint num, GstCaps *caps,
	gpointer user_data)
{
	switch (num) {
	case STREAM_NUM_VIDEO:
		return TRUE;
	default:
		return FALSE;
	}
}

static void stream_add_src_rtsp(struct stream *st) {
	GstElement *src = gst_element_factory_make("rtspsrc", NULL);
	g_object_set(G_OBJECT(src), "location", st->location, NULL);
	g_object_set(G_OBJECT(src), "latency", st->latency, NULL);
	g_signal_connect(src, "select-stream", G_CALLBACK(select_stream_cb),st);
	stream_add(st, src);
}

static void stream_add_mp4_pay(struct stream *st) {
	GstElement *pay = gst_element_factory_make("rtpmp4vpay", NULL);
	g_object_set(G_OBJECT(pay), "config-interval", 1, NULL);
	stream_add(st, pay);
}

static void stream_add_h264_pay(struct stream *st) {
	GstElement *pay = gst_element_factory_make("rtph264pay", NULL);
	g_object_set(G_OBJECT(pay), "config-interval", -1, NULL);
	stream_add(st, pay);
}

static void stream_add_later_elements(struct stream *st) {
	stream_add_sink(st);
	if (strcmp("MPEG4", st->encoding) == 0) {
		if (st->enable_config_int) {
			stream_add_mp4_pay(st);
			stream_add(st, gst_element_factory_make("rtpmp4vdepay",
				NULL));
		}
	} else if (strcmp("H264", st->encoding) == 0) {
		if (st->enable_config_int) {
			stream_add_h264_pay(st);
			stream_add(st, gst_element_factory_make("rtph264depay",
				NULL));
		}
	} else
		elog_err("Invalid encoding: %s\n", st->encoding);
}

void stream_start_pipeline(struct stream *st) {
	stream_add_later_elements(st);
	if (strncmp("rtsp", st->location, 4) == 0)
		stream_add_src_rtsp(st);
	else {
		elog_err("Invalid location: %s\n", st->location);
		return;
	}
	gst_element_set_state(st->pipeline, GST_STATE_PLAYING);
}

static void stream_remove_all(struct stream *st) {
	GstBin *bin = GST_BIN(st->pipeline);
	for (int i = 0; i < MAX_ELEMS; i++) {
		if (st->elem[i])
			gst_bin_remove(bin, st->elem[i]);
	}
	memset(st->elem, 0, sizeof(st->elem));
}

void stream_stop_pipeline(struct stream *st) {
	gst_element_set_state(st->pipeline, GST_STATE_NULL);
	stream_remove_all(st);
}

static void stream_stop(struct stream *st) {
	if (st->stop)
		st->stop(st);
}

static void stream_ack_started(struct stream *st) {
	if (st->ack_started)
		st->ack_started(st);
}

static gboolean bus_cb(GstBus *bus, GstMessage *msg, gpointer data) {
	struct stream *st = (struct stream *) data;
	switch (GST_MESSAGE_TYPE(msg)) {
	case GST_MESSAGE_EOS:
		elog_err("End of stream: %s\n", st->location);
		stream_stop(st);
		break;
	case GST_MESSAGE_ERROR: {
		gchar *debug;
		GError *error;
		gst_message_parse_error(msg, &error, &debug);
		g_free(debug);
		elog_err("Error: %s  %s\n", error->message, st->location);
		g_error_free(error);
		stream_stop(st);
		break;
	}
	case GST_MESSAGE_WARNING: {
		gchar *debug;
		GError *error;
		gst_message_parse_warning(msg, &error, &debug);
		g_free(debug);
		elog_err("Warning: %s  %s\n", error->message, st->location);
		g_error_free(error);
		break;
	}
	case GST_MESSAGE_ELEMENT:
		if (gst_message_has_name(msg, "GstUDPSrcTimeout")) {
			elog_err("udpsrc timeout -- stopping stream\n");
			stream_stop(st);
		}
		break;
	case GST_MESSAGE_ASYNC_DONE:
		stream_ack_started(st);
		break;
	default:
		break;
	}
	return TRUE;
}

void stream_init(struct stream *st, uint32_t idx) {
	char name[8];

	snprintf(name, 8, "m%d", idx);
	memset(st->location, 0, sizeof(st->location));
	memset(st->encoding, 0, sizeof(st->encoding));
	st->enable_config_int = true;
	st->latency = DEFAULT_LATENCY;
	st->pipeline = gst_pipeline_new(name);
	st->bus = gst_pipeline_get_bus(GST_PIPELINE(st->pipeline));
	gst_bus_add_watch(st->bus, bus_cb, st);
	memset(st->elem, 0, sizeof(st->elem));
	st->stop = NULL;
	st->ack_started = NULL;
}

void stream_destroy(struct stream *st) {
	stream_stop_pipeline(st);
	gst_bus_remove_watch(st->bus);
	g_object_unref(st->pipeline);
	st->pipeline = NULL;
}

void stream_set_location(struct stream *st, const char *loc) {
	strncpy(st->location, loc, sizeof(st->location));
}

void stream_set_encoding(struct stream *st, const char *encoding) {
	strncpy(st->encoding, encoding, sizeof(st->encoding));
}

void stream_set_host(struct stream *st, const char *host) {
	strncpy(st->host, host, sizeof(st->host));
}

void stream_set_port(struct stream *st, uint32_t port) {
	st->port = port;
}

void stream_set_config_interval(struct stream *st, bool enable) {
	st->enable_config_int = enable;
}
