/* ------------------------------------------------------------------------
 *   One TLS connection to a mail server, read a line at a time. The host
 *   name is checked against the certificate (endpoint identification), a
 *   line longer than 1 MiB is refused, as on the desktop (net.c).
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL-APK
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
package com.syntax990.synmail.core;

import java.io.BufferedInputStream;
import java.io.ByteArrayOutputStream;
import java.io.Closeable;
import java.io.EOFException;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.charset.StandardCharsets;

import javax.net.ssl.SSLParameters;
import javax.net.ssl.SSLSocket;
import javax.net.ssl.SSLSocketFactory;

public final class Net implements Closeable {
	public static final int LINE_MAX = 1 << 20;
	public static final int TIMEOUT_MS = 30000;

	private Socket sock;
	private InputStream in;
	private OutputStream out;
	private final String host;
	private final int port;

	private Net(String host, int port) {
		this.host = host;
		this.port = port;
	}

	/** tls false only for a port that upgrades with STARTTLS (SMTP 587); startTls must follow before any secret */
	public static Net open(String host, int port, boolean tls, SSLSocketFactory f) throws IOException {
		Net n = new Net(host, port);
		Socket raw = new Socket();
		try {
			raw.connect(new InetSocketAddress(host, port), TIMEOUT_MS);
			raw.setSoTimeout(TIMEOUT_MS);
			n.sock = raw;
			if (tls) n.upgrade(f);
			else n.streams();
		} catch (IOException e) {
			try {
				raw.close();
			} catch (IOException ignored) {
			}
			throw new MailException(host + ":" + port + ": " + why(e));
		}
		return n;
	}

	static String why(IOException e) {
		String m = e.getMessage();
		return m == null || m.isEmpty() ? e.getClass().getSimpleName() : m;
	}

	private void streams() throws IOException {
		in = new BufferedInputStream(sock.getInputStream(), 16384);
		out = sock.getOutputStream();
	}

	/** wraps the connection in TLS and checks the certificate against the host name */
	public void upgrade(SSLSocketFactory f) throws IOException {
		SSLSocket s = (SSLSocket) f.createSocket(sock, host, port, true);
		SSLParameters p = s.getSSLParameters();
		p.setEndpointIdentificationAlgorithm("HTTPS");
		s.setSSLParameters(p);
		s.setSoTimeout(TIMEOUT_MS);
		s.startHandshake();
		sock = s;
		streams();
	}

	/** one line without its CRLF, as bytes */
	public byte[] line() throws IOException {
		ByteArrayOutputStream b = new ByteArrayOutputStream(128);
		while (true) {
			int c = in.read();
			if (c < 0) throw new EOFException("connection lost");
			if (c == '\n') break;
			if (b.size() >= LINE_MAX) throw new MailException("the server sent a line too long to read (over 1 MiB)");
			b.write(c);
		}
		byte[] r = b.toByteArray();
		int n = r.length;
		if (n > 0 && r[n - 1] == '\r') {
			byte[] t = new byte[n - 1];
			System.arraycopy(r, 0, t, 0, n - 1);
			return t;
		}
		return r;
	}

	public String lineText() throws IOException {
		return new String(line(), StandardCharsets.UTF_8);
	}

	/** exactly n bytes; the caller has capped n */
	public byte[] exact(int n) throws IOException {
		byte[] b = new byte[n];
		int got = 0;
		while (got < n) {
			int r = in.read(b, got, n - got);
			if (r < 0) throw new EOFException("connection lost");
			got += r;
		}
		return b;
	}

	public void write(byte[] b) throws IOException {
		out.write(b);
		out.flush();
	}

	public void write(String s) throws IOException {
		write(s.getBytes(StandardCharsets.UTF_8));
	}

	@Override
	public void close() {
		try {
			sock.close();
		} catch (IOException ignored) {
		}
	}
}
