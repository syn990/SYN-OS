/* ------------------------------------------------------------------------
 *   Voice in and out on the phone, at the engine's 16 kHz mono.
 *
 *   Out: a ring the engine fills, drained by a thread into an AudioTrack,
 *   which takes transient audio focus (music ducks) while voice plays.
 *   In:  AudioRecord from the VOICE_COMMUNICATION source (the phone's own
 *   echo cancelling and noise suppression), plus the platform
 *   NoiseSuppressor when asked; opened only while talking, or all the time
 *   with VOX. Speech is brought up to a steady level (core/Gain), then the
 *   frames go through libopus with the Zello app's settings.
 *   Tones: the SYN-OS tone vocabulary (syn-bar-core's syn_tone_vocab.h),
 *   synthesised here.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CAST-APK
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
package com.syntax990.syncast;

import android.Manifest;
import android.content.Context;
import android.content.pm.PackageManager;
import android.media.AudioAttributes;
import android.media.AudioFocusRequest;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioRecord;
import android.media.AudioTrack;
import android.media.MediaRecorder;
import android.media.audiofx.AutomaticGainControl;
import android.media.audiofx.NoiseSuppressor;
import android.util.Log;

import com.syntax990.syncast.core.Engine;
import com.syntax990.syncast.core.Gain;
import com.syntax990.syncast.core.Opus;

final class AndroidAudio {
	private static final String TAG = "syn-cast";
	private static final int VOX_PREROLL = 5, VOX_ATTACK = 2, VOX_HANG_MS = 1200;

	private final Context ctx;
	private final AudioManager am;
	private Engine engine;
	/* each start() runs a new generation of threads; the old ones see the
	 * number change and leave, even if a restart is quick */
	private volatile int gen;
	volatile boolean speaker = true;
	volatile boolean noiseSuppression = true;
	volatile double voxThreshold = 0.5;

	AndroidAudio(Context ctx) {
		this.ctx = ctx;
		this.am = (AudioManager) ctx.getSystemService(Context.AUDIO_SERVICE);
	}

	void start(Engine e) {
		engine = e;
		final int my = ++gen;
		new Thread(() -> drain(my), "syncast-playback").start();
		new Thread(() -> capture(my, e), "syncast-capture").start();
	}

	void stop() {
		gen++;
		synchronized (ring) {
			ring.notifyAll();
		}
		synchronized (mic) {
			mic.notifyAll();
		}
	}

	/* ---------- playback ---------- */

	private final Object ring = new Object();
	private final short[] buf = new short[Opus.RATE * 4];
	private int head, len;
	private long written, played;
	private volatile int level;
	private boolean flush;

	private static int levelOf(short[] s, int n) {
		if (n == 0) return 0;
		double sum = 0;
		for (int k = 0; k < n; k++) sum += (double) s[k] * s[k];
		double rms = Math.sqrt(sum / n);
		if (rms < 1) return 0;
		double db = 20 * Math.log10(rms / 32768.0);
		return (int) Math.max(0, Math.min(100, (db + 60) * 100 / 60));
	}

	private AudioAttributes attrs() {
		return new AudioAttributes.Builder()
			.setUsage(speaker ? AudioAttributes.USAGE_MEDIA : AudioAttributes.USAGE_VOICE_COMMUNICATION)
			.setContentType(AudioAttributes.CONTENT_TYPE_SPEECH).build();
	}

	private AudioTrack openTrack() {
		int min = AudioTrack.getMinBufferSize(Opus.RATE, AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_16BIT);
		return new AudioTrack.Builder().setAudioAttributes(attrs())
			.setAudioFormat(new AudioFormat.Builder().setSampleRate(Opus.RATE)
				.setChannelMask(AudioFormat.CHANNEL_OUT_MONO).setEncoding(AudioFormat.ENCODING_PCM_16BIT).build())
			.setBufferSizeInBytes(Math.max(min, Opus.RATE * 2 / 5)).setTransferMode(AudioTrack.MODE_STREAM).build();
	}

	private void drain(int my) {
		short[] chunk = new short[Opus.RATE / 50];
		AudioTrack track = null;
		AudioFocusRequest focus = null;
		long idleSince = 0;
		while (gen == my) {
			int n;
			boolean doFlush;
			synchronized (ring) {
				if (len == 0 && !flush) {
					try {
						ring.wait(track != null ? 500 : 0);
					} catch (InterruptedException e) {
						break;
					}
				}
				doFlush = flush;
				flush = false;
				n = Math.min(len, chunk.length);
				for (int k = 0; k < n; k++) chunk[k] = buf[(head + k) % buf.length];
				head = (head + n) % buf.length;
				len -= n;
				played += n;
			}
			if (doFlush && track != null) {
				track.pause();
				track.flush();
				track.play();
			}
			if (n == 0) {
				/* two seconds of nothing: give the speaker and focus back */
				if (track != null && System.currentTimeMillis() - idleSince > 2000) {
					track.stop();
					track.release();
					track = null;
					if (focus != null) am.abandonAudioFocusRequest(focus);
					focus = null;
				}
				continue;
			}
			idleSince = System.currentTimeMillis();
			if (track == null) {
				try {
					focus = new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN_TRANSIENT_MAY_DUCK)
						.setAudioAttributes(attrs()).build();
					am.requestAudioFocus(focus);
					track = openTrack();
					track.play();
				} catch (RuntimeException e) {
					Log.w(TAG, "playback: " + e);
					track = null;
					continue;
				}
			}
			level = levelOf(chunk, n);
			track.write(chunk, 0, n);
		}
		if (track != null) track.release();
		if (focus != null) am.abandonAudioFocusRequest(focus);
	}

	int playWrite(short[] pcm, int n) {
		synchronized (ring) {
			n = Math.min(n, buf.length - len);
			for (int k = 0; k < n; k++) buf[(head + len + k) % buf.length] = pcm[k];
			len += n;
			written += n;
			ring.notifyAll();
			return n;
		}
	}

	int playQueued() {
		synchronized (ring) {
			return len;
		}
	}

	long playWritten() {
		synchronized (ring) {
			return written;
		}
	}

	long playPlayed() {
		synchronized (ring) {
			return played;
		}
	}

	int playLevel() {
		synchronized (ring) {
			return len > 0 ? level : 0;
		}
	}

	void playFlush() {
		synchronized (ring) {
			played += len;
			len = 0;
			flush = true;
			ring.notifyAll();
		}
	}

	/* ---------- capture ---------- */

	private final Object mic = new Object();
	private boolean want, vox;
	private int begins;
	private volatile int micLevel;

	private boolean micAllowed() {
		return ctx.checkSelfPermission(Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED;
	}

	private AudioRecord openMic() {
		int min = AudioRecord.getMinBufferSize(Opus.RATE, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT);
		@SuppressWarnings("MissingPermission")
		AudioRecord r = new AudioRecord(noiseSuppression ? MediaRecorder.AudioSource.VOICE_COMMUNICATION
			: MediaRecorder.AudioSource.MIC, Opus.RATE, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT,
			Math.max(min, Opus.FRAME * 2 * 4));
		if (r.getState() != AudioRecord.STATE_INITIALIZED) {
			r.release();
			return null;
		}
		if (noiseSuppression && NoiseSuppressor.isAvailable()) {
			NoiseSuppressor ns = NoiseSuppressor.create(r.getAudioSessionId());
			if (ns != null) ns.setEnabled(true);
		}
		if (AutomaticGainControl.isAvailable()) {
			AutomaticGainControl agc = AutomaticGainControl.create(r.getAudioSessionId());
			if (agc != null) agc.setEnabled(true);
		}
		return r;
	}

	private void capture(int my, Engine engine) {
		short[] frame = new short[Opus.FRAME];
		byte[][] preroll = new byte[VOX_PREROLL][];
		int[] prerollLevel = new int[VOX_PREROLL];
		int served = 0;
		Gain gain = new Gain();  /* kept across transmissions: the level it learnt is the speaker's */
		try (Opus.Encoder enc = new Opus.Encoder()) {
			while (gen == my) {
				synchronized (mic) {
					while (gen == my && !want && !vox && begins == served) {
						try {
							mic.wait();
						} catch (InterruptedException e) {
							return;
						}
					}
				}
				if (gen != my) break;
				AudioRecord rec = micAllowed() ? openMic() : null;
				if (rec == null) {
					synchronized (mic) {
						want = false;
						vox = false;
						served = begins;
					}
					engine.captureFailed(micAllowed() ? "the microphone is busy" : "no microphone permission");
					continue;
				}
				rec.startRecording();
				enc.reset();
				boolean talking = false, byPtt = false;
				int held = 0, attack = 0;
				long lastVoice = 0;
				while (gen == my) {
					int got = 0;
					while (got < frame.length) {
						int r = rec.read(frame, got, frame.length - got);
						if (r <= 0) break;
						got += r;
					}
					if (got < frame.length) {
						engine.captureFailed("the microphone stopped");
						synchronized (mic) {
							want = false;
							served = begins;
						}
						talking = false;
						break;
					}
					int raw = levelOf(frame, frame.length);  /* VOX listens to the microphone itself */
					gain.process(frame, frame.length);
					int lv = levelOf(frame, frame.length);  /* the meter shows what is sent */
					micLevel = lv;
					byte[] pkt = enc.encode(frame);
					boolean w, v, pressed;
					synchronized (mic) {
						w = want;
						v = vox;
						pressed = begins != served;
						served = begins;
					}
					/* VOX opens on level (0..100 is -60..0 dBFS): sensitivity 0.5 opens at
					 * 40, about -36 dBFS; 0.9 at nearly any sound, 0.1 only when shouting */
					boolean speech = raw >= Math.max(5, (int) (90 - voxThreshold * 100));
					if (pressed && talking && !byPtt) {
						byPtt = true; /* the button takes over a VOX transmission */
					} else if (pressed && !talking) {
						talking = byPtt = true;
						held = 0;
					}
					if (talking) {
						if (pkt != null) engine.capturePacket(pkt, lv);
						if (byPtt && !w) {
							engine.captureEnded();
							talking = byPtt = false;
						} else if (!byPtt) {
							if (speech) lastVoice = System.currentTimeMillis();
							else if (System.currentTimeMillis() - lastVoice > VOX_HANG_MS) {
								engine.captureEnded();
								talking = false;
							}
						}
					} else if (v && !w) {
						attack = speech ? attack + 1 : 0;
						if (attack >= VOX_ATTACK) {
							engine.voxStarted();
							for (int k = 0; k < held; k++) engine.capturePacket(preroll[k], prerollLevel[k]);
							held = 0;
							if (pkt != null) engine.capturePacket(pkt, lv);
							talking = true;
							byPtt = false;
							lastVoice = System.currentTimeMillis();
						} else if (pkt != null) {
							if (held == VOX_PREROLL) {
								System.arraycopy(preroll, 1, preroll, 0, VOX_PREROLL - 1);
								System.arraycopy(prerollLevel, 1, prerollLevel, 0, VOX_PREROLL - 1);
								held--;
							}
							preroll[held] = pkt;
							prerollLevel[held] = lv;
							held++;
						}
					}
					if (!talking && !w && !v) break;
				}
				if (talking) engine.captureEnded();
				micLevel = 0;
				rec.stop();
				rec.release();
			}
		}
	}

	void captureBegin() {
		synchronized (mic) {
			want = true;
			begins++;
			mic.notifyAll();
		}
	}

	void captureEnd() {
		synchronized (mic) {
			want = false;
			mic.notifyAll();
		}
	}

	void captureVox(boolean on) {
		synchronized (mic) {
			vox = on;
			mic.notifyAll();
		}
	}

	int captureLevel() {
		return micLevel;
	}

	/* ---------- tones: syn_tone_vocab.h ---------- */

	void tone(String meaning) {
		double lo, hi, sec;
		switch (meaning) {
		case "SUCCESS": lo = 1900; hi = -1; sec = 0.2; break;
		case "FAIL": lo = 697; hi = 1209; sec = 0.15; break;
		case "STOP": lo = 941; hi = 1336; sec = 0.15; break;
		case "TOGGLE_ON": lo = 852; hi = 1209; sec = 0.1; break;
		case "TOGGLE_OFF": lo = 852; hi = 1336; sec = 0.1; break;
		case "ALERT": lo = 941; hi = 1209; sec = 0.2; break;
		case "START": lo = 697; hi = 1477; sec = 0.15; break;
		default: return;
		}
		int n = (int) (Opus.RATE * sec);
		short[] s = new short[n];
		for (int k = 0; k < n; k++) {
			double t = (double) k / Opus.RATE;
			double env = Math.min(1.0, Math.min(k, n - k) / (Opus.RATE * 0.005)); /* 5 ms ramps, no clicks */
			double v = Math.sin(2 * Math.PI * lo * t) + (hi > 0 ? Math.sin(2 * Math.PI * hi * t) : 0);
			s[k] = (short) (env * v * (hi > 0 ? 6000 : 9000));
		}
		try {
			AudioTrack t = new AudioTrack.Builder().setAudioAttributes(new AudioAttributes.Builder()
					.setUsage(AudioAttributes.USAGE_ASSISTANCE_SONIFICATION)
					.setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION).build())
				.setAudioFormat(new AudioFormat.Builder().setSampleRate(Opus.RATE)
					.setChannelMask(AudioFormat.CHANNEL_OUT_MONO).setEncoding(AudioFormat.ENCODING_PCM_16BIT).build())
				.setBufferSizeInBytes(n * 2).setTransferMode(AudioTrack.MODE_STATIC).build();
			t.write(s, 0, n);
			t.play();
			new android.os.Handler(android.os.Looper.getMainLooper()).postDelayed(t::release, (long) (sec * 1000) + 300);
		} catch (RuntimeException e) {
			Log.w(TAG, "tone: " + e);
		}
	}
}
