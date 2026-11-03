/* ------------------------------------------------------------------------
 *   The reader's page, as the desktop's html.c makes it (G265): the
 *   message's HTML with the page's own guard first (a byte order mark, a
 *   Content-Security-Policy that never allows a script, a frame, a form
 *   post or a base change, and remote pictures, styles and fonts only when
 *   asked for), every cid: reference replaced by its part's bytes as a
 *   data: address, and every http-equiv the sender wrote renamed
 *   x-http-equiv-off whatever its value, so no refresh can move the page.
 *   Remote references are counted, so the reader can say what it held back.
 *   The phone's web view adds its own locks: no JavaScript, no file or
 *   content access, network loads blocked unless allowed for this message.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL-APK
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
package com.syntax990.synmail.core;

import java.util.Base64;
import java.util.Locale;

public final class HtmlPage {
	/** the same policies as html.c, with data: for the inline parts */
	static final String CSP_BLOCKED = "default-src 'none'; img-src data:; style-src 'unsafe-inline'; font-src data:; "
		+ "base-uri 'none'; form-action 'none'";
	static final String CSP_REMOTE = "default-src 'none'; img-src data: http: https:; style-src 'unsafe-inline' http: https:; "
		+ "font-src data: http: https:; base-uri 'none'; form-action 'none'";

	/** the page and how many remote references it has */
	public static final class Made {
		public final String page;
		public final int remoteRefs;

		Made(String page, int remoteRefs) {
			this.page = page;
			this.remoteRefs = remoteRefs;
		}
	}

	private HtmlPage() {
	}

	private static boolean at(String s, int i, String w) {
		return s.regionMatches(true, i, w, 0, w.length());
	}

	private static boolean space(char c) {
		return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == 0x0b;
	}

	private static boolean alnum(char c) {
		return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
	}

	/** does s[i..] (after spaces and quotes) start a remote address? */
	static boolean remoteAt(String s, int i) {
		while (i < s.length() && (space(s.charAt(i)) || s.charAt(i) == '"' || s.charAt(i) == '\'')) i++;
		return at(s, i, "http:") || at(s, i, "https:") || at(s, i, "//");
	}

	/** does the attribute whose name ends at s[i] have a remote value? Only when an = follows (after spaces);
	 *  any: anywhere in the value (a srcset's candidates), else at its start */
	static boolean attrRemote(String s, int i, boolean any) {
		int n = s.length();
		while (i < n && space(s.charAt(i))) i++;
		if (i >= n || s.charAt(i) != '=') return false;
		i++;
		while (i < n && space(s.charAt(i))) i++;
		if (!any) return remoteAt(s, i);
		char q = i < n && (s.charAt(i) == '"' || s.charAt(i) == '\'') ? s.charAt(i++) : 0;
		for (; i < n && (q != 0 ? s.charAt(i) != q : !space(s.charAt(i)) && s.charAt(i) != '>'); i++) {
			if ((i == 0 || !alnum(s.charAt(i - 1))) && remoteAt(s, i)) return true;
		}
		return false;
	}

	private static int hexv(char c) {
		if (c >= '0' && c <= '9') return c - '0';
		c = Character.toLowerCase(c);
		if (c >= 'a' && c <= 'f') return c - 'a' + 10;
		return -1;
	}

	private static String dataUrl(Mime.Part p) {
		String type = p.type.isEmpty() || !p.type.matches("[a-z0-9.+-]+/[a-z0-9.+-]+") ? "application/octet-stream" : p.type;
		return "data:" + type + ";base64," + Base64.getEncoder().encodeToString(p.data);
	}

	private static int spanOf(String s, int i, String set) {
		int k = i;
		while (k < s.length() && set.indexOf(s.charAt(k)) >= 0) k++;
		return k - i;
	}

	/** the page for the web view; remote: the reader allowed remote pictures for this message */
	public static Made make(Mime.Html h, boolean remote) {
		String s = h.html == null ? "" : h.html;
		int n = s.length(), i = 0, refs = 0;
		String tag = "";
		StringBuilder out = new StringBuilder(n + 512);
		// a byte order mark wins over any charset the page declares: the text is decoded by now
		out.append('﻿');
		if (n > 0 && s.charAt(0) == '﻿') i = 1;
		int j = i;
		while (j < n && space(s.charAt(j))) j++;
		if (at(s, j, "<!doctype")) {
			int gt = s.indexOf('>', j);
			if (gt >= 0) {
				out.append(s, i, gt + 1);
				i = gt + 1;
			}
		}
		out.append("<meta charset=\"utf-8\"><meta http-equiv=\"Content-Security-Policy\" content=\"")
			.append(remote ? CSP_REMOTE : CSP_BLOCKED)
			.append("\"><meta http-equiv=\"x-dns-prefetch-control\" content=\"off\"><meta name=\"referrer\" content=\"no-referrer\">");
		while (i < n) {
			if (at(s, i, "cid:")) {
				// cid:ID, percent-encoded (RFC 2392), ends at a quote, a bracket or a space
				int e = i + 4;
				while (e < n && "\"'() \t\r\n<>".indexOf(s.charAt(e)) < 0) e++;
				StringBuilder id = new StringBuilder();
				for (int k = i + 4; k < e; k++) {
					char c = s.charAt(k);
					if (c == '%' && k + 2 < e && hexv(s.charAt(k + 1)) >= 0 && hexv(s.charAt(k + 2)) >= 0) {
						id.append((char) (hexv(s.charAt(k + 1)) * 16 + hexv(s.charAt(k + 2))));
						k += 2;
					} else {
						id.append(c);
					}
				}
				Mime.Part hit = null;
				for (Mime.Part p : h.parts) {
					if (p.cid.equals(id.toString())) {
						hit = p;
						break;
					}
				}
				for (int k = 0; hit == null && k < h.parts.size(); k++) {
					if (h.parts.get(k).cid.equalsIgnoreCase(id.toString())) hit = h.parts.get(k);
				}
				if (hit != null) {
					out.append(dataUrl(hit));
					i = e;
					continue;
				}
			} else if (at(s, i, "http-equiv")) {
				// every http-equiv of the sender's is renamed, whatever its value (entity-encoded, spaced, any case):
				// the page's own guard metas are written above, before the sender's text, and are the only ones left
				out.append("x-http-equiv-off");
				i += 10;
				continue;
			}
			char c = s.charAt(i);
			if (c == '<') {
				int k = i + 1;
				StringBuilder t = new StringBuilder();
				while (k < n && t.length() < 15 && (alnum(s.charAt(k)) || s.charAt(k) == '-')) t.append(Character.toLowerCase(s.charAt(k++)));
				tag = t.toString();
			} else if (c == '>') {
				tag = "";
			}
			boolean edge = i == 0 || !alnum(s.charAt(i - 1));
			if (edge && ((at(s, i, "href") && tag.equals("link") && attrRemote(s, i + 4, false))
				|| ((at(s, i, "srcset") || at(s, i, "imagesrcset")) && attrRemote(s, i + (at(s, i, "srcset") ? 6 : 11), true))
				|| (at(s, i, "poster") && attrRemote(s, i + 6, false)))) {
				refs++;
			} else if (edge && ((at(s, i, "src") && i + 3 < n && (s.charAt(i + 3) == '=' || space(s.charAt(i + 3)))
				&& remoteAt(s, i + 3 + spanOf(s, i + 3, " \t=")))
				|| (at(s, i, "background=") && remoteAt(s, i + 11)) || (at(s, i, "url(") && remoteAt(s, i + 4))
				|| (at(s, i, "@import") && remoteAt(s, i + 7 + spanOf(s, i + 7, " \t(url"))))) {
				refs++;
			}
			out.append(c);
			i++;
		}
		return new Made(out.toString(), refs);
	}

	/** "1 remote picture" or "N remote pictures" */
	public static String heldBack(int refs) {
		return String.format(Locale.ROOT, "%d remote %s held back", refs, refs == 1 ? "picture" : "pictures");
	}
}
