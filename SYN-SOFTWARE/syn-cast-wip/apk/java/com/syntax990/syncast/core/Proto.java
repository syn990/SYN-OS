/* ------------------------------------------------------------------------
 *   The Zello Channel API as data, the same as the desktop's zproto.c:
 *   command builders, the 4-byte codec header and the 9-byte binary
 *   packet header. Events are parsed with Json.object() where they are
 *   handled.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CAST-APK
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
package com.syntax990.syncast.core;

import java.util.ArrayList;
import java.util.Base64;
import java.util.List;
import java.util.Map;

public final class Proto {
	public static final int BIN_AUDIO = 1, BIN_IMAGE = 2;
	public static final int IMAGE_FULL = 1, IMAGE_THUMB = 2;
	public static final int HDR = 9;

	private Proto() {
	}

	public static final class Codec {
		public final int rate, framesPerPacket, frameMs;

		public Codec(int rate, int framesPerPacket, int frameMs) {
			this.rate = rate;
			this.framesPerPacket = framesPerPacket;
			this.frameMs = frameMs;
		}

		public String encode() {
			byte[] b = {(byte) rate, (byte) (rate >> 8), (byte) framesPerPacket, (byte) frameMs};
			return Base64.getEncoder().encodeToString(b);
		}

		/** null when the header is missing or malformed */
		public static Codec decode(String b64) {
			try {
				byte[] b = Base64.getDecoder().decode(b64);
				if (b.length != 4) return null;
				Codec c = new Codec((b[0] & 0xFF) | ((b[1] & 0xFF) << 8), b[2] & 0xFF, b[3] & 0xFF);
				return c.rate == 0 || c.framesPerPacket == 0 || c.frameMs == 0 ? null : c;
			} catch (IllegalArgumentException e) {
				return null;
			}
		}

		public int packetMs() {
			return framesPerPacket * frameMs;
		}
	}

	/** What this app announces: 16 kHz, one 60 ms frame per packet. */
	public static final Codec PTT = new Codec(16000, 1, 60);

	private static Map<String, Object> command(String name, int seq, String channel, String forwho) {
		Map<String, Object> m = Json.obj("command", name, "seq", (double) seq, "channel", channel);
		if (forwho != null && !forwho.isEmpty()) m.put("for", forwho);
		return m;
	}

	public static String logon(int seq, String authToken, String refreshToken, String user, String pass,
		List<String> channels, boolean transcriptions) {
		Map<String, Object> m = command("logon", seq, null, null);
		if (refreshToken != null && !refreshToken.isEmpty()) m.put("refresh_token", refreshToken);
		else if (authToken != null && !authToken.isEmpty()) m.put("auth_token", authToken);
		if (user != null && !user.isEmpty()) {
			m.put("username", user);
			m.put("password", pass == null ? "" : pass);
		}
		m.put("channels", new ArrayList<Object>(channels));
		if (transcriptions) m.put("features", Json.obj("transcriptions", true));
		m.put("platform_type", "Android");
		m.put("platform_name", "SYN-CAST-APK");
		return Json.write(m);
	}

	public static String startStream(int seq, String channel, String forwho, Codec c) {
		Map<String, Object> m = command("start_stream", seq, channel, forwho);
		m.put("type", "audio");
		m.put("codec", "opus");
		m.put("codec_header", c.encode());
		m.put("packet_duration", (double) c.packetMs());
		return Json.write(m);
	}

	public static String stopStream(int seq, String channel, long streamId) {
		Map<String, Object> m = command("stop_stream", seq, channel, null);
		m.put("stream_id", (double) streamId);
		return Json.write(m);
	}

	public static String sendText(int seq, String channel, String forwho, String text) {
		Map<String, Object> m = command("send_text_message", seq, channel, forwho);
		m.put("text", text);
		return Json.write(m);
	}

	public static String sendImage(int seq, String channel, String forwho, int w, int h, int thumbLen, int fullLen) {
		Map<String, Object> m = command("send_image", seq, channel, forwho);
		m.put("type", "jpeg");
		m.put("thumbnail_content_length", (double) thumbLen);
		m.put("content_length", (double) fullLen);
		m.put("width", (double) w);
		m.put("height", (double) h);
		m.put("source", "library");
		return Json.write(m);
	}

	public static String sendLocation(int seq, String channel, String forwho, double lat, double lon, double acc,
		String address) {
		Map<String, Object> m = command("send_location", seq, channel, forwho);
		m.put("latitude", lat);
		m.put("longitude", lon);
		m.put("accuracy", acc);
		m.put("formatted_address", address == null ? "" : address);
		return Json.write(m);
	}

	public static byte[] binHeader(int type, long id, long sub) {
		return new byte[] {(byte) type, (byte) (id >> 24), (byte) (id >> 16), (byte) (id >> 8), (byte) id,
			(byte) (sub >> 24), (byte) (sub >> 16), (byte) (sub >> 8), (byte) sub};
	}

	public static long be32(byte[] b, int off) {
		return ((long) (b[off] & 0xFF) << 24) | ((b[off + 1] & 0xFF) << 16) | ((b[off + 2] & 0xFF) << 8) | (b[off + 3] & 0xFF);
	}
}
