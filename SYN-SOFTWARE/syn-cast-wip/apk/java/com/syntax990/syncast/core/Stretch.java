/* ------------------------------------------------------------------------
 *   Replay speed without the chipmunk: the desktop's WSOLA (stretch.c) at
 *   16 kHz. 30 ms Hann windows placed every 15 ms, each taken about
 *   15*speed ms further along the input, nudged up to 10 ms either way to
 *   where it best continues the previous one.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CAST-APK
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
package com.syntax990.syncast.core;

public final class Stretch {
	private static final int WIN = Opus.RATE * 30 / 1000;
	private static final int HOP = WIN / 2;
	private static final int DELTA = Opus.RATE * 10 / 1000;

	private final float[] win = new float[WIN];
	private double speed = 1.0;
	private float[] in = new float[16384];
	private int inLen;
	private long inBase;
	private double ana;
	private long prev;
	private boolean havePrev;
	private final float[] ola = new float[WIN];
	private short[] out = new short[8192];
	private int outLen;
	private boolean flushed;

	public Stretch(double speed) {
		for (int i = 0; i < WIN; i++) win[i] = (float) (0.5 - 0.5 * Math.cos(2 * Math.PI * i / WIN));
		this.speed = Math.max(0.5, Math.min(2.0, speed));
	}

	public double speed() {
		return speed;
	}

	private void outPush(float[] v, int n) {
		if (outLen + n > out.length) out = java.util.Arrays.copyOf(out, Math.max(out.length * 2, outLen + n));
		for (int i = 0; i < n; i++) {
			float x = v[i];
			out[outLen++] = (short) (x > 32767 ? 32767 : x < -32768 ? -32768 : x);
		}
	}

	private int outTake(short[] dst, int cap) {
		int n = Math.min(outLen, cap);
		System.arraycopy(out, 0, dst, 0, n);
		System.arraycopy(out, n, out, 0, outLen - n);
		outLen -= n;
		return n;
	}

	private void inPush(short[] src, int n, boolean zeros) {
		if (inLen + n > in.length) in = java.util.Arrays.copyOf(in, Math.max(in.length * 2, inLen + n));
		for (int i = 0; i < n; i++) in[inLen + i] = zeros ? 0f : src[i];
		inLen += n;
	}

	private boolean step() {
		long nominal = Math.round(ana);
		long lo = Math.max(Math.max(nominal - DELTA, 0), inBase);
		long hi = nominal + DELTA;
		long end = inBase + inLen;
		if (hi + WIN > end || (havePrev && prev + HOP + WIN > end)) return false;
		long best = Math.max(nominal, lo);
		if (havePrev) {
			int nat = (int) (prev + HOP - inBase);
			double bestScore = -1e30;
			for (long c = lo; c <= hi; c += 2) {
				int cand = (int) (c - inBase);
				double dot = 0, energy = 1e-9;
				for (int i = 0; i < HOP; i += 2) {
					dot += (double) in[cand + i] * in[nat + i];
					energy += (double) in[cand + i] * in[cand + i];
				}
				double score = dot / Math.sqrt(energy);
				if (score > bestScore) {
					bestScore = score;
					best = c;
				}
			}
		}
		int x = (int) (best - inBase);
		for (int i = 0; i < WIN; i++) ola[i] += win[i] * in[x + i];
		outPush(ola, HOP);
		System.arraycopy(ola, HOP, ola, 0, WIN - HOP);
		java.util.Arrays.fill(ola, WIN - HOP, WIN, 0f);
		prev = best;
		havePrev = true;
		ana += HOP * speed;

		long keepFrom = Math.min((long) ana - DELTA, prev + HOP);
		if (keepFrom > inBase + 4096) {
			int drop = (int) (keepFrom - inBase);
			System.arraycopy(in, drop, in, 0, inLen - drop);
			inLen -= drop;
			inBase += drop;
		}
		return true;
	}

	/** Feeds n samples, writes up to cap processed ones into dst, returns how many. */
	public int process(short[] src, int n, short[] dst, int cap) {
		if (speed == 1.0) {
			int k = Math.min(n, cap);
			System.arraycopy(src, 0, dst, 0, k);
			return k;
		}
		inPush(src, n, false);
		while (step()) {
		}
		return outTake(dst, cap);
	}

	/** What is left once the input has ended. */
	public int flush(short[] dst, int cap) {
		if (speed == 1.0) return 0;
		if (!flushed) {
			flushed = true;
			long realEnd = inBase + inLen;
			inPush(null, WIN + 2 * DELTA + HOP, true);
			while (ana < realEnd && step()) {
			}
			outPush(ola, HOP);
			java.util.Arrays.fill(ola, 0f);
		}
		return outTake(dst, cap);
	}
}
