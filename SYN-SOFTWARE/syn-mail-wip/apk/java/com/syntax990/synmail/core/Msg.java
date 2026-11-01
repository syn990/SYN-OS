/* ------------------------------------------------------------------------
 *   Writing a message, the same plain RFC 5322 text as the desktop's msg.c
 *   (and SYN-CASTER's fn_gmail.c): checked addresses, the subject on one
 *   line, the body in base64 UTF-8, so nothing typed can add a header.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL-APK
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
package com.syntax990.synmail.core;

import java.nio.charset.StandardCharsets;
import java.security.SecureRandom;
import java.time.ZonedDateTime;
import java.time.format.DateTimeFormatter;
import java.util.Base64;
import java.util.Locale;

public final class Msg {
	private static final SecureRandom RNG = new SecureRandom();
	private static final DateTimeFormatter DATE = DateTimeFormatter.ofPattern("EEE, dd MMM yyyy HH:mm:ss Z", Locale.ENGLISH);

	private Msg() {
	}

	/** one plain address: one @ with a dot after it, no spaces, brackets or control characters, 120 at most */
	public static boolean addrOk(String a) {
		if (a == null || a.isEmpty() || a.length() > 120) return false;
		int at = a.indexOf('@');
		if (at <= 0 || at == a.length() - 1 || a.indexOf('@', at + 1) >= 0 || a.indexOf('.', at + 1) < 0) return false;
		for (int i = 0; i < a.length(); i++) {
			char c = a.charAt(i);
			if (c <= 0x20 || c == 0x7f || Character.isWhitespace(c) || "<>()[],;:\"\\".indexOf(c) >= 0) return false;
		}
		return true;
	}

	public static String id(String fromAddr) {
		byte[] r = new byte[12];
		RNG.nextBytes(r);
		StringBuilder h = new StringBuilder();
		for (byte b : r) h.append(String.format("%02x", b & 0xff));
		int at = fromAddr.indexOf('@');
		return "<" + (System.currentTimeMillis() / 1000) + "." + h + "@" + (at >= 0 ? fromAddr.substring(at + 1) : "syn-mail.invalid") + ">";
	}

	private static boolean asciiPlain(String s) {
		for (int i = 0; i < s.length(); i++) {
			char c = s.charAt(i);
			if (c >= 0x7f || c < 0x20) return false;
		}
		return true;
	}

	private static String word(String s) {
		if (asciiPlain(s)) return s;
		return "=?UTF-8?B?" + Base64.getEncoder().encodeToString(s.getBytes(StandardCharsets.UTF_8)) + "?=";
	}

	private static String addr(String name, String a) {
		StringBuilder b = new StringBuilder();
		if (name != null && !name.isEmpty()) {
			if (asciiPlain(name)) b.append('"').append(name.replace("\\", "\\\\").replace("\"", "\\\"")).append('"');
			else b.append(word(name));
			b.append(' ');
		}
		return b.append('<').append(a).append('>').toString();
	}

	private static String oneLine(String s, int max) {
		if (s == null) return "";
		s = s.replace('\r', ' ').replace('\n', ' ').replace('\0', ' ');
		return s.length() > max ? s.substring(0, max) : s;
	}

	/** the message text with CRLF line ends; IllegalArgumentException if an address will not do */
	public static String build(String fromName, String fromAddr, String to, String subject, String body, String msgid, String replyTo,
		ZonedDateTime when) {
		if (!addrOk(fromAddr)) throw new IllegalArgumentException("the sending address is not valid");
		if (!addrOk(to)) throw new IllegalArgumentException("that is not one plain mail address: " + oneLine(to, 60));
		if (msgid == null || msgid.isEmpty()) throw new IllegalArgumentException("no Message-ID");
		StringBuilder o = new StringBuilder();
		o.append("From: ").append(addr(oneLine(fromName, 120), fromAddr));
		o.append("\r\nTo: ").append(addr(null, to));
		o.append("\r\nSubject: ").append(word(oneLine(subject, 239)));
		o.append("\r\nDate: ").append(DATE.format(when));
		o.append("\r\nMessage-ID: ").append(oneLine(msgid, 200));
		String rt = oneLine(replyTo, 200).trim();
		if (!rt.isEmpty()) {
			o.append("\r\nIn-Reply-To: ").append(rt);
			o.append("\r\nReferences: ").append(rt);
		}
		o.append("\r\nMIME-Version: 1.0\r\nContent-Type: text/plain; charset=UTF-8\r\n"
			+ "Content-Transfer-Encoding: base64\r\nX-Mailer: SYN-Mail\r\n\r\n");
		String b = Base64.getEncoder().encodeToString((body == null ? "" : body).getBytes(StandardCharsets.UTF_8));
		for (int i = 0; i < b.length(); i += 76) o.append(b, i, Math.min(b.length(), i + 76)).append("\r\n");
		return o.toString();
	}

	/** "Re: " once */
	public static String reSubject(String s) {
		s = s == null ? "" : s.trim();
		return s.regionMatches(true, 0, "re:", 0, 3) ? s : "Re: " + s;
	}

	/** the bare address out of a From value: "Name <a@b>" gives a@b */
	public static String bareAddr(String from) {
		if (from == null) return "";
		int l = from.lastIndexOf('<'), r = from.lastIndexOf('>');
		String a = l >= 0 && r > l ? from.substring(l + 1, r) : from;
		return a.trim();
	}

	/** the reply's body: the original quoted below a line */
	public static String quoted(String from, String date, String text) {
		StringBuilder b = new StringBuilder("\n\nOn ").append(date).append(", ").append(from).append(" wrote:\n");
		for (String l : text.split("\n", -1)) b.append("> ").append(l).append('\n');
		return b.toString();
	}
}
