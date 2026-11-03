/* ------------------------------------------------------------------------
 *   The account and settings, kept as config.json in the app's private
 *   files (the same keys as the desktop's ~/.config/syn-cast/config.json,
 *   with the pinned certificate inline as PEM text).
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CAST-APK
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
package com.syntax990.syncast.core;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

public final class Config {
	public String server = "";          /* wss://HOST:PORT/cast/v1 */
	public String member = "";          /* the member id the server answered hello with */
	public String token = "";           /* the member token, kept after an invite */
	public String invite = "";          /* a one-time invite code; spent on the first hello */
	public String device = "phone";
	public String certPem = "";         /* the server's own certificate, pinned; "" trusts the system store */
	/* G273: the way in from outside, wss://NAME/cast/v1, as the server last gave it (hello's remote, or a server
	   event). Tried when server cannot be reached (away from home Wi-Fi), with the system store: the tunnel's
	   certificate is the provider's, not the pinned one. "" none yet. */
	public String remote = "";
	public List<String> channels = new ArrayList<>();   /* empty: every channel the member belongs to */

	public boolean pttToggle;
	public boolean autoPlay = true;
	public boolean noiseSuppression = true;
	public boolean vox;
	public double voxThreshold = 0.5;   /* 0..1 sensitivity: higher opens on quieter speech */
	public int maxTalkSeconds = 300;
	public boolean notify = true;
	public boolean tones = true;
	public boolean volumeKeyPtt;
	public boolean speaker = true;
	public int historyDays = 30, historyMb = 256;
	public String theme = "";

	/** A server and a token or an invite: enough to say hello. */
	public boolean hasAccount() {
		return !server.isEmpty() && (!token.isEmpty() || !invite.isEmpty());
	}

	public static Config load(File f) {
		Config c = new Config();
		if (!f.exists()) return c;
		try {
			Map<String, Object> o = Json.object(new String(Files.readAllBytes(f.toPath()), StandardCharsets.UTF_8));
			if (o != null) c.read(o);
		} catch (IOException ignored) {
		}
		return c;
	}

	public void read(Map<String, Object> o) {
		if (o.containsKey("server")) server = Json.str(o, "server");
		if (o.containsKey("member")) member = Json.str(o, "member");
		if (o.containsKey("token")) token = Json.str(o, "token");
		if (o.containsKey("invite")) invite = Json.str(o, "invite");
		if (o.containsKey("device") && !Json.str(o, "device").isEmpty()) device = Json.str(o, "device");
		if (o.containsKey("cert_pem")) certPem = Json.str(o, "cert_pem");
		if (o.containsKey("remote")) remote = Json.str(o, "remote");
		Object ch = o.get("channels");
		if (ch instanceof List) {
			channels.clear();
			for (Object v : (List<?>) ch) {
				if (v instanceof String && !((String) v).trim().isEmpty()) channels.add(((String) v).trim());
			}
		}
		if (o.containsKey("ptt_mode")) pttToggle = "toggle".equals(Json.str(o, "ptt_mode"));
		if (o.containsKey("auto_play")) autoPlay = Json.bool(o, "auto_play");
		if (o.containsKey("noise_suppression")) noiseSuppression = Json.bool(o, "noise_suppression");
		if (o.containsKey("vox")) vox = Json.bool(o, "vox");
		if (o.containsKey("vox_threshold")) voxThreshold = Json.num(o, "vox_threshold");
		if (o.containsKey("max_talk_seconds")) maxTalkSeconds = Math.max(5, (int) Json.lng(o, "max_talk_seconds"));
		if (o.containsKey("notify")) notify = Json.bool(o, "notify");
		if (o.containsKey("tones")) tones = Json.bool(o, "tones");
		if (o.containsKey("volume_key_ptt")) volumeKeyPtt = Json.bool(o, "volume_key_ptt");
		if (o.containsKey("speaker")) speaker = Json.bool(o, "speaker");
		if (o.containsKey("history_days")) historyDays = (int) Json.lng(o, "history_days");
		if (o.containsKey("history_mb")) historyMb = (int) Json.lng(o, "history_mb");
		if (o.containsKey("theme")) theme = Json.str(o, "theme");
	}

	public void save(File f) throws IOException {
		Map<String, Object> o = Json.obj("server", server, "member", member, "token", token, "invite", invite,
			"device", device, "cert_pem", certPem, "remote", remote, "channels", new ArrayList<Object>(channels),
			"ptt_mode", pttToggle ? "toggle" : "hold", "auto_play", autoPlay, "noise_suppression", noiseSuppression,
			"vox", vox, "vox_threshold", voxThreshold, "max_talk_seconds", (double) maxTalkSeconds,
			"notify", notify, "tones", tones, "volume_key_ptt", volumeKeyPtt,
			"speaker", speaker, "history_days", (double) historyDays, "history_mb", (double) historyMb, "theme", theme);
		File tmp = new File(f.getPath() + ".tmp");
		try (FileOutputStream out = new FileOutputStream(tmp)) {
			out.write(Json.write(o).getBytes(StandardCharsets.UTF_8));
		}
		if (!tmp.renameTo(f)) throw new IOException("cannot write " + f);
	}
}
