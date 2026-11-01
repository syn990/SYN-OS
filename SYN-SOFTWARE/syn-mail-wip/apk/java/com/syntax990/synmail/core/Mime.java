/* ------------------------------------------------------------------------
 *   Reading a message, as the desktop's mime.c does: header fields
 *   unfolded, RFC 2047 encoded words, quoted-printable and base64, charsets,
 *   and the best text of a multipart message (text/plain first, else HTML
 *   reduced to text; attachments skipped). A message is handled as bytes
 *   (ISO-8859-1 in a String, one char a byte) until its charset is known.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL-APK
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
package com.syntax990.synmail.core;

import java.io.ByteArrayOutputStream;
import java.nio.charset.Charset;
import java.nio.charset.StandardCharsets;
import java.util.Base64;
import java.util.LinkedHashMap;
import java.util.Locale;
import java.util.Map;

public final class Mime {
	private static final int DEPTH = 8;

	private Mime() {
	}

	/** The header fields of a block, unfolded and undecoded; get() gives "" for one that is missing. */
	public static final class Headers {
		final Map<String, String> f = new LinkedHashMap<>();

		public String get(String name) {
			String v = f.get(name.toLowerCase(Locale.ROOT));
			return v == null ? "" : v;
		}

		public boolean has(String name) {
			return f.containsKey(name.toLowerCase(Locale.ROOT));
		}
	}

	static String latin(byte[] b) {
		return new String(b, StandardCharsets.ISO_8859_1);
	}

	static byte[] bytes(String latin) {
		return latin.getBytes(StandardCharsets.ISO_8859_1);
	}

	/** where the body starts: after the first blank line */
	static int bodyAt(String m) {
		if (m.startsWith("\r\n")) return 2;
		if (m.startsWith("\n")) return 1;
		for (int i = 0; i + 1 < m.length(); i++) {
			if (m.charAt(i) != '\n') continue;
			if (m.charAt(i + 1) == '\n') return i + 2;
			if (m.charAt(i + 1) == '\r' && i + 2 < m.length() && m.charAt(i + 2) == '\n') return i + 3;
		}
		return m.length();
	}

	public static Headers headers(byte[] msg) {
		return headers(latin(msg));
	}

	static Headers headers(String m) {
		Headers h = new Headers();
		String block = m.substring(0, bodyAt(m));
		String name = null;
		StringBuilder val = null;
		for (String line : block.split("\n", -1)) {
			if (line.endsWith("\r")) line = line.substring(0, line.length() - 1);
			if (!line.isEmpty() && (line.charAt(0) == ' ' || line.charAt(0) == '\t')) {
				if (val != null) val.append(' ').append(line.trim());
				continue;
			}
			if (name != null && !h.f.containsKey(name)) h.f.put(name, val.toString());
			name = null;
			int c = line.indexOf(':');
			if (c <= 0) continue;
			name = line.substring(0, c).trim().toLowerCase(Locale.ROOT);
			val = new StringBuilder(line.substring(c + 1).trim());
		}
		if (name != null && !h.f.containsKey(name)) h.f.put(name, val.toString());
		return h;
	}

	static Charset charset(String cs) {
		if (cs == null || cs.isEmpty()) return StandardCharsets.UTF_8;
		int star = cs.indexOf('*'); // an RFC 2231 language tag
		if (star >= 0) cs = cs.substring(0, star);
		try {
			return Charset.forName(cs.trim());
		} catch (RuntimeException e) {
			return StandardCharsets.UTF_8;
		}
	}

	static int hexv(char c) {
		if (c >= '0' && c <= '9') return c - '0';
		c = Character.toUpperCase(c);
		if (c >= 'A' && c <= 'F') return c - 'A' + 10;
		return -1;
	}

	/** quoted-printable; in a header '_' is a space (RFC 2047 Q) */
	static byte[] qp(String s, boolean header) {
		ByteArrayOutputStream o = new ByteArrayOutputStream(s.length());
		for (int i = 0; i < s.length(); i++) {
			char c = s.charAt(i);
			if (c == '=' && i + 1 < s.length() && (s.charAt(i + 1) == '\n' || s.charAt(i + 1) == '\r')) {
				i += (s.charAt(i + 1) == '\r' && i + 2 < s.length() && s.charAt(i + 2) == '\n') ? 2 : 1;
				continue;
			}
			if (c == '=' && i + 2 < s.length() && hexv(s.charAt(i + 1)) >= 0 && hexv(s.charAt(i + 2)) >= 0) {
				o.write(hexv(s.charAt(i + 1)) * 16 + hexv(s.charAt(i + 2)));
				i += 2;
				continue;
			}
			if (header && c == '_') c = ' ';
			o.write(c & 0xff);
		}
		return o.toByteArray();
	}

	/** base64, tolerant of line breaks and junk, as mail is */
	static byte[] b64(String s) {
		StringBuilder b = new StringBuilder(s.length());
		for (int i = 0; i < s.length(); i++) {
			char c = s.charAt(i);
			if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' || c == '/') b.append(c);
		}
		int r = b.length() % 4;
		if (r == 1) b.setLength(b.length() - 1);
		else if (r != 0) {
			while (b.length() % 4 != 0) b.append('=');
		}
		try {
			return Base64.getDecoder().decode(b.toString());
		} catch (IllegalArgumentException e) {
			return new byte[0];
		}
	}

	/** a header value with its encoded words decoded; plain bytes are read as UTF-8 */
	public static String words(String raw) {
		StringBuilder out = new StringBuilder();
		int p = 0;
		boolean lastWord = false;
		while (p < raw.length()) {
			int w = raw.indexOf("=?", p);
			if (w < 0) {
				out.append(utf8(raw.substring(p)));
				break;
			}
			int q1 = raw.indexOf('?', w + 2);
			int q2 = q1 < 0 ? -1 : raw.indexOf('?', q1 + 1);
			int end = q2 < 0 ? -1 : raw.indexOf("?=", q2 + 1);
			if (q1 < 0 || q2 < 0 || end < 0 || q2 - q1 != 2) {
				out.append(utf8(raw.substring(p, w + 2)));
				p = w + 2;
				lastWord = false;
				continue;
			}
			String between = raw.substring(p, w);
			// whitespace between two encoded words is dropped (RFC 2047 6.2)
			if (!(lastWord && between.trim().isEmpty())) out.append(utf8(between));
			Charset cs = charset(raw.substring(w + 2, q1));
			String data = raw.substring(q2 + 1, end);
			byte[] b = Character.toUpperCase(raw.charAt(q1 + 1)) == 'B' ? b64(data) : qp(data, true);
			out.append(new String(b, cs));
			p = end + 2;
			lastWord = true;
		}
		return out.toString();
	}

	private static String utf8(String latin) {
		return new String(bytes(latin), StandardCharsets.UTF_8);
	}

	/** a parameter (charset, boundary) of a Content-Type value */
	static String param(String v, String k) {
		String lk = k.toLowerCase(Locale.ROOT) + "=";
		for (int p = v.indexOf(';'); p >= 0; p = v.indexOf(';', p + 1)) {
			String s = v.substring(p + 1).trim();
			if (!s.toLowerCase(Locale.ROOT).startsWith(lk)) continue;
			s = s.substring(lk.length());
			if (s.startsWith("\"")) {
				int e = s.indexOf('"', 1);
				return e < 0 ? s.substring(1) : s.substring(1, e);
			}
			int e = 0;
			while (e < s.length() && s.charAt(e) != ';' && !Character.isWhitespace(s.charAt(e))) e++;
			return s.substring(0, e);
		}
		return "";
	}

	private static final String[][] ENTS = {
		{"&nbsp;", " "}, {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&#39;", "'"},
	};

	static String stripHtml(String s) {
		StringBuilder out = new StringBuilder();
		boolean inTag = false, skip = false;
		String low = s.toLowerCase(Locale.ROOT);
		for (int i = 0; i < s.length(); i++) {
			char c = s.charAt(i);
			if (c == '<') {
				inTag = true;
				if (low.startsWith("<style", i) || low.startsWith("<script", i)) skip = true;
				else if (low.startsWith("</style", i) || low.startsWith("</script", i)) skip = false;
				else if (low.startsWith("<br", i) || low.startsWith("<p", i) || low.startsWith("</p", i) || low.startsWith("<div", i)
					|| low.startsWith("<tr", i)) out.append('\n');
				continue;
			}
			if (inTag) {
				if (c == '>') inTag = false;
				continue;
			}
			if (skip) continue;
			if (c == '&') {
				boolean hit = false;
				for (String[] e : ENTS) {
					if (low.startsWith(e[0], i)) {
						out.append(e[1]);
						i += e[0].length() - 1;
						hit = true;
						break;
					}
				}
				if (hit) continue;
			}
			if (c != '\r') out.append(c);
		}
		return out.toString();
	}

	private static String leaf(String m, String ct) {
		Headers h = headers(m);
		String cte = h.get("content-transfer-encoding").trim().toLowerCase(Locale.ROOT);
		String body = m.substring(bodyAt(m));
		byte[] raw = cte.startsWith("base64") ? b64(body) : cte.startsWith("quoted-printable") ? qp(body, false) : bytes(body);
		String u = new String(raw, charset(param(ct, "charset")));
		if (ct.toLowerCase(Locale.ROOT).startsWith("text/html")) return stripHtml(u);
		return u.replace("\r", "");
	}

	private static String walk(String m, String want, int depth) {
		if (depth > DEPTH) return null;
		Headers h = headers(m);
		String ct = h.has("content-type") ? h.get("content-type") : "text/plain";
		String lct = ct.toLowerCase(Locale.ROOT);
		if (lct.startsWith("multipart/")) {
			String bd = param(ct, "boundary");
			if (bd.isEmpty()) return null;
			String delim = "--" + bd;
			int p = bodyAt(m), part = -1;
			while (p < m.length()) {
				int nl = m.indexOf('\n', p);
				int le = nl < 0 ? m.length() : nl;
				if (m.startsWith(delim, p) && le - p >= delim.length()) {
					if (part >= 0) {
						int pe = p;
						if (pe > part && m.charAt(pe - 1) == '\n') pe--;
						if (pe > part && m.charAt(pe - 1) == '\r') pe--;
						String r = walk(m.substring(part, pe), want, depth + 1);
						if (r != null) return r;
					}
					if (m.startsWith(delim + "--", p)) break;
					part = nl < 0 ? m.length() : nl + 1;
				}
				p = nl < 0 ? m.length() : nl + 1;
			}
			return null;
		}
		if (lct.startsWith(want) && !h.get("content-disposition").toLowerCase(Locale.ROOT).startsWith("attachment")) return leaf(m, ct);
		return null;
	}

	/** the readable text of a whole message */
	public static String text(byte[] msg) {
		String m = latin(msg);
		String r = walk(m, "text/plain", 0);
		if (r == null) r = walk(m, "text/html", 0);
		return r == null ? "(no readable text in this message)\n" : r;
	}
}
