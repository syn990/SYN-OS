/* ------------------------------------------------------------------------
 *   A small JSON reader and writer for the account files: objects, arrays,
 *   strings, numbers, true, false, null. No Android code, so the desktop
 *   JVM tests it.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL-APK
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
package com.syntax990.synmail.core;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

public final class Json {
	private final String s;
	private int i;

	private Json(String s) {
		this.s = s;
	}

	/** a Map, List, String, Long, Double, Boolean or null; IllegalArgumentException on bad input */
	public static Object parse(String text) {
		Json j = new Json(text);
		j.ws();
		Object v = j.value(0);
		j.ws();
		if (j.i != j.s.length()) throw j.bad("text after the value");
		return v;
	}

	@SuppressWarnings("unchecked")
	public static Map<String, Object> object(String text) {
		Object v = parse(text);
		if (!(v instanceof Map)) throw new IllegalArgumentException("not a JSON object");
		return (Map<String, Object>) v;
	}

	public static String str(Map<String, Object> o, String k) {
		Object v = o.get(k);
		return v instanceof String ? (String) v : "";
	}

	public static long num(Map<String, Object> o, String k) {
		Object v = o.get(k);
		return v instanceof Long ? (Long) v : v instanceof Double ? (long) (double) (Double) v : 0;
	}

	private IllegalArgumentException bad(String why) {
		return new IllegalArgumentException("JSON at " + i + ": " + why);
	}

	private void ws() {
		while (i < s.length() && " \t\r\n".indexOf(s.charAt(i)) >= 0) i++;
	}

	private Object value(int depth) {
		if (depth > 64) throw bad("nested too deep");
		if (i >= s.length()) throw bad("ends early");
		char c = s.charAt(i);
		if (c == '{') {
			i++;
			Map<String, Object> m = new LinkedHashMap<>();
			ws();
			if (peek('}')) return m;
			while (true) {
				ws();
				if (i >= s.length() || s.charAt(i) != '"') throw bad("a key must be a string");
				String k = string();
				ws();
				expect(':');
				ws();
				m.put(k, value(depth + 1));
				ws();
				if (peek('}')) return m;
				expect(',');
			}
		}
		if (c == '[') {
			i++;
			List<Object> l = new ArrayList<>();
			ws();
			if (peek(']')) return l;
			while (true) {
				ws();
				l.add(value(depth + 1));
				ws();
				if (peek(']')) return l;
				expect(',');
			}
		}
		if (c == '"') return string();
		if (s.startsWith("true", i)) { i += 4; return Boolean.TRUE; }
		if (s.startsWith("false", i)) { i += 5; return Boolean.FALSE; }
		if (s.startsWith("null", i)) { i += 4; return null; }
		int st = i;
		while (i < s.length() && "+-0123456789.eE".indexOf(s.charAt(i)) >= 0) i++;
		String n = s.substring(st, i);
		if (n.isEmpty()) throw bad("unexpected '" + c + "'");
		try {
			if (n.matches("-?\\d{1,18}")) return Long.parseLong(n);
			return Double.parseDouble(n);
		} catch (NumberFormatException e) {
			throw bad("bad number");
		}
	}

	private boolean peek(char c) {
		if (i < s.length() && s.charAt(i) == c) {
			i++;
			return true;
		}
		return false;
	}

	private void expect(char c) {
		if (!peek(c)) throw bad("expected '" + c + "'");
	}

	private String string() {
		i++;
		StringBuilder b = new StringBuilder();
		while (true) {
			if (i >= s.length()) throw bad("unterminated string");
			char c = s.charAt(i++);
			if (c == '"') return b.toString();
			if (c < 0x20) throw bad("control character in a string");
			if (c != '\\') {
				b.append(c);
				continue;
			}
			if (i >= s.length()) throw bad("unterminated escape");
			char e = s.charAt(i++);
			switch (e) {
			case '"': case '\\': case '/': b.append(e); break;
			case 'b': b.append('\b'); break;
			case 'f': b.append('\f'); break;
			case 'n': b.append('\n'); break;
			case 'r': b.append('\r'); break;
			case 't': b.append('\t'); break;
			case 'u':
				if (i + 4 > s.length()) throw bad("short \\u escape");
				try {
					b.append((char) Integer.parseInt(s.substring(i, i + 4), 16));
				} catch (NumberFormatException x) {
					throw bad("bad \\u escape");
				}
				i += 4;
				break;
			default: throw bad("bad escape");
			}
		}
	}

	/** JSON text for a Map, List, String, Number, Boolean or null */
	public static String write(Object v) {
		StringBuilder b = new StringBuilder();
		write(b, v);
		return b.toString();
	}

	private static void write(StringBuilder b, Object v) {
		if (v == null) {
			b.append("null");
		} else if (v instanceof String) {
			quote(b, (String) v);
		} else if (v instanceof Number || v instanceof Boolean) {
			b.append(v);
		} else if (v instanceof Map) {
			b.append('{');
			boolean first = true;
			for (Map.Entry<?, ?> e : ((Map<?, ?>) v).entrySet()) {
				if (!first) b.append(',');
				first = false;
				quote(b, String.valueOf(e.getKey()));
				b.append(':');
				write(b, e.getValue());
			}
			b.append('}');
		} else if (v instanceof List) {
			b.append('[');
			boolean first = true;
			for (Object x : (List<?>) v) {
				if (!first) b.append(',');
				first = false;
				write(b, x);
			}
			b.append(']');
		} else {
			quote(b, v.toString());
		}
	}

	private static void quote(StringBuilder b, String s) {
		b.append('"');
		for (int k = 0; k < s.length(); k++) {
			char c = s.charAt(k);
			switch (c) {
			case '"': b.append("\\\""); break;
			case '\\': b.append("\\\\"); break;
			case '\n': b.append("\\n"); break;
			case '\r': b.append("\\r"); break;
			case '\t': b.append("\\t"); break;
			default:
				if (c < 0x20) b.append(String.format("\\u%04x", (int) c));
				else b.append(c);
			}
		}
		b.append('"');
	}
}
