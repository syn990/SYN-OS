/* ------------------------------------------------------------------------
 *   Google sign-in on the phone: the refresh token carried from the PC
 *   (syn-mail export, Import from PC) is traded for an access token at
 *   Google's token endpoint, kept in memory only until it expires, and put
 *   in XOAUTH2 form for IMAP and SMTP. The phone never runs the browser
 *   login and no secret is in the APK: the client id and secret came with
 *   the export and live in the phone's store.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL-APK
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
package com.syntax990.synmail.core;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.URL;
import java.net.URLEncoder;
import java.nio.charset.StandardCharsets;
import java.util.Base64;
import java.util.HashMap;
import java.util.Map;

import javax.net.ssl.HttpsURLConnection;
import javax.net.ssl.SSLSocketFactory;

public final class OAuth {
	public static final String TOKEN_URL = "https://oauth2.googleapis.com/token";
	private static final int REPLY_MAX = 64 << 10;

	private final String url;
	private final SSLSocketFactory tls;
	private final Map<String, String> tok = new HashMap<>();
	private final Map<String, Long> until = new HashMap<>();

	/** url null for Google's; tls null for the system's trust */
	public OAuth(String url, SSLSocketFactory tls) {
		this.url = url == null || url.isEmpty() ? TOKEN_URL : url;
		this.tls = tls;
	}

	private static String enc(String s) throws IOException {
		return URLEncoder.encode(s, "UTF-8");
	}

	/** an access token for the account: the cached one if it has a minute left, else a fresh one */
	public synchronized String access(Account a, Secrets s) throws IOException {
		Long u = until.get(a.name);
		if (u != null && u > System.currentTimeMillis() + 60000) return tok.get(a.name);
		String id = s.get(a.name, "client_id"), sec = s.get(a.name, "client_secret"), rt = s.get(a.name, "refresh_token");
		if (id == null || sec == null) throw new MailException("no Google client stored for " + a.name + ": import the account from the PC again");
		if (rt == null) throw new MailException("no Google sign-in stored for " + a.name + ": import the account from the PC again");
		String body = "client_id=" + enc(id) + "&client_secret=" + enc(sec) + "&refresh_token=" + enc(rt) + "&grant_type=refresh_token";
		Map<String, Object> j = post(body);
		Object at = j.get("access_token");
		if (!(at instanceof String) || ((String) at).isEmpty()) throw new MailException("the token endpoint gave no access token");
		long exp = Json.num(j, "expires_in");
		if (exp <= 0 || exp > 86400) exp = 3000;
		tok.put(a.name, (String) at);
		until.put(a.name, System.currentTimeMillis() + exp * 1000);
		return (String) at;
	}

	/** forgets a cached token (after the server refused it) */
	public synchronized void forget(Account a) {
		tok.remove(a.name);
		until.remove(a.name);
	}

	private Map<String, Object> post(String body) throws IOException {
		URL u = new URL(url);
		if (!"https".equals(u.getProtocol())) throw new MailException("the token endpoint must be https");
		HttpsURLConnection h = (HttpsURLConnection) u.openConnection();
		try {
			if (tls != null) h.setSSLSocketFactory(tls);
			h.setConnectTimeout(Net.TIMEOUT_MS);
			h.setReadTimeout(Net.TIMEOUT_MS);
			h.setRequestMethod("POST");
			h.setDoOutput(true);
			h.setInstanceFollowRedirects(false);
			h.setRequestProperty("Content-Type", "application/x-www-form-urlencoded");
			byte[] b = body.getBytes(StandardCharsets.UTF_8);
			h.setFixedLengthStreamingMode(b.length);
			try (OutputStream o = h.getOutputStream()) {
				o.write(b);
			}
			int code = h.getResponseCode();
			InputStream in = code >= 400 ? h.getErrorStream() : h.getInputStream();
			String text = in == null ? "" : read(in);
			Map<String, Object> j;
			try {
				j = Json.object(text);
			} catch (IllegalArgumentException e) {
				j = null;
			}
			if (code != 200 || j == null) {
				String err = j == null ? "no JSON" : Json.str(j, "error");
				String d = j == null ? "" : Json.str(j, "error_description");
				throw new MailException("token endpoint answered " + code + ": " + err + (d.isEmpty() ? "" : ": " + d));
			}
			return j;
		} catch (MailException e) {
			throw e;
		} catch (IOException e) {
			throw new MailException("token endpoint: " + Net.why(e));
		} finally {
			h.disconnect();
		}
	}

	private static String read(InputStream in) throws IOException {
		try (InputStream i = in) {
			ByteArrayOutputStream b = new ByteArrayOutputStream();
			byte[] t = new byte[4096];
			int r;
			while ((r = i.read(t)) > 0) {
				if (b.size() + r > REPLY_MAX) throw new MailException("the token endpoint sent too much");
				b.write(t, 0, r);
			}
			return new String(b.toByteArray(), StandardCharsets.UTF_8);
		}
	}

	/** the SASL XOAUTH2 initial response (base64 of user=..^Aauth=Bearer ..^A^A) */
	public static String xoauth2(String user, String token) {
		String raw = "user=" + user + "\u0001auth=Bearer " + token + "\u0001\u0001";
		return Base64.getEncoder().encodeToString(raw.getBytes(StandardCharsets.UTF_8));
	}
}
