/* ------------------------------------------------------------------------
 *   The core, a Java port of the desktop's app.c: one session with backoff
 *   speaking SYN-CAST protocol v1 (hello with the member token or an invite
 *   code, talk/over, say, pic, loc, presence), the talk button (IDLE,
 *   CONNECTING, SENDING, RECEIVING, DISABLED), incoming streams with loss
 *   marks, the playback queue, history, texts, pictures and locations.
 *
 *   One session carries every channel: hello joins them all at once, and
 *   the selected channel is where what you send goes. The OFFLINE switch
 *   closes the session and keeps the token. If another session of this
 *   member and device replaces this one (close 4003), it takes the place
 *   straight back once; a second time within two minutes it goes offline
 *   instead of fighting.
 *
 *   Everything sent goes to the whole selected channel; a direct message
 *   that arrives for this member is shown there, marked private.
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
		/** A small JPEG cut from a received picture (JPEG, PNG or WebP bytes), or null to show the picture itself. */
		byte[] thumbnail(byte[] picture);
		void notify(String title, String text);
		boolean focused();
		void changed();
		void log(String line);
	}

	public enum Talk { IDLE, CONNECTING, SENDING, RECEIVING, DISABLED }

	private static final int PING_EVERY_MS = 25000;
	/* the server pings every 20 s; the tests shorten this with -Dsyncast.deadAfterMs */
	private static final long DEAD_AFTER_MS = Long.getLong("syncast.deadAfterMs", 70000L);
	private static final int LIVE_PREBUFFER_PACKETS = 3, LIVE_PREBUFFER_MS = 400;
	private static final int FEED_AHEAD = Opus.RATE / 2;
	private static final int HELD_MAX = 50;
	private static final long KICK_WINDOW_MS = 120000;
	private static final int HISTORY_FETCH = 50;   /* G193: lines asked for per conversation on sign-in */

	/* ---------- state ---------- */

	private enum SState { OFF, CONNECTING, LOGON, ONLINE, RETRY }

	private enum PKind { HELLO, TALK, OVER, SAY, PIC, LOC, PRESENCE, ROOMS, HISTORY }

	private static final class Pending {
		int seq;
		PKind kind;
		int channel;
		long ref;
		boolean invite;   /* HELLO: sent with the invite code */
	}

	private final class Session {
		final int idx;
		final List<Integer> chans = new ArrayList<>();
		SState state = SState.OFF;
		Ws ws;
		int seq;
		long retryAt, lastRx, lastPing;
		int backoffMs;
		String error = "";
		boolean wasOnline;
		boolean refused;   /* sign-in refused: no retries until the account is fixed */
		boolean kicked;    /* another session of this member and device replaced this one */
		long lastKick;
		final List<Pending> pend = new ArrayList<>();

		Session(int idx) {
			this.idx = idx;
		}
	}

	public static final class Channel {
		public final String name;
		int session;
		public boolean online, muted;
		/* G192: the room's own name from the server ("G192 rooms bar"), and gone: closed or taken away, so no tab
		   (its history stays on the phone) */
		public String title = "";
		boolean gone;
		/* G193: a direct-message conversation (dm-A-B) has the other member here; a room has "" */
		public String peer = "";
		boolean fetched;   /* the server's history asked for since this session signed in */
		public final List<String> who = new ArrayList<>(); /* members online in it, by name */
		public int unread;
		public String error = "";
		final History hist;

		Channel(String name, History hist) {
			this.name = name;
			this.hist = hist;
		}

		boolean add(String member) {
			for (String w : who) if (w.equalsIgnoreCase(member)) return false;
			who.add(member);
			return true;
		}

		boolean remove(String member) {
			for (int i = 0; i < who.size(); i++) {
				if (who.get(i).equalsIgnoreCase(member)) {
					who.remove(i);
					return true;
				}
			}
			return false;
		}
	}

	private static final class Rx {
		int session, channel;
		long streamId, lastPid = -1;
		String from, forwho;   /* forwho: this member, on a direct message */
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
	private final File cfgFile;
	private final Platform pf;
	private String member = "";   /* who the server says we are (the hello answer) */
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

	private Tx tx = Tx.IDLE;
	private boolean txReleased, txEndSeen, txVox, txServerOver;
	private int txChannel, txStartSeq;
	private long txStreamId, txMsgId, txStarted;
	private final List<byte[]> txHeld = new ArrayList<>();
	private History.VoiceWriter txFile;
	private int txLevel;

	private final Map<Long, long[]> imagesIn = new java.util.HashMap<>(); /* blob -> {channel, msgId, when} */
	private byte[] imgFull, imgThumb;
	private int imgW, imgH, imgChannel;

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

	/** cfgFile may be null: the token an invite earns is then kept for this run only. */
	public Engine(Config cfg, File dataDir, File cfgFile, Platform pf) {
		this.cfg = cfg;
		this.dataDir = dataDir;
		this.cfgFile = cfgFile;
		this.pf = pf;
		this.vox = cfg.vox;
		this.member = cfg.member;
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

	/** A channel opened and carried by the one session. -1 when it cannot be. */
	private int channelAdd(String name) {
		if (chan.size() >= 64) return -1;
		try {
			History h = new History(dataDir, name);
			h.prune(cfg.historyDays, (long) cfg.historyMb << 20);
			chan.add(new Channel(name, h));
		} catch (IOException e) {
			pf.log("history for " + name + ": " + e.getMessage());
			return -1;
		}
		sess.get(0).chans.add(chan.size() - 1);
		return chan.size() - 1;
	}

	/** G193: a direct-message conversation's id, dm-A-B with the two member ids sorted (the server's own name). */
	static String dmName(String a, String b) {
		String x = a.toLowerCase(Locale.ROOT), y = b.toLowerCase(Locale.ROOT);
		return x.compareTo(y) <= 0 ? "dm-" + x + "-" + y : "dm-" + y + "-" + x;
	}

	/** A member id the server takes: lower case letters, digits, _ and . (no -, so dm-A-B reads back). */
	static boolean memberOk(String id) {
		if (id == null || id.isEmpty() || id.length() > 64) return false;
		for (char c : id.toCharArray()) {
			if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.')) return false;
		}
		return true;
	}

	/** G193: the tab of direct messages with this member, made when the first one comes or is opened. -1 when it cannot be. */
	private int dmChannel(String peer) {
		String p = peer.toLowerCase(Locale.ROOT);
		for (int k = 0; k < chan.size(); k++) if (chan.get(k).peer.equals(p)) return k;
		if (!memberOk(p) || p.equalsIgnoreCase(me())) return -1;
		int ch = channelAdd(dmName(me(), p));
		if (ch < 0) return -1;
		Channel c = chan.get(ch);
		c.peer = p;
		c.title = "@" + p;
		Session ss = sess.get(c.session);
		c.online = ss.state == SState.ONLINE;
		if (c.online) fetchHistory(ss, ch);
		return ch;
	}

	/** G193: the direct-message tabs kept on the phone from before (their history folders), back after a restart. */
	private void dmRestore() {
		String mine = cfg.member.toLowerCase(Locale.ROOT);
		File[] dirs = dataDir.listFiles();
		if (mine.isEmpty() || dirs == null) return;
		java.util.Arrays.sort(dirs);
		for (File d : dirs) {
			String[] w = d.getName().split("-");
			if (!d.isDirectory() || w.length != 3 || !w[0].equals("dm")) continue;
			String peer = w[1].equals(mine) ? w[2] : w[2].equals(mine) ? w[1] : "";
			if (memberOk(peer)) dmChannel(peer);
		}
	}

	/** What a frame is addressed to: a room by its channel, a direct message by the member. */
	private static String toChannel(Channel c) {
		return c.peer.isEmpty() ? c.name : null;
	}

	private static String toMember(Channel c) {
		return c.peer.isEmpty() ? null : c.peer;
	}

	/** G193: ask the server for the last lines of a room or a direct-message conversation (what was missed while away). */
	private void fetchHistory(Session ss, int ch) {
		Channel c = chan.get(ch);
		if (c.fetched || ss.state != SState.ONLINE) return;
		int seq = ++ss.seq;
		if (send(ss, Proto.history(seq, toChannel(c), toMember(c), HISTORY_FETCH, null))) {
			c.fetched = true;
			addPending(ss, seq, PKind.HISTORY, ch, 0, false);
		}
	}

	/** G193: the server's lines merged into the phone's own history: texts and locations it does not hold yet, in time order. */
	private void mergeHistory(int ch, Object messages) {
		if (!(messages instanceof List) || ch < 0 || ch >= chan.size()) return;
		Channel c = chan.get(ch);
		int added = 0, unseen = 0;
		for (Object v : (List<?>) messages) {
			if (!(v instanceof Map)) continue;
			@SuppressWarnings("unchecked")
			Map<String, Object> l = (Map<String, Object>) v;
			String sid = Json.str(l, "id"), kind = Json.str(l, "kind"), from = Json.str(l, "from");
			if (sid.isEmpty() || c.hist.hasSid(sid)) continue;
			long ts = isoMs(Json.str(l, "at"));
			History.Msg m = new History.Msg();
			if (kind.equals("text")) {
				m.kind = History.Kind.TEXT;
				m.text = Json.str(l, "text");
				History.Msg had = c.hist.sameText(from, m.text, ts);
				if (had != null) {
					/* heard live before ids were kept: the same line, now with its id */
					had.sid = sid;
					c.hist.commit(had);
					continue;
				}
			} else if (kind.equals("loc")) {
				m.kind = History.Kind.LOCATION;
				m.lat = Json.num(l, "lat");
				m.lon = Json.num(l, "lon");
				m.acc = Json.num(l, "accuracy");
				m.text = Json.str(l, "address");
			} else {
				continue; /* voice and pictures need their files: what was missed of them stays on the server */
			}
			m.sid = sid;
			m.ts = ts;
			m.from = from;
			m.forwho = Json.str(l, "to");
			m.out = from.equalsIgnoreCase(me());
			c.hist.addPast(m);
			added++;
			if (!m.out) unseen++;
		}
		if (added > 0) {
			if (ch != active) c.unread += unseen;
			pf.log("history: " + added + " line" + (added == 1 ? "" : "s") + " fetched for " + c.name);
		}
	}

	/** An ISO time (2026-10-31T02:40:12Z) in ms; now when it cannot be read. */
	static long isoMs(String at) {
		try {
			return java.time.Instant.parse(at).toEpochMilli();
		} catch (RuntimeException e) {
			try {
				return java.time.OffsetDateTime.parse(at).toInstant().toEpochMilli();
			} catch (RuntimeException e2) {
				return System.currentTimeMillis();
			}
		}
	}

	/** G193: open the direct messages with a member (a held tab's name, or /msg NAME) and go to them. */
	public void openDm(String peer) {
		post(() -> {
			String p = peer == null ? "" : peer.trim().toLowerCase(Locale.ROOT);
			if (p.startsWith("@")) p = p.substring(1);
			if (p.equalsIgnoreCase(me())) {
				toast("that is you");
				return;
			}
			int ch = dmChannel(p);
			if (ch < 0) {
				toast("no member " + peer + " to message");
				return;
			}
			if (tx == Tx.IDLE) {
				active = ch;
				chan.get(ch).unread = 0;
			}
			changed();
		});
	}

	public void start() {
		post(() -> {
			dataDir.mkdirs();
			sess.add(new Session(0));
			for (String name : cfg.channels) channelAdd(name);
			dmRestore();
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
		return !member.isEmpty() ? member : !cfg.member.isEmpty() ? cfg.member : "you";
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

	private void addPending(Session ss, int seq, PKind k, int channel, long ref, boolean invite) {
		Pending p = new Pending();
		p.seq = seq;
		p.kind = k;
		p.channel = channel;
		p.ref = ref;
		p.invite = invite;
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
		final String url = cfg.server, pem = cfg.certPem;
		connector.execute(() -> {
			try {
				Ws w = Ws.connect(url, pem, 10000);
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
					int code = ws.closeCode();
					if (code == 4003) {
						ss.kicked = true; /* a newer session of this member and device */
						drop(ss, "replaced by another session");
					} else if (code == 4001 || code == 4004) {
						ss.refused = true;
						drop(ss, code == 4004 ? "token revoked" : "token refused");
					} else {
						drop(ss, why);
					}
				});
			}
		});
		hello(ss);
	}

	/** hello: an invite code the first time (the answer carries the token to keep), the token after. */
	private void hello(Session ss) {
		List<String> names = null;
		if (!cfg.channels.isEmpty()) {
			names = new ArrayList<>();
			for (int k : ss.chans) if (chan.get(k).peer.isEmpty()) names.add(chan.get(k).name);
		}
		boolean invite = !cfg.invite.isEmpty();
		int seq = ++ss.seq;
		addPending(ss, seq, PKind.HELLO, -1, 0, invite);
		send(ss, Proto.hello(seq, cfg.token, cfg.invite, cfg.device, names));
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
			if ((p.kind == PKind.SAY || p.kind == PKind.LOC) && p.channel >= 0) {
				History.Msg m = chan.get(p.channel).hist.find(p.ref);
				if (m != null && m.partial) {
					m.partial = false;
					chan.get(p.channel).hist.commit(m);
					event(p.channel, "not delivered (connection lost)");
				}
			}
			if (p.kind == PKind.PIC) imgFull = imgThumb = null;
		}
		ss.pend.clear();
		for (int k : ss.chans) {
			Channel c = chan.get(k);
			c.online = false;
			c.fetched = false;
			c.who.clear();
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
				/* replaced twice in a row: another SYN-CAST keeps taking this device's place, so give way */
				ss.lastKick = 0;
				ss.state = SState.OFF;
				offline = true;
				ss.error = "another " + cfg.device + " session keeps replacing this one";
				for (int k : ss.chans) event(k, "replaced by another session: went offline");
				toast("another SYN-CAST is signed in as " + me() + " on this device: went offline");
				return;
			}
			ss.lastKick = now;
			ss.backoffMs = 0;
			pf.log("another session replaced this one as " + me() + "/" + cfg.device + ": taking the place back");
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

	/** Where an event belongs: its channel, or for a direct message (no channel, "to" set) the tab of direct messages
	    with the other member (G193): the sender, or the receiver when it is this member's own line from another device. */
	private int eventChannel(Session ss, Map<String, Object> o) {
		String chName = Json.str(o, "channel");
		if (!chName.isEmpty()) return channelIn(ss, chName);
		String to = Json.str(o, "to"), from = Json.str(o, "from");
		if (to.isEmpty()) return -1;
		String peer = from.isEmpty() || from.equalsIgnoreCase(me()) ? to : from;
		return dmChannel(peer);
	}

	private void onJson(Session ss, String text) {
		Map<String, Object> o = Json.object(text);
		if (o == null) return;
		String type = Json.str(o, "type");
		if (type.equals("ok") || type.equals("error")) {
			if (o.get("seq") instanceof Double) onReply(ss, o);
			else onServerError(ss, o);
			return;
		}
		int ch = eventChannel(ss, o);
		String from = Json.str(o, "from"), to = Json.str(o, "to");
		switch (type) {
		case "talk":
			rxStart(ss, ch, o, from, to);
			break;
		case "over": {
			long stream = Json.lng(o, "stream");
			for (Rx r : new ArrayList<>(rx)) {
				if (r.session == ss.idx && r.streamId == stream) rxEnd(r);
			}
			if (tx == Tx.SENDING && txStreamId == stream && !Json.bool(o, "replay")) {
				/* the server ended our floor (a stall, the 5 minutes, a flood) */
				txServerOver = true;
				String why = Json.str(o, "reason");
				txAbort(why.isEmpty() ? "ended by the server" : why);
			}
			break;
		}
		case "say":
			if (ch >= 0) {
				History.Msg m = new History.Msg();
				m.kind = History.Kind.TEXT;
				m.text = Json.str(o, "text");
				m.from = from;
				m.forwho = to;
				m.sid = Json.str(o, "message");
				m.out = from.equalsIgnoreCase(me());
				chan.get(ch).hist.add(m);
				if (ch != active && !m.out) chan.get(ch).unread++;
				if (!chan.get(ch).muted && !m.out) tone("ALERT");
				if (!m.out) pf.notify(from + (to.isEmpty() ? " in " + Rooms.title(chan.get(ch).name, chan.get(ch).title) : " (direct)"), m.text);
			}
			break;
		case "pic":
			if (ch >= 0) {
				History.Msg m = new History.Msg();
				m.kind = History.Kind.IMAGE;
				m.partial = true;
				m.w = (int) Json.lng(o, "width");
				m.h = (int) Json.lng(o, "height");
				m.from = from;
				m.forwho = to;
				chan.get(ch).hist.add(m);
				imagesIn.put(Json.lng(o, "blob"), new long[] {ch, m.id, System.currentTimeMillis()});
			}
			break;
		case "loc":
			if (ch >= 0) {
				History.Msg m = new History.Msg();
				m.kind = History.Kind.LOCATION;
				m.lat = Json.num(o, "lat");
				m.lon = Json.num(o, "lon");
				m.acc = Json.num(o, "accuracy");
				m.text = Json.str(o, "address");
				m.from = from;
				m.forwho = to;
				m.sid = Json.str(o, "message");
				chan.get(ch).hist.add(m);
				tone("ALERT");
			}
			break;
		case "join":
			if (ch >= 0 && chan.get(ch).add(Json.str(o, "member"))) event(ch, Json.str(o, "member") + " joined");
			break;
		case "leave":
			if (ch >= 0 && chan.get(ch).remove(Json.str(o, "member"))) event(ch, Json.str(o, "member") + " left");
			break;
		case "rooms":
			applyRooms(ss, o.get("rooms"));
			break;
		case "presence":
			pf.log(Json.str(o, "member") + " is " + Json.str(o, "status")
				+ (Json.str(o, "text").isEmpty() ? "" : ": " + Json.str(o, "text")));
			break;
		default:
			break;
		}
		changed();
	}

	/** An error with no seq: the server's own word (a revoked token, a frame before hello, a bad packet). */
	private void onServerError(Session ss, Map<String, Object> o) {
		String code = Json.str(o, "code"), text = Json.str(o, "text");
		if (code.equals("revoked")) {
			ss.refused = true;
			drop(ss, "token revoked");
		} else if (code.equals("not_logged_in")) {
			drop(ss, "not signed in");
		} else {
			toast("server: " + (text.isEmpty() ? code : text));
		}
	}

	/** G192: the server's list of the member's rooms (the rooms answer or event): each becomes a tab, with who is
	    inside; a room no longer listed (a closed ticket's) loses its tab. An older server answers unknown_type and
	    nothing here runs: the tabs stay the hello's. */
	private void applyRooms(Session ss, Object rooms) {
		if (!(rooms instanceof List)) return;
		java.util.Set<String> listed = new java.util.HashSet<>();
		for (Object v : (List<?>) rooms) {
			if (!(v instanceof Map)) continue;
			@SuppressWarnings("unchecked")
			Map<String, Object> r = (Map<String, Object>) v;
			String id = Json.str(r, "channel");
			if (id.isEmpty()) continue;
			listed.add(id.toLowerCase(Locale.ROOT));
			int ch = -1;
			for (int k = 0; k < chan.size(); k++) if (chan.get(k).name.equalsIgnoreCase(id)) ch = k;
			if (ch < 0) {
				if (id.startsWith("dm-")) continue;
				ch = channelAdd(id);
				if (ch < 0) continue;
				event(ch, "room opened");
			}
			Channel c = chan.get(ch);
			if (!Json.str(r, "name").isEmpty()) c.title = Json.str(r, "name");
			c.gone = false;
			c.online = !r.containsKey("joined") || Json.bool(r, "joined");
			if (!c.online && ss.state == SState.ONLINE) {
				/* a session that named its channels (an older config): the room is joined here, never typed */
				c.online = send(ss, Proto.join(++ss.seq, id));
			}
			Object online = r.get("online");
			if (online instanceof List) {
				c.who.clear();
				for (Object w : (List<?>) online) if (w instanceof String) c.who.add((String) w);
			}
			if (c.online) fetchHistory(ss, ch);
		}
		for (int k : ss.chans) {
			Channel c = chan.get(k);
			if (!c.peer.isEmpty()) continue; /* G193: a direct-message tab is the phone's, never in the rooms list */
			if (!listed.contains(c.name.toLowerCase(Locale.ROOT)) && !c.gone) {
				c.gone = true;
				c.online = false;
				c.who.clear();
				event(k, "room closed");
			}
		}
		if (!chan.isEmpty() && chan.get(active).gone && tx == Tx.IDLE) {
			List<Integer> o = order();
			if (!o.isEmpty()) active = o.get(0);
		}
		changed();
	}

	/** A ticket's room: its id is g and the ticket's number (J42's channel_sync_tickets). */
	static boolean isTicketRoom(String id) {
		return Rooms.ticket(id) >= 0;
	}

	/** G192: the bar's order: the fixed rooms (Lobby, Crew, Alerts, Guests, Meeting Room, the HQs), then ticket rooms by number; closed rooms left out. */
	private List<Integer> order() {
		List<String> ids = new ArrayList<>();
		List<Integer> idx = new ArrayList<>();
		for (int k = 0; k < chan.size(); k++) {
			if (chan.get(k).gone) continue;
			ids.add(chan.get(k).name);
			idx.add(k);
		}
		List<Integer> out = new ArrayList<>();
		for (int r : Rooms.order(ids)) out.add(idx.get(r));
		return out;
	}

	/** The channels the hello answer joined (and any it named that the config did not), with who is in them. */
	private void helloChannels(Session ss, Map<String, Object> o) {
		Object chans = o.get("channels");
		if (chans instanceof List) {
			for (Object v : (List<?>) chans) {
				if (!(v instanceof Map)) continue;
				@SuppressWarnings("unchecked")
				Map<String, Object> zc = (Map<String, Object>) v;
				String id = Json.str(zc, "channel");
				int ch = -1;
				for (int k = 0; k < chan.size(); k++) if (chan.get(k).name.equalsIgnoreCase(id)) ch = k;
				if (ch < 0) ch = channelAdd(id);
				if (ch < 0) continue;
				Channel c = chan.get(ch);
				boolean was = c.online;
				c.online = true;
				c.gone = false;
				if (!Json.str(zc, "name").isEmpty()) c.title = Json.str(zc, "name");
				c.who.clear();
				Object online = zc.get("online");
				if (online instanceof List) {
					for (Object w : (List<?>) online) if (w instanceof String) c.who.add((String) w);
				}
				c.error = "";
				if (!was) event(ch, "connected, " + c.who.size() + " online");
			}
		}
		Object refused = o.get("refused");
		if (refused instanceof List) {
			for (Object v : (List<?>) refused) {
				if (!(v instanceof Map)) continue;
				@SuppressWarnings("unchecked")
				Map<String, Object> r = (Map<String, Object>) v;
				int ch = channelIn(ss, Json.str(r, "channel"));
				if (ch >= 0) {
					chan.get(ch).error = Json.str(r, "code");
					event(ch, "not joined: " + Json.str(r, "code"));
				}
			}
		}
		if (ss.wasOnline) tone("SUCCESS");
		ss.wasOnline = true;
	}

	private void onReply(Session ss, Map<String, Object> o) {
		Pending p = takePending(ss, (int) Json.lng(o, "seq"));
		if (p == null) return;
		boolean ok = "ok".equals(Json.str(o, "type"));
		String code = Json.str(o, "code");
		String err = Json.str(o, "text");
		if (!ok && err.isEmpty()) err = code.isEmpty() ? "refused" : code;
		switch (p.kind) {
		case HELLO:
			if (ok) {
				ss.state = SState.ONLINE;
				ss.backoffMs = 0;
				ss.error = "";
				authError = null;
				boolean save = false;
				String who = Json.str(o, "member");
				if (!who.isEmpty()) {
					member = who;
					if (!who.equals(cfg.member)) {
						cfg.member = who;
						save = true;
					}
				}
				String tok = Json.str(o, "token");
				if (!tok.isEmpty()) {
					/* the invite is spent: from now on the token signs in */
					cfg.token = tok;
					cfg.invite = "";
					save = true;
					pf.log("invite accepted: member token " + Json.str(o, "token_id") + " kept");
				}
				if (save && cfgFile != null) {
					try {
						cfg.save(cfgFile);
					} catch (IOException e) {
						pf.log("cannot write " + cfgFile + ": the token would be lost on the next start");
					}
				}
				helloChannels(ss, o);
				/* G193: the direct-message tabs ride on the session; a DM waiting gets its tab; each joined
				   conversation asks the server for what was missed */
				if (o.get("held") instanceof List) {
					for (Object h : (List<?>) o.get("held")) {
						if (h instanceof Map) {
							@SuppressWarnings("unchecked")
							Map<String, Object> hm = (Map<String, Object>) h;
							dmChannel(Json.str(hm, "with"));
						}
					}
				}
				for (int k : ss.chans) {
					Channel c = chan.get(k);
					if (!c.peer.isEmpty()) {
						c.online = true;
						c.gone = false;
					}
					if (c.online) fetchHistory(ss, k);
				}
				int rseq = ++ss.seq;
				if (send(ss, Proto.rooms(rseq))) addPending(ss, rseq, PKind.ROOMS, -1, 0, false);
				int held = 0;
				if (o.get("held") instanceof List) {
					for (Object h : (List<?>) o.get("held")) {
						if (h instanceof Map) {
							@SuppressWarnings("unchecked")
							Map<String, Object> hm = (Map<String, Object>) h;
							held += (int) Json.lng(hm, "count");
						}
					}
				}
				if (held > 0) toast(held + " direct message" + (held == 1 ? "" : "s") + " waiting for you");
				pf.log("hello: " + me() + ", session " + Json.str(o, "session"));
			} else {
				String why = "sign-in refused: " + err;
				if (Proto.tokenRefused(code)) {
					/* a bad or spent invite, a revoked or expired token: retrying cannot help */
					authError = err;
					ss.refused = true;
					if (p.invite && !cfg.invite.isEmpty()) {
						cfg.invite = "";
						if (cfgFile != null) {
							try {
								cfg.save(cfgFile);
							} catch (IOException ignored) {
							}
						}
					}
				}
				drop(ss, why);
			}
			break;
		case TALK:
			txStarted(ss, p.seq, ok, Json.lng(o, "stream"), code, err, Json.str(o, "talker"));
			break;
		case OVER:
			if (ok && !Json.str(o, "message").isEmpty()) {
				pf.log("voice message " + Json.str(o, "message") + " saved by the server, " + Json.lng(o, "ms") + " ms");
			}
			break;
		case SAY:
		case LOC: {
			History.Msg m = chan.get(p.channel).hist.find(p.ref);
			if (m != null) {
				m.partial = false;
				if (ok) m.sid = Json.str(o, "message");
				chan.get(p.channel).hist.commit(m);
			}
			if (!ok) {
				event(p.channel, "not delivered: " + err);
				tone("FAIL");
			}
			break;
		}
		case PIC:
			imageSent(ss, ok, Json.lng(o, "upload"), err);
			break;
		case ROOMS:
			if (ok) applyRooms(ss, o.get("rooms"));
			break;
		case HISTORY:
			if (ok) mergeHistory(p.channel, o.get("messages"));
			else pf.log("history refused for " + chan.get(p.channel).name + ": " + err);
			break;
		case PRESENCE:
			if (!ok) pf.log("presence refused: " + err);
			break;
		}
		changed();
	}

	private void onBinaryFrame(Session ss, byte[] d) {
		if (d.length < Proto.HDR) return;
		int kind = d[0];
		long id = Proto.be32(d, 1), seq = Proto.be32(d, 5);
		byte[] data = new byte[d.length - Proto.HDR];
		System.arraycopy(d, Proto.HDR, data, 0, data.length);
		if (kind == Proto.BIN_AUDIO) {
			for (Rx r : rx) {
				if (r.ended || r.session != ss.idx || r.streamId != id) continue;
				if (r.lastPid >= 0 && seq > r.lastPid + 1 && seq - r.lastPid <= 25) {
					/* lost packets: marked so playback covers them with FEC/PLC */
					for (long k = r.lastPid + 1; k < seq; k++) {
						r.pkts.add(new byte[0]);
						if (r.file != null) r.file.write(null);
					}
				}
				r.lastPid = seq;
				r.pkts.add(data);
				if (r.file != null) r.file.write(data);
				return;
			}
		} else if (kind == Proto.BIN_PIC) {
			/* the picture itself, one frame after its pic event: stored whole; the thumbnail is the platform's to cut */
			long[] in = imagesIn.get(id);
			if (in == null) return;
			History h = chan.get((int) in[0]).hist;
			try (FileOutputStream f = new FileOutputStream(h.image(in[1], false))) {
				f.write(data);
			} catch (IOException e) {
				pf.log("image: " + e.getMessage());
			}
			byte[] thumb = pf.thumbnail(data);
			try (FileOutputStream f = new FileOutputStream(h.image(in[1], true))) {
				f.write(thumb != null ? thumb : data);
			} catch (IOException e) {
				pf.log("thumbnail: " + e.getMessage());
			}
			History.Msg m = h.find(in[1]);
			if (m != null) {
				m.partial = false;
				h.commit(m);
				pf.notify(chan.get((int) in[0]).name, m.from + " sent a picture");
			}
			tone("ALERT");
			imagesIn.remove(id);
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
		r.streamId = Json.lng(o, "stream");
		r.from = from;
		r.forwho = forwho;
		r.codec = Proto.Codec.of(o);
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
		if (!send(ss, Proto.talk(seq, toChannel(c), toMember(c), Proto.PTT))) {
			tone("FAIL");
			toast("not connected");
			return false;
		}
		addPending(ss, seq, PKind.TALK, active, 0, false);
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
		txReleased = txEndSeen = txVox = txServerOver = false;
		txLevel = 0;
		changed();
	}

	private void txFinish() {
		Channel c = chan.get(txChannel);
		if (tx == Tx.SENDING) {
			Session ss = sess.get(c.session);
			if (!txServerOver) {
				int seq = ++ss.seq;
				if (send(ss, Proto.over(seq, txStreamId))) addPending(ss, seq, PKind.OVER, txChannel, txMsgId, false);
			}
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

	private void txStarted(Session ss, int seq, boolean ok, long streamId, String code, String err, String talker) {
		if (tx != Tx.STARTING || seq != txStartSeq) {
			if (ok && streamId != 0) {
				/* the button was given up on before the server answered */
				send(ss, Proto.over(++ss.seq, streamId));
			}
			return;
		}
		if (!ok) {
			tone("FAIL");
			toast("cannot talk: " + (code.equals("busy") && !talker.isEmpty() ? talker + " is talking" : err));
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
			if (text.length() > Proto.TEXT_MAX) {
				toast("text longer than the " + Proto.TEXT_MAX + " characters a message takes");
				return;
			}
			Session ss = sess.get(c.session);
			int seq = ++ss.seq;
			if (!send(ss, Proto.say(seq, toChannel(c), toMember(c), text))) return;
			History.Msg m = new History.Msg();
			m.kind = History.Kind.TEXT;
			m.out = m.partial = true;
			m.text = text;
			m.from = me();
			c.hist.add(m);
			addPending(ss, seq, PKind.SAY, active, m.id, false);
			changed();
		});
	}

	public void sendLocation(double lat, double lon, double acc, String address) {
		post(() -> {
			Channel c = ready();
			if (c == null) return;
			Session ss = sess.get(c.session);
			int seq = ++ss.seq;
			if (!send(ss, Proto.loc(seq, toChannel(c), toMember(c), lat, lon, acc, address))) return;
			History.Msg m = new History.Msg();
			m.kind = History.Kind.LOCATION;
			m.out = m.partial = true;
			m.lat = lat;
			m.lon = lon;
			m.acc = acc;
			m.text = address == null ? "" : address;
			m.from = me();
			c.hist.add(m);
			addPending(ss, seq, PKind.LOC, active, m.id, false);
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
			if (!send(ss, Proto.pic(seq, toChannel(c), toMember(c), "image/jpeg", w, h, full.length))) return;
			imgFull = full;
			imgThumb = thumb;
			imgW = w;
			imgH = h;
			imgChannel = active;
			addPending(ss, seq, PKind.PIC, active, 0, false);
		});
	}

	/** The pic answer reserved an upload: the picture goes as one binary frame (kind 2, the upload's id). */
	private void imageSent(Session ss, boolean ok, long upload, String err) {
		if (imgFull == null) return;
		if (ok && upload != 0) {
			sendBin(ss, Proto.binHeader(Proto.BIN_PIC, upload, 0), imgFull);
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
			if ((p.kind == PKind.SAY || p.kind == PKind.LOC) && p.channel >= 0) {
				History.Msg m = chan.get(p.channel).hist.find(p.ref);
				if (m != null && m.partial) {
					m.partial = false;
					chan.get(p.channel).hist.commit(m);
				}
			}
			if (p.kind == PKind.PIC) imgFull = imgThumb = null;
		}
		ss.pend.clear();
		for (Rx r : new ArrayList<>(rx)) rxEnd(r);
		for (int k : ss.chans) {
			chan.get(k).online = false;
			chan.get(k).fetched = false;
			chan.get(k).who.clear();
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
				toast("offline");
			} else {
				ss.lastKick = 0;
				if (mayConnect()) connect(ss);
				toast("going online");
			}
		});
	}

	/** G192: a swipe: the next tab to the right (dir 1) or left (-1), stopping at the ends. */
	public void selectStep(int dir) {
		post(() -> {
			if (tx != Tx.IDLE) return;
			List<Integer> o = order();
			int at = o.indexOf(active);
			int to = at < 0 ? 0 : at + (dir > 0 ? 1 : -1);
			if (to < 0 || to >= o.size()) return;
			active = o.get(to);
			chan.get(active).unread = 0;
			changed();
		});
	}

	/** Every channel is joined already: selecting one only aims what is sent. */
	public void selectChannel(int k) {
		post(() -> {
			if (k < 0 || k >= chan.size() || tx != Tx.IDLE) return;
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
			if (!sess.isEmpty() && sess.get(0).state == SState.ONLINE) {
				Session ss = sess.get(0);
				int seq = ++ss.seq;
				if (send(ss, Proto.presence(seq, busy ? "busy" : "online", null))) addPending(ss, seq, PKind.PRESENCE, -1, 0, false);
			}
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
		case "msg": {
			/* G193: /msg NAME opens the direct messages with NAME; /msg NAME TEXT sends it there too */
			String[] p = arg.trim().split("\\s+", 2);
			if (p[0].isEmpty()) {
				post(() -> toast("usage: /msg NAME [TEXT]"));
				break;
			}
			openDm(p[0]);
			if (p.length > 1) sendText(p[1]);
			break;
		}
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
			post(() -> toast("/msg NAME [TEXT]  /replay  /busy  /mute  /vox  /speed  /loc LAT LON [ADDRESS]"));
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
		/* G192: the room's name to show, who is inside (by member id), a ticket's room */
		public String title;
		public List<String> who = new ArrayList<>();
		public boolean ticket;
		/** G193: a direct-message tab: the other member ("" for a room) */
		public String peer = "";
	}

	public static final class View {
		public boolean configured;
		public String user, server, toast;
		public List<ChannelView> channels = new ArrayList<>();
		public int active;
		/** G192: the tabs, left to right, as indexes into channels (closed rooms have none) */
		public List<Integer> order = new ArrayList<>();
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
		/** the server refused the token or invite: "bad token", "the token has expired" and the like */
		public String authError;
	}

	public synchronized View snapshot() {
		View v = new View();
		v.configured = cfg.hasAccount();
		v.user = me().equals("you") ? "" : me();
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
		v.order = order();
		for (int k = 0; k < chan.size(); k++) {
			Channel c = chan.get(k);
			ChannelView cv = new ChannelView();
			cv.name = c.name;
			cv.online = c.online;
			cv.muted = c.muted;
			cv.users = c.who.size();
			cv.title = Rooms.title(c.name, c.title);
			cv.who.addAll(c.who);
			cv.ticket = isTicketRoom(c.name);
			cv.peer = c.peer;
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
			v.txDest = Rooms.title(chan.get(txChannel).name, chan.get(txChannel).title);
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
			c.sid = m.sid;
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
