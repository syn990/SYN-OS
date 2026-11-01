/* ------------------------------------------------------------------------
 *   Sending one message, as the desktop's smtp.c: TLS on 465 (STARTTLS on
 *   587, before any secret), AUTH PLAIN or XOAUTH2, MAIL, RCPT, DATA with
 *   dot-stuffing and CRLF line ends.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL-APK
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
package com.syntax990.synmail.core;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.Base64;

import javax.net.ssl.SSLSocketFactory;

public final class Smtp {
	private Smtp() {
	}

	private static final class Reply {
		int code = -1;
		String last = "connection lost";
	}

	private static Reply reply(Net n) throws IOException {
		Reply r = new Reply();
		while (true) {
			String l;
			try {
				l = n.lineText();
			} catch (MailException e) {
				throw e;
			} catch (IOException e) {
				return r;
			}
			r.last = l;
			if (l.length() < 3) return r;
			if (l.length() == 3 || l.charAt(3) == ' ') {
				try {
					r.code = Integer.parseInt(l.substring(0, 3));
				} catch (NumberFormatException e) {
					r.code = -1;
				}
				return r;
			}
		}
	}

	private static Reply say(Net n, String line) throws IOException {
		n.write(line);
		return reply(n);
	}

	/** the message with dot-stuffing (RFC 5321 4.5.2), bare LF made CRLF, ended with CRLF.CRLF */
	static byte[] stuff(String message) {
		StringBuilder d = new StringBuilder(message.length() + 64);
		boolean bol = true;
		for (int i = 0; i < message.length(); i++) {
			char c = message.charAt(i);
			if (bol && c == '.') d.append('.');
			if (c == '\n' && (i == 0 || message.charAt(i - 1) != '\r')) d.append('\r');
			if (c == '\r' && (i + 1 >= message.length() || message.charAt(i + 1) != '\n')) {
				d.append("\r\n"); // a bare CR is made a line end too
				bol = true;
				continue;
			}
			d.append(c);
			bol = c == '\n';
		}
		if (!bol) d.append("\r\n");
		d.append(".\r\n");
		return d.toString().getBytes(StandardCharsets.UTF_8);
	}

	public static void send(Account a, Secrets s, OAuth o, String to, String message, SSLSocketFactory f) throws IOException {
		if (!Msg.addrOk(to)) throw new MailException("that is not one plain mail address");
		boolean tls = a.smtpPort != 587 && a.smtpPort != 25;
		Net n = Net.open(a.smtpHost, a.smtpPort, tls, f);
		String stage = "greeting";
		Reply r = new Reply();
		try {
			if ((r = reply(n)).code != 220) throw fail(a, stage, r);
			stage = "EHLO";
			if ((r = say(n, "EHLO syn-mail.localdomain\r\n")).code != 250) throw fail(a, stage, r);
			if (!tls) {
				stage = "STARTTLS";
				if ((r = say(n, "STARTTLS\r\n")).code != 220) throw fail(a, stage, r);
				n.upgrade(f);
				stage = "EHLO";
				if ((r = say(n, "EHLO syn-mail.localdomain\r\n")).code != 250) throw fail(a, stage, r);
			}
			stage = "sign-in";
			if (a.oauth) {
				String tok = o.access(a, s);
				r = say(n, "AUTH XOAUTH2 " + OAuth.xoauth2(a.address, tok) + "\r\n");
				if (r.code == 334) r = say(n, "\r\n"); // the error is in the 334; an empty answer gets the final code
				if (r.code != 235) {
					o.forget(a);
					throw fail(a, stage, r);
				}
			} else {
				String pw = s.get(a.name, "password");
				if (pw == null || pw.isEmpty()) throw new MailException("no password stored for " + a.name);
				byte[] raw = ("\0" + a.address + "\0" + pw).getBytes(StandardCharsets.UTF_8);
				if ((r = say(n, "AUTH PLAIN " + Base64.getEncoder().encodeToString(raw) + "\r\n")).code != 235) throw fail(a, stage, r);
			}
			stage = "MAIL FROM";
			if ((r = say(n, "MAIL FROM:<" + a.address + ">\r\n")).code != 250) throw fail(a, stage, r);
			stage = "RCPT TO";
			r = say(n, "RCPT TO:<" + to + ">\r\n");
			if (r.code != 250 && r.code != 251) throw fail(a, stage, r);
			stage = "DATA";
			if ((r = say(n, "DATA\r\n")).code != 354) throw fail(a, stage, r);
			n.write(stuff(message));
			stage = "message";
			if ((r = reply(n)).code != 250) throw fail(a, stage, r);
			try {
				say(n, "QUIT\r\n");
			} catch (IOException ignored) {
			}
		} catch (MailException e) {
			throw e;
		} catch (IOException e) {
			throw new MailException(a.smtpHost + " at " + stage + ": " + Net.why(e));
		} finally {
			n.close();
		}
	}

	private static MailException fail(Account a, String stage, Reply r) {
		return new MailException(a.smtpHost + " at " + stage + ": " + r.last);
	}
}
