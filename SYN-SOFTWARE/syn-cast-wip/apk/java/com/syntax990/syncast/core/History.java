/* ------------------------------------------------------------------------
 *   Per-channel history, in the same files as the desktop's history.c:
 *   <dir>/<channel>/index.jsonl (a later record with the same id replaces
 *   the earlier), v<id>.spt voice ("SPT1", codec header, then [u16 BE
 *   length][opus packet]..., length 0 for a lost packet), i<id>.jpg and
 *   i<id>.t.jpg images.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CAST-APK
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
package com.syntax990.syncast.core;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.io.Writer;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

public final class History {
	public enum Kind { VOICE, TEXT, IMAGE, LOCATION, EVENT }

	public static final class Msg {
		public long id, ts;
		public Kind kind = Kind.EVENT;
		public boolean out, played, partial;
		public String from = "", forwho = "", text;
		/** G193: the server's message id (the say/loc answer or event, or a history line): what a fetch is matched on */
		public String sid = "";
		/** G228: who spoke, as the server names them: the cast character ("as", the bot speaking for a crew member),
		    its display name, and the speaker's colour ("#rrggbb", "" for none: drawn plain) */
		public String as = "", name = "", colour = "";
		public int durationMs, w, h;

		/** the name a line is shown under: the character's display name, else its id, else the member */
		public String who() {
			return !name.isEmpty() ? name : !as.isEmpty() ? as : from;
		}

		/** whose portrait goes with the line: the character, else the member */
		public String face() {
			return !as.isEmpty() ? as : from;
		}
		public double lat, lon, acc;

		Map<String, Object> toJson() {
			Map<String, Object> m = Json.obj("id", (double) id, "ts", (double) ts, "kind", kind.name().toLowerCase());
			if (out) m.put("out", true);
			if (played) m.put("played", true);
			if (partial) m.put("partial", true);
			m.put("from", from);
			if (!forwho.isEmpty()) m.put("for", forwho);
			if (sid != null && !sid.isEmpty()) m.put("sid", sid);
			if (!as.isEmpty()) m.put("as", as);
			if (!name.isEmpty()) m.put("name", name);
			if (!colour.isEmpty()) m.put("colour", colour);
			if (durationMs != 0) m.put("duration_ms", (double) durationMs);
			if (w != 0) m.put("w", (double) w);
			if (h != 0) m.put("h", (double) h);
			if (kind == Kind.LOCATION) {
				m.put("lat", lat);
				m.put("lon", lon);
				m.put("acc", acc);
			}
			if (text != null) m.put("text", text);
			return m;
		}

		static Msg fromJson(Map<String, Object> o) {
			Msg m = new Msg();
			m.id = Json.lng(o, "id");
			if (m.id == 0) return null;
			m.ts = Json.lng(o, "ts");
			try {
				m.kind = Kind.valueOf(Json.str(o, "kind").toUpperCase());
			} catch (IllegalArgumentException e) {
				m.kind = Kind.EVENT;
			}
			m.out = Json.bool(o, "out");
			m.played = Json.bool(o, "played");
			m.partial = Json.bool(o, "partial");
			m.from = Json.str(o, "from");
			m.forwho = Json.str(o, "for");
			m.sid = Json.str(o, "sid");
			m.as = Json.str(o, "as");
			m.name = Json.str(o, "name");
			m.colour = Json.str(o, "colour");
			m.durationMs = (int) Json.lng(o, "duration_ms");
			m.w = (int) Json.lng(o, "w");
			m.h = (int) Json.lng(o, "h");
			m.lat = Json.num(o, "lat");
			m.lon = Json.num(o, "lon");
			m.acc = Json.num(o, "acc");
			Object t = o.get("text");
			m.text = t instanceof String ? (String) t : null;
			return m;
		}
	}

	private static long lastId;

	public final File dir;
	public final List<Msg> msgs = new ArrayList<>();
	private Writer index;

	public static String slug(String channel) {
		StringBuilder b = new StringBuilder();
		for (char c : channel.toCharArray()) {
			boolean ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
			b.append(ok ? c : '_');
		}
		if (b.length() == 0) b.append('_');
		if (b.charAt(0) == '.') b.setCharAt(0, '_');
		return b.toString();
	}

	public History(File base, String channel) throws IOException {
		dir = new File(base, slug(channel));
		if (!dir.isDirectory() && !dir.mkdirs()) throw new IOException("cannot create " + dir);
		File f = new File(dir, "index.jsonl");
		int lines = 0;
		if (f.exists()) {
			try (BufferedReader r = new BufferedReader(new InputStreamReader(new FileInputStream(f), StandardCharsets.UTF_8))) {
				String line;
				while ((line = r.readLine()) != null) {
					lines++;
					Map<String, Object> o = Json.object(line);
					Msg m = o == null ? null : Msg.fromJson(o);
					if (m == null) continue;
					int at = indexOf(m.id);
					if (at >= 0) msgs.set(at, m);
					else msgs.add(m);
				}
			}
		}
		for (Msg m : msgs) m.partial = false; /* cut off by a crash: as complete as it gets */
		/* G193: lines fetched from the server are appended to the file but belong earlier: shown in time order */
		msgs.sort((a, b) -> Long.compare(a.ts, b.ts));
		if (lines > msgs.size() + msgs.size() / 2 + 64) rewrite();
		else index = new OutputStreamWriter(new FileOutputStream(f, true), StandardCharsets.UTF_8);
	}

	private int indexOf(long id) {
		for (int k = msgs.size() - 1; k >= 0; k--) {
			if (msgs.get(k).id == id) return k;
		}
		return -1;
	}

	public Msg find(long id) {
		int k = indexOf(id);
		return k < 0 ? null : msgs.get(k);
	}

	private void append(Msg m) {
		if (index == null) return;
		try {
			index.write(Json.write(m.toJson()));
			index.write('\n');
			index.flush();
		} catch (IOException ignored) {
		}
	}

	public synchronized Msg add(Msg m) {
		if (m.ts == 0) m.ts = System.currentTimeMillis();
		if (m.id == 0) {
			long id = System.currentTimeMillis() * 1000;
			synchronized (History.class) {
				if (id <= lastId) id = lastId + 1;
				lastId = id;
			}
			m.id = id;
		}
		msgs.add(m);
		append(m);
		return m;
	}

	/** G193: a message with this server id is held already. */
	public synchronized boolean hasSid(String sid) {
		if (sid == null || sid.isEmpty()) return false;
		for (int k = msgs.size() - 1; k >= 0; k--) if (sid.equals(msgs.get(k).sid)) return true;
		return false;
	}

	/** G193: a text line heard live before server ids were kept (no sid): the same sender and words within 10 minutes. */
	public synchronized Msg sameText(String from, String text, long ts) {
		for (int k = msgs.size() - 1; k >= 0; k--) {
			Msg m = msgs.get(k);
			if (m.kind != Kind.TEXT || (m.sid != null && !m.sid.isEmpty()) || m.text == null) continue;
			if (m.from.equalsIgnoreCase(from) && m.text.equals(text) && Math.abs(m.ts - ts) < 600000) return m;
		}
		return null;
	}

	/** G193: a message fetched from the server, missed while away: put in its place by time, not at the end. */
	public synchronized Msg addPast(Msg m) {
		add(m);
		msgs.remove(msgs.size() - 1);
		int at = msgs.size();
		while (at > 0 && msgs.get(at - 1).ts > m.ts) at--;
		msgs.add(at, m);
		return m;
	}

	public void commit(Msg m) {
		append(m);
	}

	public File voice(long id) {
		return new File(dir, "v" + id + ".spt");
	}

	public File image(long id, boolean thumb) {
		return new File(dir, "i" + id + (thumb ? ".t" : "") + ".jpg");
	}

	private void rewrite() throws IOException {
		File f = new File(dir, "index.jsonl"), tmp = new File(dir, "index.jsonl.tmp");
		try (Writer w = new OutputStreamWriter(new FileOutputStream(tmp), StandardCharsets.UTF_8)) {
			for (Msg m : msgs) {
				w.write(Json.write(m.toJson()));
				w.write('\n');
			}
		}
		if (!tmp.renameTo(f)) throw new IOException("cannot replace " + f);
		if (index != null) index.close();
		index = new OutputStreamWriter(new FileOutputStream(f, true), StandardCharsets.UTF_8);
	}

	private long bytes(Msg m) {
		if (m.kind == Kind.VOICE) return voice(m.id).length();
		if (m.kind == Kind.IMAGE) return image(m.id, false).length() + image(m.id, true).length();
		return 0;
	}

	private void unlink(Msg m) {
		if (m.kind == Kind.VOICE) voice(m.id).delete();
		if (m.kind == Kind.IMAGE) {
			image(m.id, false).delete();
			image(m.id, true).delete();
		}
	}

	/** Drops messages older than days, then oldest first until under maxBytes (0 = no limit). */
	public int prune(int days, long maxBytes) throws IOException {
		long cutoff = days > 0 ? System.currentTimeMillis() - days * 86400000L : Long.MIN_VALUE;
		long total = 0;
		for (Msg m : msgs) total += bytes(m);
		List<Msg> kept = new ArrayList<>();
		int dropped = 0;
		for (Msg m : msgs) {
			if (m.ts < cutoff || (maxBytes > 0 && total > maxBytes)) {
				total -= bytes(m);
				unlink(m);
				dropped++;
			} else {
				kept.add(m);
			}
		}
		if (dropped > 0) {
			msgs.clear();
			msgs.addAll(kept);
			rewrite();
		}
		return dropped;
	}

	public void close() {
		try {
			if (index != null) index.close();
		} catch (IOException ignored) {
		}
		index = null;
	}

	/* ---------- voice files ---------- */

	public static final class VoiceWriter implements AutoCloseable {
		private final FileOutputStream f;
		private int packets;
		private final int packetMs;

		public VoiceWriter(File path, Proto.Codec c) throws IOException {
			f = new FileOutputStream(path);
			f.write(new byte[] {'S', 'P', 'T', '1', (byte) c.rate, (byte) (c.rate >> 8), (byte) c.framesPerPacket,
				(byte) c.frameMs});
			packetMs = c.packetMs();
		}

		/** null or empty: a lost packet */
		public void write(byte[] data) {
			int n = data == null ? 0 : data.length;
			try {
				f.write(new byte[] {(byte) (n >> 8), (byte) n});
				if (n > 0) f.write(data);
				packets++;
			} catch (IOException ignored) {
			}
		}

		public int durationMs() {
			return packets * packetMs;
		}

		@Override
		public void close() {
			try {
				f.close();
			} catch (IOException ignored) {
			}
		}
	}

	/** The packets of a voice file; empty arrays mark lost ones. */
	public static List<byte[]> loadVoice(File path) throws IOException {
		byte[] b = Files.readAllBytes(path.toPath());
		if (b.length < 8 || b[0] != 'S' || b[1] != 'P' || b[2] != 'T' || b[3] != '1') throw new IOException("not a voice file");
		List<byte[]> out = new ArrayList<>();
		int off = 8;
		while (off + 2 <= b.length) {
			int n = ((b[off] & 0xFF) << 8) | (b[off + 1] & 0xFF);
			off += 2;
			if (off + n > b.length) break;
			byte[] p = new byte[n];
			System.arraycopy(b, off, p, 0, n);
			out.add(p);
			off += n;
		}
		return out;
	}
}
