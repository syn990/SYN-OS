/* ------------------------------------------------------------------------
 *   The Android app's engine on the desktop JVM, for tests/e2e_apk.py:
 *   no Android, clocks instead of sound devices (playback drains in real
 *   time into a raw file, the "mic" is a 600 Hz tone), driven by one
 *   command per line on stdin, the desktop control socket's verbs.
 *
 *   usage: java -Dsyncast.jni=libsyncast-host.so ... HeadlessClient CONFIG DATA PLAYOUT
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CAST-APK
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
package com.syntax990.syncast.test;

import com.syntax990.syncast.core.Config;
import com.syntax990.syncast.core.Engine;
import com.syntax990.syncast.core.History;
import com.syntax990.syncast.core.Json;
import com.syntax990.syncast.core.Opus;

import java.awt.Graphics2D;
import java.awt.image.BufferedImage;
import java.io.BufferedReader;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

import javax.imageio.ImageIO;

public final class HeadlessClient implements Engine.Platform {
	private final Object ring = new Object();
	private short[] buf = new short[Opus.RATE * 4];
	private int head, len;
	private long written, played;
	private volatile int playLevel;
	private final FileOutputStream playout;

	private final Object mic = new Object();
	private boolean want;
	private int begins, served;
	private Engine engine;

	HeadlessClient(String playoutPath) throws IOException {
		playout = new FileOutputStream(playoutPath);
		Thread t = new Thread(this::drain, "playback");
		t.setDaemon(true);
		t.start();
		Thread c = new Thread(this::capture, "capture");
		c.setDaemon(true);
		c.start();
	}

	/* ---------- playback: a ring drained in real time ---------- */

	private void drain() {
		short[] chunk = new short[Opus.RATE / 50];
		byte[] bytes = new byte[chunk.length * 2];
		long next = System.nanoTime();
		while (true) {
			int n;
			synchronized (ring) {
				while (len == 0) {
					try {
						ring.wait();
					} catch (InterruptedException e) {
						return;
					}
					next = System.nanoTime();
				}
				n = Math.min(len, chunk.length);
				for (int k = 0; k < n; k++) chunk[k] = buf[(head + k) % buf.length];
				head = (head + n) % buf.length;
				len -= n;
				played += n;
			}
			double sum = 0;
			for (int k = 0; k < n; k++) {
				sum += (double) chunk[k] * chunk[k];
				bytes[2 * k] = (byte) chunk[k];
				bytes[2 * k + 1] = (byte) (chunk[k] >> 8);
			}
			playLevel = (int) Math.min(100, Math.sqrt(sum / n) / 100);
			try {
				playout.write(bytes, 0, n * 2);
				playout.flush();
			} catch (IOException ignored) {
			}
			next += n * 1_000_000_000L / Opus.RATE;
			long wait = next - System.nanoTime();
			if (wait > 0) {
				try {
					Thread.sleep(wait / 1_000_000, (int) (wait % 1_000_000));
				} catch (InterruptedException e) {
					return;
				}
			}
		}
	}

	@Override
	public int playWrite(short[] pcm, int n) {
		synchronized (ring) {
			n = Math.min(n, buf.length - len);
			for (int k = 0; k < n; k++) buf[(head + len + k) % buf.length] = pcm[k];
			len += n;
			written += n;
			ring.notifyAll();
			return n;
		}
	}

	@Override
	public int playQueued() {
		synchronized (ring) {
			return len;
		}
	}

	@Override
	public long playWritten() {
		synchronized (ring) {
			return written;
		}
	}

	@Override
	public long playPlayed() {
		synchronized (ring) {
			return played;
		}
	}

	@Override
	public int playLevel() {
		return playLevel;
	}

	@Override
	public void playFlush() {
		synchronized (ring) {
			played += len;
			len = 0;
		}
	}

	/* ---------- the "microphone": a 600 Hz tone while talking ---------- */

	private void capture() {
		short[] frame = new short[Opus.FRAME];
		double phase = 0;
		try (Opus.Encoder enc = new Opus.Encoder()) {
			while (true) {
				synchronized (mic) {
					while (!want && begins == served) mic.wait();
					served = begins;
				}
				enc.reset();
				long next = System.nanoTime();
				while (true) {
					next += Opus.FRAME_MS * 1_000_000L;
					long wait = next - System.nanoTime();
					if (wait > 0) Thread.sleep(wait / 1_000_000);
					for (int k = 0; k < frame.length; k++) {
						frame[k] = (short) (3277 * Math.sin(phase));
						phase += 2 * Math.PI * 600 / Opus.RATE;
					}
					byte[] p = enc.encode(frame);
					if (p != null) engine.capturePacket(p, 66);
					boolean stop;
					synchronized (mic) {
						stop = !want;
						if (begins != served) {
							served = begins; /* pressed again within the frame: keep going */
							stop = false;
						}
					}
					if (stop) break;
				}
				engine.captureEnded();
			}
		} catch (InterruptedException ignored) {
		}
	}

	@Override
	public void captureBegin() {
		synchronized (mic) {
			want = true;
			begins++;
			mic.notifyAll();
		}
	}

	@Override
	public void captureEnd() {
		synchronized (mic) {
			want = false;
			mic.notifyAll();
		}
	}

	@Override
	public void captureVox(boolean on) {
	}

	@Override
	public int captureLevel() {
		return want ? 66 : 0;
	}

	@Override
	public void tone(String meaning) {
		System.err.println("tone " + meaning);
	}

	@Override
	public byte[] thumbnail(byte[] picture) {
		try {
			BufferedImage img = ImageIO.read(new java.io.ByteArrayInputStream(picture));
			return img == null ? null : jpeg(img, 160);
		} catch (IOException e) {
			return null;
		}
	}

	@Override
	public void notify(String title, String text) {
	}

	@Override
	public boolean focused() {
		return true;
	}

	@Override
	public void changed() {
	}

	@Override
	public void log(String line) {
		System.err.println("engine: " + line);
	}

	/* ---------- images through ImageIO ---------- */

	private static byte[] jpeg(BufferedImage src, int max) throws IOException {
		int w = src.getWidth(), h = src.getHeight();
		double s = Math.min(1.0, (double) max / Math.max(w, h));
		int dw = Math.max(1, (int) (w * s)), dh = Math.max(1, (int) (h * s));
		BufferedImage out = new BufferedImage(dw, dh, BufferedImage.TYPE_INT_RGB);
		Graphics2D g = out.createGraphics();
		g.drawImage(src, 0, 0, dw, dh, null);
		g.dispose();
		ByteArrayOutputStream b = new ByteArrayOutputStream();
		ImageIO.write(out, "jpg", b);
		return b.toByteArray();
	}

	/* ---------- commands ---------- */

	private static Map<String, Object> msgJson(History.Msg m) {
		return Json.obj("id", (double) m.id, "kind", m.kind.name().toLowerCase(), "from", m.from, "for", m.forwho,
			"out", m.out, "played", m.played, "partial", m.partial, "duration_ms", (double) m.durationMs,
			"text", m.text, "w", (double) m.w, "h", (double) m.h, "lat", m.lat, "lon", m.lon, "sid", m.sid, "ts", (double) m.ts);
	}

	public static void main(String[] args) throws Exception {
		Config cfg = Config.load(new File(args[0]));
		HeadlessClient pf = new HeadlessClient(args[2]);
		Engine e = new Engine(cfg, new File(args[1]), new File(args[0]), pf);
		pf.engine = e;
		e.start();
		BufferedReader in = new BufferedReader(new InputStreamReader(System.in));
		String line;
		while ((line = in.readLine()) != null) {
			String[] w = line.trim().split("\\s+", 2);
			String arg = w.length > 1 ? w[1] : "";
			String reply = "ok";
			switch (w[0]) {
			case "status": {
				Engine.View v = e.snapshot();
				List<Object> chans = new ArrayList<>();
				for (Engine.ChannelView c : v.channels) {
					chans.add(Json.obj("name", c.name, "online", c.online, "state", c.state, "users_online",
						(double) c.users, "unread", (double) c.unread, "error", c.error, "title", c.title,
						"who", new ArrayList<Object>(c.who), "peer", c.peer));
				}
				List<Object> bar = new ArrayList<>(); /* G192: the rooms bar, left to right */
				for (int k : v.order) bar.add(v.channels.get(k).name);
				reply = Json.write(Json.obj("talk", v.talk.name().toLowerCase(), "queue", (double) v.queue,
					"active", (double) v.active, "offline", v.offline, "member", v.user, "auth_error", v.authError,
					"busy", v.busy, "speed", v.speed, "channels", chans, "bar", bar,
					"played_samples", (double) pf.playPlayed()));
				break;
			}
			case "history": {
				List<Object> out = new ArrayList<>();
				for (History.Msg m : e.history(arg.isEmpty() ? 0 : Integer.parseInt(arg))) out.add(msgJson(m));
				reply = Json.write(out);
				break;
			}
			case "text": e.sendText(arg); break;
			case "input": e.input(arg); break;   /* G193: the text box as typed, /msg NAME [TEXT] included */
			case "dm": e.openDm(arg); break;
			case "ptt":
				if (arg.equals("down")) e.pttDown();
				else if (arg.equals("up")) e.pttUp();
				else e.pttToggle();
				break;
			case "image": {
				BufferedImage img = ImageIO.read(new File(arg));
				double s = Math.min(1.0, 1280.0 / Math.max(img.getWidth(), img.getHeight()));
				e.sendImage(jpeg(img, 1280), jpeg(img, 160), Math.max(1, (int) (img.getWidth() * s)),
					Math.max(1, (int) (img.getHeight() * s)));
				break;
			}
			case "loc": {
				String[] p = arg.split("\\s+", 3);
				e.sendLocation(Double.parseDouble(p[0]), Double.parseDouble(p[1]), 5, p.length > 2 ? p[2] : "");
				break;
			}
			case "channel": e.selectChannel(Integer.parseInt(arg)); break;
			case "swipe": e.selectStep(Integer.parseInt(arg)); break;
			case "replay": e.replayLast(); break;
			case "stop": e.stopPlayback(); break;
			case "speed": e.cycleSpeed(); break;
			case "busy": e.toggleBusy(); break;
			case "offline": e.setOffline(true); break;
			case "online": e.setOffline(false); break;
			case "quit":
				e.stop();
				System.out.println("ok");
				System.exit(0);
				break;
			default: reply = "error: unknown command";
			}
			System.out.println(reply);
			System.out.flush();
		}
		e.stop();
	}
}
