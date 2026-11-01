/* ------------------------------------------------------------------------
 *   SYN-CAST protocol v1 as data, the same as the desktop's zproto.c:
 *   request builders and the 9-byte binary frame header. Events are parsed
 *   with Json.object() where they are handled (Engine.onJson).
 *
 *   Wire summary (SYN-CASTER's docs/cast-protocol.md):
 *     text frames   one JSON object: a request carries "type" and "seq",
 *                   its answer is {"type":"ok"|"error","seq":N,...};
 *                   events carry "type" and no "seq"
 *     binary frames [kind:u8][id:u32 BE][seq:u32 BE][payload]
 *                   kind 1 audio: id = stream, seq = packet number from 0
 *                   kind 2 picture: id = upload (sent) or blob (received)
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CAST-APK
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
package com.syntax990.syncast.core;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;

public final class Proto {
	public static final int VERSION = 1;
	public static final String PATH = "/cast/v1";
	public static final int BIN_AUDIO = 1, BIN_PIC = 2;
	public static final int HDR = 9;
	public static final int TEXT_MAX = 4000;
	public static final String CLIENT = "syn-cast-apk/0.3.0";

	private Proto() {
	}

	/** How a voice stream is cut: Opus at rate Hz, framesPerPacket frames of frameMs in one packet. */
	public static final class Codec {
		public final int rate, framesPerPacket, frameMs;

		public Codec(int rate, int framesPerPacket, int frameMs) {
			this.rate = rate;
			this.framesPerPacket = framesPerPacket;
			this.frameMs = frameMs;
		}

		/** The talk event's rate and frame_ms; anything odd falls back to 16 kHz, 20 ms. */
		public static Codec of(Map<String, Object> ev) {
			int rate = (int) Json.lng(ev, "rate"), fm = (int) Json.lng(ev, "frame_ms");
			return new Codec(rate > 0 && rate < 65536 ? rate : 16000, 1, fm == 20 || fm == 40 || fm == 60 ? fm : 20);
		}

		public int packetMs() {
			return framesPerPacket * frameMs;
		}
	}

	/** What this app announces: 16 kHz, one 60 ms frame per packet. */
	public static final Codec PTT = new Codec(16000, 1, 60);

	private static Map<String, Object> request(String type, int seq) {
		return Json.obj("type", type, "seq", (double) seq);
	}

	private static void address(Map<String, Object> m, String channel, String to, String dmField) {
		if (to != null && !to.isEmpty()) m.put(dmField, to);
		else if (channel != null && !channel.isEmpty()) m.put("channel", channel);
	}

	/** An invite code the first time (the answer carries the token to keep), the token after. channels null: every one the member belongs to. */
	public static String hello(int seq, String token, String invite, String device, List<String> channels) {
		Map<String, Object> m = request("hello", seq);
		m.put("v", (double) VERSION);
		if (invite != null && !invite.isEmpty()) m.put("invite", invite);
		else m.put("token", token == null ? "" : token);
		m.put("device", device == null || device.isEmpty() ? "phone" : device);
		m.put("client", CLIENT);
		if (channels != null) m.put("channels", new ArrayList<Object>(channels));
		return Json.write(m);
	}

	public static String join(int seq, String channel) {
		Map<String, Object> m = request("join", seq);
		m.put("channel", channel);
		return Json.write(m);
	}

	public static String leave(int seq, String channel, boolean drop) {
		Map<String, Object> m = request("leave", seq);
		m.put("channel", channel);
		if (drop) m.put("drop", true);
		return Json.write(m);
	}

	public static String talk(int seq, String channel, String to, Codec c) {
		Map<String, Object> m = request("talk", seq);
		address(m, channel, to, "to");
		m.put("codec", "opus");
		m.put("rate", (double) c.rate);
		m.put("channels", 1.0);
		m.put("frame_ms", (double) c.packetMs());
		return Json.write(m);
	}

	public static String over(int seq, long stream) {
		Map<String, Object> m = request("over", seq);
		m.put("stream", (double) stream);
		return Json.write(m);
	}

	public static String say(int seq, String channel, String to, String text) {
		Map<String, Object> m = request("say", seq);
		address(m, channel, to, "to");
		m.put("text", text);
		return Json.write(m);
	}

	public static String pic(int seq, String channel, String to, String mime, int w, int h, int size) {
		Map<String, Object> m = request("pic", seq);
		address(m, channel, to, "to");
		m.put("mime", mime);
		m.put("width", (double) w);
		m.put("height", (double) h);
		m.put("size", (double) size);
		return Json.write(m);
	}

	public static String loc(int seq, String channel, String to, double lat, double lon, double acc, String address) {
		Map<String, Object> m = request("loc", seq);
		address(m, channel, to, "to");
		m.put("lat", lat);
		m.put("lon", lon);
		m.put("accuracy", acc);
		if (address != null && !address.isEmpty()) m.put("address", address);
		return Json.write(m);
	}

	/** G192: every room the member belongs to, with who is inside (the tabs). */
	public static String rooms(int seq) {
		return Json.write(request("rooms", seq));
	}

	public static String presence(int seq, String status, String text) {
		Map<String, Object> m = request("presence", seq);
		m.put("status", status);
		if (text != null && !text.isEmpty()) m.put("text", text);
		return Json.write(m);
	}

	public static String history(int seq, String channel, String with, int limit, String before) {
		Map<String, Object> m = request("history", seq);
		address(m, channel, with, "with");
		if (limit > 0) m.put("limit", (double) limit);
		if (before != null && !before.isEmpty()) m.put("before", before);
		return Json.write(m);
	}

	public static String historyPlay(int seq, String channel, String with, String message) {
		Map<String, Object> m = request("history", seq);
		address(m, channel, with, "with");
		m.put("play", message);
		return Json.write(m);
	}

	public static byte[] binHeader(int kind, long id, long seq) {
		return new byte[] {(byte) kind, (byte) (id >> 24), (byte) (id >> 16), (byte) (id >> 8), (byte) id,
			(byte) (seq >> 24), (byte) (seq >> 16), (byte) (seq >> 8), (byte) seq};
	}

	public static long be32(byte[] b, int off) {
		return ((long) (b[off] & 0xFF) << 24) | ((b[off + 1] & 0xFF) << 16) | ((b[off + 2] & 0xFF) << 8) | (b[off + 3] & 0xFF);
	}

	/** True for a reply whose code means the token or invite will never work: no retry. */
	public static boolean tokenRefused(String code) {
		return "unauthorized".equals(code) || "expired".equals(code) || "revoked".equals(code) || "version".equals(code);
	}
}
