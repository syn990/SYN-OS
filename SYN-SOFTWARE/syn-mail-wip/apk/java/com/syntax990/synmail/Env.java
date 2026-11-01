/* ------------------------------------------------------------------------
 *   One Mail for the app: the phone's own trusted certificates and Google's
 *   token address, except in the stand-in build (build.sh --standin, its own
 *   package), which carries the test stand-in's certificate and its token
 *   address in assets and trusts nothing else. The release build has no
 *   such assets, so it can never trust a test certificate.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL-APK
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
package com.syntax990.synmail;

import android.content.Context;

import com.syntax990.synmail.core.Mail;
import com.syntax990.synmail.core.OAuth;
import com.syntax990.synmail.core.Tls;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;

import javax.net.ssl.SSLSocketFactory;

final class Env {
	private static Mail mail;
	private static Store store;

	private Env() {
	}

	static synchronized Store store(Context c) {
		if (store == null) store = new Store(c.getApplicationContext());
		return store;
	}

	/** true in the stand-in build */
	static boolean standin(Context c) {
		try {
			String[] names = c.getAssets().list("");
			return names != null && java.util.Arrays.asList(names).contains("standin-ca.pem");
		} catch (IOException e) {
			return false;
		}
	}

	static String asset(Context c, String name) throws IOException {
		try (BufferedReader r = new BufferedReader(new InputStreamReader(c.getAssets().open(name), StandardCharsets.UTF_8))) {
			StringBuilder b = new StringBuilder();
			for (String l; (l = r.readLine()) != null;) b.append(l).append('\n');
			return b.toString();
		}
	}

	static synchronized Mail mail(Context c) throws IOException {
		if (mail != null) return mail;
		SSLSocketFactory tls;
		String token = null;
		if (standin(c)) {
			try (InputStream in = c.getAssets().open("standin-ca.pem")) {
				tls = Tls.trusting(in);
			}
			token = asset(c, "standin-token-url").trim();
		} else {
			tls = Tls.system();
		}
		mail = new Mail(store(c), new OAuth(token, tls), tls);
		return mail;
	}
}
