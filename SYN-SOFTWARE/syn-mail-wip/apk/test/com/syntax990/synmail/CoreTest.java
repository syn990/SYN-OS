/* ------------------------------------------------------------------------
 *   The phone app's mail core on the desktop JVM, against tests/standin.py:
 *   the same stand-in, the same checks as the desktop's tests/run.sh. No
 *   live mail. Usage: CoreTest STANDIN_DIR [EXPORT_DIR]
 *   STANDIN_DIR holds cert.pem, ports, log, sent/; EXPORT_DIR the files
 *   `syn-mail export` wrote (pw.syn-mail, oa.syn-mail), imported as the
 *   phone imports them.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL-APK
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
package com.syntax990.synmail;

import com.syntax990.synmail.core.Account;
import com.syntax990.synmail.core.Imap;
import com.syntax990.synmail.core.Mail;
import com.syntax990.synmail.core.MailException;
import com.syntax990.synmail.core.Msg;
import com.syntax990.synmail.core.OAuth;
import com.syntax990.synmail.core.Secrets;
import com.syntax990.synmail.core.Tls;
import com.syntax990.synmail.core.Utf7;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.time.ZonedDateTime;
import java.util.Base64;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import javax.net.ssl.SSLSocketFactory;

public final class CoreTest {
	static int pass, fail;
	static File dir;

	static void ok(String n) {
		pass++;
		System.out.println("ok   " + n);
	}

	static void no(String n, String why) {
		fail++;
		System.out.println("FAIL " + n);
		if (why != null) System.out.println("     " + why.replace("\n", "\n     "));
	}

	static void check(String n, boolean c, String why) {
		if (c) ok(n);
		else no(n, why);
	}

	static void has(String n, String text, String want) {
		check(n, text != null && text.contains(want), text);
	}

	static void hasnt(String n, String text, String want) {
		check(n, text == null || !text.contains(want), text);
	}

	interface Run {
		void run() throws Exception;
	}

	/** the call must fail with a MailException whose words hold want */
	static void refused(String n, String want, Run r) {
		try {
			r.run();
			no(n, "it went through");
		} catch (MailException e) {
			has(n, e.getMessage(), want);
		} catch (Exception e) {
			no(n, e.toString());
		}
	}

	static String log() throws IOException {
		File f = new File(dir, "log");
		return f.exists() ? new String(Files.readAllBytes(f.toPath()), StandardCharsets.UTF_8) : "";
	}

	static int sentCount() {
		String[] s = new File(dir, "sent").list();
		return s == null ? 0 : s.length;
	}

	static String sent(int n) throws IOException {
		return new String(Files.readAllBytes(new File(dir, "sent/" + n + ".eml").toPath()), StandardCharsets.UTF_8);
	}

	static final class MapSecrets implements Secrets {
		final Map<String, String> m = new HashMap<>();

		void put(String acct, Map<String, String> s) {
			for (Map.Entry<String, String> e : s.entrySet()) m.put(acct + "/" + e.getKey(), e.getValue());
		}

		@Override
		public String get(String account, String kind) {
			return m.get(account + "/" + kind);
		}
	}

	public static void main(String[] args) throws Exception {
		dir = new File(args[0]);
		File exports = args.length > 1 ? new File(args[1]) : null;
		String[] ports = new String(Files.readAllBytes(new File(dir, "ports").toPath()), StandardCharsets.UTF_8).trim().split(" ");
		int pi = Integer.parseInt(ports[0]), ps = Integer.parseInt(ports[1]), ph = Integer.parseInt(ports[2]);
		SSLSocketFactory tls;
		try (InputStream in = new FileInputStream(new File(dir, "cert.pem"))) {
			tls = Tls.trusting(in);
		}
		MapSecrets sec = new MapSecrets();
		OAuth oauth = new OAuth("https://localhost:" + ph + "/token", tls);
		Mail mail = new Mail(sec, oauth, tls);

		// 1. pieces: modified UTF-7, quoting, addresses
		check("utf7 decodes a folder name", Utf7.decode("&AMk-t&AOk-").equals("Été") && Utf7.decode("A&-B").equals("A&B"), Utf7.decode("&AMk-t&AOk-"));
		check("quote escapes quote and backslash", Imap.quote("s3cret pass\"word\\").equals("\"s3cret pass\\\"word\\\\\""), Imap.quote("a\"b"));
		for (String bad : new String[] {"INBOX\r\nX1 SELECT INBOX", "INBOX\nX2 SELECT INBOX", "INBOX\rX3", "INBOX\0X4"}) {
			String show = bad.replace("\r", "\\r").replace("\n", "\\n").replace("\0", "\\0");
			refused("quote refuses " + show, "line break", () -> Imap.quote(bad));
		}
		check("an address with a bracket is refused", !Msg.addrOk("x@y.z>") && !Msg.addrOk("a b@c.d") && !Msg.addrOk("a@b") && Msg.addrOk("to@example.test"), null);

		// 2. the PC's export, imported as the phone imports it
		Account pw, oa;
		if (exports != null && new File(exports, "pw.syn-mail").exists()) {
			Account.Import ip = Account.fromExport(new String(Files.readAllBytes(new File(exports, "pw.syn-mail").toPath()), StandardCharsets.UTF_8));
			Account.Import io = Account.fromExport(new String(Files.readAllBytes(new File(exports, "oa.syn-mail").toPath()), StandardCharsets.UTF_8));
			pw = ip.account;
			oa = io.account;
			sec.put(pw.name, ip.secrets);
			sec.put(oa.name, io.secrets);
			check("the PC's password export imports", !pw.oauth && "pw@example.test".equals(pw.address) && ip.secrets.containsKey("password"), pw.toString());
			check("the PC's Google export imports with its refresh token", oa.oauth && io.secrets.keySet().containsAll(List.of("refresh_token", "client_id", "client_secret")),
				io.secrets.keySet().toString());
			check("the import keeps the display name", "Pat \"the\" Tester".equals(pw.display), pw.display);
		} else {
			System.out.println("skip the PC export import: no export folder given (make test gives one)");
			pw = Account.preset("ionos", "pw", "pw@example.test", false);
			pw.display = "Pat \"the\" Tester";
			oa = Account.preset("google", "oa", "oauth@example.test", true);
			sec.m.put("pw/password", "s3cret pass\"word");
			sec.m.put("oa/refresh_token", "rt-good");
			sec.m.put("oa/client_id", "cid");
			sec.m.put("oa/client_secret", "csec");
		}
		for (Account a : new Account[] {pw, oa}) {
			a.imapHost = a.smtpHost = "localhost";
			a.imapPort = pi;
			a.smtpPort = ps;
		}
		refused("an import with no secret is refused", "no password", () -> {
			try {
				Account.fromExport("{\"syn_mail_account\":1,\"name\":\"x\",\"address\":\"x@y.zz\",\"imap_host\":\"h\",\"smtp_host\":\"h\",\"imap_port\":993,"
					+ "\"smtp_port\":465,\"auth\":\"password\",\"secrets\":{}}");
			} catch (IllegalArgumentException e) {
				throw new MailException(e.getMessage());
			}
		});
		refused("a file that is not an export is refused", "not a SYN-Mail account file", () -> {
			try {
				Account.fromExport("{\"name\":\"x\"}");
			} catch (IllegalArgumentException e) {
				throw new MailException(e.getMessage());
			}
		});

		// 3. folders: the inbox first, modified UTF-7 decoded, a literal name read, \Noselect kept but not selectable
		List<Imap.Folder> fs = mail.folders(pw);
		StringBuilder fl = new StringBuilder();
		for (Imap.Folder f : fs) fl.append(f.name).append(f.selectable ? "" : "(no select)").append(" | ");
		check("folders: the inbox first", !fs.isEmpty() && fs.get(0).raw.equals("INBOX") && fs.get(0).name.equals("Inbox"), fl.toString());
		has("folders: a modified UTF-7 name decoded", fl.toString(), "Été | ");
		has("folders: a name sent as a literal", fl.toString(), "Literal/1 | ");
		has("folders: [Gmail] cannot be opened", fl.toString(), "[Gmail](no select)");
		check("folders: the server's name kept for commands", fs.stream().anyMatch(f -> f.raw.equals("&AMk-t&AOk-")), fl.toString());

		// 4. a page: counts, newest first, encoded words, unread marked
		Imap.Page p = mail.page(pw, "INBOX", 0, 50);
		check("page counts the inbox and the unread", p.exists == 3 && p.unseen == 2, p.exists + " " + p.unseen);
		check("page lists newest first", p.headers.size() == 3 && p.headers.get(0).seq == 3 && p.headers.get(2).seq == 1, null);
		if (p.headers.size() == 3) {
			Imap.Header n = p.headers.get(0), o = p.headers.get(2);
			check("page decodes an ISO-8859-1 Q subject", n.subject.equals("Grüße aus Berlin"), n.subject);
			check("page decodes a folded UTF-8 B subject", o.subject.equals("Café réunion"), o.subject);
			check("page decodes an encoded From name", o.from.equals("Café Club <club@example.test>"), o.from);
			check("page carries UIDs and the unread flag", n.uid == 103 && !n.seen && o.uid == 101 && o.seen, n.uid + " " + n.seen + " " + o.uid + " " + o.seen);
		}
		Imap.Page p2 = mail.page(pw, "INBOX", 1, 1);
		check("page skips back from the newest", p2.headers.size() == 1 && p2.headers.get(0).seq == 2, null);
		check("an empty folder gives an empty page", mail.page(pw, "&AMk-t&AOk-", 0, 20).headers.isEmpty(), null);

		// 5. reading: multipart prefers text/plain, QP soft break, attachment skipped; HTML-only reduced to text, its charset
		Mail.Letter l = mail.open(pw, "INBOX", 102);
		has("read decodes quoted-printable UTF-8", l.text, "Your café order is on its way, and this line is long enough to be wrapped by a soft break.");
		hasnt("read prefers text/plain over HTML", l.text, "HTML VERSION");
		hasnt("read skips the attachment", l.text, "ATTACHMENT");
		Mail.Letter h = mail.open(pw, "INBOX", 103);
		check("read reduces ISO-8859-1 HTML to text", h.text.contains("Fish & chips") && h.text.contains("à bientôt") && !h.text.contains("p{x}"), h.text);
		check("read gives the subject", h.subject.equals("Grüße aus Berlin"), h.subject);
		String lg = log();
		has("bodies were fetched with PEEK", lg, "imap UID FETCH 102 PEEK");
		hasnt("nothing was ever marked read", lg, "marks read");
		hasnt("nothing was ever selected (EXAMINE only)", lg, "imap SELECT");
		check("unread count by STATUS", mail.inboxUnseen(pw) == 2, null);

		// 6. a hostile name and a hostile server
		Imap im = Imap.open(pw, tls);
		im.signin(pw, sec, oauth);
		refused("a folder name with CR LF is refused before it is sent", "line break", () -> im.examine("INBOX\r\nX1 SELECT INBOX"));
		refused("a folder name with LF alone is refused", "line break", () -> im.page("INBOX\nX2 SELECT INBOX", 0, 5));
		check("the connection still works after a refusal", im.examine("INBOX") == 3, im.status);
		im.close();
		hasnt("no smuggled SELECT reached the server", log(), "imap SELECT");
		long t0 = System.nanoTime();
		refused("a 4 GiB literal is refused before it is read", "too large to read", () -> mail.open(pw, "INBOX", 666));
		check("the refusal is at once, nothing waited for", System.nanoTime() - t0 < 5_000_000_000L, (System.nanoTime() - t0) / 1_000_000 + " ms");
		refused("a missing UID says so", "no message with UID 999", () -> mail.open(pw, "INBOX", 999));

		// 7. Google: XOAUTH2 from the refresh token; a revoked token reported in Google's words
		check("XOAUTH2 sign-in from the refresh token", mail.page(oa, "INBOX", 0, 5).exists == 3, null);
		has("the stand-in saw XOAUTH2 for the right mailbox", log(), "imap XOAUTH2 oauth@example.test");
		Account rv = Account.preset("google", "rv", "oauth@example.test", true);
		rv.imapHost = rv.smtpHost = "localhost";
		rv.imapPort = pi;
		rv.smtpPort = ps;
		sec.m.put("rv/refresh_token", "rt-revoked");
		sec.m.put("rv/client_id", "cid");
		sec.m.put("rv/client_secret", "csec");
		refused("a revoked refresh token is reported as Google says it", "invalid_grant", () -> mail.folders(rv));

		// 8. sending: PLAIN and XOAUTH2, the message as built, dot-stuffing, header injection, bad recipients
		int before = sentCount();
		mail.send(pw, "to@example.test", "Résumé\nBcc: evil@example.test", "Hello,\n.leading dot line\nCafé.\n", null);
		check("send with AUTH PLAIN", sentCount() == before + 1, null);
		has("the stand-in saw AUTH PLAIN for the right mailbox", log(), "smtp PLAIN pw@example.test");
		String e = sentCount() > before ? sent(before + 1) : "";
		has("sent: one recipient, the one asked", e, "X-Standin-Rcpt: <to@example.test>\r\n");
		has("sent: From carries the quoted display name", e, "From: \"Pat \\\"the\\\" Tester\" <pw@example.test>");
		has("sent: non-ASCII subject is RFC 2047 encoded", e, "Subject: =?UTF-8?B?");
		check("sent: a newline in the subject cannot add a header", !e.matches("(?s).*\\r\\nBcc:.*"), e);
		has("sent: Message-ID at the sender's domain", e, "@example.test>\r\n");
		int bs = e.indexOf("\r\n\r\n");
		String body = bs < 0 ? "" : new String(Base64.getMimeDecoder().decode(e.substring(bs + 4)), StandardCharsets.UTF_8);
		check("sent: the body arrives intact (dot-stuffed on the wire)", body.equals("Hello,\n.leading dot line\nCafé.\n"), body);
		String m = Msg.build("", "pw@example.test", "to@example.test", "S", "a\n.\nb", "<x@y>", "<orig@example.test>", ZonedDateTime.now());
		has("a reply carries In-Reply-To", m, "\r\nIn-Reply-To: <orig@example.test>\r\nReferences: <orig@example.test>\r\n");
		mail.send(oa, "to@example.test", "Plain subject", "Via OAuth\n", "<orig@example.test>");
		has("send with XOAUTH2", log(), "smtp XOAUTH2 oauth@example.test");
		int n2 = sentCount();
		refused("a recipient with a line break is refused, nothing sent", "not one plain mail address", () -> mail.send(pw, "a@b.cc>\r\nRCPT TO:<x@y.zz", "S", "x", null));
		refused("two recipients are refused", "not one plain mail address", () -> mail.send(pw, "a@b.cc, c@d.ee", "S", "x", null));
		check("nothing was sent for the refused recipients", sentCount() == n2, null);

		// 9. TLS: an unknown certificate, and a certificate for another name, are refused
		Account other = Account.preset("ionos", "pw", "pw@example.test", false);
		other.imapHost = "localhost";
		other.imapPort = pi;
		refused("TLS: an unknown certificate is refused", "localhost:" + pi, () -> Imap.open(other, Tls.system()));
		other.imapHost = "127.0.0.1";
		refused("TLS: a certificate for another name is refused", "127.0.0.1:" + pi, () -> Imap.open(other, tls));

		System.out.println(pass + " passed, " + fail + " failed");
		System.exit(fail == 0 ? 0 : 1);
	}
}
