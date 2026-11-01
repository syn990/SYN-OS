/* ------------------------------------------------------------------------
 *   IMAP's modified UTF-7 (RFC 3501 5.1.3) for folder names: "&AMk-t&AOk-"
 *   is "Été", "&-" is "&". Decoding only: the app never makes folders.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL-APK
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
package com.syntax990.synmail.core;

import java.util.Base64;

public final class Utf7 {
	private Utf7() {
	}

	/** the name as it reads; a broken run is left as it came */
	public static String decode(String s) {
		StringBuilder b = new StringBuilder(s.length());
		int i = 0;
		while (i < s.length()) {
			char c = s.charAt(i);
			if (c != '&') {
				b.append(c);
				i++;
				continue;
			}
			int e = s.indexOf('-', i + 1);
			if (e < 0) {
				b.append(s, i, s.length());
				break;
			}
			if (e == i + 1) {
				b.append('&');
				i = e + 1;
				continue;
			}
			String run = s.substring(i + 1, e).replace(',', '/');
			try {
				byte[] u = Base64.getDecoder().decode(pad(run));
				if (u.length % 2 != 0) throw new IllegalArgumentException();
				StringBuilder t = new StringBuilder();
				for (int k = 0; k < u.length; k += 2) t.append((char) (((u[k] & 0xff) << 8) | (u[k + 1] & 0xff)));
				b.append(t);
			} catch (IllegalArgumentException x) {
				b.append(s, i, e + 1);
			}
			i = e + 1;
		}
		return b.toString();
	}

	private static String pad(String r) {
		StringBuilder b = new StringBuilder(r);
		while (b.length() % 4 != 0) b.append('=');
		return b.toString();
	}
}
