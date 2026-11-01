/* ------------------------------------------------------------------------
 *   What the app's screens ask for, one call each, each on its own
 *   connection (a phone drops sockets freely): the folders, a page of a
 *   folder, one message, a send, and the new-mail count for the check.
 *   Read-only on the server: EXAMINE and BODY.PEEK, nothing marked read.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL-APK
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
package com.syntax990.synmail.core;

import java.io.IOException;
import java.time.ZonedDateTime;
import java.util.List;

import javax.net.ssl.SSLSocketFactory;

public final class Mail {
	public final Secrets secrets;
	public final OAuth oauth;
	public final SSLSocketFactory tls;

	public Mail(Secrets s, OAuth o, SSLSocketFactory tls) {
		secrets = s;
		oauth = o;
		this.tls = tls;
	}

	/** One message, read. */
	public static final class Letter {
		public String from = "", to = "", subject = "", date = "", messageId = "", text = "";
	}

	private Imap open(Account a) throws IOException {
		Imap im = Imap.open(a, tls);
		try {
			im.signin(a, secrets, oauth);
		} catch (IOException e) {
			if (a.oauth) oauth.forget(a);
			im.close();
			throw e;
		}
		return im;
	}

	public List<Imap.Folder> folders(Account a) throws IOException {
		try (Imap im = open(a)) {
			return im.folders();
		}
	}

	public Imap.Page page(Account a, String folder, long skip, int count) throws IOException {
		try (Imap im = open(a)) {
			return im.page(folder, skip, count);
		}
	}

	public Letter open(Account a, String folder, long uid) throws IOException {
		byte[] raw;
		try (Imap im = open(a)) {
			raw = im.body(folder, uid);
		}
		Mime.Headers h = Mime.headers(raw);
		Letter l = new Letter();
		l.from = Mime.words(h.get("from"));
		l.to = Mime.words(h.get("to"));
		l.subject = Mime.words(h.get("subject"));
		l.date = h.get("date");
		l.messageId = h.get("message-id");
		l.text = Mime.text(raw);
		return l;
	}

	/** builds and sends; replyTo is the Message-ID answered, or null */
	public void send(Account a, String to, String subject, String body, String replyTo) throws IOException {
		String m;
		try {
			m = Msg.build(a.display, a.address, to.trim(), subject, body, Msg.id(a.address), replyTo, ZonedDateTime.now());
		} catch (IllegalArgumentException e) {
			throw new MailException(e.getMessage());
		}
		Smtp.send(a, secrets, oauth, to.trim(), m, tls);
	}

	/** the unread count of the inbox, by STATUS (nothing selected, nothing marked) */
	public long inboxUnseen(Account a) throws IOException {
		try (Imap im = open(a)) {
			return im.unseen("INBOX");
		}
	}
}
