/* ------------------------------------------------------------------------
 *   The drawn parts of the screen, in the desktop window's style: the
 *   header, the on-air strip, the talk button and the history rows.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CAST-APK
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
package com.syntax990.syncast;

import android.annotation.SuppressLint;
import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.text.Layout;
import android.text.StaticLayout;
import android.text.TextPaint;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.View;

import com.syntax990.syncast.core.Engine;
import com.syntax990.syncast.core.History;

import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

final class Views {
	private Views() {
	}

	static String dur(long ms) {
		if (ms < 60000) return String.format(Locale.ROOT, "%d.%ds", ms / 1000, (ms % 1000) / 100);
		return String.format(Locale.ROOT, "%d:%02d", ms / 60000, (ms / 1000) % 60);
	}

	/** 1x, 1.25x, 1.5x */
	static String speedLabel(double s) {
		String t = String.format(Locale.ROOT, "%.2f", s).replaceAll("0+$", "").replaceAll("\\.$", "");
		return t + "x";
	}

	static TextPaint paint(Context c, float sp, boolean bold) {
		TextPaint p = new TextPaint(Paint.ANTI_ALIAS_FLAG);
		p.setTypeface(Typeface.create(Typeface.MONOSPACE, bold ? Typeface.BOLD : Typeface.NORMAL));
		p.setTextSize(sp * c.getResources().getDisplayMetrics().scaledDensity);
		return p;
	}

	static float dp(Context c, float v) {
		return v * c.getResources().getDisplayMetrics().density;
	}

	/** Draws text with its top at y; returns the width. */
	static float text(Canvas cv, String s, float x, float y, TextPaint p, int color) {
		p.setColor(color);
		cv.drawText(s, x, y - p.ascent(), p);
		return p.measureText(s);
	}

	static String fit(String s, TextPaint p, float w) {
		if (p.measureText(s) <= w) return s;
		int n = p.breakText(s, true, Math.max(0, w - p.measureText("…")), null);
		return s.substring(0, n) + "…";
	}

	/** G228: a speaker's colour, "#rrggbb" from the server, or fallback when there is none. */
	static int speaker(String hex, int fallback) {
		if (hex == null || hex.length() != 7 || hex.charAt(0) != '#') return fallback;
		try {
			return 0xff000000 | Integer.parseInt(hex.substring(1), 16);
		} catch (NumberFormatException e) {
			return fallback;
		}
	}

	/** G228: up to two initials of a name, for a speaker with no portrait. */
	static String initials(String name) {
		StringBuilder b = new StringBuilder();
		for (String w : name.trim().split("[\\s._-]+")) {
			if (!w.isEmpty() && b.length() < 2) b.append(Character.toUpperCase(w.charAt(0)));
		}
		return b.length() > 0 ? b.toString() : "?";
	}

	static void meter(Canvas cv, float x, float y, float w, float h, int level, int on, int off, Paint fill, float bar) {
		int bars = (int) (w / (bar * 1.5f));
		int lit = bars * level / 100;
		for (int b = 0; b < bars; b++) {
			fill.setColor(b < lit ? on : off);
			cv.drawRect(x + b * bar * 1.5f, y, x + b * bar * 1.5f + bar, y + h, fill);
		}
	}

	/* ---------- header ---------- */

	static final class Header extends View {
		Palette pal;
		Engine.View v;
		private final TextPaint tp;
		private final Paint fill = new Paint();

		Header(Context c, Palette pal) {
			super(c);
			this.pal = pal;
			tp = paint(c, 14, true);
		}

		@Override
		protected void onMeasure(int w, int h) {
			setMeasuredDimension(MeasureSpec.getSize(w), (int) (tp.getFontSpacing() + dp(getContext(), 12)));
		}

		@Override
		protected void onDraw(Canvas cv) {
			int W = getWidth(), H = getHeight();
			float pad = dp(getContext(), 8);
			cv.drawColor(pal.panel);
			fill.setColor(pal.border);
			cv.drawRect(0, H - 1, W, H, fill);
			float bw = tp.measureText("SYN-CAST") + 2 * pad;
			fill.setColor(pal.accent);
			cv.drawRect(0, 0, bw, H - 1, fill);
			float ty = (H - tp.getFontSpacing()) / 2;
			text(cv, "SYN-CAST", pad, ty, tp, pal.inkOn(pal.accent));
			if (v == null) return;
			/* G273: signed in by the way in from outside: say so, not the tunnel's name (the address is a door) */
			String host = v.far ? "from outside" : v.server.replaceFirst("^wss?://", "").replaceFirst("/.*$", "");
			/* badges from the right */
			float rx = W - pad;
			String[] badges = {v.busy ? "BUSY" : "AVAILABLE", v.vox ? "VOX" : null, speedLabel(v.speed)};
			for (int i = badges.length - 1; i >= 0; i--) {
				if (badges[i] == null) continue;
				float w = tp.measureText(badges[i]) + pad;
				rx -= w;
				boolean hot = (i == 0 && v.busy) || i == 1;
				if (hot) {
					fill.setColor(i == 0 ? pal.urgent : pal.accentDim);
					cv.drawRect(rx, 4, rx + w, H - 5, fill);
				}
				fill.setStyle(Paint.Style.STROKE);
				fill.setColor(hot ? pal.accent : pal.border);
				cv.drawRect(rx, 4, rx + w, H - 5, fill);
				fill.setStyle(Paint.Style.FILL);
				text(cv, badges[i], rx + pad / 2, ty, tp, hot ? pal.text : Palette.mix(pal.text, pal.bg, 30));
				rx -= pad / 2;
			}
			String who = (v.user.isEmpty() ? "anonymous" : v.user) + " @ " + host;
			text(cv, fit(who, tp, rx - bw - 2 * pad), bw + pad, ty, tp, pal.text);
		}
	}

	/* ---------- the on-air strip ---------- */

	static final class OnAir extends View {
		Palette pal;
		Engine.View v;
		private final TextPaint tp;
		private final Paint fill = new Paint();

		OnAir(Context c, Palette pal) {
			super(c);
			this.pal = pal;
			tp = paint(c, 13, false);
		}

		@Override
		protected void onMeasure(int w, int h) {
			setMeasuredDimension(MeasureSpec.getSize(w), (int) (tp.getFontSpacing() + dp(getContext(), 10)));
		}

		@Override
		protected void onDraw(Canvas cv) {
			int W = getWidth(), H = getHeight();
			float pad = dp(getContext(), 8);
			cv.drawColor(pal.panel);
			fill.setColor(pal.border);
			cv.drawRect(0, 0, W, 1, fill);
			if (v == null) return;
			float y = (H - tp.getFontSpacing()) / 2, x = pad;
			if (v.toast != null && !v.toast.isEmpty()) {
				text(cv, fit(v.toast, tp, W - 2 * pad), x, y, tp, pal.text);
				return;
			}
			float meterW = Math.min(dp(getContext(), 90), W / 4f);
			if (v.txDest != null) {
				x += text(cv, v.txOpening ? "OPENING " : "ON AIR ", x, y, tp, pal.accent);
				x += text(cv, fit("-> " + v.txDest, tp, W / 3f), x, y, tp, pal.text) + pad;
				meter(cv, x, y + 3, meterW, tp.getFontSpacing() - 6, v.txLevel, pal.accent, pal.accentDim, fill, dp(getContext(), 3));
				text(cv, dur(v.txElapsed), x + meterW + pad, y, tp, pal.text);
			} else if (v.rxName != null) {
				x += text(cv, v.rxReplay ? "REPLAY " : "RX ", x, y, tp, pal.accent);
				x += text(cv, fit(v.rxName, tp, W / 3f), x, y, tp, speaker(v.rxColour, pal.text)) + pad;
				meter(cv, x, y + 3, meterW, tp.getFontSpacing() - 6, v.rxLevel, pal.accent, pal.accentDim, fill, dp(getContext(), 3));
				text(cv, dur(v.rxElapsed), x + meterW + pad, y, tp, pal.text);
			} else {
				text(cv, "STANDBY", x, y, tp, Palette.mix(pal.text, pal.bg, 45));
			}
			if (v.queue > 0) {
				String q = "QUEUE " + v.queue;
				text(cv, q, W - pad - tp.measureText(q), y, tp, pal.urgent);
			}
		}
	}

	/* ---------- the talk button ---------- */

	interface Ptt {
		void down();
		void up();
	}

	static final class TalkButton extends View {
		Palette pal;
		Engine.View v;
		boolean toggle;
		Ptt ptt;
		private boolean pressed;
		private final TextPaint big, small;
		private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);

		TalkButton(Context c, Palette pal) {
			super(c);
			this.pal = pal;
			big = paint(c, 22, true);
			small = paint(c, 13, false);
		}

		@Override
		protected void onMeasure(int w, int h) {
			setMeasuredDimension(MeasureSpec.getSize(w), (int) dp(getContext(), 120));
		}

		@SuppressLint("ClickableViewAccessibility")
		@Override
		public boolean onTouchEvent(MotionEvent e) {
			if (ptt == null) return false;
			switch (e.getActionMasked()) {
			case MotionEvent.ACTION_DOWN:
				pressed = true;
				performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
				ptt.down();
				invalidate();
				return true;
			case MotionEvent.ACTION_UP:
			case MotionEvent.ACTION_CANCEL:
				pressed = false;
				ptt.up();
				invalidate();
				return true;
			default:
				return true;
			}
		}

		@Override
		protected void onDraw(Canvas cv) {
			float m = dp(getContext(), 6);
			RectF b = new RectF(m, m, getWidth() - m, getHeight() - m);
			int bg = pal.panel, fg = pal.text, edge = pal.accent;
			String label, sub = "";
			Engine.Talk st = v == null ? Engine.Talk.DISABLED : v.talk;
			String dest = v == null || v.channels.isEmpty() ? "" : v.channels.get(v.active).name;
			switch (st) {
			case DISABLED:
				bg = pal.bg;
				fg = Palette.mix(pal.text, pal.bg, 50);
				edge = pal.border;
				label = "OFFLINE";
				sub = v == null || v.channels.isEmpty() ? "" : v.offline ? "tap ONLINE below to join"
					: v.channels.get(v.active).state;
				break;
			case CONNECTING:
				bg = pal.accentDim;
				label = "CONNECTING…";
				sub = "wait for the beep";
				break;
			case SENDING:
				bg = pal.accent;
				fg = pal.inkOn(pal.accent);
				edge = pal.text;
				label = "TALKING";
				sub = "-> " + (v.txDest != null ? v.txDest : dest);
				break;
			case RECEIVING:
				bg = pal.panelHover;
				label = "RECEIVING " + (v.rxName == null ? "" : v.rxName);
				sub = "tap STOP to cut it short";
				break;
			default:
				bg = pressed ? pal.panelHover : pal.panel;
				label = toggle ? "TAP TO TALK" : "HOLD TO TALK";
				sub = "-> " + dest;
			}
			fill.setStyle(Paint.Style.FILL);
			fill.setColor(bg);
			cv.drawRect(b, fill);
			fill.setStyle(Paint.Style.STROKE);
			fill.setStrokeWidth(dp(getContext(), 2));
			fill.setColor(edge);
			cv.drawRect(b, fill);
			fill.setStyle(Paint.Style.FILL);
			label = fit(label, big, b.width() - 2 * m);
			sub = fit(sub, small, b.width() - 2 * m);
			float total = big.getFontSpacing() + small.getFontSpacing();
			float ly = b.top + (b.height() - total) / 2;
			text(cv, label, b.centerX() - big.measureText(label) / 2, ly, big, fg);
			text(cv, sub, b.centerX() - small.measureText(sub) / 2, ly + big.getFontSpacing(), small,
				Palette.mix(fg, bg, 25));
			if (st == Engine.Talk.SENDING) {
				meter(cv, b.left + m, b.bottom - m - dp(getContext(), 4), b.width() - 2 * m, dp(getContext(), 4),
					v.txLevel, fg, Palette.mix(pal.accent, pal.bg, 40), fill, dp(getContext(), 4));
			}
		}
	}

	/* ---------- a history row ---------- */

	interface Thumbs {
		Bitmap thumb(long id);
	}

	static final class Row extends View {
		Palette pal;
		History.Msg m;
		boolean onAir, selected;
		Engine.View v;
		Thumbs thumbs;
		private final TextPaint tp, dim;
		private final Paint fill = new Paint();
		private StaticLayout body;
		private Bitmap thumb;
		private static final SimpleDateFormat HHMM = new SimpleDateFormat("HH:mm", Locale.ROOT);

		Row(Context c, Palette pal) {
			super(c);
			this.pal = pal;
			tp = paint(c, 14, false);
			dim = paint(c, 13, false);
		}

		private String summary() {
			String tag = m.forwho.isEmpty() ? "" : "[private] ";
			switch (m.kind) {
			case VOICE:
				if (m.partial) return tag + "VOICE " + (m.out ? "SENDING" : "LIVE");
				if (onAir && v != null) {
					long el = (long) (v.rxElapsed * (v.rxReplay ? v.speed : 1.0));
					return tag + "VOICE " + dur(Math.min(el, m.durationMs)) + "/" + dur(m.durationMs) + " PLAYING";
				}
				return tag + "VOICE " + dur(m.durationMs) + (!m.played && !m.out ? " NEW" : "");
			case TEXT:
				return tag;
			case IMAGE:
				return tag + "IMAGE " + m.w + "x" + m.h + (m.partial ? " LOADING" : "");
			case LOCATION:
				return tag + String.format(Locale.ROOT, "LOCATION %.5f, %.5f", m.lat, m.lon);
			default:
				return "";
			}
		}

		@Override
		protected void onMeasure(int ws, int hs) {
			int W = MeasureSpec.getSize(ws);
			float pad = dp(getContext(), 8);
			int bodyW = (int) Math.max(dp(getContext(), 60), W - 2 * pad - dp(getContext(), 8));
			String text = m.kind == History.Kind.EVENT ? null : m.text;
			if (m.kind == History.Kind.LOCATION && (text == null || text.isEmpty())) text = null;
			body = text == null ? null : StaticLayout.Builder.obtain(text, 0, text.length(),
				m.kind == History.Kind.TEXT ? tp : dim, bodyW).setAlignment(Layout.Alignment.ALIGN_NORMAL).build();
			thumb = m.kind == History.Kind.IMAGE && thumbs != null ? thumbs.thumb(m.id) : null;
			float h = tp.getFontSpacing() + pad;
			if (body != null) h += body.getHeight();
			if (m.kind == History.Kind.IMAGE) h += (thumb != null ? thumb.getHeight() : tp.getFontSpacing()) + pad / 2;
			setMeasuredDimension(W, (int) h);
		}

		@Override
		protected void onDraw(Canvas cv) {
			int W = getWidth(), H = getHeight();
			float pad = dp(getContext(), 8);
			int dimc = Palette.mix(pal.text, pal.bg, 45);
			int who = m.out ? pal.accent : speaker(m.colour, pal.text);
			if (onAir) {
				cv.drawColor(pal.accentDim);
			} else if (selected) {
				cv.drawColor(pal.panelHover);
			} else {
				cv.drawColor(pal.bg);
			}
			/* G228: the speaker's colour down the left edge, as the web chat draws it; the accent while on air */
			if (onAir || (!m.colour.isEmpty() && m.kind != History.Kind.EVENT)) {
				fill.setColor(onAir ? pal.accent : who);
				cv.drawRect(0, 0, dp(getContext(), 3), H, fill);
			}
			float y = pad / 2, x = pad;
			x += text(cv, HHMM.format(new Date(m.ts)) + " ", x, y, dim, dimc);
			if (m.kind == History.Kind.EVENT) {
				text(cv, fit(m.text == null ? "" : m.text, dim, W - x - pad), x, y, dim, dimc);
				return;
			}
			x += text(cv, fit(m.out ? m.from : m.who(), tp, W / 3f) + " ", x, y, tp, who);
			String s = summary();
			int sc = (m.kind == History.Kind.VOICE && !m.played && !m.out) || !m.forwho.isEmpty() ? pal.urgent : pal.text;
			text(cv, fit(s, tp, W - x - pad), x, y, tp, sc);
			y += tp.getFontSpacing();
			if (body != null) {
				cv.save();
				cv.translate(pad + dp(getContext(), 8), y);
				((TextPaint) body.getPaint()).setColor(m.kind == History.Kind.TEXT && !m.partial ? pal.text : dimc);
				body.draw(cv);
				cv.restore();
				y += body.getHeight();
			}
			if (m.kind == History.Kind.IMAGE) {
				float ix = pad + dp(getContext(), 8);
				if (thumb != null) {
					cv.drawBitmap(thumb, ix, y + pad / 4, null);
					fill.setStyle(Paint.Style.STROKE);
					fill.setColor(pal.border);
					cv.drawRect(new Rect((int) ix - 1, (int) (y + pad / 4) - 1, (int) ix + thumb.getWidth() + 1,
						(int) (y + pad / 4) + thumb.getHeight() + 1), fill);
					fill.setStyle(Paint.Style.FILL);
				} else {
					text(cv, m.partial ? "loading…" : "(no thumbnail)", ix, y, dim, dimc);
				}
			}
		}
	}

	/* ---------- G228: the speaker's portrait, top right while a note plays ---------- */

	static final class Portrait extends View {
		Palette pal;
		private final TextPaint big, small;
		private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG), pic = new Paint(Paint.FILTER_BITMAP_FLAG);
		private String name = "";
		private int colour;
		private Bitmap bmp;
		private byte[] shown;

		Portrait(Context c, Palette pal) {
			super(c);
			this.pal = pal;
			big = paint(c, 28, true);
			small = paint(c, 11, true);
			setVisibility(GONE);
		}

		/** Shows who is on air (name null: hides); the PNG is decoded once per speaker. */
		void show(String name, String colour, byte[] png) {
			if (name == null) {
				if (getVisibility() != GONE) setVisibility(GONE);
				return;
			}
			if (png != shown) {
				shown = png;
				bmp = png == null ? null : BitmapFactory.decodeByteArray(png, 0, png.length);
			}
			this.name = name;
			this.colour = speaker(colour, pal.accent);
			if (getVisibility() != VISIBLE) setVisibility(VISIBLE);
			invalidate();
		}

		@Override
		protected void onDraw(Canvas cv) {
			int W = getWidth(), H = getHeight();
			float b = dp(getContext(), 2), r = dp(getContext(), 6);
			float cap = small.getFontSpacing() + dp(getContext(), 4);
			RectF card = new RectF(b / 2, b / 2, W - b / 2, H - b / 2);
			fill.setStyle(Paint.Style.FILL);
			fill.setColor(pal.panel);
			cv.drawRoundRect(card, r, r, fill);
			RectF img = new RectF(b, b, W - b, H - cap);
			if (bmp != null) {
				float sw = bmp.getWidth(), sh = bmp.getHeight(), k = Math.max(img.width() / sw, img.height() / sh);
				float cw = img.width() / k, ch = img.height() / k;
				Rect src = new Rect((int) ((sw - cw) / 2), (int) ((sh - ch) / 2), (int) ((sw + cw) / 2), (int) ((sh + ch) / 2));
				cv.save();
				cv.clipRect(img);
				cv.drawBitmap(bmp, src, img, pic);
				cv.restore();
			} else {
				fill.setColor(Palette.mix(colour, pal.bg, 55));
				cv.drawRect(img, fill);
				String in = initials(name);
				big.setColor(colour);
				cv.drawText(in, img.centerX() - big.measureText(in) / 2, img.centerY() - (big.ascent() + big.descent()) / 2, big);
			}
			fill.setColor(colour);
			cv.drawRect(b, H - cap, W - b, H - b, fill);
			text(cv, fit(name, small, W - 2 * b - dp(getContext(), 4)),
				Math.max(b + dp(getContext(), 2), (W - small.measureText(fit(name, small, W - 2 * b - dp(getContext(), 4)))) / 2),
				H - cap + dp(getContext(), 2), small, pal.inkOn(colour));
			fill.setStyle(Paint.Style.STROKE);
			fill.setStrokeWidth(b);
			fill.setColor(colour);
			cv.drawRoundRect(card, r, r, fill);
			fill.setStyle(Paint.Style.FILL);
		}
	}
}
