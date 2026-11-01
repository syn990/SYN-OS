/* ------------------------------------------------------------------------
 *   The Channel API auth_token: an RS256 JWT {"iss": issuer, "exp": now+ttl}
 *   signed with the developer's private key (PKCS#8 "PRIVATE KEY" or
 *   PKCS#1 "RSA PRIVATE KEY" PEM).
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CAST-APK
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
package com.syntax990.syncast.core;

import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;
import java.security.GeneralSecurityException;
import java.security.KeyFactory;
import java.security.PrivateKey;
import java.security.Signature;
import java.security.spec.PKCS8EncodedKeySpec;
import java.util.Base64;

public final class Jwt {
	private Jwt() {
	}

	public static PrivateKey parsePem(String pem) throws GeneralSecurityException {
		boolean pkcs1 = pem.contains("BEGIN RSA PRIVATE KEY");
		String body = pem.replaceAll("-----[A-Z ]+-----", "").replaceAll("\\s", "");
		byte[] der;
		try {
			der = Base64.getDecoder().decode(body);
		} catch (IllegalArgumentException e) {
			throw new GeneralSecurityException("the key is not PEM");
		}
		if (pkcs1) der = wrapPkcs1(der);
		return KeyFactory.getInstance("RSA").generatePrivate(new PKCS8EncodedKeySpec(der));
	}

	/** PKCS#1 RSAPrivateKey inside a PKCS#8 PrivateKeyInfo. */
	private static byte[] wrapPkcs1(byte[] pkcs1) {
		byte[] alg = {0x30, 0x0D, 0x06, 0x09, 0x2A, (byte) 0x86, 0x48, (byte) 0x86, (byte) 0xF7, 0x0D, 0x01, 0x01, 0x01,
			0x05, 0x00};
		ByteArrayOutputStream inner = new ByteArrayOutputStream();
		inner.write(new byte[] {0x02, 0x01, 0x00}, 0, 3);
		inner.write(alg, 0, alg.length);
		byte[] oct = tlv(0x04, pkcs1);
		inner.write(oct, 0, oct.length);
		return tlv(0x30, inner.toByteArray());
	}

	private static byte[] tlv(int tag, byte[] v) {
		ByteArrayOutputStream o = new ByteArrayOutputStream();
		o.write(tag);
		int n = v.length;
		if (n < 128) {
			o.write(n);
		} else if (n < 256) {
			o.write(0x81);
			o.write(n);
		} else if (n < 65536) {
			o.write(0x82);
			o.write(n >> 8);
			o.write(n);
		} else {
			o.write(0x83);
			o.write(n >> 16);
			o.write(n >> 8);
			o.write(n);
		}
		o.write(v, 0, n);
		return o.toByteArray();
	}

	private static String b64url(byte[] b) {
		return Base64.getUrlEncoder().withoutPadding().encodeToString(b);
	}

	public static String mint(PrivateKey key, String issuer, int ttlSeconds) throws GeneralSecurityException {
		String head = b64url("{\"alg\":\"RS256\",\"typ\":\"JWT\"}".getBytes(StandardCharsets.UTF_8));
		long exp = System.currentTimeMillis() / 1000 + ttlSeconds;
		String body = b64url(Json.write(Json.obj("iss", issuer, "exp", (double) exp)).getBytes(StandardCharsets.UTF_8));
		Signature sig = Signature.getInstance("SHA256withRSA");
		sig.initSign(key);
		sig.update((head + "." + body).getBytes(StandardCharsets.US_ASCII));
		return head + "." + body + "." + b64url(sig.sign());
	}
}
