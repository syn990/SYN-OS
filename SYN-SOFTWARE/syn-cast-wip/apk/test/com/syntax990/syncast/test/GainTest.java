/* ------------------------------------------------------------------------
 *   Gain: quiet speech comes up to the target, loud speech is left alone
 *   and never clips, and the gain holds through the pauses between words.
 *   One "ok" or "FAIL" line per check; exit 1 on any failure.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CAST-APK
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
package com.syntax990.syncast.test;

import com.syntax990.syncast.core.Gain;
import com.syntax990.syncast.core.Opus;

public final class GainTest {
	private static int failures;

	private static void check(String name, boolean ok, String detail) {
		System.out.println((ok ? "ok   " : "FAIL ") + name + (ok ? "" : "  (" + detail + ")"));
		if (!ok) failures++;
	}

	/* one 60 ms frame of a 220 Hz tone at this RMS (0..1), as a voice at that level */
	private static short[] tone(double rms, int frame) {
		short[] s = new short[Opus.FRAME];
		for (int k = 0; k < s.length; k++) {
			double t = (frame * s.length + k) / (double) Opus.RATE;
			s[k] = (short) Math.round(Math.sqrt(2) * rms * 32767 * Math.sin(2 * Math.PI * 220 * t));
		}
		return s;
	}

	private static double rms(short[] s) {
		double sum = 0;
		for (short v : s) sum += (double) v * v;
		return Math.sqrt(sum / s.length) / 32768.0;
	}

	private static double db(double x) {
		return 20 * Math.log10(x);
	}

	public static void main(String[] args) {
		Gain g = new Gain();
		short[] f = null;
		for (int k = 0; k < 50; k++) {  /* 3 s of speech at -36 dBFS, a quiet phone microphone */
			f = tone(0.016, k);
			g.process(f, f.length);
		}
		double out = db(rms(f));
		check("quiet speech is brought up to about -20 dBFS", Math.abs(out + 20) < 2.5, String.format("%.1f dBFS", out));

		short[] noise = new short[Opus.FRAME];
		java.util.Random r = new java.util.Random(7);
		double before = g.gain();
		for (int k = 0; k < 30; k++) {  /* 1.8 s of room noise at about -60 dBFS between words */
			for (int i = 0; i < noise.length; i++) noise[i] = (short) Math.round(r.nextGaussian() * 33);
			g.process(noise, noise.length);
		}
		check("the gain holds through a pause, the room is not pumped up", g.gain() == before,
			String.format("%.2f -> %.2f", before, g.gain()));

		Gain loud = new Gain();
		int peak = 0;
		for (int k = 0; k < 50; k++) {  /* speech already at -6 dBFS */
			f = tone(0.5, k);
			loud.process(f, f.length);
			for (short v : f) peak = Math.max(peak, Math.abs(v));
		}
		check("loud speech is not boosted", loud.gain() < 1.05, String.format("gain %.2f", loud.gain()));
		check("the limiter keeps every sample inside the range", peak < 32767, "peak " + peak);

		Gain cap = new Gain();
		for (int k = 0; k < 80; k++) {  /* a whisper at -50 dBFS: more than the most it may give */
			f = tone(0.0045, k);
			cap.process(f, f.length);
		}
		check("the boost stops at +18 dB", cap.gain() <= 8.0 && cap.gain() > 7.5, String.format("gain %.2f", cap.gain()));

		/* playback, as the player keeps it per speaker: it starts at 1.0, so nothing is raised before it is heard */
		Gain play = new Gain(1.0);
		f = tone(0.3, 0);
		int inPeak = 0, outPeak = 0;
		for (short v : f) inPeak = Math.max(inPeak, Math.abs(v));
		play.process(f, f.length);
		for (short v : f) outPeak = Math.max(outPeak, Math.abs(v));
		check("playback: a new speaker's first frame is not raised", outPeak <= inPeak, inPeak + " -> " + outPeak);

		Gain quietSpeaker = new Gain(1.0);
		for (int k = 0; k < 50; k++) {  /* one quiet message at -38 dBFS */
			f = tone(0.0125, k);
			quietSpeaker.process(f, f.length);
		}
		f = tone(0.0125, 0);  /* their next message: its first frame is already at the level learnt */
		quietSpeaker.process(f, f.length);
		out = db(rms(f));
		check("playback: a quiet speaker's next message starts at their level", Math.abs(out + 20) < 2.5,
			String.format("%.1f dBFS", out));

		System.out.println(failures == 0 ? "all passed" : failures + " failed");
		System.exit(failures == 0 ? 0 : 1);
	}
}
