/* ------------------------------------------------------------------------
 *   TLS trust: the system's, or one certificate (the stand-in's, for the
 *   tests and the stand-in build only; the real app trusts the system).
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL-APK
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
package com.syntax990.synmail.core;

import java.io.IOException;
import java.io.InputStream;
import java.security.GeneralSecurityException;
import java.security.KeyStore;
import java.security.cert.Certificate;
import java.security.cert.CertificateFactory;

import javax.net.ssl.SSLContext;
import javax.net.ssl.SSLSocketFactory;
import javax.net.ssl.TrustManagerFactory;

public final class Tls {
	private Tls() {
	}

	public static SSLSocketFactory system() {
		return (SSLSocketFactory) SSLSocketFactory.getDefault();
	}

	/** a factory that trusts only the certificate(s) in the PEM */
	public static SSLSocketFactory trusting(InputStream pem) throws IOException {
		try {
			KeyStore ks = KeyStore.getInstance(KeyStore.getDefaultType());
			ks.load(null, null);
			int i = 0;
			for (Certificate c : CertificateFactory.getInstance("X.509").generateCertificates(pem)) ks.setCertificateEntry("ca" + i++, c);
			if (i == 0) throw new IOException("no certificate in the file");
			TrustManagerFactory tmf = TrustManagerFactory.getInstance(TrustManagerFactory.getDefaultAlgorithm());
			tmf.init(ks);
			SSLContext ctx = SSLContext.getInstance("TLS");
			ctx.init(null, tmf.getTrustManagers(), null);
			return ctx.getSocketFactory();
		} catch (GeneralSecurityException e) {
			throw new IOException("the certificate could not be loaded: " + e.getMessage());
		}
	}
}
