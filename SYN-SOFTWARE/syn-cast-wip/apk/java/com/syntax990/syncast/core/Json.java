/* ------------------------------------------------------------------------
 *   A small JSON reader and writer. The core runs on Android and on the
 *   desktop JVM (for the tests), so it cannot lean on Android's org.json.
 *   Objects are LinkedHashMap, arrays ArrayList, numbers Double.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CAST-APK
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
package com.syntax990.syncast.core;

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

	/** Parses a document; throws IllegalArgumentException on bad input. */
	public static Object parse(String text) {
		Json p = new Json(text);
		p.ws();
		Object v = p.value();
		p.ws();
		if (p.i != p.s.length()) {
			throw new IllegalArgumentException("trailing data at " + p.i);
		}
		return v;
	}

	/** The object, or null when text is not a JSON object. */
	@SuppressWarnings("unchecked")
	public static Map<String, Object> object(String text) {
		try {
			Object v = parse(text);
			return v instanceof Map ? (Map<String, Object>) v : null;
		} catch (RuntimeException e) {
			return null;
		}
	}

	private void ws() {
		while (i < s.length() && Character.isWhitespace(s.charAt(i))) {
			i++;
		}
	}

	private IllegalArgumentException bad(String what) {
		return new IllegalArgumentException(what + " at " + i);
	}

	private Object value() {
		if (i >= s.length()) throw bad("end of input");
		char c = s.charAt(i);
		switch (c) {
		case '{': return obj();
		case '[': return arr();
		case '"': return str();
		case 't': return lit("true", Boolean.TRUE);
		case 'f': return lit("false", Boolean.FALSE);
		case 'n': return lit("null", null);
		default:
			if (c == '-' || (c >= '0' && c <= '9')) return num();
			throw bad("unexpected '" + c + "'");
		}
	}

	private Object lit(String word, Object v) {
		if (!s.startsWith(word, i)) throw bad("expected " + word);
		i += word.length();
		return v;
	}

	private Double num() {
		int st = i;
		if (s.charAt(i) == '-') i++;
		while (i < s.length() && "0123456789.eE+-".indexOf(s.charAt(i)) >= 0) i++;
		try {
			return Double.valueOf(s.substring(st, i));
		} catch (NumberFormatException e) {
			throw bad("bad number");
		}
	}

	private String str() {
		StringBuilder b = new StringBuilder();
		i++;
		while (i < s.length()) {
			char c = s.charAt(i++);
			if (c == '"') return b.toString();
			if (c != '\\') {
				b.append(c);
				continue;
			}
			if (i >= s.length()) break;
			char e = s.charAt(i++);
			switch (e) {
			case 'n': b.append('\n'); break;
			case 't': b.append('\t'); break;
			case 'r': b.append('\r'); break;
			case 'b': b.append('\b'); break;
			case 'f': b.append('\f'); break;
			case 'u':
				if (i + 4 > s.length()) throw bad("bad \\u escape");
				b.append((char) Integer.parseInt(s.substring(i, i + 4), 16));
				i += 4;
				break;
			default: b.append(e);
			}
		}
		throw bad("unterminated string");
	}

	private Map<String, Object> obj() {
		Map<String, Object> m = new LinkedHashMap<>();
		i++;
		ws();
		if (i < s.length() && s.charAt(i) == '}') {
			i++;
			return m;
		}
		while (true) {
			ws();
			if (i >= s.length() || s.charAt(i) != '"') throw bad("expected a key");
			String k = str();
			ws();
			if (i >= s.length() || s.charAt(i) != ':') throw bad("expected ':'");
			i++;
			ws();
			m.put(k, value());
			ws();
			if (i >= s.length()) throw bad("unterminated object");
			char c = s.charAt(i++);
			if (c == '}') return m;
			if (c != ',') throw bad("expected ',' or '}'");
		}
	}

	private List<Object> arr() {
		List<Object> a = new ArrayList<>();
		i++;
		ws();
		if (i < s.length() && s.charAt(i) == ']') {
			i++;
			return a;
		}
		while (true) {
			ws();
			a.add(value());
			ws();
			if (i >= s.length()) throw bad("unterminated array");
			char c = s.charAt(i++);
			if (c == ']') return a;
			if (c != ',') throw bad("expected ',' or ']'");
		}
	}

	/* ---------- writing ---------- */

	public static String write(Object v) {
		StringBuilder b = new StringBuilder();
		write(b, v);
		return b.toString();
	}

	@SuppressWarnings("unchecked")
	private static void write(StringBuilder b, Object v) {
		if (v == null) {
			b.append("null");
		} else if (v instanceof String) {
			quote(b, (String) v);
		} else if (v instanceof Boolean) {
			b.append(v.toString());
		} else if (v instanceof Double || v instanceof Float) {
			double d = ((Number) v).doubleValue();
			/* integers print as integers up to 2^53, where doubles stop being exact
			 * (message ids are about 1.8e15) */
			if (d == Math.rint(d) && Math.abs(d) <= 9007199254740992.0) b.append((long) d);
			else b.append(d);
		} else if (v instanceof Number) {
			b.append(((Number) v).longValue());
		} else if (v instanceof Map) {
			b.append('{');
			boolean first = true;
			for (Map.Entry<String, Object> e : ((Map<String, Object>) v).entrySet()) {
				if (!first) b.append(',');
				first = false;
				quote(b, e.getKey());
				b.append(':');
				write(b, e.getValue());
			}
			b.append('}');
		} else if (v instanceof List) {
			b.append('[');
			boolean first = true;
			for (Object o : (List<Object>) v) {
				if (!first) b.append(',');
				first = false;
				write(b, o);
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

	/* ---------- typed getters ---------- */

	public static String str(Map<String, Object> m, String k) {
		Object v = m == null ? null : m.get(k);
		return v instanceof String ? (String) v : "";
	}

	public static double num(Map<String, Object> m, String k) {
		Object v = m == null ? null : m.get(k);
		return v instanceof Number ? ((Number) v).doubleValue() : 0;
	}

	public static long lng(Map<String, Object> m, String k) {
		return (long) num(m, k);
	}

	public static boolean bool(Map<String, Object> m, String k) {
		Object v = m == null ? null : m.get(k);
		return Boolean.TRUE.equals(v);
	}

	/** Builds an object from alternating keys and values, skipping null values. */
	public static Map<String, Object> obj(Object... kv) {
		Map<String, Object> m = new LinkedHashMap<>();
		for (int k = 0; k + 1 < kv.length; k += 2) {
			if (kv[k + 1] != null) m.put((String) kv[k], kv[k + 1]);
		}
		return m;
	}
}
