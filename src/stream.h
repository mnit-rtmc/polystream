#ifndef STREAM_H
#define STREAM_H

#include <stdint.h>
#include <string.h>
#include <gst/gst.h>

#define MAX_ELEMS	(16)

struct stream {
	char		location[128];
	char		encoding[8];
	char		host[128];
	uint32_t	port;
	bool		enable_config_int;
	uint32_t	latency;
	GstElement	*pipeline;
	GstBus		*bus;
	GstElement	*elem[MAX_ELEMS];
	void		(*stop)		(struct stream *st);
	void		(*ack_started)	(struct stream *st);
};

void stream_start_pipeline(struct stream *st);
void stream_stop_pipeline(struct stream *st);
void stream_init(struct stream *st, uint32_t idx);
void stream_destroy(struct stream *st);
void stream_set_location(struct stream *st, const char *loc);
void stream_set_encoding(struct stream *st, const char *encoding);
void stream_set_host(struct stream *st, const char *host);
void stream_set_port(struct stream *st, uint32_t port);
void stream_set_config_interval(struct stream *st, bool enable);

#endif
