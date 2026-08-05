/* ------------------------------------------------------------------------
 *   SYN-AGENT — input forwarding wire format (Phase 4).
 *
 *   Deliberately the simplest thing that works: fixed-size binary
 *   structs sent as individual UDP datagrams on their own port,
 *   separate from the video stream. No framing, no muxing, no
 *   sequence numbers or acks — matches this phase's explicit scope
 *   ("keep the C code safe, don't worry about complex networking").
 *   Connection/pairing is an explicitly separate later phase; this
 *   assumes a fixed, already-known destination, same as
 *   stream_send.c/stream_view.c do today.
 *
 *   Mouse only in this pass — motion (absolute, 0..UINT16_MAX
 *   normalized range so the sender never needs to know the viewer
 *   window's pixel size, and the receiver never needs to know the
 *   remote screen's actual resolution) and button state. Keyboard
 *   forwarding is a separate follow-up (needs a real XKB keymap
 *   uploaded to the compositor first, genuinely more involved than
 *   pointer events — not scoped into this pass).
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-AGENT (Phase 4 — input forwarding)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_AGENT_INPUT_PROTOCOL_H
#define SYN_AGENT_INPUT_PROTOCOL_H

#include <stdint.h>

#define SYN_AGENT_INPUT_PORT_OFFSET 1 /* video on udp:9999, input on udp:10000 by convention (base+1) */

enum syn_agent_input_type {
	SYN_AGENT_INPUT_MOTION = 1,
	SYN_AGENT_INPUT_BUTTON = 2,
};

/* Every packet on the input socket starts with this type tag so the
 * receiver knows how to interpret the rest — deliberately not a
 * tagged union in the C sense (no portable way to send that over the
 * wire safely across compilers/architectures), just "read the type,
 * then read the matching fixed struct." */
struct syn_agent_input_header {
	uint8_t type;
} __attribute__((packed));

/* x/y are normalized to [0, UINT16_MAX] regardless of the sending
 * window's actual size or the target screen's actual resolution —
 * the receiver scales by its own known width/height. This avoids
 * ever needing to communicate screen dimensions over this channel;
 * the video channel already tells the viewer the remote resolution
 * (session's buffer_size, per capture_test.c/stream_send.c), and
 * that's the only source of truth needed. */
struct syn_agent_input_motion {
	struct syn_agent_input_header hdr;
	uint16_t x;
	uint16_t y;
} __attribute__((packed));

enum syn_agent_mouse_button {
	SYN_AGENT_BUTTON_LEFT = 1,
	SYN_AGENT_BUTTON_RIGHT = 2,
	SYN_AGENT_BUTTON_MIDDLE = 3,
};

struct syn_agent_input_button {
	struct syn_agent_input_header hdr;
	uint8_t button; /* enum syn_agent_mouse_button */
	uint8_t pressed; /* 1 = pressed, 0 = released */
} __attribute__((packed));

#endif
