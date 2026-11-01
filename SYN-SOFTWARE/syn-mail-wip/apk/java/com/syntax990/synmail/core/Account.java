/* ------------------------------------------------------------------------
 *   One mailbox: where it lives and how it signs in. The same fields as the
 *   desktop's ~/.config/syn-mail/config.json, and the file `syn-mail export`
 *   writes on the PC (the account plus its secrets) for Import from PC.
 *   No secret is ever kept in an Account: they go to a Secrets store.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL-APK
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
package com.syntax990.synmail.core;

import java.util.LinkedHashMap;
import java.util.Map;

public final class Account {
	public String name = "", address = "", display = "", imapHost = "", smtpHost = "";
	public int imapPort = 993, smtpPort = 465;
	/** true: Google sign-in (XOAUTH2 from a refresh token); false: a password (an app password for Google) */
	public boolean oauth;

	/** the presets the desktop knows: google, ionos; null if unknown */
	public static Account preset(String preset, String name, String address, boolean oauth) {
		Account a = new Account();
		a.name = name;
		a.address = address;
		switch (preset) {
		case "google":
			a.imapHost = "imap.gmail.com";
			a.smtpHost = "smtp.gmail.com";
			a.oauth = oauth;
			break;
		case "ionos":
			a.imapHost = "imap.ionos.co.uk";
			a.smtpHost = "smtp.ionos.co.uk";
			break;
		default:
			return null;
		}
		return a;
	}

	/** null when it will do, else what is wrong, in words */
	public String problem() {
		if (!name.matches("[A-Za-z0-9_.-]{1,40}")) return "the account name may hold letters, digits, dot, dash and underscore";
		if (!Msg.addrOk(address)) return "that is not a mail address";
		if (display.indexOf('\r') >= 0 || display.indexOf('\n') >= 0 || display.length() > 120) return "the display name must be one short line";
		if (!hostOk(imapHost) || !hostOk(smtpHost)) return "a server name is not valid";
		if (imapPort < 1 || imapPort > 65535 || smtpPort < 1 || smtpPort > 65535) return "a port is out of range";
		return null;
	}

	private static boolean hostOk(String h) {
		return h != null && h.matches("[A-Za-z0-9.-]{1,253}");
	}

	public Map<String, Object> toJson() {
		Map<String, Object> o = new LinkedHashMap<>();
		o.put("name", name);
		o.put("address", address);
		o.put("display_name", display);
		o.put("imap_host", imapHost);
		o.put("imap_port", (long) imapPort);
		o.put("smtp_host", smtpHost);
		o.put("smtp_port", (long) smtpPort);
		o.put("auth", oauth ? "oauth" : "password");
		return o;
	}

	public static Account fromJson(Map<String, Object> o) {
		Account a = new Account();
		a.name = Json.str(o, "name");
		a.address = Json.str(o, "address");
		a.display = Json.str(o, "display_name");
		a.imapHost = Json.str(o, "imap_host");
		a.smtpHost = Json.str(o, "smtp_host");
		a.imapPort = (int) Json.num(o, "imap_port");
		a.smtpPort = (int) Json.num(o, "smtp_port");
		a.oauth = "oauth".equals(Json.str(o, "auth"));
		return a;
	}

	/** An exported account file, read: the account, and its secrets put in the store. */
	public static final class Import {
		public final Account account;
		public final Map<String, String> secrets;

		Import(Account a, Map<String, String> s) {
			account = a;
			secrets = s;
		}
	}

	/** reads the file `syn-mail export NAME FILE` writes; IllegalArgumentException with the reason if it will not do */
	public static Import fromExport(String text) {
		Map<String, Object> o = Json.object(text);
		if (Json.num(o, "syn_mail_account") != 1) throw new IllegalArgumentException("not a SYN-Mail account file");
		Account a = fromJson(o);
		String why = a.problem();
		if (why != null) throw new IllegalArgumentException(why);
		Object so = o.get("secrets");
		if (!(so instanceof Map)) throw new IllegalArgumentException("the file holds no secrets");
		Map<String, String> s = new LinkedHashMap<>();
		String[] kinds = a.oauth ? new String[] {"refresh_token", "client_id", "client_secret"} : new String[] {"password"};
		for (String k : kinds) {
			Object v = ((Map<?, ?>) so).get(k);
			if (!(v instanceof String) || ((String) v).isEmpty()) throw new IllegalArgumentException("the file has no " + k);
			s.put(k, (String) v);
		}
		return new Import(a, s);
	}

	@Override
	public String toString() {
		return name + " <" + address + ">";
	}
}
