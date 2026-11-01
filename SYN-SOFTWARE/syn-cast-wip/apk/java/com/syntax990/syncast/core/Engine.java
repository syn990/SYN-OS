/* ------------------------------------------------------------------------
 *   The core, a Java port of the desktop's app.c: sessions with backoff and
 *   refresh tokens, the talk button (IDLE, CONNECTING, SENDING, RECEIVING,
 *   DISABLED, as the Zello app's TalkButtonState), incoming streams with
 *   loss marks, the playback queue, history, texts, images and locations.
 *
 *   One session, one channel: Zello allows one session per account and a
 *   Friends & Family logon joins one channel, so selecting another channel
 *   moves the session there. The OFFLINE switch closes it and keeps the
 *   account. If another client signs in with it, this one takes it straight
 *   back once (the Zello app doesn't fight back); a second time within two
 *   minutes it goes offline instead of fighting.
 *
 *   Everything sent goes to the whole channel (the Channel API's "for"
 *   reaches only someone in the same channel, so it isn't offered);
 *   private messages that arrive are shown.
 *
 *   Everything runs on one loop thread; socket readers, the capture thread
 *   and the UI only post to it. The UI reads state through snapshot().
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CAST-APK
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
package com.syntax990.syncast.core;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.security.PrivateKey;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;

public final class Engine {
	public interface Platform {
		/* playback at Opus.RATE, drained by the platform in real time */
		int playWrite(short[] pcm, int n);
		int playQueued();
		long playWritten();
		long playPlayed();
		int playLevel();
		void playFlush();
		/* the microphone: every captureBegin() is answered by captureEnded()
		 * or captureFailed(), packets in between through capturePacket() */
		void captureBegin();
		void captureEnd();
		void captureVox(boolean on);
		int captureLevel();
		void tone(String meaning);
		void notify(String title, String text);
		boolean focused();
		void changed();
		void log(String line);
	}

	public enum Talk { IDLE, CONNECTING, SENDING, RECEIVING, DISABLED }

	private static final int PING_EVERY_MS = 25000;
	/* Zello pings every 30 s; the tests shorten this with -Dsyncast.deadAfterMs */
	private static final long DEAD_AFTER_MS = Long.getLong("syncast.deadAfterMs", 70000L);
	private static final int LIVE_PREBUFFER_PACKETS = 3, LIVE_PREBUFFER_MS = 400;
	private static final int FEED_AHEAD = Opus.RATE / 2;
	private static final int HELD_MAX = 50;
	private static final long KICK_WINDOW_MS = 120000;

	/* ---------- state ---------- */

	private enum SState { OFF, CONNECTING, LOGON, ONLINE, RETRY }

	private enum PKind { LOGON, START, TEXT, IMAGE, LOCATION }

	private static final class Pending {
		int seq;
		PKind kind;
		int channel;
		long ref;
		boolean refresh;
	}

	private final class Session {
		final int idx;
		final List<Integer> chans = new ArrayList<>();
		SState state = SState.OFF;
		Ws ws;
		int seq;
		String refreshToken = "";
		long retryAt, lastRx, lastPing;
		int backoffMs;
		String error = "";
		boolean wasOnline;
		boolean refused;   /* sign-in refused: no retries until the account is fixed */
		boolean kicked;    /* another client signed in with this account */
		long lastKick;
		final List<Pending> pend = new ArrayList<>();

		Session(int idx) {
			this.idx = idx;
		}
	}

	public static final class Channel {
		public final String name;
		int session;
		public boolean online, imagesOk, textingOk, locationsOk, muted;
		public int usersOnline, unread;
		public String error = "";
		final History hist;

		Channel(String name, History hist) {
			this.name = name;
			this.hist = hist;
		}
	}

	private static final class Rx {
		int session, channel;
		long streamId, lastPid = -1;
		String from, forwho;
		Proto.Codec codec;
		long msgId;
		History.VoiceWriter file;
		final List<byte[]> pkts = new ArrayList<>();
		boolean ended, queued;
		long started;
	}

	private static final class Item {
		boolean replay;
		Rx rx;
		int channel;
		long msgId;
		String from;
		List<byte[]> file;
		int cursor;
		Opus.Decoder dec;
		Stretch st;
		boolean started, fedAll;
		long endMark, began;
	}

	private enum Tx { IDLE, STARTING, SENDING }

	private final Config cfg;
	private final File dataDir;
	private final Platform pf;
	private final List<Session> sess = new ArrayList<>();
	private final List<Channel> chan = new ArrayList<>();
	private int active;
	private final List<Rx> rx = new ArrayList<>();
	private final List<Item> queue = new ArrayList<>();
	private boolean playing;
	private double speed = 1.0;
	private boolean busy;
	private final java.util.List<long[]> busyQ = new java.util.ArrayList<>();
	private boolean offline;
	private boolean vox;
	private String toast = "";
	private long toastUntil;
	private long historyVersion;
	private String authError;
	private PrivateKey key;

	private Tx tx = Tx.IDLE;
	private boolean txReleased, txEndSeen, txVox;
	private int txChannel, txStartSeq;
	private long txStreamId, txMsgId, txStarted;
	private final List<byte[]> txHeld = new ArrayList<>();
	private History.VoiceWriter txFile;
	private int txLevel;

	private final Map<Long, long[]> imagesIn = new java.util.HashMap<>(); /* message_id -> {channel, msgId, when} */
	private byte[] imgFull, imgThumb;
	private int imgW, imgH, imgChannel;
	private final long[][] recentStreams = new long[32][]; /* {streamId, channel, msgId} for transcriptions */
	private int recentNext;

	private final ScheduledExecutorService loop = Executors.newSingleThreadScheduledExecutor(r -> {
		Thread t = new Thread(r, "syncast-engine");
		t.setDaemon(true);
		return t;
	});
	private final ExecutorService connector = Executors.newCachedThreadPool(r -> {
		Thread t = new Thread(r, "syncast-connect");
		t.setDaemon(true);
		return t;
	});
	private volatile boolean stopped;

	public Engine(Config cfg, File dataDir, Platform pf) {
		this.cfg = cfg;
		this.dataDir = dataDir;
		this.pf = pf;
		this.vox = cfg.vox;
	}

	/* ---------- the loop ---------- */

	public void post(Runnable r) {
		if (stopped) return;
		loop.execute(() -> {
			synchronized (Engine.this) {
				try {
					r.run();
				} catch (RuntimeException e) {
					pf.log("engine: " + e);
				}
			}
		});
	}

	private void scheduleTick(long ms) {
		if (stopped) return;
		loop.schedule(() -> {
			long next;
			synchronized (Engine.this) {
				try {
					tick();
				} catch (RuntimeException e) {
					pf.log("tick: " + e);
				}
				next = tx != Tx.IDLE || playing || !queue.isEmpty() ? 20 : 250;
			}
			scheduleTick(next);
		}, ms, TimeUnit.MILLISECONDS);
	}

	public void start() {
		post(() -> {
			dataDir.mkdirs();
			for (String name : cfg.channels) {
				try {
					History h = new History(dataDir, name);
					h.prune(cfg.historyDays, (long) cfg.historyMb << 20);
					chan.add(new Channel(name, h));
				} catch (IOException e) {
					pf.log("history for " + name + ": " + e.getMessage());
				}
			}
			if (!chan.isEmpty()) {
				sess.add(new Session(0));
				sess.get(0).chans.add(active);
			}
			if (!cfg.issuer.isEmpty() && !cfg.keyPem.isEmpty()) {
				try {
					key = Jwt.parsePem(cfg.keyPem);
				} catch (Exception e) {
					for (Session ss : sess) ss.error = "developer key: " + e.getMessage();
				}
			}
			if (mayConnect()) connect(sess.get(0));
			pf.captureVox(vox);
			changed();
		});
		scheduleTick(50);
	}

	public void stop() {
		CountDownLatchWait.run(loop, () -> {
			synchronized (Engine.this) {
				if (tx != Tx.IDLE) txAbort(null);
				while (!queue.isEmpty()) {
					playing = true;
					finishItem(false);
				}
				for (Session ss : sess) {
					if (ss.ws != null) ss.ws.close();
					ss.ws = null;
				}
				for (Rx r : new ArrayList<>(rx)) {
					if (!r.ended) rxFinalize(r);
					rxFree(r);
				}
				for (Channel c : chan) c.hist.close();
			}
		});
		stopped = true;
		loop.shutdownNow();
		connector.shutdownNow();
	}

	private void changed() {
		historyVersion++;
		pf.changed();
	}

	public void toast(String s) {
		toast = s;
		toastUntil = System.currentTimeMillis() + 5000;
		pf.log(s);
		changed();
	}

	private String me() {
		return cfg.username.isEmpty() ? "you" : cfg.username;
	}

	private History.Msg event(int ch, String text) {
		History.Msg m = new History.Msg();
		m.kind = History.Kind.EVENT;
		m.text = text;
		chan.get(ch).hist.add(m);
		changed();
		return m;
	}

	/* ---------- sessions ---------- */

	private boolean send(Session ss, String json) {
		Ws w = ss.ws;
		if (w == null || !w.sendText(json)) {
			if (w != null) drop(ss, "send failed");
			return false;
		}
		return true;
	}

	private boolean sendBin(Session ss, byte[] hdr, byte[] data) {
		Ws w = ss.ws;
		if (w == null || !w.send(Ws.BINARY, hdr, data)) {
			if (w != null) drop(ss, "send failed");
			return false;
		}
		return true;
	}

	private void addPending(Session ss, int seq, PKind k, int channel, long ref, boolean refresh) {
		Pending p = new Pending();
		p.seq = seq;
		p.kind = k;
		p.channel = channel;
		p.ref = ref;
		p.refresh = refresh;
		ss.pend.add(p);
		if (ss.pend.size() > 64) ss.pend.remove(0); /* the oldest reply is long overdue */
	}

	private Pending takePending(Session ss, int seq) {
		for (int k = 0; k < ss.pend.size(); k++) {
			if (ss.pend.get(k).seq == seq) return ss.pend.remove(k);
		}
		return null;
	}

	private void retry(Session ss, int minMs) {
		ss.state = SState.RETRY;
		ss.backoffMs = ss.backoffMs == 0 ? 1000 : Math.min(ss.backoffMs * 2, 30000);
		ss.retryAt = System.currentTimeMillis() + Math.max(ss.backoffMs, minMs) + (long) (Math.random() * 500);
		changed();
	}

	private void connect(Session ss) {
		ss.state = SState.CONNECTING;
		final String url = cfg.server;
		connector.execute(() -> {
			try {
				Ws w = Ws.connect(url, 10000);
				post(() -> connected(ss, w));
			} catch (IOException e) {
				String why = e.getMessage() != null ? e.getMessage() : e.toString();
				post(() -> {
					if (ss.state == SState.CONNECTING) {
						ss.error = why;
						pf.log("connect: " + why);
						retry(ss, 0);
					}
				});
			}
		});
		changed();
	}

	private void connected(Session ss, Ws w) {
		if (ss.state != SState.CONNECTING || stopped) {
			w.close();
			return;
		}
		ss.ws = w;
		ss.lastRx = ss.lastPing = System.currentTimeMillis();
		w.start(new Ws.Listener() {
			@Override
			public void onText(Ws ws, String text) {
				post(() -> {
					if (ss.ws == ws) {
						ss.lastRx = System.currentTimeMillis();
						onJson(ss, text);
					}
				});
			}

			@Override
			public void onBinary(Ws ws, byte[] data) {
				post(() -> {
					if (ss.ws == ws) {
						ss.lastRx = System.currentTimeMillis();
						onBinaryFrame(ss, data);
					}
				});
			}

			@Override
			public void onClosed(Ws ws, String why) {
				post(() -> {
					if (ss.ws != ws) return;
					boolean kicked = "kicked".equals(why);
					if (kicked) ss.kicked = true;
					drop(ss, kicked ? "signed in elsewhere" : why);
				});
			}
		});
		logon(ss);
	}

	private void logon(Session ss) {
		boolean useRefresh = !ss.refreshToken.isEmpty();
		String token = null;
		if (!useRefresh) {
			if (!cfg.authToken.isEmpty()) {
				token = cfg.authToken;
			} else if (key != null) {
				try {
					token = Jwt.mint(key, cfg.issuer, 120);
				} catch (Exception e) {
					ss.error = "token: " + e.getMessage();
					drop(ss, ss.error);
					return;
				}
			}
		}
		List<String> names = new ArrayList<>();
		for (int k : ss.chans) names.add(chan.get(k).name);
		int seq = ++ss.seq;
		addPending(ss, seq, PKind.LOGON, -1, 0, useRefresh);
		send(ss, Proto.logon(seq, token, useRefresh ? ss.refreshToken : null, cfg.username, cfg.password, names,
			cfg.transcriptions));
		ss.state = SState.LOGON;
	}

	private void drop(Session ss, String why) {
		boolean wasOnline = ss.state == SState.ONLINE;
		if (ss.ws != null) {
			Ws w = ss.ws;
			ss.ws = null;
			w.close();
		}
		if (why != null && !why.isEmpty()) ss.error = why;
		for (Pending p : ss.pend) {
			if ((p.kind == PKind.TEXT || p.kind == PKind.LOCATION) && p.channel >= 0) {
				History.Msg m = chan.get(p.channel).hist.find(p.ref);
				if (m != null && m.partial) {
					m.partial = false;
					chan.get(p.channel).hist.commit(m);
					event(p.channel, "not delivered (connection lost)");
				}
			}
			if (p.kind == PKind.IMAGE) imgFull = imgThumb = null;
		}
		ss.pend.clear();
		for (int k : ss.chans) {
			Channel c = chan.get(k);
			c.online = false;
			c.usersOnline = 0;
			if (wasOnline) event(k, "connection lost: " + ss.error);
			if (tx != Tx.IDLE && txChannel == k) txAbort("connection lost");
		}
		for (Rx r : new ArrayList<>(rx)) {
			if (r.session == ss.idx) rxEnd(r);
		}
		if (wasOnline) {
			tone("TOGGLE_OFF");
			ss.backoffMs = 0;
		}
		pf.log("session " + ss.idx + " dropped: " + ss.error);
		if (ss.kicked) {
			ss.kicked = false;
			long now = System.currentTimeMillis();
			if (ss.lastKick != 0 && now - ss.lastKick < KICK_WINDOW_MS) {
				/* kicked twice in a row: another client keeps taking it, so give way */
				ss.lastKick = 0;
				ss.state = SState.OFF;
				offline = true;
				ss.error = "another client keeps signing in as " + cfg.username;
				for (int k : ss.chans) event(k, "signed in elsewhere: went offline");
				toast("another client is using " + cfg.username + ": SYN-CAST went offline");
				return;
			}
			ss.lastKick = now;
			ss.backoffMs = 0;
			pf.log("another client signed in as " + cfg.username + ": taking the account back");
		}
		if (ss.refused) {
			ss.state = SState.OFF;
			changed();
		} else {
			retry(ss, 0);
		}
	}

	private int channelIn(Session ss, String name) {
		for (int k : ss.chans) {
			if (chan.get(k).name.equalsIgnoreCase(name)) return k;
		}
		return ss.chans.size() == 1 ? ss.chans.get(0) : -1;
	}

	/* ---------- incoming ---------- */

	private void onJson(Session ss, String text) {
		Map<String, Object> o = Json.object(text);
		if (o == null) return;
		String cmd = Json.str(o, "command");
		String chName = Json.str(o, "channel");
		int ch = !chName.isEmpty() || ss.chans.size() == 1 ? channelIn(ss, chName) : -1;
		String from = Json.str(o, "from"), forwho = Json.str(o, "for");
		if (cmd.isEmpty()) {
			if (o.containsKey("seq")) onReply(ss, o);
			return;
		}
		switch (cmd) {
		case "on_channel_status":
			if (ch >= 0) {
				Channel c = chan.get(ch);
				boolean was = c.online;
				c.online = "online".equals(Json.str(o, "status"));
				c.usersOnline = (int) Json.lng(o, "users_online");
				c.imagesOk = Json.bool(o, "images_supported");
				c.textingOk = Json.bool(o, "texting_supported");
				c.locationsOk = Json.bool(o, "locations_supported");
				c.error = Json.str(o, "error");
				if (c.online && !was) {
					event(ch, "connected, " + c.usersOnline + " online");
					if (ss.wasOnline) tone("SUCCESS");
					ss.wasOnline = true;
				} else if (!c.online && was) {
					event(ch, "channel offline" + (c.error.isEmpty() ? "" : ": " + c.error));
				}
			}
			break;
		case "on_stream_start":
			rxStart(ss, ch, o, from, forwho);
			break;
		case "on_stream_stop":
			for (Rx r : new ArrayList<>(rx)) {
				if (r.session == ss.idx && r.streamId == Json.lng(o, "stream_id")) rxEnd(r);
			}
			break;
		case "on_text_message":
			if (ch >= 0) {
				History.Msg m = new History.Msg();
				m.kind = History.Kind.TEXT;
				m.text = Json.str(o, "text");
				m.from = from;
				m.forwho = forwho;
				chan.get(ch).hist.add(m);
				if (ch != active) chan.get(ch).unread++;
				if (!chan.get(ch).muted) tone("ALERT");
				pf.notify(from + (forwho.isEmpty() ? " in " : " (private) in ") + chan.get(ch).name, m.text);
			}
			break;
		case "on_image":
			if (ch >= 0) {
				History.Msg m = new History.Msg();
				m.kind = History.Kind.IMAGE;
				m.partial = true;
				m.w = (int) Json.lng(o, "width");
				m.h = (int) Json.lng(o, "height");
				m.from = from;
				m.forwho = forwho;
				chan.get(ch).hist.add(m);
				imagesIn.put(Json.lng(o, "message_id"), new long[] {ch, m.id, System.currentTimeMillis()});
			}
			break;
		case "on_location":
			if (ch >= 0) {
				History.Msg m = new History.Msg();
				m.kind = History.Kind.LOCATION;
				m.lat = Json.num(o, "latitude");
				m.lon = Json.num(o, "longitude");
				m.acc = Json.num(o, "accuracy");
				String addr = Json.str(o, "formatted_address");
				m.text = addr.isEmpty() ? Json.str(o, "rgl") : addr;
				m.from = from;
				m.forwho = forwho;
				chan.get(ch).hist.add(m);
				tone("ALERT");
			}
			break;
		case "on_transcription":
			for (long[] r : recentStreams) {
				if (r != null && r[0] == Json.lng(o, "stream_id")) {
					History.Msg m = chan.get((int) r[1]).hist.find(r[2]);
					if (m != null) {
						m.text = Json.str(o, "text");
						chan.get((int) r[1]).hist.commit(m);
					}
				}
			}
			break;
		case "on_error":
			toast("server: " + Json.str(o, "error"));
			if ("server closed connection".equals(Json.str(o, "error"))) drop(ss, Json.str(o, "error"));
			else if ("kicked".equals(Json.str(o, "error"))) {
				ss.kicked = true;
				drop(ss, "signed in elsewhere");
			}
			break;
		default:
			break;
		}
		changed();
	}

	private void onReply(Session ss, Map<String, Object> o) {
		Pending p = takePending(ss, (int) Json.lng(o, "seq"));
		if (p == null) return;
		boolean ok = Json.bool(o, "success");
		String err = Json.str(o, "error");
		switch (p.kind) {
		case LOGON:
			if (ok) {
				ss.state = SState.ONLINE;
				ss.backoffMs = 0;
				ss.error = "";
				String rt = Json.str(o, "refresh_token");
				if (!rt.isEmpty()) ss.refreshToken = rt;
				pf.log("signed in as " + cfg.username);
			} else {
				String why = "sign-in refused: " + (err.isEmpty() ? "unknown" : err);
				if (p.refresh) {
					ss.refreshToken = ""; /* stale: the next try mints a token */
					drop(ss, why);
				} else {
					/* a wrong password, username or key: retrying only risks a lockout */
					authError = err.isEmpty() ? "unknown" : err;
					ss.refused = true;
					drop(ss, why);
				}
			}
			break;
		case START:
			txStarted(ss, p.seq, ok, Json.lng(o, "stream_id"), err);
			break;
		case TEXT:
		case LOCATION: {
			History.Msg m = chan.get(p.channel).hist.find(p.ref);
			if (m != null) {
				m.partial = false;
				chan.get(p.channel).hist.commit(m);
			}
			if (!ok) {
				event(p.channel, "not delivered: " + (err.isEmpty() ? "refused" : err));
				tone("FAIL");
			}
			break;
		}
		case IMAGE:
			imageSent(ss, ok, Json.lng(o, "image_id"), err);
			break;
		}
		changed();
	}

	private void onBinaryFrame(Session ss, byte[] d) {
		if (d.length < Proto.HDR) return;
		int type = d[0];
		long id = Proto.be32(d, 1), sub = Proto.be32(d, 5);
		byte[] data = new byte[d.length - Proto.HDR];
		System.arraycopy(d, Proto.HDR, data, 0, data.length);
		if (type == Proto.BIN_AUDIO) {
			for (Rx r : rx) {
				if (r.ended || r.session != ss.idx || r.streamId != id) continue;
				if (r.lastPid >= 0 && sub > r.lastPid + 1 && sub - r.lastPid <= 25) {
					/* lost packets: marked so playback covers them with FEC/PLC */
					for (long k = r.lastPid + 1; k < sub; k++) {
						r.pkts.add(new byte[0]);
						if (r.file != null) r.file.write(null);
					}
				}
				r.lastPid = sub;
				r.pkts.add(data);
				if (r.file != null) r.file.write(data);
				return;
			}
		} else if (type == Proto.BIN_IMAGE) {
			long[] in = imagesIn.get(id);
			if (in == null) return;
			History h = chan.get((int) in[0]).hist;
			try (FileOutputStream f = new FileOutputStream(h.image(in[1], sub == Proto.IMAGE_THUMB))) {
				f.write(data);
			} catch (IOException e) {
				pf.log("image: " + e.getMessage());
			}
			if (sub == Proto.IMAGE_FULL) {
				History.Msg m = h.find(in[1]);
				if (m != null) {
					m.partial = false;
					h.commit(m);
					pf.notify(chan.get((int) in[0]).name, m.from + " sent an image");
				}
				tone("ALERT");
				imagesIn.remove(id);
			}
			changed();
		}
	}

	private void rxStart(Session ss, int ch, Map<String, Object> o, String from, String forwho) {
		if (ch < 0) return;
		if (rx.size() >= 16) {
			Rx old = null;
			for (Rx r : rx) {
				if (r.ended && !r.queued && (old == null || r.started < old.started)) old = r;
			}
			if (old == null) {
				pf.log("too many streams at once; dropping one from " + from);
				return;
			}
			rxFree(old);
		}
		Rx r = new Rx();
		r.session = ss.idx;
		r.channel = ch;
		r.streamId = Json.lng(o, "stream_id");
		r.from = from;
		r.forwho = forwho;
		r.codec = Proto.Codec.decode(Json.str(o, "codec_header"));
		if (r.codec == null) r.codec = new Proto.Codec(16000, 1, 60);
		r.started = System.currentTimeMillis();
		Channel c = chan.get(ch);
		History.Msg m = new History.Msg();
		m.kind = History.Kind.VOICE;
		m.partial = true;
		m.from = from;
		m.forwho = forwho;
		c.hist.add(m);
		r.msgId = m.id;
		try {
			r.file = new History.VoiceWriter(c.hist.voice(m.id), r.codec);
		} catch (IOException e) {
			pf.log("voice file: " + e.getMessage());
		}
		recentStreams[recentNext] = new long[] {r.streamId, ch, m.id};
		recentNext = (recentNext + 1) % recentStreams.length;
		rx.add(r);
		if (cfg.autoPlay && !busy && !c.muted) {
			Item it = new Item();
			it.rx = r;
			it.channel = ch;
			it.msgId = m.id;
			it.from = from;
			it.began = System.currentTimeMillis();
			r.queued = true;
			queue.add(it);
		} else {
			c.unread++;
			if (busy) busyQ.add(new long[] { ch, m.id });
		}
		if (!pf.focused()) pf.notify(c.name, from + (forwho.isEmpty() ? " is talking" : " is talking to you"));
		changed();
	}

	private void rxFinalize(Rx r) {
		History.Msg m = chan.get(r.channel).hist.find(r.msgId);
		int dur = r.file != null ? r.file.durationMs() : 0;
		if (r.file != null) r.file.close();
		r.file = null;
		if (m != null) {
			m.partial = false;
			m.durationMs = dur;
			chan.get(r.channel).hist.commit(m);
		}
	}

	private void rxEnd(Rx r) {
		if (r.ended) return;
		r.ended = true;
		rxFinalize(r);
		if (!r.queued) rxFree(r);
		changed();
	}

	private void rxFree(Rx r) {
		if (r.file != null) r.file.close();
		rx.remove(r);
	}

	/* ---------- the player ---------- */

	private void finishItem(boolean natural) {
		if (!playing || queue.isEmpty()) return;
		Item it = queue.remove(0);
		History.Msg m = chan.get(it.channel).hist.find(it.msgId);
		if (m != null && !m.played) {
			m.played = true;
			chan.get(it.channel).hist.commit(m);
		}
		if (natural && !it.replay) tone("STOP"); /* the roger beep */
		if (it.dec != null) it.dec.close();
		if (!it.replay) {
			it.rx.queued = false;
			if (it.rx.ended) rxFree(it.rx);
		}
		playing = false;
		changed();
	}

	private final short[] pcm = new short[Opus.MAX_PACKET_SAMPLES * 22];
	private final short[] stretched = new short[Opus.MAX_PACKET_SAMPLES * 44];

	/* each speaker's playback level, as Zello's player keeps an AGC state per speaker: the most recent 64 */
	private final Map<String, Gain> levels = new LinkedHashMap<String, Gain>(16, 0.75f, true) {
		@Override
		protected boolean removeEldestEntry(Map.Entry<String, Gain> e) {
			return size() > 64;
		}
	};

	private Gain levelOf(String from) {
		String k = from == null ? "" : from.toLowerCase(Locale.ROOT);
		Gain g = levels.get(k);
		if (g == null) levels.put(k, g = new Gain(1.0));
		return g;
	}

	private void feed(Item it) {
		int n = it.replay ? it.file.size() : it.rx.pkts.size();
		boolean ended = it.replay || it.rx.ended;
		if (!it.started) {
			if (!it.replay && !ended && n - it.cursor < LIVE_PREBUFFER_PACKETS
				&& System.currentTimeMillis() - it.began < LIVE_PREBUFFER_MS) {
				return;
			}
			it.started = true;
		}
		while (pf.playQueued() < FEED_AHEAD && it.cursor < n) {
			byte[] d = it.replay ? it.file.get(it.cursor) : it.rx.pkts.get(it.cursor);
			it.cursor++;
			if (d.length == 0) {
				it.dec.lost(1);
				continue;
			}
			int got = it.dec.decode(d, pcm);
			if (got > 0) {
				levelOf(it.from).process(pcm, got);  /* a quiet speaker comes up to everyone else's level */
				int m = it.st.process(pcm, got, stretched, stretched.length);
				pf.playWrite(stretched, m);
			}
		}
		if (it.cursor >= n && ended) {
			int m;
			while ((m = it.st.flush(stretched, stretched.length)) > 0) pf.playWrite(stretched, m);
			it.fedAll = true;
			it.endMark = pf.playWritten();
		}
	}

	private void playerTick() {
		if (!playing) {
			if (queue.isEmpty() || tx != Tx.IDLE) return;
			Item it = queue.get(0);
			it.dec = new Opus.Decoder();
			it.st = new Stretch(it.replay ? speed : 1.0);
			it.began = System.currentTimeMillis();
			playing = true;
			changed();
		}
		Item it = queue.get(0);
		if (!it.fedAll) feed(it);
		if (it.fedAll && pf.playPlayed() >= it.endMark) finishItem(true);
	}

	/* ---------- talking ---------- */

	private void tone(String meaning) {
		if (cfg.tones) pf.tone(meaning);
	}

	private boolean txBegin(boolean byVox) {
		if (chan.isEmpty() || tx != Tx.IDLE) return false;
		Channel c = chan.get(active);
		if (!c.online) {
			tone("FAIL");
			toast(c.name + " is offline");
			return false;
		}
		if (playing && queue.get(0).replay) stopPlaybackNow();
		Session ss = sess.get(c.session);
		int seq = ++ss.seq;
		if (!send(ss, Proto.startStream(seq, c.name, null, Proto.PTT))) {
			tone("FAIL");
			toast("not connected");
			return false;
		}
		addPending(ss, seq, PKind.START, active, 0, false);
		tx = Tx.STARTING;
		txChannel = active;
		txStartSeq = seq;
		txStarted = System.currentTimeMillis();
		txVox = byVox;
		txReleased = txEndSeen = false;
		changed();
		return true;
	}

	private void txSendPacket(byte[] d) {
		Channel c = chan.get(txChannel);
		sendBin(sess.get(c.session), Proto.binHeader(Proto.BIN_AUDIO, txStreamId, 0), d);
		if (txFile != null) txFile.write(d);
	}

	private void txReset() {
		txHeld.clear();
		if (txFile != null) txFile.close();
		txFile = null;
		tx = Tx.IDLE;
		txReleased = txEndSeen = txVox = false;
		txLevel = 0;
		changed();
	}

	private void txFinish() {
		Channel c = chan.get(txChannel);
		if (tx == Tx.SENDING) {
			Session ss = sess.get(c.session);
			send(ss, Proto.stopStream(++ss.seq, c.name, txStreamId));
			History.Msg m = c.hist.find(txMsgId);
			if (m != null) {
				m.partial = false;
				m.durationMs = txFile != null ? txFile.durationMs() : 0;
				c.hist.commit(m);
			}
		}
		txReset();
	}

	private void txAbort(String why) {
		if (tx == Tx.IDLE) return;
		if (!txReleased && !txVox) pf.captureEnd();
		if (why != null) toast("talk ended: " + why);
		txFinish();
	}

	private void txStarted(Session ss, int seq, boolean ok, long streamId, String err) {
		if (tx != Tx.STARTING || seq != txStartSeq) {
			if (ok && streamId != 0) {
				/* the button was given up on before the server answered */
				send(ss, Proto.stopStream(++ss.seq, chan.get(tx == Tx.IDLE ? active : txChannel).name, streamId));
			}
			return;
		}
		if (!ok) {
			tone("FAIL");
			toast("cannot talk: " + (err.isEmpty() ? "refused" : err));
			if (!txReleased && !txVox) pf.captureEnd();
			txReset();
			return;
		}
		Channel c = chan.get(txChannel);
		txStreamId = streamId;
		tx = Tx.SENDING;
		tone("START"); /* talk permit */
		History.Msg m = new History.Msg();
		m.kind = History.Kind.VOICE;
		m.out = m.played = m.partial = true;
		m.from = me();
		c.hist.add(m);
		txMsgId = m.id;
		try {
			txFile = new History.VoiceWriter(c.hist.voice(m.id), Proto.PTT);
		} catch (IOException e) {
			pf.log("voice file: " + e.getMessage());
		}
		for (byte[] p : txHeld) txSendPacket(p);
		txHeld.clear();
		if (txEndSeen) txFinish();
		changed();
	}

	/** From the capture thread. */
	public void capturePacket(byte[] p, int level) {
		post(() -> {
			txLevel = level;
			if (tx == Tx.STARTING) {
				if (txHeld.size() < HELD_MAX) txHeld.add(p);
			} else if (tx == Tx.SENDING) {
				txSendPacket(p);
			}
		});
	}

	public void captureEnded() {
		post(() -> {
			if (tx == Tx.IDLE) return;
			txEndSeen = true;
			if (tx == Tx.SENDING) txFinish();
		});
	}

	public void voxStarted() {
		post(() -> {
			if (tx == Tx.IDLE) txBegin(true);
		});
	}

	public void captureFailed(String why) {
		post(() -> {
			tone("FAIL");
			toast("microphone unavailable" + (why == null ? "" : ": " + why));
			if (tx != Tx.IDLE) {
				txReleased = true;
				txAbort(null);
			}
		});
	}

	public void pttDown() {
		post(() -> {
			if (tx != Tx.IDLE) {
				if (txVox) {
					txVox = false; /* the button takes over a VOX transmission */
					pf.captureBegin();
				}
				return;
			}
			if (txBegin(false)) pf.captureBegin();
		});
	}

	public void pttUp() {
		post(() -> {
			if (tx == Tx.IDLE || txVox || txReleased) return;
			txReleased = true;
			pf.captureEnd();
		});
	}

	public void pttToggle() {
		post(() -> {
			if (tx == Tx.IDLE) {
				if (txBegin(false)) pf.captureBegin();
			} else if (!txVox && !txReleased) {
				txReleased = true;
				pf.captureEnd();
			}
		});
	}

	/* ---------- text, images, locations ---------- */

	private Channel ready() {
		if (chan.isEmpty()) {
			toast("no channel configured");
			return null;
		}
		Channel c = chan.get(active);
		if (!c.online) {
			toast(c.name + " is offline");
			return null;
		}
		return c;
	}

	public void sendText(String text) {
		post(() -> {
			Channel c = ready();
			if (c == null || text.trim().isEmpty()) return;
			if (text.length() > 30000) {
				toast("text longer than the 30 KB the API takes");
				return;
			}
			Session ss = sess.get(c.session);
			int seq = ++ss.seq;
			if (!send(ss, Proto.sendText(seq, c.name, null, text))) return;
			History.Msg m = new History.Msg();
			m.kind = History.Kind.TEXT;
			m.out = m.partial = true;
			m.text = text;
			m.from = me();
			c.hist.add(m);
			addPending(ss, seq, PKind.TEXT, active, m.id, false);
			changed();
		});
	}

	public void sendLocation(double lat, double lon, double acc, String address) {
		post(() -> {
			Channel c = ready();
			if (c == null) return;
			Session ss = sess.get(c.session);
			int seq = ++ss.seq;
			if (!send(ss, Proto.sendLocation(seq, c.name, null, lat, lon, acc, address))) return;
			History.Msg m = new History.Msg();
			m.kind = History.Kind.LOCATION;
			m.out = m.partial = true;
			m.lat = lat;
			m.lon = lon;
			m.acc = acc;
			m.text = address == null ? "" : address;
			m.from = me();
			c.hist.add(m);
			addPending(ss, seq, PKind.LOCATION, active, m.id, false);
			changed();
		});
	}

	/** full and thumb are JPEGs; w x h is the full image's size. */
	public void sendImage(byte[] full, byte[] thumb, int w, int h) {
		post(() -> {
			Channel c = ready();
			if (c == null) return;
			if (imgFull != null) {
				toast("an image is still being sent");
				return;
			}
			Session ss = sess.get(c.session);
			int seq = ++ss.seq;
			if (!send(ss, Proto.sendImage(seq, c.name, null, w, h, thumb.length, full.length))) return;
			imgFull = full;
			imgThumb = thumb;
			imgW = w;
			imgH = h;
			imgChannel = active;
			addPending(ss, seq, PKind.IMAGE, active, 0, false);
		});
	}

	private void imageSent(Session ss, boolean ok, long imageId, String err) {
		if (imgFull == null) return;
		if (ok && imageId != 0) {
			sendBin(ss, Proto.binHeader(Proto.BIN_IMAGE, imageId, Proto.IMAGE_THUMB), imgThumb);
			sendBin(ss, Proto.binHeader(Proto.BIN_IMAGE, imageId, Proto.IMAGE_FULL), imgFull);
			History h = chan.get(imgChannel).hist;
			History.Msg m = new History.Msg();
			m.kind = History.Kind.IMAGE;
			m.out = true;
			m.w = imgW;
			m.h = imgH;
			m.from = me();
			h.add(m);
			try (FileOutputStream f = new FileOutputStream(h.image(m.id, true))) {
				f.write(imgThumb);
			} catch (IOException ignored) {
			}
			try (FileOutputStream f = new FileOutputStream(h.image(m.id, false))) {
				f.write(imgFull);
			} catch (IOException ignored) {
			}
		} else {
			toast("image not sent: " + (err.isEmpty() ? "refused" : err));
		}
		imgFull = imgThumb = null;
	}

	/* ---------- replay, modes, selection ---------- */

	private void stopPlaybackNow() {
		if (playing) {
			pf.playFlush();
			finishItem(false);
		}
	}

	public void stopPlayback() {
		post(this::stopPlaybackNow);
	}

	public void replay(int channel, long msgId) {
		post(() -> replayNow(channel, msgId));
	}

	private void replayNow(int channel, long msgId) {
		if (channel < 0 || channel >= chan.size()) return;
		History h = chan.get(channel).hist;
		History.Msg m = h.find(msgId);
		if (m == null || m.kind != History.Kind.VOICE || m.partial) return;
		Item it = new Item();
		it.replay = true;
		it.channel = channel;
		it.msgId = msgId;
		it.from = m.from;
		try {
			it.file = History.loadVoice(h.voice(msgId));
		} catch (IOException e) {
			toast("cannot read that voice message");
			return;
		}
		if (playing && queue.get(0).replay) stopPlaybackNow(); /* a new replay replaces the one on air */
		queue.add(playing ? 1 : 0, it);
		changed();
	}

	public void replayLast() {
		post(() -> {
			if (chan.isEmpty()) return;
			List<History.Msg> v = chan.get(active).hist.msgs;
			for (int k = v.size() - 1; k >= 0; k--) {
				History.Msg m = v.get(k);
				if (m.kind == History.Kind.VOICE && !m.out && !m.partial) {
					replayNow(active, m.id);
					return;
				}
			}
			toast("no message to replay");
		});
	}

	private boolean mayConnect() {
		return !offline && !sess.isEmpty() && !sess.get(0).refused && cfg.hasAccount();
	}

	/** Closes the session on purpose: a channel switch, or going offline. */
	private void leave() {
		Session ss = sess.get(0);
		if (tx != Tx.IDLE) txAbort(null);
		if (ss.ws != null) {
			Ws w = ss.ws;
			ss.ws = null;
			w.close();
		}
		for (Pending p : ss.pend) {
			if ((p.kind == PKind.TEXT || p.kind == PKind.LOCATION) && p.channel >= 0) {
				History.Msg m = chan.get(p.channel).hist.find(p.ref);
				if (m != null && m.partial) {
					m.partial = false;
					chan.get(p.channel).hist.commit(m);
				}
			}
			if (p.kind == PKind.IMAGE) imgFull = imgThumb = null;
		}
		ss.pend.clear();
		for (Rx r : new ArrayList<>(rx)) rxEnd(r);
		for (int k : ss.chans) {
			chan.get(k).online = false;
			chan.get(k).usersOnline = 0;
		}
		ss.kicked = false;
		ss.backoffMs = 0;
		ss.state = SState.OFF;
		changed();
	}

	public void setOffline(boolean off) {
		post(() -> {
			if (sess.isEmpty() || off == offline) return;
			offline = off;
			Session ss = sess.get(0);
			ss.error = "";
			if (off) {
				leave();
				toast("offline: the account is free for another client");
			} else {
				ss.lastKick = 0;
				if (mayConnect()) connect(ss);
				toast("going online");
			}
		});
	}

	public void selectChannel(int k) {
		post(() -> {
			if (k < 0 || k >= chan.size() || tx != Tx.IDLE) return;
			if (k != active && !sess.isEmpty()) {
				/* join the other channel on the same session */
				Session ss = sess.get(0);
				leave();
				ss.chans.clear();
				ss.chans.add(k);
				ss.error = "";
				if (mayConnect()) connect(ss);
			}
			active = k;
			chan.get(k).unread = 0;
			changed();
		});
	}

	public void cycleSpeed() {
		post(() -> {
			speed = speed < 1.1 ? 1.25 : speed < 1.4 ? 1.5 : 1.0;
			toast(String.format(Locale.ROOT, "replay speed %.2gx", speed));
		});
	}

	public void toggleBusy() {
		post(() -> {
			busy = !busy;
			toast(busy ? "Busy: incoming voice is kept, not played" : "Available");
			if (!busy) {
				/* play the messages queued while busy, in order */
				for (int i = busyQ.size() - 1; i >= 0; i--) {
					long[] e = busyQ.get(i);
					int ch = (int) e[0];
					long mid = e[1];
					if (ch >= 0 && ch < chan.size()) {
						History.Msg mm = chan.get(ch).hist.find(mid);
						Channel cc = chan.get(ch);
						if (mm != null && !mm.partial && cc.unread > 0) cc.unread--;
					}
					replayNow(ch, mid);
				}
				busyQ.clear();
			}
			changed();
		});
	}

	public void toggleMute() {
		post(() -> {
			if (chan.isEmpty()) return;
			Channel c = chan.get(active);
			c.muted = !c.muted;
			toast(c.name + (c.muted ? " muted" : " unmuted"));
		});
	}

	public void toggleVox() {
		post(() -> {
			vox = !vox;
			pf.captureVox(vox);
			toast(vox ? "VOX on: talking opens the channel" : "VOX off");
		});
	}

	/** A line typed in: a text, or one of the desktop's /commands. */
	public void input(String line) {
		String l = line.trim();
		if (!l.startsWith("/")) {
			sendText(l);
			return;
		}
		String[] w = l.substring(1).split("\\s+", 2);
		String arg = w.length > 1 ? w[1] : "";
		switch (w[0]) {
		case "replay": replayLast(); break;
		case "busy": toggleBusy(); break;
		case "mute": toggleMute(); break;
		case "vox": toggleVox(); break;
		case "speed": cycleSpeed(); break;
		case "loc": {
			String[] p = arg.split("\\s+", 3);
			try {
				sendLocation(Double.parseDouble(p[0]), Double.parseDouble(p[1]), 0, p.length > 2 ? p[2] : "");
			} catch (RuntimeException e) {
				post(() -> toast("usage: /loc LAT LON [ADDRESS]"));
			}
			break;
		}
		default:
			post(() -> toast("/replay  /busy  /mute  /vox  /speed  /loc LAT LON [ADDRESS]"));
		}
	}

	/* ---------- the tick ---------- */

	private void tick() {
		long now = System.currentTimeMillis();
		for (Session ss : sess) {
			if (ss.state == SState.RETRY && now >= ss.retryAt) connect(ss);
			if (ss.ws != null && (ss.state == SState.LOGON || ss.state == SState.ONLINE)) {
				if (now - Math.max(ss.lastRx, ss.ws.lastRx()) > DEAD_AFTER_MS) {
					drop(ss, "no answer from the server");
				} else if (now - ss.lastPing > PING_EVERY_MS) {
					ss.lastPing = now;
					if (!ss.ws.send(Ws.PING, new byte[] {'s', 'y', 'n'}, null)) drop(ss, "connection lost");
				}
			}
		}
		if (tx != Tx.IDLE && now - txStarted > cfg.maxTalkSeconds * 1000L) {
			toast("talk time limit (" + cfg.maxTalkSeconds + " s) reached");
			if (txVox) {
				txFinish();
			} else {
				txReleased = true;
				pf.captureEnd();
				txStarted = now;
			}
		}
		playerTick();
		for (Map.Entry<Long, long[]> e : new ArrayList<>(imagesIn.entrySet())) {
			if (now - e.getValue()[2] > 120000) {
				History.Msg m = chan.get((int) e.getValue()[0]).hist.find(e.getValue()[1]);
				if (m != null) {
					m.partial = false;
					chan.get((int) e.getValue()[0]).hist.commit(m);
				}
				imagesIn.remove(e.getKey());
			}
		}
		if (!toast.isEmpty() && now >= toastUntil) {
			toast = "";
			changed();
		}
		if (tx != Tx.IDLE || playing) pf.changed(); /* meters and clocks */
	}

	/* ---------- what the UI reads ---------- */

	public static final class ChannelView {
		public String name, state, error;
		public boolean online, muted;
		public int users, unread;
	}

	public static final class View {
		public boolean configured;
		public String user, server, toast;
		public List<ChannelView> channels = new ArrayList<>();
		public int active;
		public Talk talk;
		public boolean busy, vox, offline;
		public double speed;
		public int queue;
		/* transmitting */
		public String txDest;
		public long txElapsed;
		public int txLevel;
		public boolean txOpening;
		/* on air */
		public String rxName;
		public boolean rxReplay;
		public String rxChannel;
		public int rxLevel;
		public long rxElapsed, playingMsgId = -1;
		public long historyVersion;
		/** Zello refused the account: "invalid password" and the like */
		public String authError;
	}

	public synchronized View snapshot() {
		View v = new View();
		v.configured = cfg.hasAccount();
		v.user = cfg.username;
		v.server = cfg.server;
		v.toast = toast;
		v.active = active;
		v.busy = busy;
		v.offline = offline;
		v.vox = vox;
		v.speed = speed;
		v.queue = Math.max(0, queue.size() - (playing ? 1 : 0));
		v.historyVersion = historyVersion;
		v.authError = authError;
		for (int k = 0; k < chan.size(); k++) {
			Channel c = chan.get(k);
			ChannelView cv = new ChannelView();
			cv.name = c.name;
			cv.online = c.online;
			cv.muted = c.muted;
			cv.users = c.usersOnline;
			cv.unread = c.unread;
			cv.state = stateLabel(k);
			cv.error = sess.get(c.session).error;
			v.channels.add(cv);
		}
		if (tx == Tx.STARTING) v.talk = Talk.CONNECTING;
		else if (tx == Tx.SENDING) v.talk = Talk.SENDING;
		else if (playing) v.talk = Talk.RECEIVING;
		else if (chan.isEmpty() || !chan.get(active).online) v.talk = Talk.DISABLED;
		else v.talk = Talk.IDLE;
		if (tx != Tx.IDLE) {
			v.txDest = chan.get(txChannel).name;
			v.txElapsed = System.currentTimeMillis() - txStarted;
			v.txLevel = Math.max(txLevel, pf.captureLevel());
			v.txOpening = tx == Tx.STARTING;
		}
		if (playing) {
			Item it = queue.get(0);
			v.rxName = it.from;
			v.rxReplay = it.replay;
			v.rxChannel = chan.get(it.channel).name;
			v.rxLevel = pf.playLevel();
			v.rxElapsed = System.currentTimeMillis() - it.began;
			v.playingMsgId = it.msgId;
		}
		return v;
	}

	/** A copy of a channel's messages, newest last. */
	public synchronized List<History.Msg> history(int k) {
		List<History.Msg> out = new ArrayList<>();
		if (k < 0 || k >= chan.size()) return out;
		for (History.Msg m : chan.get(k).hist.msgs) {
			History.Msg c = new History.Msg();
			c.id = m.id;
			c.ts = m.ts;
			c.kind = m.kind;
			c.out = m.out;
			c.played = m.played;
			c.partial = m.partial;
			c.from = m.from;
			c.forwho = m.forwho;
			c.text = m.text;
			c.durationMs = m.durationMs;
			c.w = m.w;
			c.h = m.h;
			c.lat = m.lat;
			c.lon = m.lon;
			c.acc = m.acc;
			out.add(c);
		}
		return out;
	}

	public synchronized File imageFile(int k, long id, boolean thumb) {
		return k < 0 || k >= chan.size() ? null : chan.get(k).hist.image(id, thumb);
	}

	private String stateLabel(int k) {
		Session ss = sess.get(chan.get(k).session);
		if (!ss.chans.contains(k)) return "NOT JOINED";
		switch (ss.state) {
		case OFF: return offline ? "OFFLINE" : ss.refused ? "REFUSED" : "OFF";
		case CONNECTING: return "CONNECTING";
		case LOGON: return "SIGNING IN";
		case ONLINE: return chan.get(k).online ? "ONLINE" : "WAITING";
		case RETRY: return "RETRY " + Math.max(0, (ss.retryAt - System.currentTimeMillis() + 999) / 1000) + "s";
		}
		return "";
	}

	/** Runs a task on the loop and waits for it (stop() needs the loop's final state). */
	private static final class CountDownLatchWait {
		static void run(ScheduledExecutorService ex, Runnable r) {
			java.util.concurrent.CountDownLatch done = new java.util.concurrent.CountDownLatch(1);
			try {
				ex.execute(() -> {
					try {
						r.run();
					} finally {
						done.countDown();
					}
				});
				done.await(3, TimeUnit.SECONDS);
			} catch (Exception ignored) {
			}
		}
	}
}
