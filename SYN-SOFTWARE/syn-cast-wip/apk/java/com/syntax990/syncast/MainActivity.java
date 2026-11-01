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
	private static final int REQ_PERMS = 1, REQ_IMAGE = 2, REQ_KEY = 3, REQ_LOCATION = 4;

	private CastService svc;
	private Palette pal;
	private final Handler main = new Handler(Looper.getMainLooper());

	private Views.Header header;
	private LinearLayout tabs;
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
	private EditText setupKey;
	private TextView onlineBtn;

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
		seedConfig();
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
		moveTaskToBack(true); /* the radio stays on; EXIT is in the notification and settings */
	}

	/** First run of a personal build: the developer key it carries, and the names it must not use. */
	private void seedConfig() {
		Map<String, Object> dk = devkey();
		if (dk == null) return;
		File f = CastService.configFile(this);
		Config c = Config.load(f);
		if (!c.issuer.isEmpty() && !c.keyPem.isEmpty()) return;
		c.issuer = Json.str(dk, "issuer");
		c.keyPem = Json.str(dk, "key_pem");
		try {
			c.save(f);
		} catch (IOException ignored) {
		}
	}

	private Map<String, Object> devkey() {
		try (InputStream in = getAssets().open("devkey.json")) {
			ByteArrayOutputStream b = new ByteArrayOutputStream();
			byte[] buf = new byte[4096];
			int n;
			while ((n = in.read(buf)) > 0) b.write(buf, 0, n);
			return Json.object(b.toString("UTF-8"));
		} catch (IOException e) {
			return null;
		}
	}

	private List<String> reserved() {
		List<String> out = new ArrayList<>();
		try (InputStream in = getAssets().open("reserved.json")) {
			byte[] b = new byte[in.available()];
			int n = in.read(b);
			Object v = Json.parse(new String(b, 0, Math.max(0, n), StandardCharsets.UTF_8));
			if (v instanceof List) {
				for (Object o : (List<?>) v) if (o instanceof String) out.add((String) o);
			}
		} catch (IOException | RuntimeException ignored) {
		}
		return out;
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

		col.addView(label("Sign in to Zello", pal.accent, 18));
		col.addView(label("Your own Zello account. Everything you send goes to the whole channel.", pal.text, 13));
		col.addView(label("SERVER", pal.accent, 12));
		EditText server = field("wss://zello.io/ws", false);
		server.setText(c.server);
		col.addView(server);
		col.addView(label("USERNAME", pal.accent, 12));
		EditText user = field("username", false);
		user.setText(c.username);
		col.addView(user);
		col.addView(label("PASSWORD", pal.accent, 12));
		EditText pass = field("password", true);
		pass.setText(c.password);
		col.addView(pass);
		col.addView(label("CHANNELS (comma separated)", pal.accent, 12));
		EditText chans = field("syntaxcorporation", false);
		chans.setText(String.join(", ", c.channels));
		col.addView(chans);

		boolean builtIn = devkey() != null && !c.issuer.isEmpty() && !c.keyPem.isEmpty();
		EditText issuer = field("developer issuer", false);
		setupKey = field("-----BEGIN PRIVATE KEY----- ...", false);
		if (builtIn) {
			col.addView(label("DEVELOPER KEY: built into this APK", pal.text, 12));
		} else {
			col.addView(label("DEVELOPER ISSUER (developers.zello.com/keys)", pal.accent, 12));
			issuer.setText(c.issuer);
			col.addView(issuer);
			col.addView(label("DEVELOPER PRIVATE KEY (PEM)", pal.accent, 12));
			setupKey.setSingleLine(false);
			setupKey.setMinLines(3);
			setupKey.setText(c.keyPem);
			col.addView(setupKey);
			TextView load = button("LOAD KEY FILE", v -> {
				Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*");
				startActivityForResult(i, REQ_KEY);
			});
			col.addView(load, new LinearLayout.LayoutParams(-1, -2));
		}
		TextView err = label("", pal.urgent, 13);
		Engine.View now = svc.engine().snapshot();
		if (now.authError != null) err.setText("Zello refused the sign-in: " + now.authError + ".");
		TextView go = button("SIGN IN", v -> {
			Config n = svc.config();
			n.server = server.getText().toString().trim();
			n.username = user.getText().toString().trim();
			n.password = pass.getText().toString();
			n.channels.clear();
			for (String s : chans.getText().toString().split(",")) {
				if (!s.trim().isEmpty()) n.channels.add(s.trim());
			}
			if (!builtIn) {
				n.issuer = issuer.getText().toString().trim();
				n.keyPem = setupKey.getText().toString().trim();
			}
			for (String r : reserved()) {
				if (r.equalsIgnoreCase(n.username)) {
					/* one session per Zello account: this one would knock the crew off */
					err.setText(n.username + " is SYN-CASTER's account. Zello allows one session per account, so use your own.");
					return;
				}
			}
			if (n.username.isEmpty() || n.channels.isEmpty()) {
				err.setText("A username and at least one channel are needed.");
				return;
			}
			if (n.authToken.isEmpty() && (n.issuer.isEmpty() || n.keyPem.isEmpty())) {
				err.setText("Zello's Channel API needs your developer issuer and private key "
					+ "(developers.zello.com/keys), or the personal build that carries them.");
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
		LinearLayout root = new LinearLayout(this);
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
		shownTabs = "";

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
		String[] names = {"REPLAY", "STOP", "SPEED", "BUSY", "MUTE", "ONLINE", "SET"};
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
		String tabKey = view.active + ":" + view.channels.size();
		StringBuilder sb = new StringBuilder(tabKey);
		for (Engine.ChannelView c : view.channels) sb.append('|').append(c.online).append(c.users).append(c.unread).append(c.muted);
		if (!sb.toString().equals(shownTabs)) {
			shownTabs = sb.toString();
			tabs.removeAllViews();
			for (int k = 0; k < view.channels.size(); k++) {
				Engine.ChannelView c = view.channels.get(k);
				boolean act = k == view.active;
				String t = c.name + "  " + (c.online ? c.users : "-") + (c.unread > 0 ? " *" + c.unread : "") + (c.muted ? " M" : "");
				TextView tab = button(t, null);
				final int idx = k;
				tab.setOnClickListener(v -> {
					svc.engine().selectChannel(idx);
					thumbs.evictAll();
				});
				tab.setBackground(box(act ? pal.accentDim : pal.bg, act ? pal.accent : pal.border));
				tab.setTextColor(c.online ? pal.text : Palette.mix(pal.text, pal.bg, 50));
				tabs.addView(tab);
			}
		}
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
		case "STOP": e.stopPlayback(); break;
		case "SPEED": e.cycleSpeed(); break;
		case "BUSY": e.toggleBusy(); break;
		case "MUTE": e.toggleMute(); break;
		case "ONLINE":
			/* the switch: offline frees the account for another client */
			e.setOffline(view == null || !view.offline);
			break;
		case "SET": settings(); break;
		default: break;
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
		} else if (req == REQ_KEY && setupKey != null) {
			try (InputStream in = getContentResolver().openInputStream(u)) {
				ByteArrayOutputStream b = new ByteArrayOutputStream();
				byte[] buf = new byte[4096];
				int n;
				while (in != null && (n = in.read(buf)) > 0 && b.size() < 65536) b.write(buf, 0, n);
				setupKey.setText(b.toString("UTF-8"));
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

	private void settings() {
		Config c = svc.config();
		ScrollView sv = new ScrollView(this);
		LinearLayout col = new LinearLayout(this);
		col.setOrientation(LinearLayout.VERTICAL);
		col.setPadding(dp(16), dp(8), dp(16), dp(8));
		col.setBackgroundColor(pal.bg);
		sv.addView(col);
		col.addView(label("CHANNELS (comma separated; one is joined at a time, the tabs switch)", pal.accent, 12));
		EditText chans = field("syntaxcorporation, another", false);
		chans.setText(String.join(", ", c.channels));
		col.addView(chans);
		CheckBox toggle = check(col, "Tap to start and stop talking (instead of hold)", c.pttToggle);
		CheckBox auto = check(col, "Play incoming messages as they arrive", c.autoPlay);
		CheckBox ns = check(col, "Noise suppression", c.noiseSuppression);
		CheckBox vol = check(col, "Volume-down key talks (while the app is open)", c.volumeKeyPtt);
		CheckBox spk = check(col, "Play through the loudspeaker", c.speaker);
		CheckBox note = check(col, "Notifications when the app is in the background", c.notify);
		CheckBox tones = check(col, "Tones (talk permit, end of message)", c.tones);
		CheckBox vox = check(col, "VOX: talking opens the channel, no button", c.vox);
		col.addView(label("VOX SENSITIVITY", pal.accent, 12));
		SeekBar sens = new SeekBar(this);
		sens.setMax(100);
		sens.setProgress((int) Math.round(c.voxThreshold * 100));
		col.addView(sens);
		final String follow = "FOLLOW SYN-OS";
		final String[] theme = {c.theme};
		TextView th = button("THEME: " + (theme[0].isEmpty() ? follow : theme[0]), null);
		th.setOnClickListener(v -> {
			List<String> names = new java.util.ArrayList<>(Palette.names(this));
			names.add(0, follow);
			new AlertDialog.Builder(this).setItems(names.toArray(new String[0]), (d, w) -> {
				theme[0] = w == 0 ? "" : names.get(w);
				th.setText("THEME: " + names.get(w));
			}).show();
		});
		LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-1, -2);
		lp.topMargin = dp(12);
		col.addView(th, lp);
		AlertDialog[] dlg = new AlertDialog[1];
		TextView exit = button("EXIT (leave the channel)", v -> {
			dlg[0].dismiss();
			startService(new Intent(this, CastService.class).setAction(CastService.ACTION_EXIT));
			finishAndRemoveTask();
		});
		col.addView(exit, lp);
		dlg[0] = new AlertDialog.Builder(this).setView(sv).setPositiveButton("SAVE", (d, w) -> {
			List<String> list = new ArrayList<>();
			for (String s : chans.getText().toString().split(",")) {
				if (!s.trim().isEmpty() && !list.contains(s.trim())) list.add(s.trim());
			}
			if (!list.isEmpty()) c.channels = list; /* an empty list would leave nothing to join */
			c.pttToggle = toggle.isChecked();
			c.autoPlay = auto.isChecked();
			c.noiseSuppression = ns.isChecked();
			c.volumeKeyPtt = vol.isChecked();
			c.speaker = spk.isChecked();
			c.notify = note.isChecked();
			c.tones = tones.isChecked();
			c.vox = vox.isChecked();
			c.voxThreshold = sens.getProgress() / 100.0;
			c.theme = theme[0];
			try {
				svc.save(c);
			} catch (IOException ignored) {
			}
			build();
		}).setNegativeButton("CANCEL", null).create();
		dlg[0].show();
	}

	private CheckBox check(LinearLayout col, String text, boolean on) {
		CheckBox b = new CheckBox(this);
		b.setText(text);
		b.setChecked(on);
		b.setTextColor(pal.text);
		b.setTypeface(android.graphics.Typeface.MONOSPACE);
		b.setTextSize(13);
		col.addView(b);
		return b;
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
