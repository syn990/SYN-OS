/* ------------------------------------------------------------------------
 *   SYN-RELAY — screen host role (watched-machine side).
 *
 *   Runs on the machine being watched/controlled. Captures this
 *   machine's screen, encodes H.264, streams it to a viewer over UDP
 *   (video_child, relocated verbatim from the pre-merge
 *   syn-agent-stream-send binary's main()), and injects mouse input
 *   forwarded back from the viewer via wlr-virtual-pointer
 *   (input_child, relocated verbatim from syn-agent-input-recv's
 *   main()) — two independent blocking event loops (Wayland capture
 *   dispatch vs UDP recv()) that can't trivially share one thread
 *   without restructuring both into a shared poll loop, so they run as
 *   two forked children under one tracked pair, exactly mirroring how
 *   the deleted syn-agent-host.zsh wrapper script paired them.
 *
 *   cmd_host_start() daemonizes the whole pair (fork+setsid+background,
 *   matching cmd_stats_server.c's --start pattern), tracks both child
 *   PIDs via syn_agent_state.h, and prompts for the viewer's IP via
 *   syn-uplink-dialpad when called with none — stream_send is a UDP
 *   sender, it needs a real destination; no pairing/discovery exists yet
 *   to learn this automatically (trusted-LAN-only posture, matching the
 *   stats roles).
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-RELAY (screen host role)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "cmd_host.h"
#include "ext-image-capture-source-v1-client-protocol.h"
#include "ext-image-copy-capture-v1-client-protocol.h"
#include "wlr-virtual-pointer-unstable-v1-client-protocol.h"
#include "input_protocol.h"
#include "syn_agent_state.h"
#include "syn_dialpad_prompt.h"
#include "syn_bar_notify.h"
#include "syn_tone_vocab.h"

#include <wayland-client.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <signal.h>

static const int SYN_RELAY_VIDEO_PORT = 9999;

/* ==========================================================================
 * Video-send child (relocated from syn-agent-stream-send's main.c body)
 * ========================================================================== */

static volatile sig_atomic_t g_video_stop_requested = 0;

static void handle_video_signal(int sig) {
	(void)sig;
	g_video_stop_requested = 1;
}

struct capture_state {
	struct wl_display *display;
	struct wl_registry *registry;

	struct wl_shm *shm;
	struct wl_output *output;
	struct ext_output_image_capture_source_manager_v1 *source_manager;
	struct ext_image_copy_capture_manager_v1 *capture_manager;
	struct ext_image_capture_source_v1 *source;
	struct ext_image_copy_capture_session_v1 *session;

	uint32_t width;
	uint32_t height;
	uint32_t shm_format;
	int have_constraints;

	int frame_done;
	int frame_ok;

	struct wl_shm_pool *pool;
	struct wl_buffer *buffer;
	void *pool_data;
	size_t pool_size;
	uint32_t stride;
};

static void video_registry_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version) {
	struct capture_state *st = data;
	(void)version;

	if (strcmp(interface, wl_shm_interface.name) == 0) {
		st->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	} else if (strcmp(interface, wl_output_interface.name) == 0) {
		if (!st->output) {
			st->output = wl_registry_bind(registry, name, &wl_output_interface, 2);
		}
	} else if (strcmp(interface, ext_output_image_capture_source_manager_v1_interface.name) == 0) {
		st->source_manager = wl_registry_bind(registry, name,
			&ext_output_image_capture_source_manager_v1_interface, 1);
	} else if (strcmp(interface, ext_image_copy_capture_manager_v1_interface.name) == 0) {
		st->capture_manager = wl_registry_bind(registry, name,
			&ext_image_copy_capture_manager_v1_interface, 1);
	}
}

static void video_registry_global_remove(void *data, struct wl_registry *registry,
		uint32_t name) {
	(void)data;
	(void)registry;
	(void)name;
}

static const struct wl_registry_listener video_registry_listener = {
	.global = video_registry_global,
	.global_remove = video_registry_global_remove,
};

static void session_buffer_size(void *data,
		struct ext_image_copy_capture_session_v1 *session,
		uint32_t width, uint32_t height) {
	(void)session;
	struct capture_state *st = data;
	st->width = width;
	st->height = height;
}

static void session_shm_format(void *data,
		struct ext_image_copy_capture_session_v1 *session,
		uint32_t format) {
	(void)session;
	struct capture_state *st = data;
	if (st->shm_format == 0) {
		st->shm_format = format;
	}
}

static void session_dmabuf_device(void *data,
		struct ext_image_copy_capture_session_v1 *session,
		struct wl_array *device) {
	(void)data;
	(void)session;
	(void)device;
}

static void session_dmabuf_format(void *data,
		struct ext_image_copy_capture_session_v1 *session,
		uint32_t format, struct wl_array *modifiers) {
	(void)data;
	(void)session;
	(void)format;
	(void)modifiers;
}

static void session_done(void *data,
		struct ext_image_copy_capture_session_v1 *session) {
	(void)session;
	struct capture_state *st = data;
	st->have_constraints = 1;
}

static void session_stopped(void *data,
		struct ext_image_copy_capture_session_v1 *session) {
	(void)session;
	struct capture_state *st = data;
	fprintf(stderr, "syn-relay: capture session stopped by compositor\n");
	st->frame_done = 1;
	st->frame_ok = 0;
	g_video_stop_requested = 1;
}

static const struct ext_image_copy_capture_session_v1_listener session_listener = {
	.buffer_size = session_buffer_size,
	.shm_format = session_shm_format,
	.dmabuf_device = session_dmabuf_device,
	.dmabuf_format = session_dmabuf_format,
	.done = session_done,
	.stopped = session_stopped,
};

static void frame_transform(void *data,
		struct ext_image_copy_capture_frame_v1 *frame, uint32_t transform) {
	(void)data;
	(void)frame;
	(void)transform;
}

static void frame_damage(void *data,
		struct ext_image_copy_capture_frame_v1 *frame,
		int32_t x, int32_t y, int32_t width, int32_t height) {
	(void)data;
	(void)frame;
	(void)x;
	(void)y;
	(void)width;
	(void)height;
}

static void frame_presentation_time(void *data,
		struct ext_image_copy_capture_frame_v1 *frame,
		uint32_t tv_sec_hi, uint32_t tv_sec_lo, uint32_t tv_nsec) {
	(void)data;
	(void)frame;
	(void)tv_sec_hi;
	(void)tv_sec_lo;
	(void)tv_nsec;
}

static void frame_ready(void *data,
		struct ext_image_copy_capture_frame_v1 *frame) {
	(void)frame;
	struct capture_state *st = data;
	st->frame_done = 1;
	st->frame_ok = 1;
}

static void frame_failed(void *data,
		struct ext_image_copy_capture_frame_v1 *frame,
		uint32_t reason) {
	(void)frame;
	struct capture_state *st = data;
	fprintf(stderr, "syn-relay: frame capture failed (reason=%u)\n", reason);
	st->frame_done = 1;
	st->frame_ok = 0;
}

static const struct ext_image_copy_capture_frame_v1_listener frame_listener = {
	.transform = frame_transform,
	.damage = frame_damage,
	.presentation_time = frame_presentation_time,
	.ready = frame_ready,
	.failed = frame_failed,
};

static int create_shm_file(size_t size) {
	int fd = memfd_create("syn-relay-host-video", 0);
	if (fd < 0) {
		return -1;
	}
	if (ftruncate(fd, (off_t)size) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static int capture_one_frame(struct capture_state *st) {
	st->frame_done = 0;
	st->frame_ok = 0;

	struct ext_image_copy_capture_frame_v1 *frame =
		ext_image_copy_capture_session_v1_create_frame(st->session);
	ext_image_copy_capture_frame_v1_add_listener(frame, &frame_listener, st);
	ext_image_copy_capture_frame_v1_attach_buffer(frame, st->buffer);
	ext_image_copy_capture_frame_v1_damage_buffer(frame, 0, 0, (int32_t)st->width, (int32_t)st->height);
	ext_image_copy_capture_frame_v1_capture(frame);

	while (!st->frame_done) {
		if (g_video_stop_requested) {
			ext_image_copy_capture_frame_v1_destroy(frame);
			return -1;
		}
		if (wl_display_dispatch(st->display) < 0) {
			fprintf(stderr, "syn-relay: display disconnected mid-capture\n");
			return -1;
		}
	}

	ext_image_copy_capture_frame_v1_destroy(frame);
	return st->frame_ok ? 0 : -1;
}

/* Runs entirely in the video-send child. dest_url is udp://<viewer-ip>:<port>. */
static int run_video_child(const char *dest_url) {
	/* signal()'s implicit SA_RESTART on glibc/Linux would silently
	 * restart the blocked wl_display_dispatch() in capture_one_frame()
	 * instead of interrupting it, so --stop-hosting's SIGTERM wouldn't
	 * be noticed until the next Wayland event arrives on its own —
	 * sigaction() with sa_flags=0 is required to actually get EINTR. */
	struct sigaction sa = {0};
	sa.sa_handler = handle_video_signal;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	struct capture_state st;
	memset(&st, 0, sizeof(st));

	st.display = wl_display_connect(NULL);
	if (!st.display) {
		fprintf(stderr, "syn-relay: cannot connect to Wayland display\n");
		return 1;
	}

	st.registry = wl_display_get_registry(st.display);
	wl_registry_add_listener(st.registry, &video_registry_listener, &st);
	wl_display_roundtrip(st.display);

	if (!st.shm || !st.output || !st.source_manager || !st.capture_manager) {
		fprintf(stderr, "syn-relay: compositor is missing a required global for screen capture\n");
		return 1;
	}

	st.source = ext_output_image_capture_source_manager_v1_create_source(st.source_manager, st.output);
	st.session = ext_image_copy_capture_manager_v1_create_session(st.capture_manager, st.source, 0);
	ext_image_copy_capture_session_v1_add_listener(st.session, &session_listener, &st);

	while (!st.have_constraints) {
		if (wl_display_dispatch(st.display) < 0) {
			fprintf(stderr, "syn-relay: display disconnected waiting for capture constraints\n");
			return 1;
		}
	}

	if (st.width == 0 || st.height == 0 || st.shm_format == 0) {
		fprintf(stderr, "syn-relay: no usable shm format/size advertised for capture\n");
		return 1;
	}
	fprintf(stderr, "syn-relay: streaming %ux%u to %s\n", st.width, st.height, dest_url);

	st.stride = st.width * 4;
	st.pool_size = (size_t)st.stride * st.height;

	int fd = create_shm_file(st.pool_size);
	if (fd < 0) {
		perror("syn-relay: create_shm_file");
		return 1;
	}
	st.pool_data = mmap(NULL, st.pool_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (st.pool_data == MAP_FAILED) {
		perror("syn-relay: mmap");
		close(fd);
		return 1;
	}
	st.pool = wl_shm_create_pool(st.shm, fd, (int32_t)st.pool_size);
	st.buffer = wl_shm_pool_create_buffer(st.pool, 0,
		(int32_t)st.width, (int32_t)st.height, (int32_t)st.stride, st.shm_format);
	close(fd);

	/* ---- FFmpeg encode + network output setup ---- */

	const AVCodec *codec = avcodec_find_encoder_by_name("libx264");
	if (!codec) {
		fprintf(stderr, "syn-relay: libx264 encoder not found in this FFmpeg build\n");
		return 1;
	}

	AVCodecContext *enc_ctx = avcodec_alloc_context3(codec);
	enc_ctx->width = (int)st.width;
	enc_ctx->height = (int)st.height;
	enc_ctx->time_base = (AVRational){1, 30};
	enc_ctx->framerate = (AVRational){30, 1};
	enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
	enc_ctx->gop_size = 15;
	enc_ctx->max_b_frames = 0;
	/* Pinned explicitly so x264/the decoder/SDL's YUV->RGB conversion
	 * all agree — left to chance, they can disagree, producing a
	 * wrong-but-plausible-looking tint. */
	enc_ctx->colorspace = AVCOL_SPC_BT709;
	enc_ctx->color_range = AVCOL_RANGE_MPEG;
	/* zerolatency matters for a live stream: B-frame reordering/lookahead
	 * delay directly becomes visible lag between what happens on screen
	 * and what the viewer sees. */
	av_opt_set(enc_ctx->priv_data, "preset", "ultrafast", 0);
	av_opt_set(enc_ctx->priv_data, "tune", "zerolatency", 0);
	/* x264 only emits SPS/PPS once, at the very start of the stream, by
	 * default — a live UDP viewer that joins mid-stream (the normal
	 * case) would never see them and can't decode anything.
	 * repeat-headers re-sends them before every keyframe so a
	 * late-joining viewer can sync up at the next GOP boundary. */
	av_opt_set(enc_ctx->priv_data, "x264-params", "repeat-headers=1", 0);

	int ret = avcodec_open2(enc_ctx, codec, NULL);
	if (ret < 0) {
		char errbuf[128];
		av_strerror(ret, errbuf, sizeof(errbuf));
		fprintf(stderr, "syn-relay: avcodec_open2 failed: %s\n", errbuf);
		return 1;
	}

	AVFormatContext *fmt_ctx = NULL;
	ret = avformat_alloc_output_context2(&fmt_ctx, NULL, "mpegts", dest_url);
	if (ret < 0 || !fmt_ctx) {
		char errbuf[128];
		av_strerror(ret, errbuf, sizeof(errbuf));
		fprintf(stderr, "syn-relay: avformat_alloc_output_context2 failed for %s: %s\n",
			dest_url, errbuf);
		return 1;
	}

	AVStream *stream = avformat_new_stream(fmt_ctx, NULL);
	stream->time_base = enc_ctx->time_base;
	ret = avcodec_parameters_from_context(stream->codecpar, enc_ctx);
	if (ret < 0) {
		fprintf(stderr, "syn-relay: avcodec_parameters_from_context failed\n");
		return 1;
	}

	if (!(fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
		ret = avio_open(&fmt_ctx->pb, dest_url, AVIO_FLAG_WRITE);
		if (ret < 0) {
			char errbuf[128];
			av_strerror(ret, errbuf, sizeof(errbuf));
			fprintf(stderr, "syn-relay: avio_open failed for %s: %s\n", dest_url, errbuf);
			return 1;
		}
	}

	ret = avformat_write_header(fmt_ctx, NULL);
	if (ret < 0) {
		char errbuf[128];
		av_strerror(ret, errbuf, sizeof(errbuf));
		fprintf(stderr, "syn-relay: avformat_write_header failed: %s\n", errbuf);
		return 1;
	}

	struct SwsContext *sws = sws_getContext(
		(int)st.width, (int)st.height, AV_PIX_FMT_BGR0,
		(int)st.width, (int)st.height, AV_PIX_FMT_YUV420P,
		SWS_BILINEAR, NULL, NULL, NULL);
	if (!sws) {
		fprintf(stderr, "syn-relay: sws_getContext failed\n");
		return 1;
	}

	AVFrame *yuv_frame = av_frame_alloc();
	yuv_frame->format = AV_PIX_FMT_YUV420P;
	yuv_frame->width = (int)st.width;
	yuv_frame->height = (int)st.height;
	ret = av_frame_get_buffer(yuv_frame, 0);
	if (ret < 0) {
		fprintf(stderr, "syn-relay: av_frame_get_buffer failed\n");
		return 1;
	}

	AVPacket *pkt = av_packet_alloc();

	int64_t frame_index = 0;
	int64_t frames_sent = 0;
	fprintf(stderr, "syn-relay: video streaming started\n");

	while (!g_video_stop_requested) {
		if (capture_one_frame(&st) != 0) {
			if (g_video_stop_requested) {
				break;
			}
			fprintf(stderr, "syn-relay: capture failed, stopping video\n");
			break;
		}

		const uint8_t *src_planes[1] = { st.pool_data };
		int src_stride[1] = { (int)st.stride };
		sws_scale(sws, src_planes, src_stride, 0, (int)st.height,
			yuv_frame->data, yuv_frame->linesize);

		yuv_frame->pts = frame_index++;

		ret = avcodec_send_frame(enc_ctx, yuv_frame);
		if (ret < 0) {
			fprintf(stderr, "syn-relay: avcodec_send_frame failed\n");
			break;
		}

		while (ret >= 0) {
			ret = avcodec_receive_packet(enc_ctx, pkt);
			if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
				break;
			} else if (ret < 0) {
				fprintf(stderr, "syn-relay: avcodec_receive_packet failed\n");
				break;
			}
			av_packet_rescale_ts(pkt, enc_ctx->time_base, stream->time_base);
			pkt->stream_index = stream->index;
			int write_ret = av_interleaved_write_frame(fmt_ctx, pkt);
			if (write_ret < 0) {
				char errbuf[128];
				av_strerror(write_ret, errbuf, sizeof(errbuf));
				fprintf(stderr, "syn-relay: write failed: %s\n", errbuf);
			}
			av_packet_unref(pkt);
			frames_sent++;
		}
	}

	fprintf(stderr, "syn-relay: video stopping — sent %ld packets\n", (long)frames_sent);

	avcodec_send_frame(enc_ctx, NULL);
	while (1) {
		ret = avcodec_receive_packet(enc_ctx, pkt);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
			break;
		} else if (ret < 0) {
			break;
		}
		av_packet_rescale_ts(pkt, enc_ctx->time_base, stream->time_base);
		pkt->stream_index = stream->index;
		av_interleaved_write_frame(fmt_ctx, pkt);
		av_packet_unref(pkt);
	}

	av_write_trailer(fmt_ctx);

	av_packet_free(&pkt);
	av_frame_free(&yuv_frame);
	sws_freeContext(sws);
	if (!(fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
		avio_closep(&fmt_ctx->pb);
	}
	avformat_free_context(fmt_ctx);
	avcodec_free_context(&enc_ctx);

	munmap(st.pool_data, st.pool_size);
	ext_image_copy_capture_session_v1_destroy(st.session);
	wl_display_disconnect(st.display);

	return 0;
}

/* ==========================================================================
 * Input-recv child (relocated from syn-agent-input-recv's main.c body)
 * ========================================================================== */

static volatile sig_atomic_t g_input_stop_requested = 0;

static void handle_input_signal(int sig) {
	(void)sig;
	g_input_stop_requested = 1;
}

struct injector_state {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_seat *seat;
	struct zwlr_virtual_pointer_manager_v1 *pointer_manager;
	struct zwlr_virtual_pointer_v1 *pointer;
};

static void input_registry_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version) {
	struct injector_state *st = data;
	(void)version;

	if (strcmp(interface, wl_seat_interface.name) == 0) {
		if (!st->seat) {
			st->seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
		}
	} else if (strcmp(interface, zwlr_virtual_pointer_manager_v1_interface.name) == 0) {
		st->pointer_manager = wl_registry_bind(registry, name,
			&zwlr_virtual_pointer_manager_v1_interface, 1);
	}
}

static void input_registry_global_remove(void *data, struct wl_registry *registry,
		uint32_t name) {
	(void)data;
	(void)registry;
	(void)name;
}

static const struct wl_registry_listener input_registry_listener = {
	.global = input_registry_global,
	.global_remove = input_registry_global_remove,
};

static uint32_t now_ms(void) {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

/* Runs entirely in the input-recv child. Injects mouse events arriving
 * on listen_port into this machine's own Wayland session via
 * wlr-virtual-pointer. screen_width/height convert the wire protocol's
 * normalized 0..65535 coordinates back to real pixel positions —
 * auto-detected by the caller via wlr-randr before forking. */
static int run_input_child(int listen_port, uint32_t screen_width, uint32_t screen_height) {
	/* Same SA_RESTART trap as run_video_child() above — the blocking
	 * recv() below needs a real EINTR to notice --stop-hosting. */
	struct sigaction sa = {0};
	sa.sa_handler = handle_input_signal;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	struct injector_state st;
	memset(&st, 0, sizeof(st));

	st.display = wl_display_connect(NULL);
	if (!st.display) {
		fprintf(stderr, "syn-relay: cannot connect to Wayland display for input injection\n");
		return 1;
	}

	st.registry = wl_display_get_registry(st.display);
	wl_registry_add_listener(st.registry, &input_registry_listener, &st);
	wl_display_roundtrip(st.display);

	if (!st.pointer_manager) {
		fprintf(stderr, "syn-relay: compositor does not support "
			"wlr-virtual-pointer-unstable-v1 — cannot inject mouse input\n");
		return 1;
	}

	st.pointer = zwlr_virtual_pointer_manager_v1_create_virtual_pointer(st.pointer_manager, st.seat);
	if (!st.pointer) {
		fprintf(stderr, "syn-relay: create_virtual_pointer failed\n");
		return 1;
	}
	wl_display_roundtrip(st.display);
	fprintf(stderr, "syn-relay: input injector ready, screen=%ux%u\n", screen_width, screen_height);

	int sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0) {
		perror("syn-relay: socket");
		return 1;
	}
	struct sockaddr_in addr = {0};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons((uint16_t)listen_port);
	if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("syn-relay: bind");
		return 1;
	}

	uint8_t buf[64];
	while (!g_input_stop_requested) {
		ssize_t n = recv(sock, buf, sizeof(buf), 0);
		if (n < (ssize_t)sizeof(struct syn_agent_input_header)) {
			continue; /* short/garbage packet, ignore */
		}

		uint8_t type = buf[0];
		if (type == SYN_AGENT_INPUT_MOTION && n >= (ssize_t)sizeof(struct syn_agent_input_motion)) {
			struct syn_agent_input_motion m;
			memcpy(&m, buf, sizeof(m));
			uint32_t px = (uint32_t)(((uint64_t)m.x * screen_width) / 65535);
			uint32_t py = (uint32_t)(((uint64_t)m.y * screen_height) / 65535);
			zwlr_virtual_pointer_v1_motion_absolute(st.pointer, now_ms(),
				px, py, screen_width, screen_height);
			zwlr_virtual_pointer_v1_frame(st.pointer);
			wl_display_flush(st.display);
		} else if (type == SYN_AGENT_INPUT_BUTTON && n >= (ssize_t)sizeof(struct syn_agent_input_button)) {
			struct syn_agent_input_button b;
			memcpy(&b, buf, sizeof(b));
			uint32_t evdev_button;
			switch (b.button) {
			case SYN_AGENT_BUTTON_LEFT: evdev_button = 0x110; break;   /* BTN_LEFT */
			case SYN_AGENT_BUTTON_RIGHT: evdev_button = 0x111; break;  /* BTN_RIGHT */
			case SYN_AGENT_BUTTON_MIDDLE: evdev_button = 0x112; break; /* BTN_MIDDLE */
			default: continue;
			}
			zwlr_virtual_pointer_v1_button(st.pointer, now_ms(), evdev_button,
				b.pressed ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED);
			zwlr_virtual_pointer_v1_frame(st.pointer);
			wl_display_flush(st.display);
		}
	}

	close(sock);
	zwlr_virtual_pointer_v1_destroy(st.pointer);
	wl_display_disconnect(st.display);
	return 0;
}

/* ==========================================================================
 * Public entry points
 * ========================================================================== */

/* Screen resolution via `wlr-randr`, parsed for the "current" mode line
 * (e.g. "3440x1440 px, 99.997002 Hz (preferred, current)"). Needed so
 * the input child can convert the wire protocol's normalized
 * coordinates back to real pixels without hardcoding a resolution. */
static bool detect_screen_resolution(uint32_t *width_out, uint32_t *height_out) {
	FILE *pipe = popen("wlr-randr 2>/dev/null", "r");
	if (!pipe) {
		return false;
	}

	char line[512];
	bool found = false;
	unsigned w = 0, h = 0;
	while (fgets(line, sizeof(line), pipe)) {
		if (strstr(line, "current") && sscanf(line, " %ux%u px", &w, &h) == 2) {
			found = true;
			break;
		}
	}
	pclose(pipe);

	if (!found || w == 0 || h == 0) {
		return false;
	}
	*width_out = w;
	*height_out = h;
	return true;
}

int cmd_host_start(const char *viewer_ip_arg) {
	char prompted[256];
	const char *viewer_ip = viewer_ip_arg;
	if (!viewer_ip || !viewer_ip[0]) {
		if (!syn_dialpad_prompt("Allow which machine to watch/control this one (IP):", prompted, sizeof(prompted))) {
			return 0; /* cancelled */
		}
		viewer_ip = prompted;
	}

	pid_t existing_video, existing_input;
	char existing_viewer[256];
	if (syn_host_state_get(&existing_video, &existing_input, existing_viewer, sizeof(existing_viewer))) {
		fprintf(stderr, "syn-relay: already being watched by %s — stop that session first\n", existing_viewer);
		syn_bar_notify_tone_dtmf(SYN_TONE_FAIL_LOW, SYN_TONE_FAIL_HIGH, SYN_TONE_FAIL_SECONDS);
		return 1;
	}

	uint32_t screen_w, screen_h;
	if (!detect_screen_resolution(&screen_w, &screen_h)) {
		fprintf(stderr, "syn-relay: could not detect screen resolution (is wlr-randr installed?)\n");
		syn_bar_notify_tone_dtmf(SYN_TONE_FAIL_LOW, SYN_TONE_FAIL_HIGH, SYN_TONE_FAIL_SECONDS);
		return 1;
	}

	int video_port = SYN_RELAY_VIDEO_PORT;
	int input_port = video_port + SYN_AGENT_INPUT_PORT_OFFSET;

	char dest_url[64];
	snprintf(dest_url, sizeof(dest_url), "udp://%s:%d", viewer_ip, video_port);

	const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
	if (!runtime_dir) {
		runtime_dir = "/tmp";
	}

	/* Fork the video-send child first. */
	pid_t video_pid = fork();
	if (video_pid < 0) {
		perror("syn-relay: fork (video)");
		syn_bar_notify_tone_dtmf(SYN_TONE_FAIL_LOW, SYN_TONE_FAIL_HIGH, SYN_TONE_FAIL_SECONDS);
		return 1;
	}
	if (video_pid == 0) {
		setsid();
		int devnull = open("/dev/null", O_RDWR);
		if (devnull >= 0) {
			dup2(devnull, STDIN_FILENO);
		}
		char log_path[512];
		snprintf(log_path, sizeof(log_path), "%s/syn-relay.host-video.log", runtime_dir);
		int log_fd = open(log_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (log_fd >= 0) {
			dup2(log_fd, STDOUT_FILENO);
			dup2(log_fd, STDERR_FILENO);
		}
		int rc = run_video_child(dest_url);
		_exit(rc);
	}

	/* Fork the input-recv child. */
	pid_t input_pid = fork();
	if (input_pid < 0) {
		perror("syn-relay: fork (input)");
		kill(video_pid, SIGTERM);
		syn_bar_notify_tone_dtmf(SYN_TONE_FAIL_LOW, SYN_TONE_FAIL_HIGH, SYN_TONE_FAIL_SECONDS);
		return 1;
	}
	if (input_pid == 0) {
		setsid();
		int devnull = open("/dev/null", O_RDWR);
		if (devnull >= 0) {
			dup2(devnull, STDIN_FILENO);
		}
		char log_path[512];
		snprintf(log_path, sizeof(log_path), "%s/syn-relay.host-input.log", runtime_dir);
		int log_fd = open(log_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (log_fd >= 0) {
			dup2(log_fd, STDOUT_FILENO);
			dup2(log_fd, STDERR_FILENO);
		}
		int rc = run_input_child(input_port, screen_w, screen_h);
		_exit(rc);
	}

	/* Parent: both children forked, record the pair, return immediately —
	 * matching cmd_stats_server.c's --start contract. */
	if (!syn_host_state_save(video_pid, input_pid, viewer_ip)) {
		fprintf(stderr, "syn-relay: failed to save host state\n");
	}
	/* Confirms both children forked, not that the stream is actually
	 * flowing yet — same "session started" framing as cmd_watch_start()'s
	 * own tone above. */
	syn_bar_notify_tone(SYN_TONE_SUCCESS_HZ, SYN_TONE_SUCCESS_SECONDS);
	printf("Now being watched by %s (video+input ready)\n", viewer_ip);
	return 0;
}

int cmd_host_stop(void) {
	pid_t video_pid, input_pid;
	char viewer[256];
	if (!syn_host_state_get(&video_pid, &input_pid, viewer, sizeof(viewer))) {
		fprintf(stderr, "syn-relay: not currently being watched\n");
		syn_host_state_clear();
		syn_bar_notify_tone_dtmf(SYN_TONE_FAIL_LOW, SYN_TONE_FAIL_HIGH, SYN_TONE_FAIL_SECONDS);
		return 1;
	}
	kill(video_pid, SIGTERM);
	kill(input_pid, SIGTERM);
	syn_host_state_clear();
	syn_bar_notify_tone_dtmf(SYN_TONE_STOP_LOW, SYN_TONE_STOP_HIGH, SYN_TONE_STOP_SECONDS);
	printf("Stopped being watched\n");
	return 0;
}

int cmd_stat_agent(void) {
	pid_t video_pid, input_pid;
	char viewer[256];
	if (!syn_host_state_get(&video_pid, &input_pid, viewer, sizeof(viewer))) {
		printf("{\"text\": \"\", \"tooltip\": \"\"}\n");
		return 0;
	}
	printf("{\"text\": \" agent: live\", \"tooltip\": \"SYN-RELAY streaming to %s — video:%d input:%d\", \"class\": \"agent-live\"}\n",
		viewer, SYN_RELAY_VIDEO_PORT, SYN_RELAY_VIDEO_PORT + SYN_AGENT_INPUT_PORT_OFFSET);
	return 0;
}
