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

    def __init__(self, root, srv, channels, name="jim", token=None, invite=None):
        self.dir = os.path.join(root, name)
        os.makedirs(self.dir)
        self.cfg = os.path.join(self.dir, "config.json")
        cfg = {"server": srv.url(), "cert_pem": open(srv.cert).read(), "device": "phone", "channels": channels,
               "tones": False}
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
             "-Xmx256m", "-Dsyncast.deadAfterMs=22000", "-cp", os.path.join(HOST, "classes"),
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
    st = jim.status()
    check("swiping back runs to the first tab and stops there", st["channels"][int(st["active"])]["name"] == st["bar"][0], st)
    jim.cmd("swipe 1")
    st = jim.status()
    check("and a swipe forward moves one tab", st["channels"][int(st["active"])]["name"] == st["bar"][1], (first, st))
    e2e.write_json(room, {"name": "G999 a ticket room", "kind": "invite", "owner": "alice", "members": ["frank"],
                          "closed": True})
    check("a closed ticket room leaves the bar by itself", wait_for(lambda: "g999" not in jim.status()["bar"], 12),
          jim.status())


def decode(spt, out):
    subprocess.run([BIN, "decode", spt, out], capture_output=True)
    return e2e.wav_info(out)


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

        dave = e2e.Raw(srv, srv.token("dave"), channels=["test"])
        r = dave.req("say", to="jim", text="just for jim")
        m = wait_for(lambda: jim.find(kind="text", text="just for jim"))
        check("an incoming direct message is shown on the APK, marked for it", m and m["for"] == "jim", m)
        check("and the desktop doesn't get it", not alice.find("test", kind="text", text="just for jim"))
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

        jim.cmd("busy")
        p2 = jim.played_seconds()
        n0 = len(jim.voices(out=False))
        alice.ctl("say", wav)
        wait_for(lambda: len([v for v in jim.voices(out=False) if not v.get("partial")]) > n0, 8)
        time.sleep(0.5)
        last = [v for v in jim.history() if v["kind"] == "voice"][-1]
        check("Busy on the APK keeps the message unplayed", not last["played"] and jim.played_seconds() - p2 < 0.1, last)
        check("and the channel hears jim is busy", wait_for(lambda: alice.log_count("jim is busy") >= 1, 3))
        jim.cmd("busy")
        check("Available again plays what Busy kept", wait_for(lambda: jim.played_seconds() - p2 >= 1.9, 8))

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
              wait_for(lambda: [c["state"] for c in jim.status()["channels"]] == ["ONLINE", "ONLINE"]
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
