/* ------------------------------------------------------------------------
 *   IMAP4rev1 (RFC 3501), the part a reader needs, as on the desktop
 *   (imap.c, box.c): sign-in (LOGIN, AUTHENTICATE XOAUTH2), LIST, EXAMINE,
 *   STATUS, FETCH. It only ever reads: EXAMINE, never SELECT, and
 *   BODY.PEEK, never BODY, so no mail is marked read. A literal over
 *   64 MiB is refused before a byte of it is read; a quoted string with CR,
 *   LF or NUL is refused before anything is sent.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL-APK
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
package com.syntax990.synmail.core;

import java.io.Closeable;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import javax.net.ssl.SSLSocketFactory;

public final class Imap implements Closeable {
	public static final long LITERAL_MAX = 64L << 20;

	public static final class Resp {
		/** the response line(s), a literal shown as {N} */
		public String text;
		/** the first literal it carried, or null */
		public byte[] lit;
	}

	public static final class Folder {
		/** the name on the server (modified UTF-7), used in commands */
		public final String raw;
		/** the name to show */
		public final String name;
		public final boolean selectable;

		Folder(String raw, boolean selectable) {
			this.raw = raw;
			this.name = "INBOX".equalsIgnoreCase(raw) ? "Inbox" : Utf7.decode(raw);
			this.selectable = selectable;
		}
	}

	private final Net net;
	private int tag;
	public final List<Resp> resp = new ArrayList<>();
	/** the tagged completion of the last command, after the tag */
	public String status = "";

	private Imap(Net n) {
		net = n;
	}

	public static Imap open(Account a, SSLSocketFactory f) throws IOException {
		Net n = Net.open(a.imapHost, a.imapPort, true, f);
		Imap im = new Imap(n);
		try {
			String g = n.lineText();
			if (!g.startsWith("* OK")) throw new MailException(a.imapHost + ": no IMAP greeting");
		} catch (IOException e) {
			n.close();
			throw e instanceof MailException ? e : new MailException(a.imapHost + ": " + Net.why(e));
		}
		return im;
	}

	/** s as an IMAP quoted string; refused if it holds CR, LF or NUL (they would end the command and start another) */
	public static String quote(String s) throws MailException {
		StringBuilder b = new StringBuilder(s.length() + 2).append('"');
		for (int i = 0; i < s.length(); i++) {
			char c = s.charAt(i);
			if (c == '\r' || c == '\n' || c == 0) throw new MailException("a line break in a name or password is refused: nothing sent");
			if (c == '"' || c == '\\') b.append('\\');
			b.append(c);
		}
		return b.append('"').toString();
	}

	/** 0 OK, 1 NO, 2 BAD; a MailException when the connection is lost or the reply is refused */
	public int cmd(String line) throws IOException {
		resp.clear();
		String t = "A" + (++tag);
		net.write(t + " " + line + "\r\n");
		while (true) {
			Resp r = read();
			if (r.text.startsWith("+")) {
				// a challenge we do not answer (a failed XOAUTH2 says why here): an empty reply ends it
				net.write("\r\n");
				continue;
			}
			if (r.text.startsWith(t + " ")) {
				status = r.text.substring(t.length() + 1);
				String u = status.toUpperCase(Locale.ROOT);
				return u.startsWith("OK") ? 0 : u.startsWith("NO") ? 1 : 2;
			}
			resp.add(r);
		}
	}

	private static final Pattern LITERAL = Pattern.compile("\\{(\\d{1,19})\\+?\\}$");

	private Resp read() throws IOException {
		Resp r = new Resp();
		StringBuilder text = new StringBuilder();
		long total = 0;
		while (true) {
			byte[] line;
			try {
				line = net.line();
			} catch (MailException e) {
				throw e;
			} catch (IOException e) {
				throw new MailException("connection lost");
			}
			String s = new String(line, StandardCharsets.UTF_8);
			text.append(s);
			total += line.length;
			Matcher m = LITERAL.matcher(s);
			if (!m.find()) break;
			long n;
			try {
				n = Long.parseLong(m.group(1));
			} catch (NumberFormatException e) {
				n = Long.MAX_VALUE;
			}
			if (n > LITERAL_MAX || total + n > LITERAL_MAX) {
				throw new MailException("the server sent a reply too large to read (" + (n > LITERAL_MAX ? n : total + n)
					+ " bytes; the limit is " + (LITERAL_MAX >> 20) + " MiB)");
			}
			byte[] lit;
			try {
				lit = net.exact((int) n);
			} catch (IOException e) {
				throw new MailException("connection lost");
			}
			total += n;
			if (r.lit == null) r.lit = lit;
			else {
				// a second literal in one response (a name in LIST after a body): shown inline
				text.append(new String(lit, StandardCharsets.UTF_8));
			}
		}
		r.text = text.toString();
		return r;
	}

	public void signin(Account a, Secrets s, OAuth o) throws IOException {
		int rc;
		if (a.oauth) {
			String tok = o.access(a, s);
			rc = cmd("AUTHENTICATE XOAUTH2 " + OAuth.xoauth2(a.address, tok));
		} else {
			String pw = s.get(a.name, "password");
			if (pw == null || pw.isEmpty()) throw new MailException("no password stored for " + a.name);
			rc = cmd("LOGIN " + quote(a.address) + " " + quote(pw));
		}
		if (rc != 0) throw new MailException(a.imapHost + " refused the sign-in for " + a.name + ": " + status);
	}

	/** the folders, the inbox first */
	public List<Folder> folders() throws IOException {
		if (cmd("LIST \"\" \"*\"") != 0) throw new MailException("LIST: " + status);
		List<Folder> out = new ArrayList<>();
		for (Resp r : resp) {
			if (!r.text.startsWith("* LIST ")) continue;
			int fl = r.text.indexOf('('), fe = r.text.indexOf(')');
			if (fl < 0 || fe < fl) continue;
			String flags = r.text.substring(fl + 1, fe).toLowerCase(Locale.ROOT);
			String rest = r.text.substring(fe + 1).trim();
			// the delimiter: NIL or a quoted character
			int sp = rest.startsWith("\"") ? rest.indexOf('"', rest.charAt(1) == '\\' ? 3 : 2) + 1 : rest.indexOf(' ');
			if (sp <= 0 || sp >= rest.length()) continue;
			String name = rest.substring(sp).trim();
			String raw;
			if (r.lit != null && LITERAL.matcher(name).find()) raw = new String(r.lit, StandardCharsets.UTF_8);
			else if (name.startsWith("\"")) raw = unquote(name);
			else raw = name;
			boolean sel = !flags.contains("\\noselect") && !flags.contains("\\nonexistent");
			Folder f = new Folder(raw, sel);
			if ("INBOX".equalsIgnoreCase(raw)) out.add(0, f);
			else out.add(f);
		}
		return out;
	}

	static String unquote(String q) {
		StringBuilder b = new StringBuilder();
		for (int i = 1; i < q.length(); i++) {
			char c = q.charAt(i);
			if (c == '\\' && i + 1 < q.length()) b.append(q.charAt(++i));
			else if (c == '"') break;
			else b.append(c);
		}
		return b.toString();
	}

	/** EXAMINE (read-only): the number of messages */
	public long examine(String raw) throws IOException {
		if (cmd("EXAMINE " + quote(raw)) != 0) throw new MailException("EXAMINE " + raw + ": " + status);
		return count("EXISTS");
	}

	private long count(String what) {
		for (Resp r : resp) {
			String[] w = r.text.split(" ");
			if (w.length >= 3 && w[0].equals("*") && w[2].equalsIgnoreCase(what)) {
				try {
					return Long.parseLong(w[1]);
				} catch (NumberFormatException ignored) {
				}
			}
		}
		return -1;
	}

	public long unseen(String raw) throws IOException {
		if (cmd("STATUS " + quote(raw) + " (UNSEEN)") != 0) return -1;
		for (Resp r : resp) {
			int u = r.text.indexOf("UNSEEN ");
			if (u >= 0) {
				Matcher m = Pattern.compile("(\\d+)").matcher(r.text.substring(u + 7));
				if (m.lookingAt()) return Long.parseLong(m.group(1));
			}
		}
		return -1;
	}

	private static final Pattern UID = Pattern.compile("\\bUID (\\d+)");
	private static final Pattern FLAGS = Pattern.compile("\\bFLAGS \\(([^)]*)\\)");

	/** a page of the folder, newest first: count messages, skip back from the newest */
	public Page page(String raw, long skip, int count) throws IOException {
		Page p = new Page();
		p.exists = examine(raw);
		p.unseen = unseen(raw);
		if (p.exists <= 0 || count <= 0 || skip >= p.exists) return p;
		long hi = p.exists - skip, lo = Math.max(1, hi - count + 1);
		if (cmd("FETCH " + lo + ":" + hi + " (UID FLAGS BODY.PEEK[HEADER.FIELDS (FROM TO SUBJECT DATE MESSAGE-ID)])") != 0) {
			throw new MailException("FETCH: " + status);
		}
		for (Resp r : resp) {
			String[] w = r.text.split(" ", 4);
			if (w.length < 3 || !w[0].equals("*") || !w[2].equalsIgnoreCase("FETCH")) continue;
			Header h = new Header();
			try {
				h.seq = Long.parseLong(w[1]);
			} catch (NumberFormatException e) {
				continue;
			}
			Matcher m = UID.matcher(r.text);
			if (m.find()) h.uid = Long.parseLong(m.group(1));
			m = FLAGS.matcher(r.text);
			h.seen = m.find() && m.group(1).toLowerCase(Locale.ROOT).contains("\\seen");
			if (r.lit != null) {
				Mime.Headers hd = Mime.headers(r.lit);
				h.from = Mime.words(hd.get("from"));
				h.to = Mime.words(hd.get("to"));
				h.subject = Mime.words(hd.get("subject"));
				h.date = hd.get("date").trim();
				h.messageId = hd.get("message-id").trim();
			}
			p.headers.add(h);
		}
		p.headers.sort((x, y) -> Long.compare(y.seq, x.seq));
		return p;
	}

	/** the whole message by UID, read with BODY.PEEK so it stays unread */
	public byte[] body(String raw, long uid) throws IOException {
		examine(raw);
		if (cmd("UID FETCH " + uid + " (UID BODY.PEEK[])") != 0) throw new MailException("FETCH: " + status);
		for (Resp r : resp) {
			Matcher m = UID.matcher(r.text);
			if (r.lit != null && m.find() && Long.parseLong(m.group(1)) == uid) return r.lit;
		}
		throw new MailException("no message with UID " + uid + " in " + raw);
	}

	@Override
	public void close() {
		try {
			net.write("Z999 LOGOUT\r\n");
		} catch (IOException ignored) {
		}
		net.close();
	}

	public static final class Header {
		public long seq, uid;
		public boolean seen;
		public String from = "", to = "", subject = "", date = "", messageId = "";
	}

	public static final class Page {
		public long exists = -1, unseen = -1;
		public final List<Header> headers = new ArrayList<>();
	}

}
