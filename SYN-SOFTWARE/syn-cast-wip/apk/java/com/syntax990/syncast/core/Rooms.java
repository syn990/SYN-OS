/* ------------------------------------------------------------------------
 *   The rooms bar's order and names (G192): the fixed rooms first, in the
 *   boss's order, then the ticket rooms by ticket number, then anything
 *   else as the server listed it. Pure: no Android, no engine state, so
 *   RoomsTest runs it on the desktop JVM.
 *
 *   5 Nov 2026, the boss's new layout: the server names each room's group
 *   (chat, service, crew, people, meeting) and the bar goes by it:
 *     Chat (id ops), Service (s-<you>), one room per crew member (each
 *     called after them), one per person, then booked meetings (only while
 *     booked, only for the people in them). A room without a group (an
 *     older server) falls back to the old order below.
 *
 *     Lobby (id ops), Crew, Meeting Room, Reminders, Alerts, Guests, the HQs (*-hq),
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
	public static final String[] FIXED = {"ops", "crew", "meeting-room", "reminders", "alerts", "guests"};
	/** The fixed rooms' names on the bar, the boss's (2 Nov): Lobby, Crew, Meeting Room, Reminders, Alerts, Guests; ops is Chat since 5 Nov. */
	public static final String[] FIXED_NAMES = {"Chat", "Crew", "Meeting Room", "Reminders", "Alerts", "Guests"};
	/** The server's groups (5 Nov 2026), in bar order. */
	public static final String[] GROUPS = {"chat", "service", "crew", "people", "meeting", "ambient"};

	/** A group's place on the bar, or -1 when the server gave none (or one this build does not know). */
	public static int groupRank(String group) {
		if (group == null) return -1;
		for (int i = 0; i < GROUPS.length; i++) if (GROUPS[i].equalsIgnoreCase(group)) return i;
		return -1;
	}

	/** The bar's order when the server names groups: by group, then as the server listed them (stable). Rooms with no
	 *  group go after, in the old order. */
	public static List<Integer> order(List<String> ids, List<String> groups) {
		List<Integer> out = new ArrayList<>();
		for (int i = 0; i < ids.size(); i++) {
			int at = out.size();
			while (at > 0 && compareG(ids.get(i), groups.get(i), ids.get(out.get(at - 1)), groups.get(out.get(at - 1))) < 0) at--;
			out.add(at, i);
		}
		return out;
	}

	private static int compareG(String a, String ga, String b, String gb) {
		int ra = groupRank(ga), rb = groupRank(gb);
		if (ra >= 0 && rb >= 0) return ra - rb;
		if (ra >= 0) return -1;
		if (rb >= 0) return 1;
		return compare(a, b);
	}
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

	/** What the tab says: the server's name (a 1-1 room is named after the other one, for you), else the fixed name,
	 *  else the id. Chat for ops when the server still calls it SYN-LOBBY. */
	public static String title(String id, String name) {
		if (name != null && !name.trim().isEmpty() && !"SYN-LOBBY".equals(name.trim())) return name.trim();
		if (id != null) for (int i = 0; i < FIXED.length; i++) if (FIXED[i].equalsIgnoreCase(id)) return FIXED_NAMES[i];
		return id == null ? "" : id;
	}
}
