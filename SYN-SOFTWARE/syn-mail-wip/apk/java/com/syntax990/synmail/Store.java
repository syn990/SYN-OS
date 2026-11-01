/* ------------------------------------------------------------------------
 *   The phone's accounts and their secrets. Accounts are plain JSON in the
 *   app's own files (accounts.json); each secret (a password, an app
 *   password, a Google refresh token and its client) is sealed with
 *   AES-256-GCM under a key that lives in the Android Keystore and never
 *   leaves it, and only the sealed text is written (secrets.json). Nothing
 *   is backed up (allowBackup false). No secret ships in the APK: they come
 *   from the PC's `syn-mail export` file or are typed on the phone.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL-APK
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
package com.syntax990.synmail;

import android.content.Context;
import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import android.util.Base64;

import com.syntax990.synmail.core.Account;
import com.syntax990.synmail.core.Json;
import com.syntax990.synmail.core.Secrets;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.security.GeneralSecurityException;
import java.security.KeyStore;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;

final class Store implements Secrets {
	private static final String ALIAS = "syn-mail-secrets";
	private final File accounts, secrets;

	Store(Context c) {
		accounts = new File(c.getFilesDir(), "accounts.json");
		secrets = new File(c.getFilesDir(), "secrets.json");
	}

	/* ---------- accounts ---------- */

	synchronized List<Account> accounts() {
		List<Account> out = new ArrayList<>();
		Object v = read(accounts);
		if (v instanceof List) {
			for (Object o : (List<?>) v) {
				if (o instanceof Map) {
					@SuppressWarnings("unchecked")
					Map<String, Object> m = (Map<String, Object>) o;
					out.add(Account.fromJson(m));
				}
			}
		}
		return out;
	}

	synchronized Account account(String name) {
		for (Account a : accounts()) if (a.name.equals(name)) return a;
		return null;
	}

	/** adds the account, or replaces the one of the same name, with its secrets */
	synchronized void save(Account a, Map<String, String> s) throws IOException {
		List<Object> list = new ArrayList<>();
		for (Account o : accounts()) if (!o.name.equals(a.name)) list.add(o.toJson());
		list.add(a.toJson());
		Map<String, Object> sealed = sealedAll();
		sealed.keySet().removeIf(k -> k.startsWith(a.name + "/"));
		for (Map.Entry<String, String> e : s.entrySet()) sealed.put(a.name + "/" + e.getKey(), seal(e.getValue()));
		write(secrets, Json.write(sealed));
		write(accounts, Json.write(list));
	}

	synchronized void remove(String name) throws IOException {
		List<Object> list = new ArrayList<>();
		for (Account o : accounts()) if (!o.name.equals(name)) list.add(o.toJson());
		Map<String, Object> sealed = sealedAll();
		sealed.keySet().removeIf(k -> k.startsWith(name + "/"));
		write(secrets, Json.write(sealed));
		write(accounts, Json.write(list));
	}

	/* ---------- secrets ---------- */

	@Override
	public synchronized String get(String account, String kind) throws IOException {
		Object v = sealedAll().get(account + "/" + kind);
		return v instanceof String ? open((String) v) : null;
	}

	private Map<String, Object> sealedAll() {
		Object v = read(secrets);
		Map<String, Object> m = new LinkedHashMap<>();
		if (v instanceof Map) {
			for (Map.Entry<?, ?> e : ((Map<?, ?>) v).entrySet()) m.put(String.valueOf(e.getKey()), e.getValue());
		}
		return m;
	}

	private static SecretKey key() throws GeneralSecurityException, IOException {
		KeyStore ks = KeyStore.getInstance("AndroidKeyStore");
		ks.load(null);
		KeyStore.Entry e = ks.getEntry(ALIAS, null);
		if (e instanceof KeyStore.SecretKeyEntry) return ((KeyStore.SecretKeyEntry) e).getSecretKey();
		KeyGenerator g = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore");
		g.init(new KeyGenParameterSpec.Builder(ALIAS, KeyProperties.PURPOSE_ENCRYPT | KeyProperties.PURPOSE_DECRYPT)
			.setBlockModes(KeyProperties.BLOCK_MODE_GCM)
			.setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
			.setKeySize(256)
			.build());
		return g.generateKey();
	}

	/** "iv:ciphertext", both base64; the Keystore picks the IV */
	private static String seal(String plain) throws IOException {
		try {
			Cipher c = Cipher.getInstance("AES/GCM/NoPadding");
			c.init(Cipher.ENCRYPT_MODE, key());
			byte[] ct = c.doFinal(plain.getBytes(StandardCharsets.UTF_8));
			return Base64.encodeToString(c.getIV(), Base64.NO_WRAP) + ":" + Base64.encodeToString(ct, Base64.NO_WRAP);
		} catch (GeneralSecurityException e) {
			throw new IOException("the phone's keystore would not seal the secret: " + e.getMessage(), e);
		}
	}

	private static String open(String sealed) throws IOException {
		int c = sealed.indexOf(':');
		if (c < 0) throw new IOException("a stored secret is damaged: add the account again");
		try {
			Cipher ci = Cipher.getInstance("AES/GCM/NoPadding");
			ci.init(Cipher.DECRYPT_MODE, key(), new GCMParameterSpec(128, Base64.decode(sealed.substring(0, c), Base64.NO_WRAP)));
			return new String(ci.doFinal(Base64.decode(sealed.substring(c + 1), Base64.NO_WRAP)), StandardCharsets.UTF_8);
		} catch (GeneralSecurityException | IllegalArgumentException e) {
			throw new IOException("a stored secret could not be opened: add the account again", e);
		}
	}

	/* ---------- files ---------- */

	private static Object read(File f) {
		try {
			return f.exists() ? Json.parse(new String(Files.readAllBytes(f.toPath()), StandardCharsets.UTF_8)) : null;
		} catch (IOException | RuntimeException e) {
			return null;
		}
	}

	/** written whole to a new file, then renamed over the old one */
	private static void write(File f, String text) throws IOException {
		File t = new File(f.getPath() + ".new");
		try (FileOutputStream o = new FileOutputStream(t)) {
			o.write(text.getBytes(StandardCharsets.UTF_8));
			o.getFD().sync();
		}
		if (!t.renameTo(f)) throw new IOException("could not write " + f.getName());
	}
}
