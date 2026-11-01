/* ------------------------------------------------------------------------
 *   The rooms bar's order and names (G192): the fixed rooms first, in the
 *   boss's order, then the ticket rooms by ticket number, then anything
 *   else as the server listed it. Pure: no Android, no engine state, so
 *   RoomsTest runs it on the desktop JVM.
 *
 *     Lobby (id ops), Crew, Alerts, Guests, Meeting Room, the HQs (*-hq),
 *     g<n> ticket rooms by n, then the rest in arrival order.
 *
 *   Nobody makes a room from the app: the list is the server's.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CAST-APK
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
package com.syntax990.syncast.core;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

public final class Rooms {
	private Rooms() {
	}

	/** The fixed rooms, in the order they sit on the bar. */
	public static final String[] FIXED = {"ops", "crew", "alerts", "guests", "meeting-room"};
	public static final int RANK_HQ = FIXED.length, RANK_TICKET = FIXED.length + 1, RANK_OTHER = FIXED.length + 2;

	/** The rank a channel id sorts by: its place among the fixed rooms, then HQs, then tickets, then the rest. */
	public static int rank(String id) {
		String k = id == null ? "" : id.toLowerCase(Locale.ROOT);
		for (int i = 0; i < FIXED.length; i++) if (FIXED[i].equals(k)) return i;
		if (k.endsWith("-hq")) return RANK_HQ;
		if (ticket(k) >= 0) return RANK_TICKET;
		return RANK_OTHER;
	}

	/** The ticket number of a ticket room (g192, g192-demo), or -1. */
	public static long ticket(String id) {
		String k = id == null ? "" : id.toLowerCase(Locale.ROOT);
		if (k.length() < 2 || k.charAt(0) != 'g' || !Character.isDigit(k.charAt(1))) return -1;
		int e = 1;
		while (e < k.length() && Character.isDigit(k.charAt(e))) e++;
		if (e < k.length() && k.charAt(e) != '-') return -1;
		try {
			return Long.parseLong(k.substring(1, e));
		} catch (NumberFormatException x) {
			return -1;
		}
	}

	/** Negative when a sits before b on the bar; 0 keeps their arrival order. */
	public static int compare(String a, String b) {
		int ra = rank(a), rb = rank(b);
		if (ra != rb) return ra - rb;
		if (ra == RANK_HQ) return a.compareToIgnoreCase(b);
		if (ra == RANK_TICKET) {
			long ta = ticket(a), tb = ticket(b);
			return ta != tb ? Long.compare(ta, tb) : a.compareToIgnoreCase(b);
		}
		return 0;
	}

	/** The indexes of ids in bar order (a stable sort: equal ranks keep their arrival order). */
	public static List<Integer> order(List<String> ids) {
		List<Integer> out = new ArrayList<>();
		for (int i = 0; i < ids.size(); i++) {
			int at = out.size();
			while (at > 0 && compare(ids.get(i), ids.get(out.get(at - 1))) < 0) at--;
			out.add(at, i);
		}
		return out;
	}

	/** What the tab says: the server's name, the Lobby for ops, the id when there is no name. */
	public static String title(String id, String name) {
		if (id != null && id.equalsIgnoreCase("ops")) return "Lobby";
		if (name == null || name.trim().isEmpty()) return id == null ? "" : id;
		return name.trim();
	}
}
