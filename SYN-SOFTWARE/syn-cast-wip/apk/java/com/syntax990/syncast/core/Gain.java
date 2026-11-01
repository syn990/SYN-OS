/* ------------------------------------------------------------------------
 *   Automatic gain, both ways, as the Zello app does (its WebRTC AGC on the
 *   recorder, and on the player with a state kept per speaker). Out: the
 *   phone's call-audio microphone (VOICE_COMMUNICATION) records quietly,
 *   so speech is brought up to a steady level before it is encoded. In:
 *   each speaker's messages are levelled as they play, so a quiet sender,
 *   or a message recorded before the microphone had this, is heard at the
 *   same level as everyone else. The level is learnt only from frames
 *   loud enough to be speech, so a quiet room is not pumped up between
 *   words; the gain falls quickly and rises gently; a soft limiter keeps
 *   the boosted peaks from clipping.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CAST-APK
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
package com.syntax990.syncast.core;

public final class Gain {
	static final double TARGET = 0.10;   /* the speech RMS aimed for: -20 dBFS */
	static final double MAX = 8.0;       /* +18 dB at most */
	static final double SPEECH = 0.004;  /* a frame quieter than -48 dBFS is not speech: the gain holds */

	private double gain, speech;

	/** The microphone's: starts at +6 dB, as a phone's call audio is quiet. */
	public Gain() {
		this(2.0);
	}

	/** Starting at this gain (1.0 for playback: nothing is raised before the speaker is heard). */
	public Gain(double start) {
		gain = start;
	}

	public double gain() {
		return gain;
	}

	/** s[0..n) boosted in place. */
	public void process(short[] s, int n) {
		if (n <= 0) return;
		double sum = 0;
		for (int k = 0; k < n; k++) sum += (double) s[k] * s[k];
		double rms = Math.sqrt(sum / n) / 32768.0;
		double from = gain;
		if (rms >= SPEECH) {
			speech = speech == 0 ? rms : speech * 0.8 + rms * 0.2;  /* the speech level, over about 300 ms */
			double want = Math.max(1.0, Math.min(MAX, TARGET / speech));
			gain += (want - gain) * (want < gain ? 0.5 : 0.15);
		}
		for (int k = 0; k < n; k++) {
			double g = from + (gain - from) * (k + 1) / n;  /* a ramp across the frame: no step to hear */
			s[k] = (short) Math.round(limit(s[k] * g / 32768.0) * 32767);
		}
	}

	/* straight up to 0.7 (-3 dBFS), then a smooth bend towards 1.0 that never reaches it */
	static double limit(double x) {
		double a = Math.abs(x);
		if (a <= 0.7) return x;
		return Math.copySign(0.7 + 0.3 * Math.tanh((a - 0.7) / 0.3), x);
	}
}
