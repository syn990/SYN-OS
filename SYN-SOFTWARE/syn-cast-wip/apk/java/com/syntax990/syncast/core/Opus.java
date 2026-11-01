/* ------------------------------------------------------------------------
 *   libopus through JNI (jni/syncast_opus.c). The encoder carries the
 *   Zello app's settings; Decoder covers lost packets the way the desktop
 *   does: the last one of a gap from the next packet's FEC, the rest by
 *   concealment.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CAST-APK
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
package com.syntax990.syncast.core;

public final class Opus {
	public static final int RATE = 16000;
	public static final int FRAME_MS = 60;
	public static final int FRAME = RATE * FRAME_MS / 1000;
	public static final int MAX_PACKET_SAMPLES = RATE * 120 / 1000;
	public static final int MAX_PACKET = 1275;

	static {
		String override = System.getProperty("syncast.jni");
		if (override != null) System.load(override); /* the desktop JVM tests */
		else System.loadLibrary("syncast");
	}

	private Opus() {
	}

	static native long encCreate(int rate);
	static native void encReset(long h);
	static native int encode(long h, short[] pcm, int samples, byte[] out);
	static native void encDestroy(long h);
	static native long decCreate(int rate);
	static native int decode(long h, byte[] data, int len, short[] out, int maxSamples, boolean fec);
	static native int packetSamples(byte[] data, int len, int rate);
	static native void decDestroy(long h);

	public static final class Encoder implements AutoCloseable {
		private long h = encCreate(RATE);
		private final byte[] buf = new byte[MAX_PACKET];

		/** One 60 ms frame in, one packet out (null on an encoder error). */
		public byte[] encode(short[] frame) {
			int n = Opus.encode(h, frame, FRAME, buf);
			if (n <= 0) return null;
			byte[] p = new byte[n];
			System.arraycopy(buf, 0, p, 0, n);
			return p;
		}

		public void reset() {
			encReset(h);
		}

		@Override
		public void close() {
			encDestroy(h);
			h = 0;
		}
	}

	public static final class Decoder implements AutoCloseable {
		private long h = decCreate(RATE);
		private int lost;
		private int lastSamples = FRAME;

		public void lost(int count) {
			lost = Math.min(lost + count, 20); /* beyond ~2 s a gap is silence */
		}

		/** PCM for any covered losses plus this packet into out; returns samples. */
		public int decode(byte[] data, short[] out) {
			int ps = packetSamples(data, data.length, RATE);
			if (ps <= 0 || ps > MAX_PACKET_SAMPLES) ps = lastSamples;
			int total = 0;
			short[] tmp = new short[MAX_PACKET_SAMPLES];
			while (lost > 0 && total + ps * 2 <= out.length) {
				int r = lost == 1 ? Opus.decode(h, data, data.length, tmp, ps, true) : Opus.decode(h, null, 0, tmp, lastSamples, false);
				if (r > 0) {
					System.arraycopy(tmp, 0, out, total, r);
					total += r;
				}
				lost--;
			}
			lost = 0;
			int r = Opus.decode(h, data, data.length, tmp, MAX_PACKET_SAMPLES, false);
			if (r > 0 && total + r <= out.length) {
				System.arraycopy(tmp, 0, out, total, r);
				total += r;
				lastSamples = r;
			}
			return total;
		}

		@Override
		public void close() {
			decDestroy(h);
			h = 0;
		}
	}
}
