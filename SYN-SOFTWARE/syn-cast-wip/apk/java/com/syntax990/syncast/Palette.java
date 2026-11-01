/* ------------------------------------------------------------------------
 *   SYN-OS colours on the phone: the same SYN_* .theme files the desktop
 *   reads (all 63 ship in assets/themes), defaulting to the theme that was
 *   live on the machine that built this APK.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CAST-APK
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
package com.syntax990.syncast;

import android.content.Context;
import android.graphics.Color;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

final class Palette {
	/* SYN-OS-M141, used whenever a theme can't be read */
	int bg = 0xFF000000, panel = 0xFF1A0000, panelHover = 0xFF330000, accent = 0xFFE00000, accentDim = 0xFF4D0000,
		text = 0xFFFFFFFF, border = 0xFF555555, urgent = 0xFFFF2020;
	String name = "SYN-OS-M141";

	static List<String> names(Context c) {
		List<String> out = new ArrayList<>();
		try {
			String[] files = c.getAssets().list("themes");
			if (files != null) {
				Arrays.sort(files);
				for (String f : files) {
					if (f.endsWith(".theme")) out.add(f.substring(0, f.length() - 6));
				}
			}
		} catch (IOException ignored) {
		}
		return out;
	}

	static String defaultName(Context c) {
		try (BufferedReader r = new BufferedReader(new InputStreamReader(c.getAssets().open("default-theme")))) {
			String l = r.readLine();
			return l == null ? "SYN-OS-M141" : l.trim();
		} catch (IOException e) {
			return "SYN-OS-M141";
		}
	}

	/** the theme SYN-LAUNCHER wears on this phone, or null without it */
	static String system(Context c) {
		try (android.database.Cursor q = c.getContentResolver().query(
				android.net.Uri.parse("content://com.syntax990.synlauncher.theme"), null, null, null, null)) {
			if (q != null && q.moveToFirst()) return q.getString(0);
		} catch (RuntimeException ignored) {
		}
		return null;
	}

	/** an empty name follows SYN-OS: the launcher's theme, else the one this APK was built with */
	static Palette load(Context c, String name) {
		Palette p = new Palette();
		if (name == null || name.isEmpty()) name = system(c);
		if (name == null || name.isEmpty()) name = defaultName(c);
		try (BufferedReader r = new BufferedReader(new InputStreamReader(c.getAssets().open("themes/" + name + ".theme")))) {
			p.name = name;
			String line;
			while ((line = r.readLine()) != null) {
				int eq = line.indexOf('=');
				if (eq < 0) continue;
				String k = line.substring(0, eq).trim();
				String v = line.substring(eq + 1).trim().replace("\"", "");
				if (!v.startsWith("#") || v.length() != 7) continue;
				int col;
				try {
					col = Color.parseColor(v);
				} catch (IllegalArgumentException e) {
					continue;
				}
				switch (k) {
				case "SYN_BG": p.bg = col; break;
				case "SYN_PANEL": p.panel = col; break;
				case "SYN_PANEL_HOVER": p.panelHover = col; break;
				case "SYN_ACCENT": p.accent = col; break;
				case "SYN_ACCENT_DIM": p.accentDim = col; break;
				case "SYN_TEXT": p.text = col; break;
				case "SYN_BORDER": p.border = col; break;
				case "SYN_URGENT": p.urgent = col; break;
				default: break;
				}
			}
		} catch (IOException ignored) {
		}
		return p;
	}

	/** a over b, pct percent of the way to b */
	static int mix(int a, int b, int pct) {
		int r = (Color.red(a) * (100 - pct) + Color.red(b) * pct) / 100;
		int g = (Color.green(a) * (100 - pct) + Color.green(b) * pct) / 100;
		int bl = (Color.blue(a) * (100 - pct) + Color.blue(b) * pct) / 100;
		return Color.rgb(r, g, bl);
	}

	private static int luma(int c) {
		return (Color.red(c) * 299 + Color.green(c) * 587 + Color.blue(c) * 114) / 1000;
	}

	/** text or background colour, whichever reads better on c */
	int inkOn(int c) {
		int l = luma(c);
		return Math.abs(luma(text) - l) >= Math.abs(luma(bg) - l) ? text : bg;
	}
}
