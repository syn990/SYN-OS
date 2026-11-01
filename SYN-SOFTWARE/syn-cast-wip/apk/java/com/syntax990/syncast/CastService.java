/* ------------------------------------------------------------------------
 *   The radio itself: a foreground service that owns the engine and the
 *   audio, so the channel keeps playing with the screen off and the app in
 *   the background. Its notification shows who's on air and carries TALK
 *   and EXIT; texts and voice that arrive while the window is hidden get a
 *   notification of their own.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CAST-APK
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
package com.syntax990.syncast;

import android.Manifest;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.pm.ServiceInfo;
import android.net.wifi.WifiManager;
import android.os.Binder;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.PowerManager;
import android.util.Log;

import com.syntax990.syncast.core.Config;
import com.syntax990.syncast.core.Engine;

import java.io.File;
import java.io.IOException;

public final class CastService extends Service implements Engine.Platform {
	static final String TAG = "syn-cast";
	private static final String CH_SERVICE = "service", CH_MESSAGES = "messages";
	private static final int NOTE_SERVICE = 1;
	static final String ACTION_TALK = "com.syntax990.syncast.TALK", ACTION_EXIT = "com.syntax990.syncast.EXIT";

	final class Local extends Binder {
		CastService service() {
			return CastService.this;
		}
	}

	private final Local binder = new Local();
	private final Handler main = new Handler(Looper.getMainLooper());
	private AndroidAudio audio;
	private Engine engine;
	private Config cfg;
	private Runnable listener;
	private final java.util.concurrent.atomic.AtomicBoolean refreshPending = new java.util.concurrent.atomic.AtomicBoolean();
	private volatile boolean visible;
	private PowerManager.WakeLock wake;
	private WifiManager.WifiLock wifi;
	private int noteId = 100;
	private String lastOnAir = "";

	static File configFile(Context c) {
		return new File(c.getFilesDir(), "config.json");
	}

	/* ---------- lifecycle ---------- */

	@Override
	public void onCreate() {
		super.onCreate();
		NotificationManager nm = getSystemService(NotificationManager.class);
		nm.createNotificationChannel(new NotificationChannel(CH_SERVICE, "Connection", NotificationManager.IMPORTANCE_LOW));
		NotificationChannel msgs = new NotificationChannel(CH_MESSAGES, "Messages", NotificationManager.IMPORTANCE_HIGH);
		nm.createNotificationChannel(msgs);
		goForeground();
		PowerManager pm = getSystemService(PowerManager.class);
		wake = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "syn-cast:radio");
		wake.setReferenceCounted(false);
		wake.acquire();
		WifiManager wm = (WifiManager) getApplicationContext().getSystemService(Context.WIFI_SERVICE);
		wifi = wm.createWifiLock(Build.VERSION.SDK_INT >= 29 ? WifiManager.WIFI_MODE_FULL_LOW_LATENCY
			: WifiManager.WIFI_MODE_FULL_HIGH_PERF, "syn-cast:radio");
		wifi.setReferenceCounted(false);
		wifi.acquire();
		audio = new AndroidAudio(this);
		restart();
	}

	@Override
	public int onStartCommand(Intent intent, int flags, int startId) {
		if (intent != null && ACTION_EXIT.equals(intent.getAction())) {
			stopSelf();
			return START_NOT_STICKY;
		}
		if (intent != null && ACTION_TALK.equals(intent.getAction()) && engine != null) {
			engine.pttToggle();
		}
		goForeground(); /* the microphone type, once the permission exists */
		return START_STICKY;
	}

	@Override
	public void onDestroy() {
		if (engine != null) engine.stop();
		audio.stop();
		if (wake.isHeld()) wake.release();
		if (wifi.isHeld()) wifi.release();
		super.onDestroy();
	}

	@Override
	public IBinder onBind(Intent intent) {
		return binder;
	}

	/** (Re)reads the config and starts a fresh engine. */
	void restart() {
		if (engine != null) {
			engine.stop();
			audio.stop();
		}
		cfg = Config.load(configFile(this));
		audio.speaker = cfg.speaker;
		audio.noiseSuppression = cfg.noiseSuppression;
		audio.voxThreshold = cfg.voxThreshold;
		engine = new Engine(cfg, new File(getFilesDir(), "history"), this);
		audio.start(engine);
		engine.start();
		changed();
	}

	void save(Config c) throws IOException {
		c.save(configFile(this));
		restart();
	}

	Engine engine() {
		return engine;
	}

	Config config() {
		return cfg;
	}

	void setListener(Runnable r) {
		listener = r;
	}

	void setVisible(boolean v) {
		visible = v;
	}

	private void goForeground() {
		Notification n = serviceNote("");
		if (Build.VERSION.SDK_INT >= 29) {
			int type = ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK;
			/* the microphone type exists from Android 11; before that the mic needs no type */
			if (Build.VERSION.SDK_INT >= 30
				&& checkSelfPermission(Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED) {
				type |= ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE;
			}
			startForeground(NOTE_SERVICE, n, type);
		} else {
			startForeground(NOTE_SERVICE, n);
		}
	}

	private PendingIntent self(String action) {
		Intent i = new Intent(this, CastService.class).setAction(action);
		return PendingIntent.getService(this, action.hashCode(), i, PendingIntent.FLAG_IMMUTABLE);
	}

	private Notification serviceNote(String onAir) {
		Intent open = new Intent(this, MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP);
		String text = !onAir.isEmpty() ? onAir : cfg != null && cfg.hasAccount() ? "On the channel" : "Not signed in";
		return new Notification.Builder(this, CH_SERVICE).setSmallIcon(R.drawable.ic_stat)
			.setContentTitle("SYN-CAST").setContentText(text).setOngoing(true)
			.setContentIntent(PendingIntent.getActivity(this, 0, open, PendingIntent.FLAG_IMMUTABLE))
			.addAction(new Notification.Action.Builder(null, "TALK", self(ACTION_TALK)).build())
			.addAction(new Notification.Action.Builder(null, "EXIT", self(ACTION_EXIT)).build()).build();
	}

	/* ---------- Engine.Platform ---------- */

	@Override public int playWrite(short[] pcm, int n) { return audio.playWrite(pcm, n); }
	@Override public int playQueued() { return audio.playQueued(); }
	@Override public long playWritten() { return audio.playWritten(); }
	@Override public long playPlayed() { return audio.playPlayed(); }
	@Override public int playLevel() { return audio.playLevel(); }
	@Override public void playFlush() { audio.playFlush(); }
	@Override public void captureBegin() { audio.captureBegin(); }
	@Override public void captureEnd() { audio.captureEnd(); }
	@Override public void captureVox(boolean on) { audio.captureVox(on); }
	@Override public int captureLevel() { return audio.captureLevel(); }
	@Override public void tone(String meaning) { audio.tone(meaning); }
	@Override public boolean focused() { return visible; }
	@Override public void log(String line) { Log.i(TAG, line); }

	@Override
	public void notify(String title, String text) {
		if (visible || !cfg.notify) return;
		Intent open = new Intent(this, MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP);
		Notification n = new Notification.Builder(this, CH_MESSAGES).setSmallIcon(R.drawable.ic_stat)
			.setContentTitle(title).setContentText(text).setAutoCancel(true)
			.setContentIntent(PendingIntent.getActivity(this, 1, open, PendingIntent.FLAG_IMMUTABLE)).build();
		main.post(() -> {
			try {
				getSystemService(NotificationManager.class).notify(noteId++, n);
			} catch (SecurityException ignored) {
				/* notifications not allowed */
			}
		});
	}

	@Override
	public void changed() {
		if (!refreshPending.compareAndSet(false, true)) return;
		main.postDelayed(() -> {
			refreshPending.set(false);
			if (listener != null) listener.run();
			if (engine != null) {
				Engine.View v = engine.snapshot();
				String onAir = v.rxName != null ? "RX " + v.rxName + " in " + v.rxChannel
					: v.txDest != null ? "TALKING to " + v.txDest : "";
				if (!onAir.equals(lastOnAir)) {
					lastOnAir = onAir;
					getSystemService(NotificationManager.class).notify(NOTE_SERVICE, serviceNote(onAir));
				}
			}
		}, 33);
	}
}
