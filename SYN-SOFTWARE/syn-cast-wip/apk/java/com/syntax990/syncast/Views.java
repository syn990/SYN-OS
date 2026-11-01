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
			String host = v.server.replaceFirst("^wss?://", "").replaceFirst("/.*$", "");
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
				x += text(cv, fit(v.rxName, tp, W / 3f), x, y, tp, pal.text) + pad;
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
			String dest = v == null || v.channels.isEmpty() ? "" : v.channels.get(v.active).title; /* G227: the tab's words (Lobby), never the raw id (ops) */
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
			if (onAir) {
				cv.drawColor(pal.accentDim);
				fill.setColor(pal.accent);
				cv.drawRect(0, 0, dp(getContext(), 3), H, fill);
			} else if (selected) {
				cv.drawColor(pal.panelHover);
			} else {
				cv.drawColor(pal.bg);
			}
			float y = pad / 2, x = pad;
			x += text(cv, HHMM.format(new Date(m.ts)) + " ", x, y, dim, dimc);
			if (m.kind == History.Kind.EVENT) {
				text(cv, fit(m.text == null ? "" : m.text, dim, W - x - pad), x, y, dim, dimc);
				return;
			}
			x += text(cv, fit(m.from, tp, W / 3f) + " ", x, y, tp, m.out ? pal.accent : pal.text);
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
}
