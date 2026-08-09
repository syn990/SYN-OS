/* ------------------------------------------------------------------------
 *   SYN-RELAY — screen watch role (viewer side).
 *
 *   Receives the MPEG-TS/H.264 stream from the host role (cmd_host.c,
 *   on the remote machine) and decodes+displays it in a real SDL2
 *   window in real time, forwarding mouse motion/clicks back over a
 *   second small UDP socket. Keyboard forwarding is a separate
 *   follow-up (needs a real XKB keymap, not scoped into this pass).
 *
 *   The video/SDL logic here (run_watch_child, interrupt_callback) is
 *   relocated verbatim from the pre-merge syn-agent-stream-view
 *   binary's main() — see the top-level merge plan for why. What's new
 *   in this file: cmd_watch_start() daemonizes (fork+setsid+background,
 *   matching cmd_stats_server.c's --start pattern) instead of running
 *   in the foreground, tracks its PID via syn_agent_state.h, and
 *   prompts for the remote IP via syn-uplink-dialpad when called with
 *   none — no more separate wrapper script for that.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-RELAY (screen watch role)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "cmd_watch.h"
#include "input_protocol.h"
#include "syn_agent_state.h"
#include "syn_dialpad_prompt.h"
#include "syn_bar_notify.h"
#include "syn_tone_vocab.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

#include <SDL2/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/socket.h>

static const int SYN_RELAY_VIDEO_PORT = 9999;

static volatile sig_atomic_t g_stop_requested = 0;

static void handle_signal(int sig) {
	(void)sig;
	g_stop_requested = 1;
}

/* Module-level, not passed through FFmpeg's opaque pointer: this
 * process only ever runs one watch session (one window, one input
 * socket), and AVFormatContext.interrupt_callback's opaque param would
 * just carry the same handful of values back to a callback that only
 * this file calls — a context struct would add indirection with no
 * benefit here. */
static SDL_Window *g_window = NULL;
static int g_input_sock = -1;
static struct sockaddr_in g_input_addr;

/* The real cause of a once-diagnosed "1-5 mouse events/sec" bug:
 * SDL_PollEvent was only called once per outer loop iteration, and that
 * iteration is gated by av_read_frame() — a blocking call. Whenever
 * av_read_frame() took any real time to return (normal network/decode
 * jitter), mouse events piled up in SDL's internal queue completely
 * unread, so almost all motion was lost between video packets rather
 * than forwarded continuously. Moving event polling to a separate
 * thread was considered and rejected — SDL_PollEvent is not documented
 * as safe to call from a thread other than the one that created the
 * window/did SDL_Init, so that would trade one real bug for a subtler
 * one.
 *
 * This uses FFmpeg's own documented mechanism for exactly this
 * situation instead: AVFormatContext.interrupt_callback is polled
 * periodically *during* a blocking I/O wait (not just before/after
 * it), so input events get drained and forwarded every few
 * milliseconds even while av_read_frame() is still waiting on the
 * network — no threading, no change to the video receive/decode logic
 * itself. This callback is the ONLY place SDL_PollEvent is called —
 * splitting event handling between two poll sites would mean whichever
 * ran first silently stole events from the other. */
static int interrupt_callback(void *opaque) {
	(void)opaque;
	SDL_Event event;
	static uint32_t last_motion_sent_ms = 0;
	while (SDL_PollEvent(&event)) {
		if (event.type == SDL_QUIT) {
			g_stop_requested = 1;
		} else if (g_input_sock >= 0 &&
				(event.type == SDL_MOUSEMOTION ||
				 event.type == SDL_MOUSEBUTTONDOWN ||
				 event.type == SDL_MOUSEBUTTONUP)) {
			int win_w, win_h;
			SDL_GetWindowSize(g_window, &win_w, &win_h);
			if (win_w <= 0) win_w = 1;
			if (win_h <= 0) win_h = 1;

			if (event.type == SDL_MOUSEMOTION) {
				uint32_t now = SDL_GetTicks();
				if (now - last_motion_sent_ms >= 16) {
					last_motion_sent_ms = now;
					struct syn_agent_input_motion m;
					m.hdr.type = SYN_AGENT_INPUT_MOTION;
					m.x = (uint16_t)(((int64_t)event.motion.x * 65535) / win_w);
					m.y = (uint16_t)(((int64_t)event.motion.y * 65535) / win_h);
					sendto(g_input_sock, &m, sizeof(m), 0,
						(struct sockaddr *)&g_input_addr, sizeof(g_input_addr));
				}
			} else {
				uint8_t syn_button;
				switch (event.button.button) {
				case SDL_BUTTON_LEFT: syn_button = SYN_AGENT_BUTTON_LEFT; break;
				case SDL_BUTTON_RIGHT: syn_button = SYN_AGENT_BUTTON_RIGHT; break;
				case SDL_BUTTON_MIDDLE: syn_button = SYN_AGENT_BUTTON_MIDDLE; break;
				default: syn_button = 0; break;
				}
				if (syn_button != 0) {
					struct syn_agent_input_button b;
					b.hdr.type = SYN_AGENT_INPUT_BUTTON;
					b.button = syn_button;
					b.pressed = (event.type == SDL_MOUSEBUTTONDOWN) ? 1 : 0;
					sendto(g_input_sock, &b, sizeof(b), 0,
						(struct sockaddr *)&g_input_addr, sizeof(g_input_addr));
				}
			}
		}
	}
	return g_stop_requested ? 1 : 0; /* non-zero tells FFmpeg to abort the blocking call */
}

/* Runs entirely in the daemonized child — this IS the old
 * syn-agent-stream-view main(), unchanged in substance, just no longer
 * returning to a shell. remote_ip is the host being watched;
 * input_port defaults to video_port+1 per input_protocol.h's
 * SYN_AGENT_INPUT_PORT_OFFSET convention. */
static int run_watch_child(const char *remote_ip, int video_port, int input_port) {
	char listen_url[64];
	snprintf(listen_url, sizeof(listen_url), "udp://0.0.0.0:%d", video_port);

	g_input_sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (g_input_sock >= 0) {
		g_input_addr.sin_family = AF_INET;
		g_input_addr.sin_port = htons((uint16_t)input_port);
		if (inet_pton(AF_INET, remote_ip, &g_input_addr.sin_addr) != 1) {
			fprintf(stderr, "syn-relay: invalid remote IP %s, input forwarding disabled\n", remote_ip);
			close(g_input_sock);
			g_input_sock = -1;
		}
	} else {
		fprintf(stderr, "syn-relay: could not open input socket, input forwarding disabled\n");
	}

	signal(SIGINT, handle_signal);
	signal(SIGTERM, handle_signal);

	AVFormatContext *in_ctx = NULL;
	AVDictionary *opts = NULL;
	av_dict_set(&opts, "listen_timeout", "15000000", 0); /* 15s, microseconds */
	/* fflags nobuffer avoids seconds of buffered lag once playing, but
	 * NOT shrinking probesize/analyzeduration below FFmpeg's own
	 * defaults — a real bug was traced to exactly that: a too-small
	 * probe budget let avformat_find_stream_info() finish before a
	 * real SPS/PPS had actually arrived, baking an empty/stale
	 * codecpar->extradata into the decoder context permanently. */
	av_dict_set(&opts, "fflags", "nobuffer", 0);

	fprintf(stderr, "syn-relay: waiting for stream on %s...\n", listen_url);
	/* A UDP socket bound before the sender has started anything can
	 * fail the very first open attempt outright (observed in testing)
	 * rather than blocking for listen_timeout. Retrying the whole open
	 * call a few times with a short pause covers this cleanly. */
	int ret = -1;
	for (int attempt = 1; attempt <= 5; attempt++) {
		/* in_ctx must be pre-allocated (not left NULL for
		 * avformat_open_input to allocate itself) so interrupt_callback
		 * can be registered before the blocking open/probe happens —
		 * SDL_Init/window creation haven't happened yet at this point,
		 * so g_window is still NULL here, but that's fine:
		 * interrupt_callback only touches g_window inside the
		 * SDL_MOUSEMOTION/BUTTON branches, and SDL won't have generated
		 * any such events yet since there's no window for them to be
		 * about. The callback still needs to be live this early so
		 * SDL_QUIT (closing the window mid-retry-loop) is honored. */
		in_ctx = avformat_alloc_context();
		in_ctx->interrupt_callback.callback = interrupt_callback;
		in_ctx->interrupt_callback.opaque = NULL;

		AVDictionary *opts_copy = NULL;
		av_dict_copy(&opts_copy, opts, 0);
		ret = avformat_open_input(&in_ctx, listen_url, NULL, &opts_copy);
		av_dict_free(&opts_copy);
		if (ret >= 0) {
			break;
		}
		char errbuf[128];
		av_strerror(ret, errbuf, sizeof(errbuf));
		fprintf(stderr, "syn-relay: open attempt %d/5 failed: %s, retrying...\n", attempt, errbuf);
		if (g_stop_requested) {
			break;
		}
		usleep(500000); /* 0.5s */
	}
	av_dict_free(&opts);
	if (ret < 0 || g_stop_requested) {
		fprintf(stderr, "syn-relay: giving up\n");
		return 1;
	}

	ret = avformat_find_stream_info(in_ctx, NULL);
	if (ret < 0) {
		fprintf(stderr, "syn-relay: avformat_find_stream_info failed\n");
		return 1;
	}

	int video_stream_idx = -1;
	for (unsigned i = 0; i < in_ctx->nb_streams; i++) {
		if (in_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
			video_stream_idx = (int)i;
			break;
		}
	}
	if (video_stream_idx < 0) {
		fprintf(stderr, "syn-relay: no video stream found in incoming data\n");
		return 1;
	}
	AVStream *in_stream = in_ctx->streams[video_stream_idx];
	int width = in_stream->codecpar->width;
	int height = in_stream->codecpar->height;
	fprintf(stderr, "syn-relay: found video stream, codec=%s, %dx%d\n",
		avcodec_get_name(in_stream->codecpar->codec_id), width, height);

	const AVCodec *decoder = avcodec_find_decoder(in_stream->codecpar->codec_id);
	if (!decoder) {
		fprintf(stderr, "syn-relay: no decoder available for this codec\n");
		return 1;
	}
	AVCodecContext *dec_ctx = avcodec_alloc_context3(decoder);
	avcodec_parameters_to_context(dec_ctx, in_stream->codecpar);
	ret = avcodec_open2(dec_ctx, decoder, NULL);
	if (ret < 0) {
		fprintf(stderr, "syn-relay: avcodec_open2 (decoder) failed\n");
		return 1;
	}
	if (SDL_Init(SDL_INIT_VIDEO) != 0) {
		fprintf(stderr, "syn-relay: SDL_Init failed: %s\n", SDL_GetError());
		return 1;
	}
	/* SDL defaults its YUV->RGB conversion to BT.601 — the host role
	 * explicitly tags its encode as BT.709 (see cmd_host.c), so this
	 * must match or every frame gets a wrong-but-plausible color cast. */
	SDL_SetYUVConversionMode(SDL_YUV_CONVERSION_BT709);

	g_window = SDL_CreateWindow("SYN-RELAY — watching",
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		width, height, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
	if (!g_window) {
		fprintf(stderr, "syn-relay: SDL_CreateWindow failed: %s\n", SDL_GetError());
		return 1;
	}
	SDL_Window *window = g_window; /* local alias — rest of this function already refers to `window` */

	/* No PRESENTVSYNC: vsync ties SDL_RenderPresent to the display's
	 * refresh cycle, which can block the render call for up to a full
	 * frame interval — meanwhile UDP keeps receiving into the kernel
	 * socket buffer in the background. This was the real, measured
	 * cause of input feeling laggy — a bufferbloat problem, not an
	 * encode-speed one. */
	SDL_Renderer *renderer = SDL_CreateRenderer(window, -1,
		SDL_RENDERER_ACCELERATED);
	if (!renderer) {
		fprintf(stderr, "syn-relay: SDL_CreateRenderer failed: %s\n", SDL_GetError());
		return 1;
	}

	/* SDL_UpdateYUVTexture always takes its three plane arguments as
	 * literal Y/U/V regardless of which of YV12/IYUV the texture itself
	 * was created with — SDL reorders internally to match. */
	SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_YV12,
		SDL_TEXTUREACCESS_STREAMING, width, height);
	if (!texture) {
		fprintf(stderr, "syn-relay: SDL_CreateTexture failed: %s\n", SDL_GetError());
		return 1;
	}

	AVPacket *pkt = av_packet_alloc();
	AVFrame *frame = av_frame_alloc();

	fprintf(stderr, "syn-relay: playing — close the window or Ctrl+C to stop\n");

	int64_t frames_shown = 0;
	while (!g_stop_requested) {
		/* Event polling (including mouse motion/button forwarding) now
		 * happens entirely inside interrupt_callback, which FFmpeg
		 * calls periodically during av_read_frame()'s blocking wait —
		 * see that function's comment for why. No SDL_PollEvent call
		 * belongs here. */
		ret = av_read_frame(in_ctx, pkt);
		if (ret < 0) {
			if (ret == AVERROR_EOF) {
				fprintf(stderr, "syn-relay: sender closed the stream\n");
			} else {
				char errbuf[128];
				av_strerror(ret, errbuf, sizeof(errbuf));
				fprintf(stderr, "syn-relay: av_read_frame: %s\n", errbuf);
			}
			break;
		}

		if (pkt->stream_index != video_stream_idx) {
			av_packet_unref(pkt);
			continue;
		}

		ret = avcodec_send_packet(dec_ctx, pkt);
		av_packet_unref(pkt);
		if (ret < 0) {
			continue; /* drop bad packets, don't abort the whole session over one */
		}

		while (ret >= 0) {
			ret = avcodec_receive_frame(dec_ctx, frame);
			if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
				break;
			} else if (ret < 0) {
				break;
			}

			/* U/V planes swapped deliberately here (frame->data[2]
			 * passed as the Uplane arg, data[1] as Vplane) — this is
			 * what actually fixed a real, severe color-swap bug
			 * (Cb/Cr swapped, ~180 degree hue rotation) found in
			 * testing. FFmpeg's AV_PIX_FMT_YUV420P layout is Y,U,V in
			 * data[0..2]; this swap is intentional, not a mistake. */
			SDL_UpdateYUVTexture(texture, NULL,
				frame->data[0], frame->linesize[0],
				frame->data[2], frame->linesize[2],
				frame->data[1], frame->linesize[1]);
			SDL_RenderClear(renderer);
			SDL_RenderCopy(renderer, texture, NULL, NULL);
			SDL_RenderPresent(renderer);
			frames_shown++;
		}
	}

	fprintf(stderr, "syn-relay: stopped — displayed %ld frames\n", (long)frames_shown);

	if (g_input_sock >= 0) {
		close(g_input_sock);
	}
	av_frame_free(&frame);
	av_packet_free(&pkt);
	SDL_DestroyTexture(texture);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();

	avcodec_free_context(&dec_ctx);
	avformat_close_input(&in_ctx);

	return 0;
}

int cmd_watch_start(const char *ip_arg) {
	char prompted[256];
	const char *remote_ip = ip_arg;
	if (!remote_ip || !remote_ip[0]) {
		if (!syn_dialpad_prompt("Watch/control node (IP or hostname):", prompted, sizeof(prompted))) {
			return 0; /* cancelled */
		}
		remote_ip = prompted;
	}

	char existing_host[256];
	if (syn_watch_state_get(existing_host, sizeof(existing_host)) != 0) {
		fprintf(stderr, "syn-relay: already watching %s — stop that session first\n", existing_host);
		syn_bar_notify_tone_dtmf(SYN_TONE_FAIL_LOW, SYN_TONE_FAIL_HIGH, SYN_TONE_FAIL_SECONDS);
		return 1;
	}

	int video_port = SYN_RELAY_VIDEO_PORT;
	int input_port = video_port + SYN_AGENT_INPUT_PORT_OFFSET;

	/* Daemonize: fork, save the child's PID, return immediately —
	 * matching cmd_stats_server.c's --start pattern so menu.xml's
	 * Execute action never blocks on a session that runs until
	 * --stop-watching. */
	pid_t pid = fork();
	if (pid < 0) {
		perror("syn-relay: fork");
		syn_bar_notify_tone_dtmf(SYN_TONE_FAIL_LOW, SYN_TONE_FAIL_HIGH, SYN_TONE_FAIL_SECONDS);
		return 1;
	}
	if (pid > 0) {
		if (!syn_watch_state_save(pid, remote_ip)) {
			fprintf(stderr, "syn-relay: failed to save watch state\n");
		}
		/* Confirms the daemon launched, not that it's actually
		 * connected yet (that happens deep in run_watch_child() below,
		 * invisible from here) — same "session started" framing as
		 * cmd_host_start()'s own tone below. */
		syn_bar_notify_tone(SYN_TONE_SUCCESS_HZ, SYN_TONE_SUCCESS_SECONDS);
		printf("Watching %s\n", remote_ip);
		return 0;
	}

	/* Child: detach, redirect stdio to a log file (no terminal exists
	 * when launched from menu.xml), run the video loop. */
	setsid();
	int devnull = open("/dev/null", O_RDWR);
	if (devnull >= 0) {
		dup2(devnull, STDIN_FILENO);
	}
	char log_path[512];
	const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
	snprintf(log_path, sizeof(log_path), "%s/syn-relay.watch-video.log", runtime_dir ? runtime_dir : "/tmp");
	int log_fd = open(log_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (log_fd >= 0) {
		dup2(log_fd, STDOUT_FILENO);
		dup2(log_fd, STDERR_FILENO);
	}

	int rc = run_watch_child(remote_ip, video_port, input_port);
	syn_watch_state_clear();
	_exit(rc);
}

int cmd_watch_stop(void) {
	char host[256];
	pid_t pid = syn_watch_state_get(host, sizeof(host));
	if (pid == 0) {
		fprintf(stderr, "syn-relay: not currently watching a node\n");
		syn_watch_state_clear();
		syn_bar_notify_tone_dtmf(SYN_TONE_FAIL_LOW, SYN_TONE_FAIL_HIGH, SYN_TONE_FAIL_SECONDS);
		return 1;
	}
	kill(pid, SIGTERM);
	syn_watch_state_clear();
	syn_bar_notify_tone_dtmf(SYN_TONE_STOP_LOW, SYN_TONE_STOP_HIGH, SYN_TONE_STOP_SECONDS);
	printf("Stopped watching %s\n", host);
	return 0;
}

int cmd_stat_watching(void) {
	char host[256];
	pid_t pid = syn_watch_state_get(host, sizeof(host));
	if (pid == 0) {
		printf("{\"text\": \"\", \"tooltip\": \"\"}\n");
		return 0;
	}
	printf("{\"text\": \" watching: %s\", \"tooltip\": \"SYN-RELAY watching %s\", \"class\": \"agent-watching\"}\n",
		host, host);
	return 0;
}
