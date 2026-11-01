/* ------------------------------------------------------------------------
 *   The parts every screen is made of, in the desktop's SYN-OS style: a
 *   header with the accent block, monospace text, square bordered buttons,
 *   flat fields and message rows with an accent bar for unread mail.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL-APK
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
package com.syntax990.synmail;

import android.content.Context;
import android.graphics.Typeface;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.text.InputType;
import android.text.TextUtils;
import android.view.Gravity;
import android.view.View;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.time.ZonedDateTime;
import java.time.format.DateTimeFormatter;
import java.util.Locale;

final class Views {
	private Views() {
	}

	static int dp(Context c, float v) {
		return Math.round(v * c.getResources().getDisplayMetrics().density);
	}

	static final Typeface MONO = Typeface.MONOSPACE, MONO_BOLD = Typeface.create(Typeface.MONOSPACE, Typeface.BOLD);

	static TextView text(Context c, Palette p, String s, float sp, boolean bold, int color) {
		TextView t = new TextView(c);
		t.setText(s);
		t.setTextSize(sp);
		t.setTypeface(bold ? MONO_BOLD : MONO);
		t.setTextColor(color);
		return t;
	}

	/** one line, cut with … */
	static TextView line(Context c, Palette p, String s, float sp, boolean bold, int color) {
		TextView t = text(c, p, s, sp, bold, color);
		t.setSingleLine(true);
		t.setEllipsize(TextUtils.TruncateAt.END);
		return t;
	}

	private static GradientDrawable box(int fill, int stroke, int w) {
		GradientDrawable d = new GradientDrawable();
		d.setColor(fill);
		d.setStroke(w, stroke);
		return d;
	}

	static TextView button(Context c, Palette p, String s, boolean primary, View.OnClickListener on) {
		TextView b = text(c, p, s, 15, true, primary ? p.inkOn(p.accent) : p.text);
		int w = Math.max(1, dp(c, 1));
		StateListDrawable bg = new StateListDrawable();
		bg.addState(new int[] {android.R.attr.state_pressed}, box(p.panelHover, p.accent, w));
		bg.addState(new int[] {}, primary ? box(p.accent, p.accent, w) : box(p.panel, p.border, w));
		b.setBackground(bg);
		b.setGravity(Gravity.CENTER);
		int pad = dp(c, 12);
		b.setPadding(pad, pad, pad, pad);
		b.setClickable(true);
		b.setOnClickListener(on);
		return b;
	}

	static EditText field(Context c, Palette p, String hint, int type) {
		EditText e = new EditText(c);
		e.setHint(hint);
		e.setTypeface(MONO);
		e.setTextSize(15);
		e.setTextColor(p.text);
		e.setHintTextColor(Palette.mix(p.text, p.bg, 55));
		e.setInputType(type);
		e.setBackground(box(p.panel, p.border, Math.max(1, dp(c, 1))));
		int pad = dp(c, 10);
		e.setPadding(pad, pad, pad, pad);
		if ((type & InputType.TYPE_TEXT_FLAG_MULTI_LINE) == 0) e.setSingleLine(true);
		return e;
	}

	/** the bar at the top: SYN-MAIL on the accent, then where you are */
	static LinearLayout header(Context c, Palette p, String where) {
		LinearLayout h = new LinearLayout(c);
		h.setOrientation(LinearLayout.HORIZONTAL);
		h.setBackground(box(p.panel, p.panel, 0));
		h.setGravity(Gravity.CENTER_VERTICAL);
		int pad = dp(c, 10);
		TextView brand = text(c, p, "SYN-MAIL", 16, true, p.inkOn(p.accent));
		brand.setBackground(new ColorDrawable(p.accent));
		brand.setPadding(pad, pad, pad, pad);
		h.addView(brand);
		TextView w = line(c, p, where, 15, true, p.text);
		w.setPadding(pad, 0, pad, 0);
		h.addView(w, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
		return h;
	}

	/** a list row: a bold first line, a dimmer second, and an accent bar on the left when it is unread */
	static LinearLayout row(Context c, Palette p, String first, String second, String right, boolean unread) {
		LinearLayout r = new LinearLayout(c);
		r.setOrientation(LinearLayout.HORIZONTAL);
		StateListDrawable bg = new StateListDrawable();
		bg.addState(new int[] {android.R.attr.state_pressed}, new ColorDrawable(p.panelHover));
		bg.addState(new int[] {}, new ColorDrawable(p.bg));
		r.setBackground(bg);
		View bar = new View(c);
		bar.setBackground(new ColorDrawable(unread ? p.accent : p.bg));
		r.addView(bar, new LinearLayout.LayoutParams(dp(c, 4), LinearLayout.LayoutParams.MATCH_PARENT));
		LinearLayout col = new LinearLayout(c);
		col.setOrientation(LinearLayout.VERTICAL);
		int pad = dp(c, 10);
		col.setPadding(pad, pad, pad, pad);
		LinearLayout top = new LinearLayout(c);
		top.setOrientation(LinearLayout.HORIZONTAL);
		TextView a = line(c, p, first, 15, unread, p.text);
		a.setTag("first");
		top.addView(a, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
		if (right != null) {
			TextView rt = line(c, p, right, 12, false, Palette.mix(p.text, p.bg, 35));
			rt.setPadding(pad, 0, 0, 0);
			top.addView(rt);
		}
		col.addView(top);
		if (second != null) {
			TextView b = line(c, p, second, 13, false, unread ? p.text : Palette.mix(p.text, p.bg, 30));
			b.setTag("second");
			col.addView(b);
		}
		r.addView(col, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
		r.setClickable(true);
		return r;
	}

	static View rule(Context c, Palette p) {
		View v = new View(c);
		v.setBackground(new ColorDrawable(p.border));
		v.setLayoutParams(new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, Math.max(1, dp(c, 1))));
		return v;
	}

	private static final DateTimeFormatter SHORT = DateTimeFormatter.ofPattern("d MMM HH:mm", Locale.UK);

	/** "Sun, 1 Nov 2026 14:05:00 +0000" as "1 Nov 14:05", in the phone's zone; the header as it came if it will not parse */
	static String when(String rfc) {
		try {
			String s = rfc.replaceAll("\\s*\\(.*\\)\\s*$", "").trim();
			return ZonedDateTime.parse(s, DateTimeFormatter.RFC_1123_DATE_TIME)
				.withZoneSameInstant(java.time.ZoneId.systemDefault()).format(SHORT);
		} catch (RuntimeException e) {
			return rfc;
		}
	}
}
