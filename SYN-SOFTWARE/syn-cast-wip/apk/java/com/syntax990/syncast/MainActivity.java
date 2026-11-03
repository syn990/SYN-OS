/* ------------------------------------------------------------------------
 *   The screen: header, channel tabs, history, the on-air strip, the text
 *   line, the talk button and a row of actions; or the sign-in form when
 *   there is no account yet. Everything real happens in CastService.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CAST-APK
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
package com.syntax990.syncast;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.app.Dialog;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.content.pm.PackageManager;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Matrix;
import android.graphics.drawable.GradientDrawable;
import android.location.Address;
import android.location.Geocoder;
import android.location.Location;
import android.location.LocationManager;
import android.media.ExifInterface;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.text.InputType;
import android.util.LruCache;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowManager;
import android.view.inputmethod.EditorInfo;
import android.widget.AbsListView;
import android.widget.BaseAdapter;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.HorizontalScrollView;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.TextView;

import com.syntax990.syncast.core.Config;
import com.syntax990.syncast.core.Proto;
import com.syntax990.syncast.core.Engine;
import com.syntax990.syncast.core.History;
import com.syntax990.syncast.core.Json;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.Map;

public final class MainActivity extends Activity {
	private static final int REQ_PERMS = 1, REQ_IMAGE = 2, REQ_CERT = 3, REQ_LOCATION = 4;

	private CastService svc;
	private Palette pal;
	private final Handler main = new Handler(Looper.getMainLooper());

	private Views.Header header;
	private LinearLayout tabs;
	private HorizontalScrollView tabScroll;
	private TextView peek; /* G192: who is inside the room you are in */
	private int peekAt = -1; /* a held tab's room shown in the peek line until peekUntil */
	private long peekUntil;
	private ListView list;
	private Views.OnAir onAir;
	private Views.TalkButton talk;
	private EditText input;
	private final List<History.Msg> rows = new ArrayList<>();
	private long shownVersion = -1;
	private int shownChannel = -1;
	private long shownPlaying = -2;
	private String shownTabs = "";
	private Engine.View view;
	private final LruCache<Long, Bitmap> thumbs = new LruCache<>(48);
	private boolean volumeDown;
	private EditText setupCert;
	private TextView onlineBtn;
	/* the terminal: the menu drawn as a text screen in the chat's place, and back to the chat when done */
	private LinearLayout root;
	private Views.Terminal term;
	private String termPage;          /* null: the chat is showing */
	private final List<Runnable> termActs = new ArrayList<>();
	private final java.util.Map<String, Integer> bankSpeaker = new java.util.HashMap<>();

	/* ---------- the service ---------- */

	private final ServiceConnection conn = new ServiceConnection() {
		@Override
		public void onServiceConnected(ComponentName n, IBinder b) {
			svc = ((CastService.Local) b).service();
			svc.setListener(MainActivity.this::refresh);
			svc.setVisible(true);
			followOnce();
			build();
		}

		@Override
		public void onServiceDisconnected(ComponentName n) {
			svc = null;
		}
	};

	@Override
	protected void onCreate(Bundle state) {
		super.onCreate(state);
		requestWindowFeature(Window.FEATURE_NO_TITLE);
		getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);  /* the screen stays on while the app is in front */
		Intent i = new Intent(this, CastService.class);
		startForegroundService(i);
		bindService(i, conn, Context.BIND_AUTO_CREATE);
		List<String> want = new ArrayList<>();
		want.add(Manifest.permission.RECORD_AUDIO);
		if (Build.VERSION.SDK_INT >= 33) want.add(Manifest.permission.POST_NOTIFICATIONS);
		List<String> missing = new ArrayList<>();
		for (String p : want) {
			if (checkSelfPermission(p) != PackageManager.PERMISSION_GRANTED) missing.add(p);
		}
		if (!missing.isEmpty()) requestPermissions(missing.toArray(new String[0]), REQ_PERMS);
	}

	@Override
	public void onRequestPermissionsResult(int req, String[] perms, int[] res) {
		if (req == REQ_PERMS) {
			/* the service can now add the microphone to its foreground type */
			startForegroundService(new Intent(this, CastService.class));
		} else if (req == REQ_LOCATION && res.length > 0 && res[0] == PackageManager.PERMISSION_GRANTED) {
			sendLocation();
		}
	}

	@Override
	protected void onResume() {
		super.onResume();
		if (svc != null) {
			svc.setVisible(true);
			/* following SYN-OS: the launcher may have changed theme while we were away */
			if (pal != null && svc.config().theme.isEmpty() && !Palette.load(this, "").name.equals(pal.name)) build();
			refresh();
		}
	}

	@Override
	protected void onPause() {
		if (svc != null) svc.setVisible(false);
		if (volumeDown && svc != null && svc.engine() != null) {
			volumeDown = false;
			svc.engine().pttUp();
		}
		super.onPause();
	}

	@Override
	protected void onDestroy() {
		if (svc != null) {
			svc.setListener(null);
			svc.setVisible(false);
		}
		unbindService(conn);
		super.onDestroy();
	}

	@Override
	public void onBackPressed() {
		if (termPage != null) {
			if (termPage.equals("main")) closeTerminal();
			else terminal("main");
			return;
		}
		moveTaskToBack(true); /* the radio stays on; EXIT is in the notification and the menu */
	}

	/* ---------- styling helpers ---------- */

	private int dp(float v) {
		return (int) Views.dp(this, v);
	}

	private GradientDrawable box(int fill, int stroke) {
		GradientDrawable g = new GradientDrawable();
		g.setColor(fill);
		g.setStroke(dp(1), stroke);
		return g;
	}

	private TextView button(String label, View.OnClickListener l) {
		TextView b = new TextView(this);
		b.setText(label);
		b.setTypeface(android.graphics.Typeface.MONOSPACE, android.graphics.Typeface.BOLD);
		b.setTextSize(13);
		b.setTextColor(pal.text);
		b.setGravity(Gravity.CENTER);
		b.setPadding(dp(8), dp(10), dp(8), dp(10));
		b.setBackground(box(pal.panel, pal.border));
		b.setOnClickListener(l);
		return b;
	}

	private EditText field(String hint, boolean secret) {
		EditText e = new EditText(this);
		e.setHint(hint);
		e.setTypeface(android.graphics.Typeface.MONOSPACE);
		e.setTextSize(14);
		e.setTextColor(pal.text);
		e.setHintTextColor(Palette.mix(pal.text, pal.bg, 55));
		e.setBackground(box(pal.bg, pal.border));
		e.setPadding(dp(8), dp(8), dp(8), dp(8));
		e.setSingleLine(true);
		if (secret) e.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);
		return e;
	}

	private TextView label(String s, int color, float sp) {
		TextView t = new TextView(this);
		t.setText(s);
		t.setTypeface(android.graphics.Typeface.MONOSPACE);
		t.setTextSize(sp);
		t.setTextColor(color);
		t.setPadding(0, dp(10), 0, dp(4));
		return t;
	}

	/* once, when SYN-OS theming arrived: a theme picked before it goes back to following SYN-OS */
	private void followOnce() {
		android.content.SharedPreferences sp = getSharedPreferences("syncast", MODE_PRIVATE);
		if (sp.getBoolean("follow-syn-os", false)) return;
		Config c = svc.config();
		if (!c.theme.isEmpty()) {
			c.theme = "";
			try {
				svc.save(c);
			} catch (IOException ignored) {
			}
		}
		sp.edit().putBoolean("follow-syn-os", true).apply();
	}

	/* ---------- building the screen ---------- */

	private void build() {
		pal = Palette.load(this, svc.config().theme);
		getWindow().setStatusBarColor(pal.panel);
		getWindow().setNavigationBarColor(pal.bg);
		if (svc.config().hasAccount()) buildMain();
		else buildSetup();
	}

	private void buildSetup() {
		list = null;
		Config c = svc.config();
		ScrollView sv = new ScrollView(this);
		sv.setBackgroundColor(pal.bg);
		LinearLayout col = new LinearLayout(this);
		col.setOrientation(LinearLayout.VERTICAL);
		col.setPadding(dp(16), dp(8), dp(16), dp(16));
		sv.addView(col);
		header = new Views.Header(this, pal);
		LinearLayout root = new LinearLayout(this);
		root.setOrientation(LinearLayout.VERTICAL);
		root.setBackgroundColor(pal.bg);
		root.addView(header);
		root.addView(sv, new LinearLayout.LayoutParams(-1, 0, 1));

		col.addView(label("Sign in to SYN-CAST", pal.accent, 18));
		col.addView(label("The crew's own radio. Paste the one-time invite code you were given; the member token it "
			+ "earns stays on this phone. Everything you send goes to the whole selected room, or only to the member on a direct-message tab (@name).", pal.text, 13));
		col.addView(label("SERVER", pal.accent, 12));
		EditText server = field("wss://HOST:PORT/cast/v1", false);
		server.setText(c.server);
		col.addView(server);
		col.addView(label(c.token.isEmpty() ? "INVITE CODE" : "INVITE CODE (empty keeps the token you have)", pal.accent, 12));
		EditText invite = field("one-time code from syn-caster cast invite", false);
		invite.setText(c.invite);
		col.addView(invite);
		col.addView(label("SERVER CERTIFICATE (PEM, pinned; empty trusts the system store)", pal.accent, 12));
		setupCert = field("-----BEGIN CERTIFICATE----- ...", false);
		setupCert.setSingleLine(false);
		setupCert.setMinLines(2);
		setupCert.setText(c.certPem);
		col.addView(setupCert);
		TextView load = button("LOAD CERTIFICATE FILE", v -> {
			Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*");
			startActivityForResult(i, REQ_CERT);
		});
		col.addView(load, new LinearLayout.LayoutParams(-1, -2));
		TextView err = label("", pal.urgent, 13);
		Engine.View now = svc.engine().snapshot();
		if (now.authError != null) err.setText("The server refused the sign-in: " + now.authError + ".");
		TextView go = button("SIGN IN", v -> {
			Config n = svc.config();
			n.server = server.getText().toString().trim();
			if (!n.server.isEmpty() && !n.server.contains("/cast/")) {
				/* the host alone was given: the path is always the same */
				n.server = n.server.replaceFirst("/+$", "") + Proto.PATH;
			}
			String code = invite.getText().toString().trim();
			if (!code.isEmpty()) {
				n.invite = code;
				n.token = "";
			}
			n.channels.clear(); /* G192: never typed: every room the member belongs to, as tabs */
			n.certPem = setupCert.getText().toString().trim();
			if (n.server.isEmpty() || (n.invite.isEmpty() && n.token.isEmpty())) {
				err.setText("The server and an invite code are needed.");
				return;
			}
			try {
				svc.save(n);
			} catch (IOException e) {
				err.setText("Cannot save: " + e.getMessage());
				return;
			}
			build();
		});
		LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-1, -2);
		lp.topMargin = dp(16);
		col.addView(go, lp);
		col.addView(err);
		if (now.authError != null) {
			col.removeView(err);
			col.addView(err, 2);
		}
		setContentView(root);
		refresh();
	}

	private void buildMain() {
		root = new LinearLayout(this);
		term = null;
		root.setOrientation(LinearLayout.VERTICAL);
		root.setBackgroundColor(pal.bg);

		header = new Views.Header(this, pal);
		root.addView(header);

		HorizontalScrollView hs = new HorizontalScrollView(this);
		hs.setHorizontalScrollBarEnabled(false);
		hs.setBackgroundColor(pal.bg);
		tabs = new LinearLayout(this);
		hs.addView(tabs);
		root.addView(hs);
		tabScroll = hs;
		shownTabs = "";
		peek = label("", Palette.mix(pal.text, pal.bg, 30), 12);
		peek.setPadding(dp(8), dp(2), dp(8), dp(2));
		peek.setSingleLine(true);
		peek.setEllipsize(android.text.TextUtils.TruncateAt.END);
		root.addView(peek);

		list = new ListView(this);
		list.setBackgroundColor(pal.bg);
		list.setDivider(null);
		list.setStackFromBottom(true);
		list.setTranscriptMode(AbsListView.TRANSCRIPT_MODE_NORMAL);
		list.setAdapter(adapter);
		list.setOnItemClickListener((p, v, pos, id) -> activate(rows.get(pos)));
		list.setOnItemLongClickListener((p, v, pos, id) -> {
			menu(rows.get(pos));
			return true;
		});
		/* G192: swipe the messages left or right to move to the next room */
		android.view.GestureDetector swipe = new android.view.GestureDetector(this,
			new android.view.GestureDetector.SimpleOnGestureListener() {
				@Override
				public boolean onFling(android.view.MotionEvent a, android.view.MotionEvent b, float vx, float vy) {
					if (a == null || b == null) return false;
					float dx = b.getX() - a.getX(), dy = b.getY() - a.getY();
					if (Math.abs(dx) < dp(80) || Math.abs(dx) < 2 * Math.abs(dy) || Math.abs(vx) < dp(300)) return false;
					svc.engine().selectStep(dx < 0 ? 1 : -1);
					thumbs.evictAll();
					return true;
				}
			});
		list.setOnTouchListener((v, e) -> swipe.onTouchEvent(e) && false);
		root.addView(list, new LinearLayout.LayoutParams(-1, 0, 1));

		onAir = new Views.OnAir(this, pal);
		root.addView(onAir);

		LinearLayout line = new LinearLayout(this);
		line.setPadding(dp(4), dp(4), dp(4), 0);
		input = field("type a message, or /help", false);
		input.setImeOptions(EditorInfo.IME_ACTION_SEND);
		input.setOnEditorActionListener((v, a, e) -> {
			if (a == EditorInfo.IME_ACTION_SEND || (e != null && e.getKeyCode() == KeyEvent.KEYCODE_ENTER)) {
				sendLine();
				return true;
			}
			return false;
		});
		line.addView(input, new LinearLayout.LayoutParams(0, -2, 1));
		line.addView(button("SEND", v -> sendLine()));
		line.addView(button("IMG", v -> {
			Intent i = new Intent(Intent.ACTION_GET_CONTENT).setType("image/*");
			startActivityForResult(Intent.createChooser(i, "Send an image"), REQ_IMAGE);
		}));
		line.addView(button("LOC", v -> sendLocation()));
		root.addView(line);

		talk = new Views.TalkButton(this, pal);
		talk.toggle = svc.config().pttToggle;
		talk.ptt = new Views.Ptt() {
			@Override
			public void down() {
				Engine e = svc.engine();
				if (talk.toggle) e.pttToggle();
				else e.pttDown();
			}

			@Override
			public void up() {
				if (!talk.toggle) svc.engine().pttUp();
			}
		};
		root.addView(talk);

		LinearLayout acts = new LinearLayout(this);
		acts.setPadding(dp(4), 0, dp(4), dp(4));
		String[] names = {"REPLAY", "UNHEARD", "STOP", "VOX", "ONLINE", "MENU"};
		for (String n : names) {
			TextView b = button(n, v -> action(n));
			b.setTextSize(12);
			b.setPadding(dp(2), dp(10), dp(2), dp(10));
			if (n.equals("ONLINE")) onlineBtn = b;
			acts.addView(b, new LinearLayout.LayoutParams(0, -2, 1));
		}
		root.addView(acts);
		setContentView(root);
		shownVersion = -1;
		refresh();
		if (termPage != null) terminal(termPage); /* a rebuild (theme, settings) keeps the menu open */
	}

	/* ---------- refreshing ---------- */

	private final BaseAdapter adapter = new BaseAdapter() {
		@Override
		public int getCount() {
			return rows.size();
		}

		@Override
		public Object getItem(int i) {
			return rows.get(i);
		}

		@Override
		public long getItemId(int i) {
			return rows.get(i).id;
		}

		@Override
		public View getView(int i, View old, ViewGroup parent) {
			Views.Row r = old instanceof Views.Row ? (Views.Row) old : new Views.Row(MainActivity.this, pal);
			r.pal = pal;
			r.m = rows.get(i);
			r.v = view;
			r.onAir = view != null && view.playingMsgId == r.m.id;
			r.thumbs = MainActivity.this::thumb;
			r.requestLayout();
			r.invalidate();
			return r;
		}
	};

	private Bitmap thumb(long id) {
		Bitmap b = thumbs.get(id);
		if (b != null) return b;
		File f = svc.engine().imageFile(view == null ? 0 : view.active, id, true);
		if (f == null || !f.exists()) return null;
		b = BitmapFactory.decodeFile(f.getPath());
		if (b == null) return null;
		int max = dp(160);
		if (b.getWidth() > max || b.getHeight() > max) {
			float s = Math.min((float) max / b.getWidth(), (float) max / b.getHeight());
			b = Bitmap.createScaledBitmap(b, Math.max(1, (int) (b.getWidth() * s)), Math.max(1, (int) (b.getHeight() * s)), true);
		}
		thumbs.put(id, b);
		return b;
	}

	private void refresh() {
		if (svc == null || svc.engine() == null || pal == null) return;
		view = svc.engine().snapshot();
		if (header != null) {
			header.v = view;
			header.invalidate();
		}
		if (list == null) return;
		if (view.authError != null) {
			buildSetup(); /* fix the account here; the reason is shown on the form */
			return;
		}
		onAir.v = view;
		onAir.invalidate();
		if (onlineBtn != null) {
			onlineBtn.setText(view.offline ? "OFFLINE" : "ONLINE");
			onlineBtn.setTextColor(view.offline ? pal.inkOn(pal.urgent) : pal.text);
			onlineBtn.setBackground(box(view.offline ? pal.urgent : pal.panel, view.offline ? pal.urgent : pal.border));
		}
		talk.v = view;
		talk.invalidate();
		String tabKey = view.active + ":" + view.channels.size() + ":" + view.order;
		StringBuilder sb = new StringBuilder(tabKey);
		for (Engine.ChannelView c : view.channels) {
			sb.append('|').append(c.online).append(c.users).append(c.unread).append(c.muted).append(c.left).append(c.title);
		}
		if (!sb.toString().equals(shownTabs)) {
			shownTabs = sb.toString();
			tabs.removeAllViews();
			View activeTab = null;
			for (int k : view.order) {
				Engine.ChannelView c = view.channels.get(k);
				boolean act = k == view.active;
				String t = tabTitle(c) + "  " + (c.online ? c.users : "-") + (c.unread > 0 ? " *" + c.unread : "") + (c.muted || c.left ? " off" : "");
				TextView tab = button(t, null);
				final int idx = k;
				tab.setOnClickListener(v -> {
					svc.engine().selectChannel(idx);
					thumbs.evictAll();
				});
				/* G192: hold a tab to see who is inside before going in */
				tab.setOnLongClickListener(v -> {
					peekAt = idx;
					peekUntil = System.currentTimeMillis() + 4000;
					roomCard(idx);
					return true;
				});
				tab.setBackground(box(act ? pal.accentDim : pal.bg, act ? pal.accent : pal.border));
				/* G278: a room switched off is dimmed; it still counts what came in, for REPLAY */
				tab.setTextColor(c.online && !c.muted ? pal.text : Palette.mix(pal.text, pal.bg, 50));
				tabs.addView(tab);
				if (act) activeTab = tab;
			}
			if (activeTab != null) {
				View at = activeTab;
				tabScroll.post(() -> tabScroll.smoothScrollTo(Math.max(0, at.getLeft() - dp(24)), 0));
			}
		}
		int pk = System.currentTimeMillis() < peekUntil ? peekAt : view.active;
		if (pk >= 0 && pk < view.channels.size()) {
			Engine.ChannelView c = view.channels.get(pk);
			peek.setText((pk == view.active ? "In here: " : "In " + tabTitle(c) + ": ")
				+ (c.who.isEmpty() ? "nobody else" : String.join(", ", c.who)));
		} else {
			peek.setText("");
		}
		if (termPage != null && term != null) renderTerminal(false);
		boolean playingChanged = view.playingMsgId != shownPlaying;
		if (view.historyVersion != shownVersion || view.active != shownChannel || playingChanged || view.rxName != null) {
			if (view.historyVersion != shownVersion || view.active != shownChannel) {
				rows.clear();
				rows.addAll(svc.engine().history(view.active));
			}
			shownVersion = view.historyVersion;
			shownChannel = view.active;
			shownPlaying = view.playingMsgId;
			adapter.notifyDataSetChanged();
		}
	}

	/* ---------- actions ---------- */

	private void sendLine() {
		String s = input.getText().toString();
		if (s.trim().isEmpty()) return;
		input.setText("");
		svc.engine().input(s);
	}

	private void action(String n) {
		Engine e = svc.engine();
		switch (n) {
		case "REPLAY": e.replayLast(); break;
		case "UNHEARD": e.replayUnheard(); break;
		case "STOP": e.stopPlayback(); break;
		case "VOX": if ("vox".equals(termPage)) closeTerminal(); else terminal("vox"); break;
		case "ONLINE":
			/* the switch: offline frees the account for another client */
			e.setOffline(view == null || !view.offline);
			break;
		case "MENU": if (termPage != null) closeTerminal(); else terminal("main"); break;
		default: break;
		}
	}

	/* ---------- the terminal menu ---------- */

	private void terminal(String page) {
		if (root == null || list == null) return;
		if (term == null) {
			term = new Views.Terminal(this, pal);
			term.onTap = row -> {
				Runnable r = row < termActs.size() ? termActs.get(row) : null;
				if (r != null) r.run();
			};
		}
		boolean opening = termPage == null;
		termPage = page;
		int at = root.indexOfChild(list);
		if (at >= 0) {
			ViewGroup.LayoutParams lp = list.getLayoutParams();
			root.removeViewAt(at);
			root.addView(term, at, lp);
		}
		if (page.equals("vox")) svc.engine().voxVoices();
		term.top();
		renderTerminal(true);
		if (!opening) return;
	}

	private void closeTerminal() {
		if (termPage == null || term == null) return;
		termPage = null;
		int at = root.indexOfChild(term);
		if (at >= 0) {
			ViewGroup.LayoutParams lp = term.getLayoutParams();
			root.removeViewAt(at);
			root.addView(list, at, lp);
		}
		shownVersion = -1; /* the chat redraws from the engine */
		refresh();
	}

	private Views.TermLine line(String text, int kind, Runnable act) {
		termActs.add(act);
		return new Views.TermLine(text, kind, act);
	}

	private static String pad(String s, int w) {
		StringBuilder b = new StringBuilder(s);
		while (b.length() < w) b.append(' ');
		return b.toString();
	}

	private void renderTerminal(boolean reveal) {
		if (term == null || termPage == null || svc == null || svc.engine() == null) return;
		Engine e = svc.engine();
		Engine.View v = view != null ? view : e.snapshot();
		Config c = svc.config();
		List<Views.TermLine> L = new ArrayList<>();
		termActs.clear();
		switch (termPage) {
		case "vox": {
			L.add(line("SYN-CAST ▸ VOX FILTER", 6, null));
			L.add(line("Your take comes back in this voice: pitch, pace and stress kept, the timbre changed.", 4, null));
			L.add(line("Hold TAKE, speak, let go; it converts, then goes out to the room.", 4, null));
			L.add(line("", 0, null));
			List<Engine.Voice> vs = e.voices();
			String cur = v.voxId;
			L.add(line((cur.isEmpty() ? "● " : "○ ") + "UNFILTERED  your own voice", cur.isEmpty() ? 3 : 2, () -> { e.setVox("", ""); terminal("vox"); }));
			String[][] groups = {{"character", "CHARACTERS  the crew's pool"}, {"voice", "VOICES  the other Piper voices"}, {"bank", "BANKS  many speakers, pick a number"}};
			for (String[] g : groups) {
				boolean any = false;
				for (Engine.Voice x : vs) if (x.kind.equals(g[0])) any = true;
				if (!any) continue;
				L.add(line("", 0, null));
				L.add(line(g[1], 1, null));
				for (Engine.Voice x : vs) {
					if (!x.kind.equals(g[0])) continue;
					if (x.kind.equals("bank")) {
						int sp = bankSpeaker.containsKey(x.id) ? bankSpeaker.get(x.id) : 0;
						String id = x.id + "#" + sp;
						boolean on = cur.equals(id);
						L.add(line((on ? "● " : "○ ") + x.name + "  #" + sp, on ? 3 : 2, () -> { e.setVox(id, x.name + " #" + sp); terminal("vox"); }));
						if (on || cur.startsWith(x.id + "#")) {
							L.add(line("    ◂ previous speaker", 2, () -> { bankSpeaker.put(x.id, (sp - 1 + x.speakers) % x.speakers); e.setVox(x.id + "#" + ((sp - 1 + x.speakers) % x.speakers), x.name + " #" + ((sp - 1 + x.speakers) % x.speakers)); terminal("vox"); }));
							L.add(line("    next speaker ▸", 2, () -> { bankSpeaker.put(x.id, (sp + 1) % x.speakers); e.setVox(x.id + "#" + ((sp + 1) % x.speakers), x.name + " #" + ((sp + 1) % x.speakers)); terminal("vox"); }));
						}
					} else {
						boolean on = cur.equals(x.id);
						String st = x.building ? "  building…" : x.ready ? "" : "  (builds on first take)";
						L.add(line((on ? "● " : x.building ? "◌ " : "○ ") + x.name + st, on ? 3 : 2, () -> { e.setVox(x.id, x.name); terminal("vox"); }));
					}
				}
			}
			if (vs.isEmpty()) {
				L.add(line("", 0, null));
				L.add(line("No voices yet: SYN-VOX on the host is not answering, or the cast server has no vox door.", 4, null));
			}
			L.add(line("", 0, null));
			L.add(line("─", 5, null));
			L.add(line("BACK TO THE CHAT", 2, this::closeTerminal));
			break;
		}
		case "rooms": {
			L.add(line("SYN-CAST ▸ ROOMS", 6, null));
			L.add(line("Tap a room to switch it on or off. Off: nothing plays aloud and no alert, but every voice message is kept; switch it on, or REPLAY there, to hear what you missed.", 4, null));
			L.add(line("/join NAME in the chat adds a ticket's room (g227) or a meeting room.", 4, null));
			L.add(line("", 0, null));
			for (int k : v.order) {
				Engine.ChannelView ch = v.channels.get(k);
				final int idx = k;
				boolean on = ch.online && !ch.left && !ch.muted;
				String right = on ? ch.users + " inside" : ch.muted ? "off, kept for REPLAY" : ch.left ? "out" : ch.state.toLowerCase(Locale.ROOT);
				L.add(line((on ? "● " : "○ ") + pad(tabTitle(ch), 24) + right + (ch.unread > 0 ? "  *" + ch.unread : ""), on ? 3 : 2, () -> { e.toggleRoom(idx); terminal("rooms"); }));
			}
			L.add(line("", 0, null));
			L.add(line("─", 5, null));
			L.add(line("BACK", 2, () -> terminal("main")));
			break;
		}
		case "settings": {
			L.add(line("SYN-CAST ▸ SETTINGS", 6, null));
			L.add(line("Tap a line to flip it. It is saved at once.", 4, null));
			L.add(line("", 0, null));
			Object[][] opts = {
				{"Tap to start and stop talking (instead of hold)", c.pttToggle, (Runnable) () -> c.pttToggle = !c.pttToggle},
				{"Play incoming messages as they arrive", c.autoPlay, (Runnable) () -> c.autoPlay = !c.autoPlay},
				{"Noise suppression (not on Vox takes)", c.noiseSuppression, (Runnable) () -> c.noiseSuppression = !c.noiseSuppression},
				{"Volume-down key talks (while the app is open)", c.volumeKeyPtt, (Runnable) () -> c.volumeKeyPtt = !c.volumeKeyPtt},
				{"Play through the loudspeaker", c.speaker, (Runnable) () -> c.speaker = !c.speaker},
				{"Notifications when the app is in the background", c.notify, (Runnable) () -> c.notify = !c.notify},
				{"Tones (talk permit, end of message)", c.tones, (Runnable) () -> c.tones = !c.tones},
				{"Hands-free: talking opens the channel, no button", c.vox, (Runnable) () -> c.vox = !c.vox},
			};
			for (Object[] o : opts) {
				final Runnable flip = (Runnable) o[2];
				L.add(line(((Boolean) o[1] ? "[x] " : "[ ] ") + o[0], 2, () -> { flip.run(); saveConfig(c); terminal("settings"); }));
			}
			int pct = (int) Math.round(c.voxThreshold * 100);
			L.add(line("", 0, null));
			L.add(line("HANDS-FREE SENSITIVITY  " + bar(pct) + " " + pct + "%", 1, null));
			L.add(line("    ◂ less sensitive", 2, () -> { c.voxThreshold = Math.max(0, c.voxThreshold - 0.1); saveConfig(c); terminal("settings"); }));
			L.add(line("    more sensitive ▸", 2, () -> { c.voxThreshold = Math.min(1, c.voxThreshold + 0.1); saveConfig(c); terminal("settings"); }));
			L.add(line("", 0, null));
			L.add(line("─", 5, null));
			L.add(line("BACK", 2, () -> terminal("main")));
			break;
		}
		case "theme": {
			L.add(line("SYN-CAST ▸ THEME", 6, null));
			L.add(line("", 0, null));
			List<String> names = new ArrayList<>(Palette.names(this));
			boolean follow = c.theme.isEmpty();
			L.add(line((follow ? "● " : "○ ") + "FOLLOW SYN-OS", follow ? 3 : 2, () -> { c.theme = ""; saveConfig(c); build(); }));
			for (String n : names) {
				boolean on = n.equals(c.theme);
				L.add(line((on ? "● " : "○ ") + n, on ? 3 : 2, () -> { c.theme = n; saveConfig(c); build(); }));
			}
			L.add(line("", 0, null));
			L.add(line("─", 5, null));
			L.add(line("BACK", 2, () -> terminal("main")));
			break;
		}
		default: {
			int on = 0, off = 0;
			for (int k : v.order) { Engine.ChannelView ch = v.channels.get(k); if (ch.online && !ch.left && !ch.muted) on++; else off++; }
			L.add(line("SYN-CAST ▸ MENU", 6, null));
			L.add(line(v.user.isEmpty() ? "" : v.user + " @ " + v.server.replaceFirst("^wss?://", "").replaceFirst("/.*$", ""), 4, null));
			L.add(line("", 0, null));
			L.add(line(pad("VOX FILTER", 18) + v.voxName, 2, () -> terminal("vox")));
			L.add(line(pad("ROOMS", 18) + on + " on · " + off + " off", 2, () -> terminal("rooms")));
			L.add(line(pad("REPLAY UNHEARD", 18) + "every room's voice messages you have not heard", 2, () -> { e.replayUnheard(); closeTerminal(); }));
			L.add(line(pad("REPLAY SPEED", 18) + Views.speedLabel(v.speed), 2, () -> { e.cycleSpeed(); main.postDelayed(() -> { refresh(); renderTerminal(false); }, 80); }));
			L.add(line(pad("HANDS-FREE", 18) + (v.vox ? "on" : "off"), 2, () -> { e.toggleVoxActivation(); main.postDelayed(() -> { refresh(); renderTerminal(false); }, 80); }));
			L.add(line(pad("SETTINGS", 18) + "talk mode, playback, tones…", 2, () -> terminal("settings")));
			L.add(line(pad("THEME", 18) + (c.theme.isEmpty() ? "FOLLOW SYN-OS" : c.theme), 2, () -> terminal("theme")));
			L.add(line("", 0, null));
			L.add(line("─", 5, null));
			L.add(line(pad("EXIT", 18) + "leave the channel and close", 2, () -> {
				startService(new Intent(this, CastService.class).setAction(CastService.ACTION_EXIT));
				finishAndRemoveTask();
			}));
			L.add(line("BACK TO THE CHAT", 2, this::closeTerminal));
		}
		}
		term.show(L, reveal);
	}

	private static String bar(int pct) {
		StringBuilder b = new StringBuilder();
		for (int i = 0; i < 10; i++) b.append(i * 10 < pct ? '█' : '░');
		return b.toString();
	}

	private void saveConfig(Config c) {
		c.channels = new ArrayList<>(); /* G192: every room the member belongs to; none typed */
		try {
			svc.save(c);
		} catch (IOException ignored) {
		}
	}

	private void activate(History.Msg m) {
		switch (m.kind) {
		case VOICE:
			svc.engine().replay(view.active, m.id);
			break;
		case IMAGE:
			showImage(m);
			break;
		case LOCATION: {
			Uri u = Uri.parse(String.format(Locale.ROOT, "geo:%f,%f?q=%f,%f(%s)", m.lat, m.lon, m.lat, m.lon,
				Uri.encode(m.from)));
			try {
				startActivity(new Intent(Intent.ACTION_VIEW, u));
			} catch (RuntimeException ignored) {
			}
			break;
		}
		case TEXT:
			copy(m.text);
			break;
		default:
			break;
		}
	}

	private void copy(String text) {
		if (text == null) return;
		getSystemService(ClipboardManager.class).setPrimaryClip(ClipData.newPlainText("SYN-CAST", text));
	}

	private void menu(History.Msg m) {
		List<String> items = new ArrayList<>();
		if (m.kind == History.Kind.VOICE && !m.partial) items.add("Replay");
		if (m.text != null && !m.text.isEmpty()) items.add("Copy text");
		if (m.kind == History.Kind.IMAGE || m.kind == History.Kind.LOCATION) items.add("Open");
		if (items.isEmpty()) return;
		new AlertDialog.Builder(this).setItems(items.toArray(new String[0]), (d, w) -> {
			String it = items.get(w);
			if (it.equals("Copy text")) copy(m.text);
			else activate(m);
		}).show();
	}

	private void showImage(History.Msg m) {
		File f = svc.engine().imageFile(view.active, m.id, false);
		if (f == null || !f.exists()) return;
		BitmapFactory.Options o = new BitmapFactory.Options();
		o.inJustDecodeBounds = true;
		BitmapFactory.decodeFile(f.getPath(), o);
		int max = Math.max(getResources().getDisplayMetrics().widthPixels, getResources().getDisplayMetrics().heightPixels);
		o.inSampleSize = 1;
		while (Math.max(o.outWidth, o.outHeight) / o.inSampleSize > max * 2) o.inSampleSize *= 2;
		o.inJustDecodeBounds = false;
		Bitmap b = BitmapFactory.decodeFile(f.getPath(), o);
		if (b == null) return;
		Dialog d = new Dialog(this, android.R.style.Theme_Black_NoTitleBar_Fullscreen);
		ImageView iv = new ImageView(this);
		iv.setImageBitmap(b);
		iv.setBackgroundColor(0xFF000000);
		iv.setOnClickListener(v -> d.dismiss());
		d.setContentView(iv);
		d.show();
	}

	/* ---------- images out ---------- */

	@Override
	protected void onActivityResult(int req, int res, Intent data) {
		if (res != RESULT_OK || data == null || data.getData() == null) return;
		Uri u = data.getData();
		if (req == REQ_IMAGE) {
			new Thread(() -> prepareImage(u)).start();
		} else if (req == REQ_CERT && setupCert != null) {
			try (InputStream in = getContentResolver().openInputStream(u)) {
				ByteArrayOutputStream b = new ByteArrayOutputStream();
				byte[] buf = new byte[4096];
				int n;
				while (in != null && (n = in.read(buf)) > 0 && b.size() < 65536) b.write(buf, 0, n);
				setupCert.setText(b.toString("UTF-8"));
			} catch (IOException ignored) {
			}
		}
	}

	private static Bitmap scaled(Bitmap src, int max) {
		float s = Math.min(1f, (float) max / Math.max(src.getWidth(), src.getHeight()));
		if (s >= 1f) return src;
		return Bitmap.createScaledBitmap(src, Math.max(1, Math.round(src.getWidth() * s)),
			Math.max(1, Math.round(src.getHeight() * s)), true);
	}

	private void prepareImage(Uri u) {
		try {
			BitmapFactory.Options o = new BitmapFactory.Options();
			o.inJustDecodeBounds = true;
			try (InputStream in = getContentResolver().openInputStream(u)) {
				BitmapFactory.decodeStream(in, null, o);
			}
			o.inSampleSize = 1;
			while (Math.max(o.outWidth, o.outHeight) / (o.inSampleSize * 2) >= 1280) o.inSampleSize *= 2;
			o.inJustDecodeBounds = false;
			Bitmap b;
			try (InputStream in = getContentResolver().openInputStream(u)) {
				b = BitmapFactory.decodeStream(in, null, o);
			}
			if (b == null) throw new IOException("not an image");
			int rot = 0;
			try (InputStream in = getContentResolver().openInputStream(u)) {
				int or = new ExifInterface(in).getAttributeInt(ExifInterface.TAG_ORIENTATION, ExifInterface.ORIENTATION_NORMAL);
				rot = or == ExifInterface.ORIENTATION_ROTATE_90 ? 90 : or == ExifInterface.ORIENTATION_ROTATE_180 ? 180
					: or == ExifInterface.ORIENTATION_ROTATE_270 ? 270 : 0;
			} catch (IOException | RuntimeException ignored) {
			}
			if (rot != 0) {
				Matrix mx = new Matrix();
				mx.postRotate(rot);
				b = Bitmap.createBitmap(b, 0, 0, b.getWidth(), b.getHeight(), mx, true);
			}
			Bitmap full = scaled(b, 1280), small = scaled(b, 160);
			ByteArrayOutputStream fo = new ByteArrayOutputStream(), to = new ByteArrayOutputStream();
			full.compress(Bitmap.CompressFormat.JPEG, 85, fo);
			small.compress(Bitmap.CompressFormat.JPEG, 75, to);
			svc.engine().sendImage(fo.toByteArray(), to.toByteArray(), full.getWidth(), full.getHeight());
		} catch (IOException | RuntimeException e) {
			main.post(() -> android.widget.Toast.makeText(this, "Image: " + e.getMessage(), android.widget.Toast.LENGTH_LONG).show());
		}
	}

	/* ---------- location out ---------- */

	@SuppressWarnings({"MissingPermission", "deprecation"})
	private void sendLocation() {
		if (checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) != PackageManager.PERMISSION_GRANTED) {
			requestPermissions(new String[] {Manifest.permission.ACCESS_FINE_LOCATION, Manifest.permission.ACCESS_COARSE_LOCATION},
				REQ_LOCATION);
			return;
		}
		LocationManager lm = getSystemService(LocationManager.class);
		String provider = lm.isProviderEnabled(LocationManager.NETWORK_PROVIDER) ? LocationManager.NETWORK_PROVIDER
			: LocationManager.GPS_PROVIDER;
		java.util.function.Consumer<Location> got = loc -> {
			if (loc == null) {
				android.widget.Toast.makeText(this, "No location fix", android.widget.Toast.LENGTH_SHORT).show();
				return;
			}
			new Thread(() -> {
				String addr = "";
				try {
					if (Geocoder.isPresent()) {
						List<Address> a = new Geocoder(this).getFromLocation(loc.getLatitude(), loc.getLongitude(), 1);
						if (a != null && !a.isEmpty()) addr = a.get(0).getAddressLine(0);
					}
				} catch (IOException | RuntimeException ignored) {
				}
				svc.engine().sendLocation(loc.getLatitude(), loc.getLongitude(), loc.getAccuracy(), addr);
			}).start();
		};
		if (Build.VERSION.SDK_INT >= 30) {
			lm.getCurrentLocation(provider, null, getMainExecutor(), got);
		} else {
			Location last = lm.getLastKnownLocation(provider);
			got.accept(last);
		}
	}

	/* ---------- settings ---------- */

	/** A tab's words: a ticket room's "G192 SYN-CAST rooms bar..." cut to fit, a fixed room's name as it is. */
	private static String tabTitle(Engine.ChannelView c) {
		String t = c.title == null || c.title.isEmpty() ? c.name : c.title;
		return t.length() > 22 ? t.substring(0, 21) + "\u2026" : t;
	}

	/** G192: who is inside a room, from a held tab, with the choice to go in. */
	private void roomCard(int k) {
		Engine.View v = svc.engine().snapshot();
		if (k < 0 || k >= v.channels.size()) return;
		Engine.ChannelView c = v.channels.get(k);
		String head = (c.title == null || c.title.isEmpty() ? c.name : c.title) + "  ("
			+ (!c.peer.isEmpty() ? "direct messages" : (c.ticket ? "ticket room, " : "room, ") + c.users + " inside") + ")";
		/* G193: tap a name to open the direct messages with them */
		List<String> names = new java.util.ArrayList<>();
		for (String w : c.who) if (!w.equalsIgnoreCase(v.user)) names.add(w);
		AlertDialog.Builder b = new AlertDialog.Builder(this).setTitle(head);
		if (!c.peer.isEmpty()) {
			b.setMessage("Only you and " + c.peer + " see these.");
		} else if (names.isEmpty()) {
			b.setMessage("Nobody else is in it now. Type /msg NAME to message anyone.");
		} else {
			List<String> items = new java.util.ArrayList<>();
			for (String w : names) items.add("MESSAGE " + w);
			b.setItems(items.toArray(new String[0]), (d, w) -> {
				svc.engine().openDm(names.get(w));
				thumbs.evictAll();
			});
		}
		b.setPositiveButton("GO IN", (d, w) -> {
			svc.engine().selectChannel(k);
			thumbs.evictAll();
		}).setNegativeButton("CLOSE", null).show();
	}

	/* ---------- hardware talk keys ---------- */

	@Override
	public boolean dispatchKeyEvent(KeyEvent e) {
		Config c = svc == null ? null : svc.config();
		if (c != null && c.volumeKeyPtt && c.hasAccount() && e.getKeyCode() == KeyEvent.KEYCODE_VOLUME_DOWN) {
			Engine eng = svc.engine();
			if (e.getAction() == KeyEvent.ACTION_DOWN && e.getRepeatCount() == 0 && !volumeDown) {
				volumeDown = true;
				if (c.pttToggle) eng.pttToggle();
				else eng.pttDown();
			} else if (e.getAction() == KeyEvent.ACTION_UP && volumeDown) {
				volumeDown = false;
				if (!c.pttToggle) eng.pttUp();
			}
			return true;
		}
		return super.dispatchKeyEvent(e);
	}
}
