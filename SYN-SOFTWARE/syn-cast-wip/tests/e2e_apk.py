#!/usr/bin/env python3
"""SYN-CAST-APK's engine against desktop SYN-CAST, on a SYN-CAST protocol v1 server.

The Android app's core (apk/java/.../core, with libopus through the host
build of its JNI glue) runs on this JVM as "jim"; the desktop binary runs
headless as "alice". Both sign in with member tokens over wss with the
server's certificate pinned (jim keeps it inline as PEM text), talk, text,
send pictures and locations both ways, and each one's stored voice files
are decoded by the other's code. The server is found as tests/e2e.py finds
it (exit 77 without one).

usage: tests/e2e_apk.py path/to/syn-cast
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import e2e  # noqa: E402  (helpers: Server, Instance, Raw, check, wait_for, tone_wav, wav_info, ...)
from e2e import check, wait_for  # noqa: E402

BIN = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "build", "syn-cast"))
e2e.BIN = BIN
APK = os.path.join(HERE, "..", "apk")
HOST = os.path.join(APK, "out", "host")


def build_core():
    os.makedirs(os.path.join(HOST, "classes"), exist_ok=True)
    jdk = subprocess.run(["sh", "-c", "dirname $(dirname $(readlink -f $(which javac)))"], capture_output=True,
                         text=True).stdout.strip()
    opus = subprocess.run(["pkg-config", "--cflags", "--libs", "opus"], capture_output=True, text=True).stdout.split()
    subprocess.run(["gcc", "-O2", "-shared", "-fPIC", "-o", os.path.join(HOST, "libsyncast-host.so"),
                    os.path.join(APK, "jni", "syncast_opus.c"), f"-I{jdk}/include", f"-I{jdk}/include/linux", *opus],
                   check=True)
    srcs = [os.path.join(d, f) for root in ("java/com/syntax990/syncast/core", "test")
            for d, _, fs in os.walk(os.path.join(APK, root)) for f in fs if f.endswith(".java")]
    subprocess.run(["javac", "-nowarn", "-d", os.path.join(HOST, "classes"), *srcs], check=True)


class Jim:
    """The APK engine in a JVM, one command per line."""

    def __init__(self, root, srv, channels, name="jim", token=None, invite=None, extra=None, jvm=()):
        self.dir = os.path.join(root, name)
        os.makedirs(self.dir)
        self.cfg = os.path.join(self.dir, "config.json")
        cfg = {"server": srv.url(), "cert_pem": open(srv.cert).read(), "device": "phone", "channels": channels,
               "tones": False}
        cfg.update(extra or {})
        self.jvm = list(jvm)
        if invite:
            cfg["invite"] = invite
        else:
            cfg["token"] = token if token is not None else srv.token(name)
        with open(self.cfg, "w") as f:
            json.dump(cfg, f)
        self.playout = os.path.join(self.dir, "playout.raw")
        self.log_path = os.path.join(self.dir, "log.txt")
        self.start()

    def start(self):
        self.log = open(self.log_path, "a")
        self.proc = subprocess.Popen(
            ["java", "--enable-native-access=ALL-UNNAMED", f"-Dsyncast.jni={os.path.join(HOST, 'libsyncast-host.so')}",
             "-Xmx256m", "-Dsyncast.deadAfterMs=22000", *self.jvm, "-cp", os.path.join(HOST, "classes"),
             "com.syntax990.syncast.test.HeadlessClient", self.cfg, os.path.join(self.dir, "data"), self.playout],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self.log, text=True, bufsize=1)

    def cmd(self, line):
        self.proc.stdin.write(line + "\n")
        self.proc.stdin.flush()
        return self.proc.stdout.readline().strip()

    def status(self):
        return json.loads(self.cmd("status"))

    def online(self):
        st = self.status()
        return bool(st["channels"]) and st["channels"][int(st["active"])]["online"]

    def config(self):
        return json.load(open(self.cfg))

    def history(self, k=0):
        return json.loads(self.cmd(f"history {k}"))

    def find(self, k=0, **want):
        for m in reversed(self.history(k)):
            if all(m.get(a) == b for a, b in want.items()):
                return m
        return None

    def dm(self, peer):
        """G193: the index of the direct-message tab with peer, or None"""
        for k, c in enumerate(self.status()["channels"]):
            if c.get("peer") == peer:
                return k
        return None

    def dm_find(self, peer, **want):
        k = self.dm(peer)
        return None if k is None else self.find(k, **want)

    def voices(self, k=0, **want):
        return [v for v in self.history(k) if v["kind"] == "voice" and all(v.get(a) == b for a, b in want.items())]

    def played_seconds(self):
        return os.path.getsize(self.playout) / 2 / 16000 if os.path.exists(self.playout) else 0

    def spt(self, channel, msg_id):
        return os.path.join(self.dir, "data", channel, f"v{msg_id}.spt")

    def log_count(self, needle):
        return open(self.log_path).read().count(needle)

    def stop(self):
        if self.proc.poll() is None:
            try:
                self.cmd("quit")
                self.proc.wait(5)
            except Exception:
                self.proc.kill()
        return self.proc.returncode


def rooms_bar(srv, jim):
    """G192: the rooms bar follows the server's rooms: a ticket room opened for the member appears as a tab, already
    joined, ahead of the other rooms; swiping moves along the bar; a closed room leaves it. Needs a server with the
    rooms request (SYN-CASTER after G192); an older one answers it with an error and the checks are skipped."""
    probe = e2e.Raw(srv, srv.token("dave"), channels=["test"])
    r = probe.req("rooms")
    probe.close()
    if not r or r.get("type") != "ok":
        print("SKIP rooms bar: this server has no rooms request")
        return
    check("the bar lists the rooms the member is in", wait_for(lambda: sorted(jim.status()["bar"]) == ["second", "test"], 4),
          jim.status())
    room = os.path.join(srv.data, "channels", "g999", "channel.json")
    e2e.write_json(room, {"name": "G999 a ticket room", "kind": "invite", "owner": "alice", "members": ["frank"]})
    check("a ticket room opened for him appears on the bar by itself, before the other rooms",
          wait_for(lambda: jim.status()["bar"][:1] == ["g999"], 12), jim.status())
    g = [c for c in jim.status()["channels"] if c["name"] == "g999"]
    check("its tab carries the ticket's title and it is joined, no name typed",
          g and g[0]["title"] == "G999 a ticket room" and wait_for(lambda: [c for c in jim.status()["channels"]
                                                                            if c["name"] == "g999"][0]["online"], 5), g)
    first = jim.status()["active"]
    jim.cmd("swipe -1")
    jim.cmd("swipe -1")
    jim.cmd("swipe -1")

    def on_tab(k):  # the swipes are queued on the engine's thread: read the status once they have applied
        st = jim.status()
        return st["channels"][int(st["active"])]["name"] == st["bar"][k]
    check("swiping back runs to the first tab and stops there", wait_for(lambda: on_tab(0), 4), jim.status())
    jim.cmd("swipe 1")
    check("and a swipe forward moves one tab", wait_for(lambda: on_tab(1), 4), (first, jim.status()))
    e2e.write_json(room, {"name": "G999 a ticket room", "kind": "invite", "owner": "alice", "members": ["frank"],
                          "closed": True})
    check("a closed ticket room leaves the bar by itself", wait_for(lambda: "g999" not in jim.status()["bar"], 12),
          jim.status())


def rooms_on_off(srv, jim, alice, wav):
    """G278: a room switched off keeps every voice message unheard and plays none; REPLAY plays this room's missed
    ones oldest first, each once however often it is tapped; the switch is kept by the server across a restart."""
    if not e2e.has_rooms_switch(srv):
        print("rooms on/off: needs the SYN-CASTER server with the room request (G278): skipped")
        return
    idle = lambda: (lambda st: st["queue"] == 0 and st["talk"] == "idle")(jim.status())
    off = lambda: jim.status()["channels"][0].get("off")
    wait_for(idle, 8)
    jim.cmd("channel 0")
    jim.cmd("room off")
    check("room off: the tab shows it off", wait_for(off, 3), jim.status()["channels"][0])
    time.sleep(1.0)
    check("room off: the server kept it (no refusal)", jim.log_count("the switch was not saved") == 0)

    def say_missed(n):
        heard0 = len([v for v in jim.voices(out=False) if not v.get("partial")])
        for _ in range(n):
            wait_for(lambda: alice.status()["talk"] == "idle", 6)
            alice.ctl("say", wav)
            want = heard0 + 1
            wait_for(lambda: len([v for v in jim.voices(out=False) if not v.get("partial")]) >= want, 10)
            heard0 = want
            time.sleep(0.5)

    p0 = jim.played_seconds()
    say_missed(2)
    time.sleep(1.0)
    unheard = [v for v in jim.voices(out=False) if not v.get("played")]
    check("room off: both voice messages are kept, unheard", len(unheard) == 2, [(v["id"], v.get("played")) for v in unheard])
    check("room off: nothing played aloud", jim.played_seconds() - p0 < 0.1, jim.played_seconds() - p0)
    check("room off: the tab counts the two unheard", jim.status()["channels"][0]["unread"] >= 2, jim.status()["channels"][0])
    p1 = jim.played_seconds()
    jim.cmd("replay")
    jim.cmd("replay")
    wait_for(lambda: jim.played_seconds() > p1, 4)
    wait_for(idle, 12)
    got = jim.played_seconds() - p1
    check("REPLAY tapped twice plays the two missed voice messages once each", 3.8 < got < 4.6, got)
    check("each is marked heard as it finishes", not [v for v in jim.voices(out=False) if not v.get("played")])

    jim.stop()
    jim.start()
    check("room off: back after a restart, signed in", wait_for(jim.online, 10))
    check("room off: the server gives the switch back on sign-in", wait_for(off, 4), jim.status()["channels"][0])
    p2 = jim.played_seconds()
    say_missed(1)
    time.sleep(1.0)
    check("room off after the restart: still nothing plays aloud", jim.played_seconds() - p2 < 0.1, jim.played_seconds() - p2)
    jim.cmd("room on")
    check("room on: the tab shows it on", wait_for(lambda: off() is False, 3), jim.status()["channels"][0])
    wait_for(lambda: jim.played_seconds() > p2, 4)
    wait_for(idle, 8)
    got = jim.played_seconds() - p2
    check("room on: the one missed while off plays, once", 1.8 < got < 2.4, got)
    p3 = jim.played_seconds()
    say_missed(1)
    check("room on: a new voice message plays aloud again", wait_for(lambda: jim.played_seconds() - p3 > 1.8, 8),
          jim.played_seconds() - p3)
    wait_for(idle, 8)


def direct_messages(srv, jim, alice, dave):
    """G193: a direct message gets its own tab (@dave), /msg answers on it, and what was missed while away (a direct
    message and a room's line) is fetched from the server's history on the way back, without doubling what was held."""
    m = wait_for(lambda: jim.dm_find("dave", kind="text", text="just for jim"))
    check("an incoming direct message gets its own tab, marked for jim", m and m["for"] == "jim", jim.status())
    k = jim.dm("dave")
    c = jim.status()["channels"][k] if k is not None else {}
    check("the tab is the conversation dm-dave-jim, called @dave", c.get("name") == "dm-dave-jim" and c.get("title") == "@dave", c)
    check("and the line is not in the room that was open", not jim.find(0, kind="text", text="just for jim"))
    check("and the desktop doesn't get it", not alice.find("test", kind="text", text="just for jim"))
    jim.cmd("input /msg dave hello dave, from the phone")
    ev = dave.event("say", timeout=6, **{"from": "jim"})
    check("/msg dave TEXT answers by direct message: dave gets it from jim, addressed to him",
          ev and ev.get("to") == "dave" and ev.get("text") == "hello dave, from the phone" and not ev.get("channel"), ev)
    mine = wait_for(lambda: (lambda x: x if x and x.get("sid") and not x.get("partial") else None)(
        jim.dm_find("dave", out=True, text="hello dave, from the phone")), 4)
    check("the phone keeps its copy on the @dave tab, confirmed with the server's message id", mine, jim.dm_find("dave", out=True))
    st = jim.status()
    check("and /msg went to that tab", st["channels"][int(st["active"])].get("peer") == "dave", st)
    check("the rooms stay joined beside it", [c["state"] for c in st["channels"] if not c.get("peer")] == ["ONLINE", "ONLINE"], st)

    jim.cmd("offline")
    wait_for(lambda: jim.status()["channels"][0]["state"] == "OFFLINE", 5)
    dave.req("say", to="jim", text="sent while jim was away")
    alice.ctl("text", "the room carried on without jim")
    wait_for(lambda: alice.find("test", kind="text", text="the room carried on without jim", out=True), 4)
    time.sleep(1)
    jim.cmd("online")
    check("back online", wait_for(jim.online, 10), jim.status())
    check("the direct message missed while away is fetched from the server's history",
          wait_for(lambda: jim.dm_find("dave", kind="text", text="sent while jim was away"), 8), jim.history(jim.dm("dave") or 0))
    got = wait_for(lambda: jim.find(0, kind="text", text="the room carried on without jim"), 8)
    check("and so is the room's line, from alice, with its id", got and got["from"] == "alice" and got.get("sid"), got)
    room = [x.get("text") for x in jim.history(0) if x["kind"] == "text"]
    dm = [x.get("text") for x in jim.history(jim.dm("dave"))]
    check("lines already held are not doubled by the fetch",
          room.count("hello from the desktop") == 1 and room.count("hello from the phone") == 1
          and dm.count("just for jim") == 1 and dm.count("hello dave, from the phone") == 1, (room, dm))
    check("the room's missed line counts as unread on its tab", jim.status()["channels"][0]["unread"] >= 1, jim.status())
    jim.cmd("channel 0")
    # selectChannel is posted to the engine's thread, so the tab may move a moment after the command returns
    check("back to the room for the rest", wait_for(lambda: int(jim.status()["active"]) == 0, 3), jim.status())


def speakers(srv, jim, root, packets):
    """G228: a line or a note spoken for a character (the bot speaks for the whole crew as one member) carries as, the
    character's name and colour; the APK keeps them on the line, names and colours whoever is talking, and fetches
    the speaker's portrait once, for the top right corner while the note plays."""
    from PIL import Image
    data = srv.data
    e2e.write_json(os.path.join(data, "members", "kirk", "member.json"),
                   {"id": "kirk", "role": "cast", "services": [{"id": s} for s in ("chat", "radio", "messages", "channels")]})
    e2e.write_json(os.path.join(data, "members", "data", "member.json"),
                   {"id": "data", "name": "Commander Data", "colour": "#4fc3f7", "services": []})
    png = os.path.join(data, "members", "data", "portrait.png")
    Image.new("RGB", (64, 64), (79, 195, 247)).save(png)
    kirk = e2e.Raw(srv, srv.token("kirk"), channels=["test"])
    try:
        probe = kirk.req("portrait", member="data")
        if probe and probe.get("type") == "error" and probe.get("code") in ("unknown_type", "bad_request"):
            print("G228: this server has no speaker tags or portraits yet (SYN-CASTER C227): skipped")
            return
        r = kirk.req("say", channel="test", text="Captain, the figures are in.", **{"as": "data"})
        m = wait_for(lambda: jim.find(kind="text", text="Captain, the figures are in."), 6)
        check("a line spoken for a character keeps who spoke on the APK: as, name and colour",
              m and m.get("from") == "kirk" and m.get("as") == "data" and m.get("name") == "Commander Data"
              and m.get("colour") == "#4fc3f7", (r, m))
        wait_for(lambda: jim.status()["queue"] == 0 and jim.status()["talk"] == "idle", 8)
        r = kirk.req("talk", channel="test", codec="opus", rate=16000, channels=1, frame_ms=20, **{"as": "data"})
        sid = r.get("stream") if r else None
        check("the cast member takes the floor as the character", r and r.get("type") == "ok" and sid, r)
        seen = []
        t0 = time.time()
        for i, p in enumerate(packets):
            kirk.ws.send(bytes([1]) + int(sid).to_bytes(4, "big") + i.to_bytes(4, "big") + p)
            if i % 10 == 9:
                seen.append(jim.status())
            time.sleep(max(0, t0 + (i + 1) * 0.02 - time.time()))
        r = kirk.req("over", stream=sid)
        wait_for(lambda: (seen.append(jim.status()) or seen[-1]["queue"] == 0 and seen[-1]["talk"] == "idle"), 8)
        on = [st for st in seen if st.get("rx_name")]
        check("while the note plays the APK names the character, not the member who carried it",
              on and all(st["rx_name"] == "Commander Data" and st["rx_face"] == "data" for st in on), on[:2])
        check("in the character's colour", on and all(st["rx_colour"] == "#4fc3f7" for st in on), on[:2])
        size = os.path.getsize(png)
        check("and shows the portrait, fetched while it plays",
              any(st["rx_portrait"] == size for st in on), [st.get("rx_portrait") for st in on])
        got = json.loads(jim.cmd("portrait data"))
        check("the portrait is held whole, PNG bytes as the server keeps them", got.get("bytes") == size, (got, size))
        check("asked for once, however many notes", jim.log_count("portrait of data:") == 1, jim.log_count("portrait of data:"))
        v = wait_for(lambda: jim.find(kind="voice", **{"from": "kirk"}), 4)
        check("the note keeps the speaker on its line for replay and the list",
              v and v.get("as") == "data" and v.get("name") == "Commander Data" and v.get("colour") == "#4fc3f7", v)
    finally:
        kirk.close()


def decode(spt, out):
    subprocess.run([BIN, "decode", spt, out], capture_output=True)
    return e2e.wav_info(out)


def way_in_from_outside(root, srv):
    """G273: the home address does not answer (off the home Wi-Fi), so the APK signs in by the way in from outside the
    server gave it; and it keeps that way in, learned from hello's ok, in its config."""
    print("the way in from outside (G273)")
    dead = f"wss://127.0.0.1:{e2e.free_port()}{e2e.PATH}"
    carol = Jim(root, srv, ["test"], name="carol", extra={"server": dead, "remote": srv.url()},
                jvm=["-Dsyncast.remotePinned=true"])
    try:
        check("with the home address dead, the APK signs in by the way in from outside",
              wait_for(carol.online, 15), carol.status())
        check("and says it is on from outside", carol.status().get("far") is True, carol.status())
        check("the try at home was made first and failed", carol.log_count("connect: ") >= 1, carol.log_count("connect: "))
        check("the sign-in is logged as from outside", carol.log_count(", from outside") >= 1,
              carol.log_count(", from outside"))
    finally:
        carol.stop()
    usage = subprocess.run([*srv.argv], capture_output=True, text=True)
    if "--remote-log" not in usage.stdout + usage.stderr:
        print("  (this server has no --remote-log: the learning check is skipped)")
        return
    tlog = os.path.join(root, "cast-tunnel.log")
    with open(tlog, "w") as f:
        f.write("INF |  https://abc-def-ghi.trycloudflare.com  |\n")
    port = e2e.free_port()
    log = open(os.path.join(root, "server-remote.log"), "w")
    proc = subprocess.Popen([*srv.argv, "serve", "--data", srv.data, "--port", str(port), "--cert", srv.cert,
                             "--key", srv.key, "--remote-log", tlog], stdout=log, stderr=log)
    bob = None
    try:
        wait_for(lambda: subprocess.run(["sh", "-c", f"exec 3<>/dev/tcp/127.0.0.1/{port}"]).returncode == 0, 5)
        bob = Jim(root, srv, ["test"], name="bob", extra={"server": f"wss://127.0.0.1:{port}{e2e.PATH}"})
        check("signed in at home, the APK is told the way in from outside and keeps it",
              wait_for(lambda: bob.config().get("remote") == "wss://abc-def-ghi.trycloudflare.com/cast/v1", 10),
              bob.config().get("remote"))
        with open(tlog, "a") as f:
            f.write("INF |  https://new-name-here.trycloudflare.com  |\n")
        check("a new address from the server replaces it",
              wait_for(lambda: bob.config().get("remote") == "wss://new-name-here.trycloudflare.com/cast/v1", 12),
              bob.config().get("remote"))
        check("and the APK never logs it", bob.log_count("trycloudflare") == 0)
    finally:
        if bob:
            bob.stop()
        proc.terminate()
        proc.wait()


def main():
    if not e2e.find_server():
        print("no SYN-CAST protocol v1 server found ($SYN_CASTER, syn-caster on PATH, or $CASTD_REF): skipped")
        sys.exit(77)
    build_core()
    gain = subprocess.run(["java", "-Xmx64m", "-cp", os.path.join(HOST, "classes"), "com.syntax990.syncast.test.GainTest"],
                          capture_output=True, text=True)
    for line in gain.stdout.splitlines():
        if line.startswith(("ok ", "FAIL ")):
            check("gain: " + line[5:].split("  (")[0], line.startswith("ok "), line)
    check("gain: the test ran", gain.returncode in (0, 1) and ("passed" in gain.stdout or "failed" in gain.stdout),
          gain.stderr)
    root = tempfile.mkdtemp(prefix="syn-cast-apk-e2e-")
    srv = e2e.Server(root)
    print("server:", " ".join(srv.argv))
    alice = e2e.Instance(root, "alice", srv, ["test", "second"], playout=True)
    jim = Jim(root, srv, ["test", "second"])
    try:
        check("the APK engine signs in with its member token over wss, the certificate pinned as PEM text",
              wait_for(lambda: jim.online() and alice.online(), 15))
        st = jim.status()
        check("every configured channel is joined at once", [c["state"] for c in st["channels"]] == ["ONLINE", "ONLINE"], st)
        check("status names the member the server answered with", st["member"] == "jim", st)

        time.sleep(24)
        lost = sum(1 for m in jim.history() if (m.get("text") or "").startswith("connection lost"))
        check("a quiet spell longer than the APK's dead-link timeout keeps its session (the server's pings count)",
              lost == 0 and jim.online())

        alice.ctl("text", "hello from the desktop")
        check("desktop text reaches the APK", wait_for(lambda: jim.find(kind="text", text="hello from the desktop")))
        jim.cmd("text hello from the phone")
        m = wait_for(lambda: alice.find("test", kind="text", text="hello from the phone"))
        check("APK text reaches the desktop", m and m["from"] == "jim", m)
        check("the APK's copy is confirmed", wait_for(lambda: not (jim.find(kind="text", out=True) or {}).get("partial", True)))

        wav = os.path.join(root, "tone440.wav")
        e2e.tone_wav(wav, 440, 2.0)
        alice.ctl("say", wav)
        m = wait_for(lambda: (lambda v: v if v and not v["partial"] else None)(jim.find(kind="voice", **{"from": "alice"})), 8)
        check("desktop voice is stored by the APK", m and abs(m["duration_ms"] - 2040) <= 120, m)
        alice_voice = m["id"] if m else None
        check("and played by it", wait_for(lambda: jim.played_seconds() >= 1.9, 6), jim.played_seconds())
        if m:
            dur, hz = decode(jim.spt("test", m["id"]), os.path.join(root, "jim-440.wav"))
            check("the APK's voice file decodes with the desktop's code: 2 s of 440 Hz",
                  abs(dur - 2.04) < 0.15 and abs(hz - 440) < 20, (dur, hz))

        jim.cmd("ptt down")
        check("APK talk state reaches sending", wait_for(lambda: jim.status()["talk"] == "sending", 3))
        time.sleep(1.5)
        jim.cmd("ptt up")
        check("and returns to idle", wait_for(lambda: jim.status()["talk"] in ("idle", "receiving"), 3))
        m = wait_for(lambda: (lambda v: v if v and not v.get("partial") else None)(alice.find("test", kind="voice", **{"from": "jim"})), 6)
        check("APK voice reaches the desktop", m and 1300 <= m.get("duration_ms", 0) <= 2100, m)
        if m:
            dur, hz = decode(os.path.join(alice.dir, "data", "test", f"v{m['id']}.spt"), os.path.join(root, "alice-600.wav"))
            check("its audio is the APK's 600 Hz mic tone (libopus on the APK side)", abs(hz - 600) < 25, hz)
        mine = jim.find(kind="voice", out=True)
        check("the APK keeps its own message", mine and mine["duration_ms"] >= 1300, mine)
        check("the server saved it whole (over answered with the message id)",
              wait_for(lambda: jim.log_count("saved by the server") >= 1, 4))

        jim.cmd("ptt down")
        jim.cmd("ptt up")
        check("a quick tap on the APK ends cleanly", wait_for(lambda: jim.status()["talk"] in ("idle", "receiving"), 4))

        wait_for(lambda: alice.status()["talk"] == "idle", 6)
        alice.ctl("ptt", "down")
        wait_for(lambda: alice.status()["talk"] == "sending", 3)
        n0 = len(jim.voices(out=True))
        jim.cmd("ptt down")
        check("the APK is refused busy while the desktop holds the floor",
              wait_for(lambda: jim.status()["talk"] in ("idle", "receiving"), 4))
        jim.cmd("ptt up")
        time.sleep(0.3)
        check("and keeps no voice message for it", len(jim.voices(out=True)) == n0)
        check("the refusal names the talker", wait_for(lambda: jim.log_count("cannot talk: alice is talking") >= 1, 2))
        alice.ctl("ptt", "up")
        wait_for(lambda: alice.status()["talk"] == "idle", 4)

        from PIL import Image
        jpg = os.path.join(root, "pic.jpg")
        Image.new("RGB", (640, 400), (200, 20, 20)).save(jpg)
        jim.cmd(f"image {jpg}")
        m = wait_for(lambda: (lambda i: i if i and not i.get("partial") else None)(alice.find("test", kind="image")), 8)
        check("APK picture reaches the desktop complete", m and m.get("w") == 640 and m.get("h") == 400, m)
        alice.ctl("image", jpg)
        m = wait_for(lambda: (lambda i: i if i and not i["partial"] else None)(jim.find(kind="image", **{"from": "alice"})), 8)
        check("desktop picture reaches the APK", m)
        if m:
            data = open(os.path.join(jim.dir, "data", "test", f"i{m['id']}.jpg"), "rb").read()
            thumb = os.path.join(jim.dir, "data", "test", f"i{m['id']}.t.jpg")
            check("stored as a JPEG with a thumbnail cut from it",
                  data[:2] == b"\xff\xd8" and os.path.exists(thumb) and 0 < os.path.getsize(thumb) < len(data))

        jim.cmd("loc 48.8584 2.2945 Eiffel Tower")
        m = wait_for(lambda: alice.find("test", kind="location"))
        check("APK location reaches the desktop", m and abs(m["lat"] - 48.8584) < 1e-6 and m.get("text") == "Eiffel Tower", m)
        alice.ctl("loc", "51.5", "-0.12", "London")
        check("desktop location reaches the APK", wait_for(lambda: jim.find(kind="location", text="London")))

        if alice_voice is not None:
            speakers(srv, jim, root, e2e.spt_packets(jim.spt("test", alice_voice)))

        dave = e2e.Raw(srv, srv.token("dave"), channels=["test"])
        r = dave.req("say", to="jim", text="just for jim")
        direct_messages(srv, jim, alice, dave)
        dave.close()

        wait_for(lambda: jim.status()["queue"] == 0 and jim.status()["talk"] == "idle", 8)
        n0 = len(jim.voices(out=False))
        alice.ctl("say", wav)
        wait_for(lambda: len([v for v in jim.voices(out=False) if not v.get("partial")]) > n0, 8)
        wait_for(lambda: jim.status()["queue"] == 0 and jim.status()["talk"] == "idle", 8)
        p0 = jim.played_seconds()
        jim.cmd("replay")
        wait_for(lambda: jim.played_seconds() > p0, 4)
        wait_for(lambda: jim.status()["queue"] == 0 and jim.status()["talk"] == "idle", 8)
        one = jim.played_seconds() - p0
        jim.cmd("speed")
        jim.cmd("speed")
        p1 = jim.played_seconds()
        jim.cmd("replay")
        wait_for(lambda: jim.played_seconds() > p1, 4)
        wait_for(lambda: jim.status()["queue"] == 0 and jim.status()["talk"] == "idle", 8)
        fast = jim.played_seconds() - p1
        check("APK replay plays the last message", 1.8 < one < 2.3, one)
        check("APK 1.5x replay takes two thirds of the time", 0.6 < fast / one < 0.73, (one, fast))
        jim.cmd("speed")

        # Busy was taken out of the app in 0.6.0 (Vox replaced it), so its three checks are gone with it.

        rooms_on_off(srv, jim, alice, wav)

        other = e2e.Raw(srv, srv.token("jim"), device="phone", channels=["test"])
        check("a second hello as jim/phone replaces the APK's session", wait_for(lambda: not jim.online(), 6))
        check("which takes its place straight back", wait_for(jim.online, 10))
        check("closing the other with code 4003", other.close_code() == 4003)
        other.close()

        jim.cmd("offline")
        check("OFFLINE on the APK closes the session",
              wait_for(lambda: jim.status()["channels"][0]["state"] == "OFFLINE", 5))
        check("and the channel sees jim leave", wait_for(lambda: alice.status()["channels"][0]["users_online"] == 1, 5))
        jim.cmd("online")
        check("ONLINE brings it back", wait_for(jim.online, 8))

        jim.cmd("channel 1")
        check("selecting another channel only aims what is sent; both stay joined",
              wait_for(lambda: [c["state"] for c in jim.status()["channels"] if not c.get("peer")] == ["ONLINE", "ONLINE"]
                       and jim.status()["active"] == 1, 4), jim.status())

        code = srv.invite("frank")
        frank = Jim(root, srv, [], name="frank", invite=code)
        check("an invite code signs the APK in", wait_for(frank.online, 10), frank.status())
        cfg = wait_for(lambda: (lambda c: c if c.get("token") and not c.get("invite") else None)(frank.config()), 4)
        check("the token it earned is written to the config and the invite removed", bool(cfg), frank.config())
        st = frank.status()
        check("with no channels configured, hello joined every channel frank belongs to",
              st and sorted(c["name"] for c in st["channels"]) == ["second", "test"], st)
        frank.stop()
        frank.start()
        check("frank restarts on the kept token", wait_for(frank.online, 10), frank.status())
        rooms_bar(srv, frank)
        frank.stop()

        eve = Jim(root, srv, ["test"], name="eve", token="sct1.bad.token")
        check("a refused sign-in on the APK shows REFUSED",
              wait_for(lambda: eve.status()["channels"][0]["state"] == "REFUSED", 10), eve.status())
        check("and tells the setup screen why", eve.status().get("auth_error"), eve.status())
        time.sleep(4)
        check("and is tried exactly once", eve.log_count("sign-in refused") == 1, eve.log_count("sign-in refused"))
        eve.stop()

        way_in_from_outside(root, srv)
    finally:
        rc = [jim.stop(), alice.stop()]
        srv.stop()
    check("clean exits", rc == [0, 0], rc)
    errs = [l for l in open(os.path.join(jim.dir, "log.txt")) if "Exception" in l or "engine: " in l and "Error" in l]
    check("no exceptions in the APK engine log", not errs, errs[:5])
    if e2e.failures:
        print(f"\n{len(e2e.failures)} failed; logs in {root}")
        sys.exit(1)
    shutil.rmtree(root, ignore_errors=True)
    print("\nall passed")


if __name__ == "__main__":
    main()
