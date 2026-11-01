/* ------------------------------------------------------------------------
 *   A WebSocket client (RFC 6455) over TLS (wss://) or plain TCP (ws://,
 *   for a test server on loopback). connect() blocks; after it, a reader
 *   thread hands each message to the listener, and send() may be called
 *   from any thread (writes are serialised). With a pinned certificate
 *   (PEM text) that certificate alone is trusted and the host name is not
 *   checked against it; without one the system store and the name decide.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CAST-APK
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
package com.syntax990.syncast.core;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.EOFException;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.net.URI;
import java.nio.charset.StandardCharsets;
import java.security.KeyStore;
import java.security.MessageDigest;
import java.security.SecureRandom;
import java.security.cert.Certificate;
import java.security.cert.CertificateFactory;
import java.util.Base64;

import javax.net.ssl.HostnameVerifier;
import javax.net.ssl.HttpsURLConnection;
import javax.net.ssl.SSLContext;
import javax.net.ssl.SSLParameters;
import javax.net.ssl.SSLSocket;
import javax.net.ssl.SSLSocketFactory;
import javax.net.ssl.TrustManagerFactory;

public final class Ws {
	public static final int TEXT = 1, BINARY = 2, CLOSE = 8, PING = 9, PONG = 10;
	private static final int MAX_MESSAGE = 32 << 20;
	private static final SecureRandom RNG = new SecureRandom();

	public interface Listener {
		void onText(Ws ws, String text);
		void onBinary(Ws ws, byte[] data);
		/** Called once, from the reader thread, when the connection ends. */
		void onClosed(Ws ws, String why);
	}

	private final Socket sock;
	private final InputStream in;
	private final OutputStream out;
	private volatile boolean closed;
	private volatile int closeCode;
	private volatile long lastRx = System.currentTimeMillis();
	private Thread reader;

	private Ws(Socket sock, InputStream in, OutputStream out) {
		this.sock = sock;
		this.in = in;
		this.out = out;
	}

	public static Ws connect(String url, int timeoutMs) throws IOException {
		return connect(url, "", timeoutMs);
	}

	/** A socket factory trusting only the pinned certificate(s) in pem. */
	private static SSLSocketFactory pinned(String pem) throws IOException {
		try {
			CertificateFactory cf = CertificateFactory.getInstance("X.509");
			KeyStore ks = KeyStore.getInstance(KeyStore.getDefaultType());
			ks.load(null, null);
			int n = 0;
			for (Certificate c : cf.generateCertificates(new ByteArrayInputStream(pem.getBytes(StandardCharsets.US_ASCII)))) {
				ks.setCertificateEntry("pin" + n++, c);
			}
			if (n == 0) throw new IOException("no certificate in the pinned PEM");
			TrustManagerFactory tmf = TrustManagerFactory.getInstance(TrustManagerFactory.getDefaultAlgorithm());
			tmf.init(ks);
			SSLContext ctx = SSLContext.getInstance("TLS");
			ctx.init(null, tmf.getTrustManagers(), null);
			return ctx.getSocketFactory();
		} catch (java.security.GeneralSecurityException e) {
			throw new IOException("pinned certificate: " + e.getMessage(), e);
		}
	}

	public static Ws connect(String url, String certPem, int timeoutMs) throws IOException {
		URI u = URI.create(url);
		boolean tls = "wss".equals(u.getScheme());
		if (!tls && !"ws".equals(u.getScheme())) throw new IOException("not a ws:// or wss:// URL: " + url);
		String host = u.getHost();
		int port = u.getPort() > 0 ? u.getPort() : tls ? 443 : 80;
		String path = u.getRawPath() == null || u.getRawPath().isEmpty() ? "/" : u.getRawPath();
		if (u.getRawQuery() != null) path += "?" + u.getRawQuery();

		Socket raw = new Socket();
		raw.connect(new InetSocketAddress(host, port), timeoutMs);
		raw.setTcpNoDelay(true);
		raw.setSoTimeout(timeoutMs);
		Socket s = raw;
		if (tls) {
			boolean pin = certPem != null && !certPem.trim().isEmpty();
			SSLSocketFactory sf = pin ? pinned(certPem) : (SSLSocketFactory) SSLSocketFactory.getDefault();
			SSLSocket ssl = (SSLSocket) sf.createSocket(raw, host, port, true);
			SSLParameters p = ssl.getSSLParameters();
			if (!pin) p.setEndpointIdentificationAlgorithm("HTTPS");
			ssl.setSSLParameters(p);
			ssl.startHandshake();
			if (!pin) {
				HostnameVerifier hv = HttpsURLConnection.getDefaultHostnameVerifier();
				if (!hv.verify(host, ssl.getSession())) {
					ssl.close();
					throw new IOException("TLS certificate does not match " + host);
				}
			}
			s = ssl;
		}
		InputStream in = s.getInputStream();
		OutputStream out = s.getOutputStream();

		byte[] keyRaw = new byte[16];
		RNG.nextBytes(keyRaw);
		String key = Base64.getEncoder().encodeToString(keyRaw);
		String req = "GET " + path + " HTTP/1.1\r\nHost: " + host + "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
			+ "Sec-WebSocket-Key: " + key + "\r\nSec-WebSocket-Version: 13\r\nUser-Agent: syn-cast-apk\r\n\r\n";
		out.write(req.getBytes(StandardCharsets.US_ASCII));
		out.flush();

		/* the response headers, byte by byte so no frame bytes are eaten */
		ByteArrayOutputStream hdr = new ByteArrayOutputStream();
		int matched = 0;
		while (matched < 4) {
			int c = in.read();
			if (c < 0) throw new IOException("connection closed during the upgrade");
			hdr.write(c);
			matched = (c == "\r\n\r\n".charAt(matched)) ? matched + 1 : (c == '\r' ? 1 : 0);
			if (hdr.size() > 16384) throw new IOException("upgrade response too long");
		}
		String head = hdr.toString("ISO-8859-1");
		if (!head.startsWith("HTTP/1.1 101")) {
			s.close();
			throw new IOException("upgrade refused: " + head.split("\r\n")[0]);
		}
		String want;
		try {
			MessageDigest sha = MessageDigest.getInstance("SHA-1");
			want = Base64.getEncoder().encodeToString(
				sha.digest((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").getBytes(StandardCharsets.US_ASCII)));
		} catch (Exception e) {
			throw new IOException(e);
		}
		boolean ok = false;
		for (String line : head.split("\r\n")) {
			int colon = line.indexOf(':');
			if (colon > 0 && line.substring(0, colon).trim().equalsIgnoreCase("Sec-WebSocket-Accept")) {
				ok = line.substring(colon + 1).trim().equals(want);
			}
		}
		if (!ok) {
			s.close();
			throw new IOException("bad Sec-WebSocket-Accept from " + host);
		}
		s.setSoTimeout(0);
		return new Ws(s, in, out);
	}

	public void start(Listener l) {
		reader = new Thread(() -> readLoop(l), "ws-reader");
		reader.setDaemon(true);
		reader.start();
	}

	private void readFully(byte[] b, int off, int n) throws IOException {
		while (n > 0) {
			int r = in.read(b, off, n);
			if (r < 0) throw new EOFException("connection closed");
			off += r;
			n -= r;
		}
	}

	private void readLoop(Listener l) {
		String why = "connection closed";
		ByteArrayOutputStream frag = null;
		int fragOp = 0;
		byte[] h = new byte[14];
		try {
			while (!closed) {
				readFully(h, 0, 2);
				lastRx = System.currentTimeMillis(); /* pings count: a quiet channel is not a dead link */
				boolean fin = (h[0] & 0x80) != 0;
				int op = h[0] & 0x0F;
				boolean masked = (h[1] & 0x80) != 0;
				long len = h[1] & 0x7F;
				if (len == 126) {
					readFully(h, 2, 2);
					len = ((h[2] & 0xFF) << 8) | (h[3] & 0xFF);
				} else if (len == 127) {
					readFully(h, 2, 8);
					len = 0;
					for (int k = 0; k < 8; k++) len = (len << 8) | (h[2 + k] & 0xFF);
				}
				if (len > MAX_MESSAGE) throw new IOException("frame too large");
				byte[] mask = null;
				if (masked) {
					mask = new byte[4];
					readFully(mask, 0, 4);
				}
				byte[] data = new byte[(int) len];
				readFully(data, 0, data.length);
				if (mask != null) {
					for (int k = 0; k < data.length; k++) data[k] ^= mask[k & 3];
				}
				switch (op) {
				case PING:
					send(PONG, data, null);
					break;
				case PONG:
					break;
				case CLOSE: {
					closeCode = data.length >= 2 ? ((data[0] & 0xFF) << 8) | (data[1] & 0xFF) : 0;
					send(CLOSE, data.length >= 2 ? new byte[] {data[0], data[1]} : new byte[0], null);
					why = "closed by the server";
					return;
				}
				case 0:
					if (frag == null) throw new IOException("stray continuation frame");
					frag.write(data);
					if (frag.size() > MAX_MESSAGE) throw new IOException("message too large");
					if (fin) {
						deliver(l, fragOp, frag.toByteArray());
						frag = null;
					}
					break;
				case TEXT:
				case BINARY:
					if (fin) {
						deliver(l, op, data);
					} else {
						frag = new ByteArrayOutputStream();
						frag.write(data);
						fragOp = op;
					}
					break;
				default:
					throw new IOException("unknown opcode " + op);
				}
			}
		} catch (IOException e) {
			if (!closed) why = e.getMessage() != null ? e.getMessage() : e.toString();
		} finally {
			closeQuietly();
			l.onClosed(this, why);
		}
	}

	private void deliver(Listener l, int op, byte[] data) {
		if (op == TEXT) l.onText(this, new String(data, StandardCharsets.UTF_8));
		else l.onBinary(this, data);
	}

	/** Sends one frame whose payload is a||b. Returns false once the connection is gone. */
	public boolean send(int op, byte[] a, byte[] b) {
		int an = a == null ? 0 : a.length, bn = b == null ? 0 : b.length;
		int len = an + bn;
		int hn = len < 126 ? 2 : len < 65536 ? 4 : 10;
		byte[] f = new byte[hn + 4 + len];
		f[0] = (byte) (0x80 | op);
		if (len < 126) {
			f[1] = (byte) (0x80 | len);
		} else if (len < 65536) {
			f[1] = (byte) (0x80 | 126);
			f[2] = (byte) (len >> 8);
			f[3] = (byte) len;
		} else {
			f[1] = (byte) (0x80 | 127);
			for (int k = 0; k < 8; k++) f[2 + k] = (byte) ((long) len >> (8 * (7 - k)));
		}
		byte[] mask = new byte[4];
		RNG.nextBytes(mask);
		System.arraycopy(mask, 0, f, hn, 4);
		int p = hn + 4;
		if (an > 0) System.arraycopy(a, 0, f, p, an);
		if (bn > 0) System.arraycopy(b, 0, f, p + an, bn);
		for (int k = 0; k < len; k++) f[p + k] ^= mask[k & 3];
		synchronized (out) {
			if (closed) return false;
			try {
				out.write(f);
				out.flush();
				return true;
			} catch (IOException e) {
				closeQuietly();
				return false;
			}
		}
	}

	public boolean sendText(String s) {
		return send(TEXT, s.getBytes(StandardCharsets.UTF_8), null);
	}

	public long lastRx() {
		return lastRx;
	}

	/** The peer's close frame code (4003 replaced, 4001 refused, 4004 revoked), 0 when there was none. */
	public int closeCode() {
		return closeCode;
	}

	public boolean isClosed() {
		return closed;
	}

	private void closeQuietly() {
		closed = true;
		try {
			sock.close();
		} catch (IOException ignored) {
		}
	}

	public void close() {
		if (!closed) {
			send(CLOSE, new byte[] {0x03, (byte) 0xE8}, null);
		}
		closeQuietly();
	}
}
