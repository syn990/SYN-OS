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
import java.util.Base64;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
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
		/** raw mode: frames come back as PCM through captureRawFrame(), untouched (no gain, no noise suppression), for the Vox filter */
		void captureRaw(boolean on);
		int captureLevel();
		void tone(String meaning);
		/** A small JPEG cut from a received picture (JPEG, PNG or WebP bytes), or null to show the picture itself. */
		byte[] thumbnail(byte[] picture);
		void notify(String title, String text);
		boolean focused();
		void changed();
		void log(String line);
	}

	public enum Talk { IDLE, CONNECTING, SENDING, RECEIVING, DISABLED, TAKING, CONVERTING }

	private static final int PING_EVERY_MS = 25000;
	/* the server pings every 20 s; the tests shorten this with -Dsyncast.deadAfterMs */
	private static final long DEAD_AFTER_MS = Long.getLong("syncast.deadAfterMs", 70000L);
	private static final int LIVE_PREBUFFER_PACKETS = 3, LIVE_PREBUFFER_MS = 400;
	private static final int FEED_AHEAD = Opus.RATE / 2;
	private static final int HELD_MAX = 50;
	private static final long KICK_WINDOW_MS = 120000;
	private static final int HISTORY_FETCH = 50;   /* G193: lines asked for per conversation on sign-in */
	/* G273: the way in from outside is Cloudflare's, so it is checked against the system's trust store; the tests,
	   whose "outside" is the same local server, keep the pinned certificate with -Dsyncast.remotePinned=true */
	private static final boolean REMOTE_PINNED = Boolean.getBoolean("syncast.remotePinned");

	/* ---------- state ---------- */

	private enum SState { OFF, CONNECTING, LOGON, ONLINE, RETRY }

	private enum PKind { HELLO, TALK, OVER, SAY, PIC, LOC, PRESENCE, ROOMS, HISTORY, PORTRAIT, JOIN, LEAVE, VOX, VOXLIST, ROOM }

	private static final class Pending {
		int seq;
		PKind kind;
		int channel;
		long ref;
		boolean invite;   /* HELLO: sent with the invite code */
		String key = "";  /* PORTRAIT: whose */
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
		boolean far;       /* G273: this try (or this connection) is by the way in from outside, not the home address */
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
		public boolean online;
		/** G278: the room is off for this member (saved on the server): nothing plays aloud, no tone, no
		    notification, but every voice message is kept unheard for REPLAY */
		public boolean muted;
		/** switched off here (F4 / the ROOMS page): left for this session, not auto-joined again until switched on */
		public boolean left;
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
		String who = "", face = "", colour = "";   /* G228: shown while it plays: name, portrait key, colour */
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
	private boolean offline;
	private boolean vox;
	private String toast = "";
	private long toastUntil;
	private long historyVersion;
	private String authError;

	/* ---------- the Vox filter: the take, converted into a character's voice by SYN-VOX, then spoken as a file ---------- */
	public static final class Voice {
		public String id = "", name = "", kind = "";
		public boolean ready, building;
		public int speakers;
	}
	private String voxId = "", voxName = "Unfiltered";
	private final List<Voice> voices = new ArrayList<>();
	private long voicesAt;
	private boolean taking, converting;
	/** the boss lands in Crew, the operations room, once per run (2 Nov): the Lobby is the open space */
	private boolean landed;
	private long takeStarted;
	private final java.io.ByteArrayOutputStream take = new java.io.ByteArrayOutputStream();
	private static final int TAKE_MAX_MS = 90000;
	/* the take on its way through the caster (protocol vox): the upload stream, what comes back, when it was sent */
	private long voxStream = -1, voxSentAt;
	private byte[] voxOut;
	private final java.io.ByteArrayOutputStream voxIn = new java.io.ByteArrayOutputStream();
	private static final int VOX_WAIT_MS = 150000;
	/* a file (the converted take) spoken instead of the mic: paced like the desktop's say_tick */
	private short[] sayPcm;
	private int sayN, sayPos, saySent;
	private long sayT0;
	private Opus.Encoder sayEnc;
	private static final int SAY_LEAD_MS = 200;
	/* a converted take refused the floor (someone talking): kept and sent when the floor frees, for up to 45 s */
	private short[] pendSay;
	private int pendN;
	private long pendRetryAt, pendUntil;
	private boolean pendSaid;   /* the "waiting for the floor" line said once per take */

	private Tx tx = Tx.IDLE;
	private boolean txReleased, txEndSeen, txVox, txServerOver;
	private int txChannel, txStartSeq;
	private long txStreamId, txMsgId, txStarted;
	private final List<byte[]> txHeld = new ArrayList<>();
	private History.VoiceWriter txFile;
	private int txLevel;

	/* G228: portraits by member or character id, PNG bytes (empty: none), each asked for once per run */
	private final Map<String, byte[]> portraits = new HashMap<>();
	private final Map<String, String> names = new HashMap<>();   /* member or character id -> the name the server gives (portrait answer) */
	private final Set<String> portraitAsked = new HashSet<>();
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
			speaker(m, l);
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
				keepUnheard = true;
				while (!queue.isEmpty()) {
					playing = true;
					finishItem(false);
				}
				keepUnheard = false;
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
		final boolean far = ss.far && !cfg.remote.isEmpty();
		ss.far = far;
		final String url = far ? cfg.remote : cfg.server, pem = far && !REMOTE_PINNED ? "" : cfg.certPem;
		connector.execute(() -> {
			try {
				Ws w = Ws.connect(url, pem, 10000);
				post(() -> connected(ss, w));
			} catch (IOException e) {
				String why = e.getMessage() != null ? e.getMessage() : e.toString();
				post(() -> {
					if (ss.state == SState.CONNECTING) {
						ss.error = why;
						pf.log("connect" + (far ? " (from outside)" : "") + ": " + why);
						/* G273: the home address and the way in from outside take turns until one answers; the one
						   that answers is kept for the next connection */
						if (!cfg.remote.isEmpty()) ss.far = !far;
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
		send(ss, Proto.hello(seq, cfg.token, cfg.invite, cfg.user, cfg.password, cfg.device, names));
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
		if (type.equals("vox_done")) {
			if (!converting || Json.lng(o, "stream") != voxStream) return;
			converting = false;
			voxStream = -1;
			byte[] got = voxIn.toByteArray();
			voxIn.reset();
			if (!Json.bool(o, "ok") || got.length < 2) {
				tone("FAIL");
				toast(got.length < 2 && Json.bool(o, "ok") ? "no audio came back" : voxWhy(Json.str(o, "code"), Json.str(o, "text"), "vox"));
				changed();
				return;
			}
			short[] pcm = new short[got.length / 2];
			for (int i = 0; i < pcm.length; i++) pcm[i] = (short) ((got[2 * i] & 0xff) | (got[2 * i + 1] << 8));
			pf.log("vox " + voxId + " via the caster: " + pcm.length * 1000 / Opus.RATE + " ms in " + (System.currentTimeMillis() - voxSentAt) + " ms");
			sayBegin(pcm, pcm.length);
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
				speaker(m, o);
				m.sid = Json.str(o, "message");
				m.out = from.equalsIgnoreCase(me());
				chan.get(ch).hist.add(m);
				if (ch != active && !m.out) chan.get(ch).unread++;
				if (!chan.get(ch).muted && !m.out) tone("ALERT");
				if (!m.out && !chan.get(ch).muted) pf.notify(m.who() + (to.isEmpty() ? " in " + Rooms.title(chan.get(ch).name, chan.get(ch).title) : " (direct)"), m.text);
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
				speaker(m, o);
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
				speaker(m, o);
				m.sid = Json.str(o, "message");
				chan.get(ch).hist.add(m);
				if (!chan.get(ch).muted) tone("ALERT");
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
		case "server":
			/* G273: the way in from outside has a new address (a quick tunnel restarted) */
			learnRemote(Json.str(o, "remote"));
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

	/** G273: a new way in from outside, kept in the config (never shown or logged: the address is a door). */
	private void learnRemote(String remote) {
		if (remote.isEmpty() || remote.equals(cfg.remote)) return;
		cfg.remote = remote;
		pf.log("the way in from outside changed");
		if (cfgFile == null) return;
		try {
			cfg.save(cfgFile);
		} catch (IOException e) {
			pf.log("cannot write " + cfgFile + ": the new way in from outside is kept for this run only");
		}
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
			if (r.containsKey("off")) roomOff(ch, Json.bool(r, "off"));
			if (c.left) {
				c.online = false;
				continue;
			}
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
		land();
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
		land();
	}

	/** Once per run, after the rooms are known: the active tab goes to Crew when it is there and online. */
	private void land() {
		if (landed || tx != Tx.IDLE) return;
		for (int k = 0; k < chan.size(); k++) {
			Channel c = chan.get(k);
			if (c.name.equalsIgnoreCase("crew") && c.online && !c.gone && !c.left) {
				active = k;
				c.unread = 0;
				landed = true;
				changed();
				return;
			}
		}
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
				String remote = Json.str(o, "remote");
				if (!remote.isEmpty() && !remote.equals(cfg.remote)) {
					/* G273: the server's way in from outside, kept for when the home address does not answer */
					cfg.remote = remote;
					save = true;
					pf.log("the way in from outside is known");
				}
				String tok = Json.str(o, "token");
				if (!tok.isEmpty()) {
					/* the invite is spent: from now on the token signs in */
					cfg.token = tok;
					cfg.invite = ""; cfg.password = "";   /* the token stands in for the password from here */
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
				helloOff(ss, o.get("off"));
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
				pf.log("hello: " + me() + ", session " + Json.str(o, "session") + (ss.far ? ", from outside" : ""));
			} else {
				String why = "sign-in refused: " + err;
				if (Proto.tokenRefused(code)) {
					/* a bad or spent invite, a revoked or expired token: retrying cannot help */
					authError = err;
					ss.refused = true;
					if (p.invite && !cfg.invite.isEmpty()) {
						cfg.invite = ""; cfg.password = "";
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
		case PORTRAIT: {
			byte[] png = new byte[0];
			String b64 = ok ? Json.str(o, "png") : "";
			if (!b64.isEmpty()) {
				try {
					png = Base64.getDecoder().decode(b64);
				} catch (IllegalArgumentException e) {
					pf.log("portrait of " + p.key + ": not base64");
				}
			}
			if (!ok) pf.log("portrait of " + p.key + " refused: " + err);
			else pf.log("portrait of " + p.key + ": " + (png.length > 0 ? png.length + " bytes" : "none"));
			portraits.put(p.key, png);
			if (ok && !Json.str(o, "name").isEmpty()) names.put(p.key, Json.str(o, "name"));
			break;
		}
		case JOIN:
			if (p.channel >= 0 && p.channel < chan.size()) {
				Channel c = chan.get(p.channel);
				if (ok) {
					c.left = false;
					c.online = true;
					c.error = "";
					c.who.clear();
					Object online = o.get("online");
					if (online instanceof List) for (Object w : (List<?>) online) if (w instanceof String) c.who.add((String) w);
					event(p.channel, "switched on, " + c.who.size() + " online");
					fetchHistory(ss, p.channel);
				} else {
					c.error = code;
					event(p.channel, "not joined: " + err);
					toast(Rooms.title(c.name, c.title) + ": " + err);
				}
			}
			break;
		case VOXLIST: {
			if (!ok) {
				toast(voxWhy(code, err, "voices"));
				break;
			}
			List<Voice> got = parseVoices(o.get("voices"));
			synchronized (Engine.this) {
				voices.clear();
				voices.addAll(got);
			}
			voicesAt = System.currentTimeMillis();
			break;
		}
		case VOX: {
			if (!ok || voxOut == null) {
				converting = false;
				voxOut = null;
				tone("FAIL");
				toast(voxWhy(code, err, "vox"));
				break;
			}
			voxStream = Json.lng(o, "stream");
			/* the take up to the caster: raw s16 frames of 60 ms on the stream, then over */
			int frame = Opus.FRAME * 2, n = 0;
			for (int at = 0; at < voxOut.length; at += frame) {
				byte[] chunk = java.util.Arrays.copyOfRange(voxOut, at, Math.min(voxOut.length, at + frame));
				if (!sendBin(ss, Proto.binHeader(Proto.BIN_AUDIO, voxStream, n++), chunk)) break;
			}
			voxOut = null;
			send(ss, Proto.over(++ss.seq, voxStream));
			break;
		}
		case ROOM:
			if (p.channel >= 0 && p.channel < chan.size()) {
				Channel c = chan.get(p.channel);
				boolean off = p.ref == 0;
				if (!ok) {
					/* the switch was flipped at once; the server said no, so it goes back */
					if (c.muted == off) {
						c.muted = !off;
						if (!off) dropRoomQueue(p.channel); /* asked on, refused: off again, so its queue goes */
					}
					toast(Rooms.title(c.name, c.title) + ": the switch was not saved: " + err);
				}
			}
			break;
		case LEAVE:
			if (!ok && p.channel >= 0 && p.channel < chan.size()) {
				chan.get(p.channel).left = false;
				chan.get(p.channel).online = true;
				toast("could not leave: " + err);
			}
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
		if (kind == Proto.BIN_AUDIO && converting && id == voxStream && voxStream >= 0) {
			voxIn.write(data, 0, data.length);
			return;
		}
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
				if (!chan.get((int) in[0]).muted) pf.notify(chan.get((int) in[0]).name, m.from + " sent a picture");
			}
			if (!chan.get((int) in[0]).muted) tone("ALERT");
			imagesIn.remove(id);
			changed();
		}
	}

	/** G228: who a server line or event is from as people see it: the cast character speaking ("as"), its display
	    name, and the colour the server draws the speaker in (else none, and the row is drawn plain) */
	static void speaker(History.Msg m, Map<String, Object> o) {
		m.as = Json.str(o, "as");
		m.name = Json.str(o, "name");
		String c = Json.str(o, "colour");
		m.colour = c.matches("#[0-9a-fA-F]{6}") ? c : "";
	}

	/** G228: ask the server once for a speaker's portrait (an older server answers unknown_type: none) */
	private void wantPortrait(Session ss, String key) {
		if (key == null || key.isEmpty() || ss == null || ss.state != SState.ONLINE || !portraitAsked.add(key)) return;
		int seq = ++ss.seq;
		if (send(ss, Proto.portrait(seq, key))) {
			addPending(ss, seq, PKind.PORTRAIT, -1, 0, false);
			ss.pend.get(ss.pend.size() - 1).key = key;
		} else portraitAsked.remove(key);
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
		speaker(m, o);
		c.hist.add(m);
		wantPortrait(ss, m.face());
		r.msgId = m.id;
		try {
			r.file = new History.VoiceWriter(c.hist.voice(m.id), r.codec);
		} catch (IOException e) {
			pf.log("voice file: " + e.getMessage());
		}
		rx.add(r);
		if (cfg.autoPlay && !c.muted) {
			Item it = new Item();
			it.rx = r;
			it.channel = ch;
			it.msgId = m.id;
			it.from = from;
			it.who = m.who();
			it.face = m.face();
			it.colour = m.colour;
			it.began = System.currentTimeMillis();
			r.queued = true;
			queue.add(it);
		}
		if (!pf.focused() && !c.muted) pf.notify(c.name, from + (forwho.isEmpty() ? " is talking" : " is talking to you"));
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

	/** G278: set while the queue is dropped without being heard (a room switched off, the engine stopping): the
	    messages stay unheard, so REPLAY still has them. */
	private boolean keepUnheard;

	private void finishItem(boolean natural) {
		if (!playing || queue.isEmpty()) return;
		Item it = queue.remove(0);
		History.Msg m = chan.get(it.channel).hist.find(it.msgId);
		if (m != null && !m.played && !keepUnheard) {
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
		sayPcm = null;
		if (sayEnc != null) sayEnc.close();
		sayEnc = null;
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
		if (!txReleased && !txVox && sayPcm == null) pf.captureEnd();
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
			if (sayPcm != null && code.equals("busy")) {
				/* the converted take waits for the floor instead of being lost */
				if (pendSay == null) {
					pendSay = sayPcm;
					pendN = sayN;
					if (!pendSaid) {
						pendUntil = System.currentTimeMillis() + 45000;
						toast((talker.isEmpty() ? "someone" : talker) + " is talking; your take goes out when the floor is free");
						pendSaid = true;
					}
				}
				pendRetryAt = System.currentTimeMillis() + 1500;
				sayPcm = null; /* txReset must not drop the pending copy's encoder state; it is rebuilt on retry */
				txReset();
				return;
			}
			tone("FAIL");
			toast("cannot talk: " + (code.equals("busy") && !talker.isEmpty() ? talker + " is talking" : err));
			if (!txReleased && !txVox) pf.captureEnd();
			txReset();
			return;
		}
		Channel c = chan.get(txChannel);
		txStreamId = streamId;
		tx = Tx.SENDING;
		if (sayPcm != null) pendSaid = false;
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
			if (taking) {
				taking = false;
				pf.captureRaw(false);
				convertTake();
				return;
			}
			if (tx == Tx.IDLE) return;
			txEndSeen = true;
			if (tx == Tx.SENDING) txFinish();
		});
	}

	/** A raw 16 kHz frame of the take (the platform in raw mode). */
	public void captureRawFrame(short[] pcm, int n, int level) {
		post(() -> {
			if (!taking) return;
			txLevel = level;
			byte[] b = new byte[n * 2];
			for (int i = 0; i < n; i++) {
				b[2 * i] = (byte) pcm[i];
				b[2 * i + 1] = (byte) (pcm[i] >> 8);
			}
			take.write(b, 0, b.length);
			if (System.currentTimeMillis() - takeStarted > TAKE_MAX_MS) {
				toast("take limit reached");
				pf.captureEnd();
			}
			pf.changed();
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
			if (!voxId.isEmpty() && tx == Tx.IDLE && !txVox) {
				takeBegin();
				return;
			}
			if (taking) return;
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
			if (taking) {
				pf.captureEnd();
				return;
			}
			if (tx == Tx.IDLE || txVox || txReleased) return;
			txReleased = true;
			pf.captureEnd();
		});
	}

	public void pttToggle() {
		post(() -> {
			if (taking) {
				pf.captureEnd();
				return;
			}
			if (!voxId.isEmpty() && tx == Tx.IDLE) {
				takeBegin();
				return;
			}
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
		replayNow(channel, msgId, false);
	}

	/** Whether that voice message is on air or waiting in the play queue already (G278: never twice). */
	private boolean queued(int channel, long msgId) {
		for (Item q : queue) if (q.channel == channel && q.msgId == msgId) return true;
		return false;
	}

	/** append: after everything queued (a run of unheard, oldest first); else next, replacing a replay on air. */
	private boolean replayNow(int channel, long msgId, boolean append) {
		if (channel < 0 || channel >= chan.size()) return false;
		if (append && queued(channel, msgId)) return false;
		History h = chan.get(channel).hist;
		History.Msg m = h.find(msgId);
		if (m == null || m.kind != History.Kind.VOICE || m.partial) return false;
		Item it = new Item();
		it.replay = true;
		it.channel = channel;
		it.msgId = msgId;
		it.from = m.from;
		it.who = m.who();
		it.face = m.face();
		it.colour = m.colour;
		wantPortrait(sess.get(chan.get(channel).session), it.face);
		try {
			it.file = History.loadVoice(h.voice(msgId));
		} catch (IOException e) {
			toast("cannot read that voice message");
			return false;
		}
		if (append) {
			queue.add(it);
		} else {
			if (playing && queue.get(0).replay) stopPlaybackNow(); /* a new replay replaces the one on air */
			queue.add(playing ? 1 : 0, it);
		}
		changed();
		return true;
	}

	/** G278: every unheard voice message in room ch, oldest first, after what is playing; skips any already queued.
	    Each is marked heard as it finishes (the played flag, kept on disk), so STOP leaves the rest unheard. */
	private int playMissed(int ch) {
		if (ch < 0 || ch >= chan.size()) return 0;
		int n = 0;
		for (History.Msg m : new ArrayList<>(chan.get(ch).hist.msgs)) {
			if (m.kind == History.Kind.VOICE && !m.out && !m.partial && !m.played && replayNow(ch, m.id, true)) n++;
			if (queue.size() >= 64) break;
		}
		return n;
	}

	/** How many voice messages room ch holds unheard: the tab's count, from the saved flag. */
	private int unheard(Channel c) {
		int n = 0;
		for (History.Msg m : c.hist.msgs) if (m.kind == History.Kind.VOICE && !m.out && !m.partial && !m.played) n++;
		return n;
	}

	/** The server's word on a room's switch (hello's off list, the rooms event, the room answer). */
	private void roomOff(int ch, boolean off) {
		Channel c = chan.get(ch);
		if (c.muted == off) return;
		c.muted = off;
		if (off) dropRoomQueue(ch); /* switched off elsewhere (the crew, another device): its queue waits for REPLAY too */
		event(ch, off ? "switched off: voice messages are kept for REPLAY" : "switched on");
	}

	private void helloOff(Session ss, Object off) {
		if (!(off instanceof List)) return;
		java.util.Set<String> ids = new java.util.HashSet<>();
		for (Object v : (List<?>) off) if (v instanceof String) ids.add(((String) v).toLowerCase(Locale.ROOT));
		for (int k = 0; k < chan.size(); k++) {
			Channel c = chan.get(k);
			if (sess.get(c.session) == ss && c.peer.isEmpty()) roomOff(k, ids.contains(c.name.toLowerCase(Locale.ROOT)));
		}
	}

	/** REPLAY: this room's unheard voice messages, oldest first, one after another; with none unheard, the last one. */
	public void replayLast() {
		post(() -> {
			if (chan.isEmpty()) return;
			int n = playMissed(active);
			if (n > 0) {
				toast("replaying " + n + " unheard");
				changed();
				return;
			}
			for (Item q : queue) if (q.channel == active && q.replay) return; /* its unheard are playing already */
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

	/* ---------- the Vox filter ---------- */

	public String voxId() {
		return voxId;
	}

	public String voxName() {
		return voxName;
	}

	/** The last list SYN-VOX gave (a copy); voxVoices() refreshes it. */
	public synchronized List<Voice> voices() {
		return new ArrayList<>(voices);
	}

	/** Unfiltered ("" or "raw"), or a voice id from the list (overwatch, zofia, vctk#40 ...). */
	public void setVox(String id, String name) {
		post(() -> {
			String v = id == null || id.equals("raw") ? "" : id;
			if (taking || converting) {
				toast("finish the take first");
				return;
			}
			voxId = v;
			voxName = v.isEmpty() ? "Unfiltered" : (name == null || name.isEmpty() ? v : name);
			toast(v.isEmpty() ? "Vox off: your own voice" : "Vox: " + voxName);
			changed();
		});
	}

	/** SYN-VOX: cfg.voxUrl when set, else plain HTTP to port 7791 on the cast server's host (the home network or the
	 * link; no secret, only audio goes through). The cast protocol will carry it with the member token (a1's door). */
	private String voxBase() {
		if (!cfg.voxUrl.trim().isEmpty()) return cfg.voxUrl.trim().replaceAll("/+$", "");
		String host = cfg.server.replaceFirst("^wss?://", "").replaceFirst("/.*$", "").replaceFirst(":\\d+$", "");
		return "http://" + host + ":7791/api/vox";
	}

	private java.net.HttpURLConnection voxOpen(String method, String url, String ctype, int readMs) throws IOException {
		java.net.HttpURLConnection c = (java.net.HttpURLConnection) new java.net.URL(url).openConnection();
		if (c instanceof javax.net.ssl.HttpsURLConnection && !cfg.certPem.trim().isEmpty()) {
			javax.net.ssl.HttpsURLConnection h = (javax.net.ssl.HttpsURLConnection) c;
			h.setSSLSocketFactory(Ws.pinned(cfg.certPem));
			h.setHostnameVerifier((host, session) -> true); /* the pin is the trust, as for the WebSocket */
		}
		c.setRequestMethod(method);
		c.setConnectTimeout(10000);
		c.setReadTimeout(readMs);
		if (!cfg.token.isEmpty()) c.setRequestProperty("Authorization", "Bearer " + cfg.token);
		if (ctype != null) c.setRequestProperty("Content-Type", ctype);
		return c;
	}

	private static byte[] readAll(java.io.InputStream in) throws IOException {
		java.io.ByteArrayOutputStream o = new java.io.ByteArrayOutputStream();
		byte[] b = new byte[16384];
		for (int n; (n = in.read(b)) > 0;) o.write(b, 0, n);
		return o.toByteArray();
	}

	/** Asks SYN-VOX (through the cast server) for the voices; the list lands in voices() and the view changes. */
	public void voxVoices() {
		if (!cfg.hasAccount()) return;
		if (cfg.voxUrl.trim().isEmpty()) {
			post(() -> {
				Session ss = sess.isEmpty() ? null : sess.get(0);
				if (ss == null || ss.state != SState.ONLINE) {
					toast("not connected to the caster");
					return;
				}
				int seq = ++ss.seq;
				if (send(ss, Proto.voxVoices(seq))) addPending(ss, seq, PKind.VOXLIST, -1, 0, false);
			});
			return;
		}
		connector.execute(() -> {
			List<Voice> got = new ArrayList<>();
			String why = "";
			try {
				java.net.HttpURLConnection c = voxOpen("GET", voxBase() + "/voices", null, 10000);
				int code = c.getResponseCode();
				byte[] body = readAll(code < 400 ? c.getInputStream() : c.getErrorStream() == null ? new java.io.ByteArrayInputStream(new byte[0]) : c.getErrorStream());
				if (code != 200) {
					why = "voices: HTTP " + code;
				} else {
					Object o = Json.parse(new String(body, java.nio.charset.StandardCharsets.UTF_8));
					Object vs = o instanceof Map ? ((Map<?, ?>) o).get("voices") : null;
					if (vs instanceof List) {
						for (Object x : (List<?>) vs) {
							if (!(x instanceof Map)) continue;
							@SuppressWarnings("unchecked")
							Map<String, Object> m = (Map<String, Object>) x;
							Voice v = new Voice();
							v.id = Json.str(m, "id");
							v.name = Json.str(m, "name");
							v.kind = Json.str(m, "kind");
							v.ready = Json.bool(m, "ready");
							v.building = Json.bool(m, "building");
							v.speakers = (int) Json.lng(m, "speakers");
							if (!v.id.isEmpty()) got.add(v);
						}
					}
				}
			} catch (IOException | RuntimeException e) {
				why = "no voice converter: " + (e.getMessage() == null ? e.getClass().getSimpleName() : e.getMessage());
			}
			final String w = why;
			post(() -> {
				if (!got.isEmpty()) {
					synchronized (Engine.this) {
						voices.clear();
						voices.addAll(got);
					}
					voicesAt = System.currentTimeMillis();
				}
				if (!w.isEmpty()) toast(w);
				changed();
			});
		});
	}

	/** The caster's vox codes in the boss's words. */
	private static String voxWhy(String code, String err, String what) {
		switch (code) {
		case "unknown_type": return "the caster has no vox door yet";
		case "down": return "SYN-VOX is not running on the host";
		case "building": return err.isEmpty() ? "that voice is still building; try again in a minute" : err;
		case "busy": return "a take is still being converted";
		case "too_big": return "the take is too long (90 s at most)";
		case "bad_format": case "bad_request": return what + ": " + (err.isEmpty() ? code : err);
		default: return what + ": " + (err.isEmpty() ? code : err);
		}
	}

	private static List<Voice> parseVoices(Object vs) {
		List<Voice> got = new ArrayList<>();
		if (vs instanceof List) {
			for (Object x : (List<?>) vs) {
				if (!(x instanceof Map)) continue;
				@SuppressWarnings("unchecked")
				Map<String, Object> m = (Map<String, Object>) x;
				Voice v = new Voice();
				v.id = Json.str(m, "id");
				v.name = Json.str(m, "name");
				v.kind = Json.str(m, "kind");
				v.ready = Json.bool(m, "ready");
				v.building = Json.bool(m, "building");
				v.speakers = (int) Json.lng(m, "speakers");
				if (!v.id.isEmpty()) got.add(v);
			}
		}
		return got;
	}

	private void takeBegin() {
		if (converting) {
			toast("still converting the last take");
			return;
		}
		Channel c = chan.isEmpty() ? null : chan.get(active);
		if (c == null || !c.online) {
			tone("FAIL");
			toast(c == null ? "no room" : c.name + " is offline");
			return;
		}
		if (playing && queue.get(0).replay) stopPlaybackNow();
		taking = true;
		take.reset();
		takeStarted = System.currentTimeMillis();
		pf.captureRaw(true);
		pf.captureBegin();
		tone("SUCCESS");
		changed();
	}

	private static byte[] wav16k(byte[] pcm) {
		java.nio.ByteBuffer b = java.nio.ByteBuffer.allocate(44 + pcm.length).order(java.nio.ByteOrder.LITTLE_ENDIAN);
		b.put("RIFF".getBytes(java.nio.charset.StandardCharsets.US_ASCII)).putInt(36 + pcm.length)
			.put("WAVEfmt ".getBytes(java.nio.charset.StandardCharsets.US_ASCII)).putInt(16).putShort((short) 1).putShort((short) 1)
			.putInt(Opus.RATE).putInt(Opus.RATE * 2).putShort((short) 2).putShort((short) 16)
			.put("data".getBytes(java.nio.charset.StandardCharsets.US_ASCII)).putInt(pcm.length).put(pcm);
		return b.array();
	}

	/** 16-bit PCM out of a WAV (any rate SYN-VOX answers with is asked to be ours). */
	private static short[] wavPcm(byte[] wav) {
		java.nio.ByteBuffer b = java.nio.ByteBuffer.wrap(wav).order(java.nio.ByteOrder.LITTLE_ENDIAN);
		if (wav.length < 12 || b.getInt(0) != 0x46464952) return null;
		int at = 12;
		while (at + 8 <= wav.length) {
			int id = b.getInt(at), len = b.getInt(at + 4);
			if (id == 0x61746164) { /* data */
				int n = Math.min(len, wav.length - at - 8) / 2;
				short[] pcm = new short[n];
				for (int i = 0; i < n; i++) pcm[i] = b.getShort(at + 8 + 2 * i);
				return pcm;
			}
			at += 8 + len + (len & 1);
		}
		return null;
	}

	private void convertTake() {
		byte[] pcm = take.toByteArray();
		take.reset();
		int ms = pcm.length / 2 * 1000 / Opus.RATE;
		if (ms < 300) {
			toast("too short");
			changed();
			return;
		}
		final String id = voxId, name = voxName;
		converting = true;
		toast("converting " + Views_dur(ms) + " into " + name + " …");
		changed();
		if (cfg.voxUrl.trim().isEmpty()) {
			/* home to the caster: the take goes up the cast protocol, the caster asks SYN-VOX and sends the result back */
			Session ss = sess.isEmpty() ? null : sess.get(0);
			if (ss == null || ss.state != SState.ONLINE) {
				converting = false;
				tone("FAIL");
				toast("not connected to the caster");
				changed();
				return;
			}
			voxOut = pcm;
			voxIn.reset();
			voxStream = -1;
			voxSentAt = System.currentTimeMillis();
			int seq = ++ss.seq;
			if (!send(ss, Proto.vox(seq, id, 4, Opus.RATE, ms))) {
				converting = false;
				voxOut = null;
				toast("not connected");
				changed();
				return;
			}
			addPending(ss, seq, PKind.VOX, -1, 0, false);
			return;
		}
		connector.execute(() -> {
			short[] out = null;
			String why = "";
			long t0 = System.currentTimeMillis();
			try {
				java.net.HttpURLConnection c = voxOpen("POST", voxBase() + "/convert?voice=" + java.net.URLEncoder.encode(id, "UTF-8") + "&rate=" + Opus.RATE, "audio/wav", 120000);
				c.setDoOutput(true);
				byte[] wav = wav16k(pcm);
				c.setFixedLengthStreamingMode(wav.length);
				try (java.io.OutputStream os = c.getOutputStream()) {
					os.write(wav);
				}
				int code = c.getResponseCode();
				if (code == 200) {
					out = wavPcm(readAll(c.getInputStream()));
					if (out == null) why = "the converter sent no audio";
				} else {
					String body = c.getErrorStream() == null ? "" : new String(readAll(c.getErrorStream()), java.nio.charset.StandardCharsets.UTF_8);
					why = code == 503 ? name + " is still building its voice; try again in a minute"
						: code == 502 ? "SYN-VOX is not running on the host"
						: code == 401 ? "the cast server refused the token for vox"
						: "converter: HTTP " + code + (body.isEmpty() ? "" : " " + body.replaceAll("\\s+", " ").trim());
				}
			} catch (IOException | RuntimeException e) {
				why = "no voice converter: " + (e.getMessage() == null ? e.getClass().getSimpleName() : e.getMessage());
			}
			final short[] res = out;
			final String w = why;
			final long dt = System.currentTimeMillis() - t0;
			post(() -> {
				converting = false;
				if (res == null) {
					tone("FAIL");
					toast(w);
					changed();
					return;
				}
				pf.log("vox " + id + ": " + ms + " ms converted in " + dt + " ms");
				sayBegin(res, res.length);
			});
		});
	}

	private static String Views_dur(long ms) {
		return String.format(Locale.ROOT, "%d:%02d", ms / 60000, (ms / 1000) % 60);
	}

	/** Speaks PCM (16 kHz mono) on the room as a transmission, paced in real time. */
	private void sayBegin(short[] pcm, int n) {
		if (!txBegin(false)) {
			if (pendSay == null) toast(toast.isEmpty() ? "cannot talk now" : toast);
			return;
		}
		sayPcm = pcm;
		sayN = n;
		sayPos = saySent = 0;
		sayEnc = new Opus.Encoder();
		sayT0 = System.currentTimeMillis();
	}

	private void sayTick() {
		if (sayPcm == null || txEndSeen || tx == Tx.IDLE) return;
		long due = (System.currentTimeMillis() - sayT0 + SAY_LEAD_MS) / Opus.FRAME_MS + 1;
		short[] frame = new short[Opus.FRAME];
		while (saySent < due && sayPos < sayN) {
			int t = Math.min(sayN - sayPos, Opus.FRAME);
			System.arraycopy(sayPcm, sayPos, frame, 0, t);
			java.util.Arrays.fill(frame, t, Opus.FRAME, (short) 0);
			sayPos += t;
			byte[] p = sayEnc.encode(frame);
			if (p != null) {
				txLevel = 60;
				if (tx == Tx.STARTING) {
					if (txHeld.size() < HELD_MAX) txHeld.add(p);
				} else if (tx == Tx.SENDING) {
					txSendPacket(p);
				}
			}
			saySent++;
		}
		if (sayPos >= sayN) {
			txEndSeen = true;
			if (tx == Tx.SENDING) txFinish();
		}
	}

	/* ---------- replay all unheard, rooms on and off ---------- */

	/** Every voice message not heard yet, oldest first, across the rooms you are in, queued after what is playing. */
	public void replayUnheard() {
		post(() -> {
			List<Object[]> all = new ArrayList<>();
			for (int k : order()) {
				Channel c = chan.get(k);
				if (c.left) continue;
				for (History.Msg m : c.hist.msgs) {
					if (m.kind == History.Kind.VOICE && !m.out && !m.partial && !m.played) all.add(new Object[] {m.ts, k, m.id});
				}
			}
			all.sort((a, b) -> Long.compare((Long) a[0], (Long) b[0]));
			int n = 0;
			for (Object[] e : all) {
				int k = (Integer) e[1];
				History h = chan.get(k).hist;
				History.Msg m = h.find((Long) e[2]);
				if (m == null || queued(k, m.id)) continue; /* G278: already on air or waiting: never twice */
				Item it = new Item();
				it.replay = true;
				it.channel = k;
				it.msgId = m.id;
				it.from = m.from;
				try {
					it.file = History.loadVoice(h.voice(m.id));
				} catch (IOException ex) {
					continue;
				}
				queue.add(it);
				n++;
				if (queue.size() >= 32) break;
			}
			toast(n == 0 ? "nothing unheard" : "replaying " + n + " unheard");
			changed();
		});
	}

	/** G278: room k on or off for this member, saved on the server for every device of theirs. Off keeps the room
	    joined, so every voice message still arrives and is kept unheard; nothing plays aloud, no tone, no notification.
	    On plays what was missed there, oldest first. */
	public void toggleRoom(int k) {
		post(() -> {
			if (k < 0 || k >= chan.size()) return;
			Channel c = chan.get(k);
			if (!c.peer.isEmpty()) {
				toast("a direct message has no switch");
				return;
			}
			Session ss = sess.get(c.session);
			if (ss.state != SState.ONLINE) {
				toast("not connected: the switch is kept on the server, try again online");
				return;
			}
			boolean on = c.muted;
			int seq = ++ss.seq;
			if (!send(ss, Proto.room(seq, c.name, on))) return;
			addPending(ss, seq, PKind.ROOM, k, on ? 1 : 0, false);
			c.muted = !on;
			if (on) {
				int n = playMissed(k);
				event(k, n > 0 ? "switched on: playing " + n + " missed" : "switched on");
			} else {
				dropRoomQueue(k);
				event(k, "switched off: voice messages are kept for REPLAY");
			}
			changed();
		});
	}

	/** A room switched off: what of it waits in the queue (or is on air) comes out, still unheard. */
	private void dropRoomQueue(int k) {
		keepUnheard = true;
		if (playing && !queue.isEmpty() && queue.get(0).channel == k) stopPlaybackNow();
		keepUnheard = false;
		for (int i = queue.size() - 1; i >= (playing ? 1 : 0); i--) {
			Item it = queue.get(i);
			if (it.channel != k) continue;
			queue.remove(i);
			if (it.dec != null) it.dec.close();
			if (!it.replay) {
				it.rx.queued = false;
				if (it.rx.ended) rxFree(it.rx);
			}
		}
	}

	/** /leave and /join: out of room k for this session (kept as a member), or in again. */
	private void toggleJoin(int k) {
		post(() -> {
			if (k < 0 || k >= chan.size()) return;
			Channel c = chan.get(k);
			if (!c.peer.isEmpty()) {
				toast("a direct message has no switch");
				return;
			}
			Session ss = sess.get(c.session);
			if (ss.state != SState.ONLINE) {
				toast("not connected");
				return;
			}
			if (tx != Tx.IDLE && txChannel == k) txAbort("room switched off");
			int seq = ++ss.seq;
			if (!c.left && c.online) {
				if (send(ss, Proto.leave(seq, c.name, false))) {
					addPending(ss, seq, PKind.LEAVE, k, 0, false);
					c.left = true;
					c.online = false;
					c.who.clear();
					event(k, "switched off");
				}
			} else {
				if (send(ss, Proto.join(seq, c.name))) {
					addPending(ss, seq, PKind.JOIN, k, 0, false);
					c.left = false;
					event(k, "switching on …");
				}
			}
			changed();
		});
	}

	/** /join NAME: a room the server has (a ticket's g-number, a meeting) that is not on the bar yet. */
	public void joinRoom(String name) {
		post(() -> {
			String id = name.trim().toLowerCase(Locale.ROOT);
			if (id.isEmpty()) return;
			int k = -1;
			for (int i = 0; i < chan.size(); i++) if (chan.get(i).name.equalsIgnoreCase(id)) k = i;
			if (k < 0) k = channelAdd(id);
			if (k < 0) return;
			Channel c = chan.get(k);
			c.gone = false;
			c.left = true;
			c.online = false;
			toggleJoin(k);
			active = k;
		});
	}

	public void toggleVoxActivation() {
		post(() -> {
			vox = !vox;
			pf.captureVox(vox);
			toast(vox ? "hands-free on: talking opens the channel" : "hands-free off");
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
		case "unheard": replayUnheard(); break;
		case "vox": {
			String a = arg.trim();
			if (a.isEmpty() || a.equalsIgnoreCase("list")) {
				voxVoices();
				StringBuilder sb = new StringBuilder("vox: " + voxName + ". Voices:");
				for (Voice v : voices()) sb.append(' ').append(v.id);
				post(() -> toast(sb.toString()));
			} else if (a.equalsIgnoreCase("off") || a.equalsIgnoreCase("raw")) {
				setVox("", "");
			} else {
				Voice hit = null;
				for (Voice v : voices()) if (v.id.equalsIgnoreCase(a) || v.name.equalsIgnoreCase(a)) hit = v;
				if (hit == null) for (Voice v : voices()) if (v.name.toLowerCase(Locale.ROOT).contains(a.toLowerCase(Locale.ROOT))) hit = v;
				setVox(hit == null ? a : hit.id, hit == null ? a : hit.name);
			}
			break;
		}
		case "handsfree": toggleVoxActivation(); break;
		case "join": joinRoom(arg); break;
		case "leave": post(() -> toggleJoin(active)); break;
		case "room": {
			/* /room, /room on, /room off: this room's switch (G278) */
			String a = arg.trim().toLowerCase(Locale.ROOT);
			post(() -> {
				if (active < 0 || active >= chan.size()) return;
				boolean off = chan.get(active).muted;
				if (a.isEmpty() || (a.equals("on") && off) || (a.equals("off") && !off)) toggleRoom(active);
				else toast("already " + a);
			});
			break;
		}
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
			post(() -> toast("/msg NAME [TEXT]  /replay  /unheard  /vox NAME|off|list  /join ROOM  /leave  /handsfree  /speed  /loc LAT LON [ADDRESS]"));
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
			if (txVox || sayPcm != null) {
				txFinish();
			} else {
				txReleased = true;
				pf.captureEnd();
				txStarted = now;
			}
		}
		if (converting && voxSentAt > 0 && cfg.voxUrl.trim().isEmpty() && now - voxSentAt > VOX_WAIT_MS) {
			converting = false;
			voxStream = -1;
			voxOut = null;
			toast("the caster did not answer the vox take");
			changed();
		}
		if (pendSay != null && tx == Tx.IDLE && !taking && !converting && now >= pendRetryAt) {
			short[] p = pendSay;
			int n = pendN;
			if (now > pendUntil) {
				pendSay = null;
				pendSaid = false;
				tone("FAIL");
				toast("the floor stayed busy; your take was not sent");
				changed();
			} else {
				pendRetryAt = now + 1500;
				if (!chan.isEmpty() && chan.get(active).online) {
					sayBegin(p, n);
					if (sayPcm != null) pendSay = null; /* on the air (or waiting for the server); a busy answer re-pends it */
				}
			}
		}
		sayTick();
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
		if (tx != Tx.IDLE || playing || taking || converting) pf.changed(); /* meters and clocks */
	}

	/* ---------- what the UI reads ---------- */

	public static final class ChannelView {
		public String name, state, error;
		public boolean online, muted, left;
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
		/* the Vox filter */
		public String voxId = "", voxName = "Unfiltered";
		public boolean taking, converting;
		public long takeElapsed;
		/** G273: signed in by the way in from outside (the home address did not answer) */
		public boolean far;
		public double speed;
		public int queue;
		/* transmitting */
		public String txDest;
		public long txElapsed;
		public int txLevel;
		public boolean txOpening;
		/* on air */
		public String rxName;
		/** G228: the speaker's colour ("#rrggbb" or "") and portrait (PNG bytes, null while unknown or none) */
		public String rxColour = "", rxFace = "";
		public byte[] rxPortrait;
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
		for (Session ss : sess) if (ss.state == SState.ONLINE && ss.far) v.far = true;
		v.toast = toast;
		v.active = active;
		v.offline = offline;
		v.vox = vox;
		v.voxId = voxId;
		v.voxName = voxName;
		v.taking = taking;
		v.converting = converting;
		v.takeElapsed = taking ? System.currentTimeMillis() - takeStarted : 0;
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
			cv.left = c.left;
			cv.users = c.who.size();
			cv.title = Rooms.title(c.name, c.title);
			cv.who.addAll(c.who);
			cv.ticket = isTicketRoom(c.name);
			cv.peer = c.peer;
			cv.unread = c.unread + unheard(c); /* G278: texts since last looked, plus voice messages not heard yet */
			cv.state = stateLabel(k);
			cv.error = sess.get(c.session).error;
			v.channels.add(cv);
		}
		if (taking) v.talk = Talk.TAKING;
		else if (converting) v.talk = Talk.CONVERTING;
		else if (tx == Tx.STARTING) v.talk = Talk.CONNECTING;
		else if (tx == Tx.SENDING) v.talk = Talk.SENDING;
		else if (playing) v.talk = Talk.RECEIVING;
		else if (chan.isEmpty() || !chan.get(active).online) v.talk = Talk.DISABLED;
		else v.talk = Talk.IDLE;
		if (taking) v.txLevel = Math.max(txLevel, pf.captureLevel());
		if (tx != Tx.IDLE) {
			v.txDest = chan.get(txChannel).name;
			v.txElapsed = System.currentTimeMillis() - txStarted;
			v.txLevel = sayPcm != null ? txLevel : Math.max(txLevel, pf.captureLevel());
			v.txOpening = tx == Tx.STARTING;
		}
		if (playing) {
			Item it = queue.get(0);
			v.rxName = it.who.isEmpty() ? it.from : it.who;
			v.rxColour = it.colour;
			v.rxFace = it.face.isEmpty() ? it.from : it.face;
			byte[] png = portraits.get(v.rxFace);
			v.rxPortrait = png != null && png.length > 0 ? png : null;
			v.rxReplay = it.replay;
			v.rxChannel = chan.get(it.channel).name;
			v.rxLevel = pf.playLevel();
			v.rxElapsed = System.currentTimeMillis() - it.began;
			v.playingMsgId = it.msgId;
		}
		return v;
	}

	/** A copy of a channel's messages, newest last. */
	/** G228: a speaker's portrait as fetched (null: not fetched yet; empty: the server has none). */
	/** The name people see for a member or character id: the server's, once asked for (the portrait request carries it), else the id. */
	public synchronized String displayName(String id) {
		String n = names.get(id);
		if (n != null) return n;
		for (Channel c : chan) { Session ss = sess.get(c.session); if (ss != null && ss.state == SState.ONLINE) { wantPortrait(ss, id); break; } }
		return id;
	}

	public synchronized byte[] portrait(String id) {
		return portraits.get(id);
	}

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
			c.as = m.as;
			c.name = m.name;
			c.colour = m.colour;
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
		if (chan.get(k).left) return "OUT";
		if (chan.get(k).muted) return "OFF";
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
