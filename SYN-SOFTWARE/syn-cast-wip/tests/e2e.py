#!/usr/bin/env python3
"""End to end: two headless syn-cast instances (alice, bob) and raw clients on a
SYN-CAST protocol v1 server, everything isolated in a temp dir. No audio
devices, no syn-bar-core.

The server is SYN-CASTER's own: `syn-caster cast serve` ($SYN_CASTER, or
syn-caster on PATH), or its Python reference tests/castd_ref.py ($CASTD_REF,
or ~/GithubProjects/SYN-CASTER/tests/castd_ref.py). With neither, exit 77
(ctest reports the test skipped). It serves wss:// with a self-signed
certificate that the clients pin.

usage: tests/e2e.py path/to/syn-cast
"""
import json
import math
import os
import shutil
import socket
import ssl
import struct
import subprocess
import sys
import tempfile
import time
import warnings
import wave

warnings.filterwarnings("ignore", category=DeprecationWarning)
HERE = os.path.dirname(os.path.abspath(__file__))
BIN = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "build", "syn-cast"))
PATH = "/cast/v1"
failures = []


def check(name, ok, detail=""):
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else f"  ({detail})"), flush=True)
    if not ok:
        failures.append(name)


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


def wait_for(pred, timeout=8.0, step=0.1):
    end = time.time() + timeout
    while time.time() < end:
        try:
            v = pred()
            if v:
                return v
        except Exception:
            pass
        time.sleep(step)
    return None


def tone_wav(path, hz, seconds, rate=16000):
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(b"".join(struct.pack("<h", int(9000 * math.sin(2 * math.pi * hz * i / rate)))
                               for i in range(int(rate * seconds))))


def wav_info(path):
    with wave.open(path) as w:
        n, rate = w.getnframes(), w.getframerate()
        s = struct.unpack(f"<{n}h", w.readframes(n))
    return n / rate, pitch(s, rate)


def pitch(samples, rate):
    mid = samples[len(samples) // 10: len(samples) * 9 // 10]
    if len(mid) < 2:
        return 0
    z = sum(1 for a, b in zip(mid, mid[1:]) if (a < 0) != (b < 0))
    return z / 2 / (len(mid) / rate)


# ---------- the server ----------

def find_server():
    """argv prefix for the server CLI (serve, token, invite, revoke), or None"""
    cand = os.environ.get("SYN_CASTER") or shutil.which("syn-caster")
    if cand and os.access(cand, os.X_OK):
        return [cand, "cast"]
    ref = os.environ.get("CASTD_REF") or os.path.expanduser("~/GithubProjects/SYN-CASTER/tests/castd_ref.py")
    if os.path.isfile(ref):
        try:
            import websockets  # noqa: F401  (castd_ref needs it)
            return [sys.executable, "-I", ref]
        except ImportError:
            return None
    return None


def write_json(path, obj):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        json.dump(obj, f, indent=1)


MEMBERS = ["alice", "bob", "carol", "dave", "eve", "frank", "jim"]


def setup_data(data):
    for m in MEMBERS:
        write_json(os.path.join(data, "members", m, "member.json"),
                   {"id": m, "services": [{"id": s} for s in ("chat", "radio", "messages", "channels")]})
    for cid, name in (("test", "Test"), ("second", "Second")):
        write_json(os.path.join(data, "channels", cid, "channel.json"),
                   {"name": name, "kind": "open", "owner": "alice", "members": MEMBERS})


class Server:
    def __init__(self, root):
        self.argv = find_server()
        self.data = os.path.join(root, "workspace")
        setup_data(self.data)
        self.cert, self.key = os.path.join(root, "cert.pem"), os.path.join(root, "key.pem")
        subprocess.run(["openssl", "req", "-x509", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:prime256v1",
                        "-nodes", "-keyout", self.key, "-out", self.cert, "-days", "1", "-subj", "/CN=syn-cast-e2e",
                        "-addext", "subjectAltName=IP:127.0.0.1"], check=True, capture_output=True)
        self.port = free_port()
        self.log_path = os.path.join(root, "server.log")
        self.log = open(self.log_path, "w")
        self.proc = subprocess.Popen([*self.argv, "serve", "--data", self.data, "--port", str(self.port), "--cert",
                                      self.cert, "--key", self.key, "--stall", "1"], stdout=self.log, stderr=self.log)
        for _ in range(100):
            try:
                socket.create_connection(("127.0.0.1", self.port), 0.1).close()
                break
            except OSError:
                time.sleep(0.05)

    def cli(self, *args):
        r = subprocess.run([*self.argv, args[0], "--data", self.data, *args[1:]], capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError(f"{' '.join(self.argv)} {' '.join(args)}: {r.stderr.strip()}")
        return r.stdout.split()

    def token(self, member):
        return self.cli("token", member)[0]

    def invite(self, member):
        return self.cli("invite", member)[0]

    def url(self):
        return f"wss://127.0.0.1:{self.port}{PATH}"

    def stop(self):
        self.proc.terminate()
        self.proc.wait()


# ---------- a headless syn-cast ----------

class Instance:
    def __init__(self, root, name, srv, channels, playout=False, token=None, invite=None, device="window",
                 dead_after_ms=22000):
        self.name = name
        self.dir = os.path.join(root, name)
        os.makedirs(self.dir, exist_ok=True)
        self.cfg = os.path.join(self.dir, "config.json")
        cfg = {"server": srv.url(), "cert": srv.cert, "device": device, "channels": channels, "tones": False,
               "notify": False, "noise_suppression": False, "history_days": 0, "history_mb": 0}
        if invite:
            cfg["invite"] = invite
        else:
            cfg["token"] = token if token is not None else srv.token(name)
        with open(self.cfg, "w") as f:
            json.dump(cfg, f)
        os.chmod(self.cfg, 0o600)
        self.playout = os.path.join(self.dir, "playout.raw") if playout else ""
        self.env = dict(os.environ, SYN_CAST_DEAD_AFTER_MS=str(dead_after_ms), SYN_CAST_CONFIG=self.cfg,
                        SYN_CAST_DATA=os.path.join(self.dir, "data"), SYN_CAST_SOCK=os.path.join(self.dir, "ctl.sock"),
                        SYN_CAST_NOAUDIO="1", SYN_CAST_PLAYOUT=self.playout, XDG_RUNTIME_DIR=self.dir,
                        WAYLAND_DISPLAY="")
        self.log_path = os.path.join(self.dir, "log.txt")
        self.start()

    def start(self):
        self.log = open(self.log_path, "a")
        self.proc = subprocess.Popen([BIN, "--headless"], env=self.env, stdout=self.log, stderr=self.log)

    def ctl(self, *a):
        r = subprocess.run([BIN, *a], env=self.env, capture_output=True, text=True, timeout=10)
        return r.stdout.strip(), r.returncode

    def status(self):
        out, rc = self.ctl("status")
        return json.loads(out) if rc == 0 else None

    def online(self):
        st = self.status()
        return st and any(c["active"] and c["online"] for c in st["channels"])

    def config(self):
        return json.load(open(self.cfg))

    def history(self, channel):
        path = os.path.join(self.dir, "data", channel, "index.jsonl")
        msgs = {}
        if os.path.exists(path):
            for line in open(path):
                m = json.loads(line)
                msgs[m["id"]] = m
        return list(msgs.values())

    def find(self, channel, **want):
        for m in reversed(self.history(channel)):
            if all(m.get(k) == v for k, v in want.items()):
                return m
        return None

    def voices(self, channel, **want):
        """voice records matching want; a key the record leaves out counts as False"""
        return [v for v in self.history(channel) if v["kind"] == "voice" and all(v.get(k, False) == x for k, x in want.items())]

    def played_seconds(self):
        return os.path.getsize(self.playout) / 2 / 48000 if self.playout and os.path.exists(self.playout) else 0

    def log_count(self, needle):
        return open(self.log_path).read().count(needle)

    def stop(self):
        if self.proc.poll() is None:
            self.ctl("quit")
            try:
                self.proc.wait(5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        return self.proc.returncode


def spt_packets(path):
    b = open(path, "rb").read()
    assert b[:4] == b"SPT1"
    out, off = [], 8
    while off + 2 <= len(b):
        n = int.from_bytes(b[off:off + 2], "big")
        out.append(b[off + 2: off + 2 + n])
        off += 2 + n
    return out


# ---------- a raw protocol v1 client ----------

def has_rooms_switch(srv):
    """G278: whether this server keeps each room's on/off (its hello answer carries "off"); asked once, kept"""
    if not hasattr(srv, "rooms_switch"):
        probe = Raw(srv, srv.token("dave"), device="probe", channels=[])
        srv.rooms_switch = bool(probe.hello and probe.hello.get("type") == "ok" and "off" in probe.hello)
        probe.close()
    return srv.rooms_switch


class Raw:
    def __init__(self, srv, token, device="phone", channels=None, invite=None):
        from websockets.sync.client import connect
        ctx = ssl.create_default_context(cafile=srv.cert)
        for attempt in range(3):
            try:
                self.ws = connect(srv.url(), ssl=ctx, max_size=64 << 20, open_timeout=10)
                break
            except (OSError, TimeoutError) as e:
                # seen once with the C server: an upgrade not answered within 10 s right after a session closed
                print(f"raw connect try {attempt + 1}: {e}", flush=True)
                if attempt == 2:
                    raise
                time.sleep(1)
        self.seq = 0
        hello = {"v": 1, "device": device, "client": "e2e"}
        if invite:
            hello["invite"] = invite
        else:
            hello["token"] = token
        if channels is not None:
            hello["channels"] = channels
        self.hello = self.req("hello", **hello)

    def req(self, type, **d):
        self.seq += 1
        self.ws.send(json.dumps({"type": type, "seq": self.seq, **d}))
        return self.reply(self.seq)

    def reply(self, seq, timeout=5):
        end = time.time() + timeout
        while time.time() < end:
            m = self.ws.recv(timeout=max(0.1, end - time.time()))
            if isinstance(m, str):
                d = json.loads(m)
                if d.get("seq") == seq:
                    return d
        return None

    def event(self, type, timeout=5, **match):
        end = time.time() + timeout
        while time.time() < end:
            try:
                m = self.ws.recv(timeout=max(0.1, end - time.time()))
            except Exception:
                return None
            if isinstance(m, str):
                d = json.loads(m)
                if d.get("type") == type and all(d.get(k) == v for k, v in match.items()):
                    return d
        return None

    def close_code(self, timeout=5):
        end = time.time() + timeout
        while time.time() < end:
            try:
                self.ws.recv(timeout=max(0.1, end - time.time()))
            except Exception:
                break
        return self.ws.close_code

    def close(self):
        try:
            self.ws.close()
        except Exception:
            pass


def decode(spt, out):
    subprocess.run([BIN, "decode", spt, out], capture_output=True)
    return wav_info(out)


def main():
    if not find_server():
        print("no SYN-CAST protocol v1 server found ($SYN_CASTER, syn-caster on PATH, or $CASTD_REF): skipped")
        sys.exit(77)
    root = tempfile.mkdtemp(prefix="syn-cast-e2e-")
    srv = Server(root)
    print("server:", " ".join(srv.argv))
    alice = Instance(root, "alice", srv, ["test", "second"])
    bob = Instance(root, "bob", srv, ["test", "second"], playout=True)
    try:
        check("both sign in with their member tokens over wss, the server's certificate pinned",
              wait_for(lambda: alice.online() and bob.online(), 10))
        st = bob.status()
        check("the hello answer counts both members online in the channel",
              st and st["channels"][0]["users_online"] == 2, st)
        check("every configured channel is joined at once, none NOT JOINED",
              st and [c["state"] for c in st["channels"]] == ["ONLINE", "ONLINE"], st)
        check("status names the member the server answered with", st and st["member"] == "bob", st)

        # a quiet channel is not a dead link: the server's 20 s pings keep it (dead after 22 s here)
        n_lost = lambda inst: sum(1 for m in inst.history("test") if (m.get("text") or "").startswith("connection lost"))
        lost0 = n_lost(bob)
        time.sleep(24)
        check("a quiet spell longer than the dead-link timeout keeps the session (the server's pings count)",
              n_lost(bob) == lost0 and bob.online())

        # text, then a direct message
        alice.ctl("text", "hello bob")
        m = wait_for(lambda: bob.find("test", kind="text", text="hello bob"))
        check("say arrives", m and m["from"] == "alice")
        check("sender's copy confirmed by the ok", wait_for(lambda: (alice.find("test", kind="text", text="hello bob") or {}).get("partial") is None))
        dave = Raw(srv, srv.token("dave"), channels=["test"])
        check("a raw client says hello", dave.hello and dave.hello.get("type") == "ok", dave.hello)
        r = dave.req("say", to="bob", text="just you")
        check("a direct message is accepted", r and r.get("type") == "ok", r)
        m = wait_for(lambda: bob.find("test", kind="text", text="just you"))
        check("an incoming direct message is shown in the selected channel, marked for its receiver",
              m and m.get("for") == "bob" and m.get("from") == "dave", m)
        check("and nobody else gets it", not alice.find("test", kind="text", text="just you"))
        dave.close()
        check("the channel sees dave leave", wait_for(lambda: bob.find("test", kind="event", text="dave left"), 5))
        out, rc = alice.ctl("to", "bob")
        check("there is no private send (`to` is not a command)", rc != 0, out)

        # a WAV talked in
        wav = os.path.join(root, "tone440.wav")
        tone_wav(wav, 440, 2.0)
        before = bob.played_seconds()
        out, rc = alice.ctl("say", wav)
        check("say accepted", rc == 0, out)
        m = wait_for(lambda: (lambda v: v if v and not v.get("partial") else None)(bob.find("test", kind="voice", **{"from": "alice"})), 8)
        check("voice message stored", m and abs(m.get("duration_ms", 0) - 2040) <= 120, m)
        check("played live", wait_for(lambda: bob.played_seconds() - before >= 1.9, 6), bob.played_seconds() - before)
        check("marked played", wait_for(lambda: bob.find("test", kind="voice", **{"from": "alice"}).get("played"), 4))
        if m:
            dur, hz = decode(os.path.join(bob.dir, "data", "test", f"v{m['id']}.spt"), os.path.join(root, "bob-440.wav"))
            check("decoded copy is 2 s of 440 Hz", abs(dur - 2.04) < 0.15 and abs(hz - 440) < 20, (dur, hz))
        mine = alice.find("test", kind="voice", out=True)
        check("sender keeps its own voice message", mine and mine.get("played") and mine.get("duration_ms", 0) >= 1900, mine)
        check("the server saved it whole (over answered with the message id)",
              wait_for(lambda: alice.log_count("saved by the server") >= 1, 4))
        ogg = os.path.join(srv.data, "channels", "test", "audio")
        check("as an Ogg Opus file in the channel's record", os.path.isdir(ogg) and any(f.endswith(".ogg") for f in os.listdir(ogg)))

        # push to talk from the (test tone) microphone
        alice.ctl("ptt", "down")
        check("talk state goes through connecting/sending", wait_for(lambda: alice.status()["talk"] == "sending", 3))
        time.sleep(1.5)
        alice.ctl("ptt", "up")
        check("back to idle after release", wait_for(lambda: alice.status()["talk"] in ("idle", "receiving"), 3))
        last = wait_for(lambda: (lambda v: v[-1] if len(v) >= 2 and not v[-1].get("partial") else None)(
            [v for v in bob.history("test") if v["kind"] == "voice"]), 5)
        check("PTT message arrives with its length", last and 1300 <= last.get("duration_ms", 0) <= 2000, last)
        if last:
            dur, hz = decode(os.path.join(bob.dir, "data", "test", f"v{last['id']}.spt"), os.path.join(root, "bob-ptt.wav"))
            check("PTT audio is the 600 Hz mic tone", abs(hz - 600) < 25, hz)

        # a tap shorter than a frame still opens and closes cleanly
        alice.ctl("ptt", "down")
        alice.ctl("ptt", "up")
        check("a quick tap ends cleanly", wait_for(lambda: alice.status()["talk"] in ("idle", "receiving"), 4))

        # the floor: bob holds it, alice is refused busy
        wait_for(lambda: bob.status()["talk"] == "idle", 6)
        bob.ctl("ptt", "down")
        wait_for(lambda: bob.status()["talk"] == "sending", 3)
        n_before = len(alice.voices("test", out=True))
        alice.ctl("say", wav)
        check("second talker is refused busy and returns to idle",
              wait_for(lambda: alice.status()["talk"] in ("idle", "receiving"), 4))
        time.sleep(0.3)
        check("refused talk leaves no voice message", len(alice.voices("test", out=True)) == n_before)
        check("the refusal names the talker", wait_for(lambda: alice.log_count("cannot talk: bob is talking") >= 1, 2))
        bob.ctl("ptt", "up")
        wait_for(lambda: bob.status()["talk"] == "idle", 4)

        # a picture (a PPM goes through the JPEG encoder; one binary frame on the wire)
        ppm = os.path.join(root, "pic.ppm")
        with open(ppm, "wb") as f:
            f.write(b"P6 640 400 255\n" + bytes([200, 20, 20]) * 640 * 400)
        out, rc = alice.ctl("image", ppm)
        check("image accepted", rc == 0, out)
        m = wait_for(lambda: (lambda i: i if i and not i.get("partial") else None)(bob.find("test", kind="image")), 8)
        check("picture arrives complete", m and m.get("w") == 640 and m.get("h") == 400, m)
        if m:
            full = open(os.path.join(bob.dir, "data", "test", f"i{m['id']}.jpg"), "rb").read()
            thumb = os.path.join(bob.dir, "data", "test", f"i{m['id']}.t.jpg")
            check("stored as a JPEG with a thumbnail cut from it",
                  full[:2] == b"\xff\xd8" and os.path.exists(thumb) and 0 < os.path.getsize(thumb) < len(full), len(full))
        pics = os.path.join(srv.data, "channels", "test", "pics")
        check("the server kept the picture", os.path.isdir(pics) and os.listdir(pics))

        # a location
        alice.ctl("loc", "51.5", "-0.12", "London")
        m = wait_for(lambda: bob.find("test", kind="location"))
        check("location arrives with its address", m and abs(m["lat"] - 51.5) < 1e-6 and m.get("text") == "London", m)

        # replay, and replay speed (of a full message: alice says the tone again)
        wait_for(lambda: bob.status()["queue"] == 0 and bob.status()["talk"] == "idle", 8)
        n0 = len(bob.voices("test", out=False))
        alice.ctl("say", wav)
        wait_for(lambda: len([v for v in bob.voices("test", out=False) if not v.get("partial")]) > n0, 8)
        wait_for(lambda: bob.status()["queue"] == 0 and bob.status()["talk"] == "idle", 8)
        p0 = bob.played_seconds()
        bob.ctl("replay")
        wait_for(lambda: bob.played_seconds() > p0, 4)
        wait_for(lambda: bob.status()["queue"] == 0 and bob.status()["talk"] == "idle", 8)
        one = bob.played_seconds() - p0
        bob.ctl("speed")
        bob.ctl("speed")
        p1 = bob.played_seconds()
        bob.ctl("replay")
        wait_for(lambda: bob.played_seconds() > p1, 4)
        wait_for(lambda: bob.status()["queue"] == 0 and bob.status()["talk"] == "idle", 8)
        fast = bob.played_seconds() - p1
        check("replay plays the last message", 1.3 < one < 2.1, one)
        check("1.5x replay takes two thirds of the time", 0.6 < fast / one < 0.73, (one, fast))
        bob.ctl("speed")

        # G278: a room switched off keeps every note unplayed, saved on the server so a restart keeps it; Replay or
        # switching it on plays what was missed, oldest first, each once. (Busy and Mute are gone.)
        if has_rooms_switch(srv):
            chan = lambda: next(c for c in bob.status()["channels"] if c["name"] == "test")
            settle = lambda: wait_for(lambda: bob.status()["queue"] == 0 and bob.status()["talk"] == "idle", 8)
            bob.ctl("channel", "test")
            bob.ctl("room")
            check("room off: the status says off", wait_for(lambda: chan()["off"], 3), chan())
            rooms = os.path.join(srv.data, "members", "bob", "rooms.json")
            check("and the server keeps it for bob",
                  wait_for(lambda: os.path.exists(rooms) and '"test"' in open(rooms).read(), 3))
            p2 = bob.played_seconds()
            for _ in range(2):
                n0 = len([v for v in bob.voices("test", out=False) if not v.get("partial")])
                alice.ctl("say", wav)
                wait_for(lambda: len([v for v in bob.voices("test", out=False) if not v.get("partial")]) > n0, 8)
                wait_for(lambda: alice.status()["talk"] == "idle", 5)
            time.sleep(0.5)
            kept = [v for v in bob.history("test") if v["kind"] == "voice" and not v.get("out")][-2:]
            check("an off room keeps both messages unplayed", len(kept) == 2 and not any(v.get("played") for v in kept)
                  and bob.played_seconds() - p2 < 0.1, kept)
            bob.stop()
            bob.start()
            check("after a restart the room is still off (the server's switch)",
                  wait_for(lambda: bob.status() and bob.online() and chan()["off"], 10), bob.status())
            p3 = bob.played_seconds()
            bob.ctl("replay")
            bob.ctl("replay")
            time.sleep(0.3)
            settle()
            check("Replay tapped twice plays the two missed once each, oldest first",
                  3.7 < bob.played_seconds() - p3 < 4.4, bob.played_seconds() - p3)
            check("and marks them heard", all(v.get("played") for v in
                  [v for v in bob.history("test") if v["kind"] == "voice" and not v.get("out")][-2:]))
            n0 = len([v for v in bob.voices("test", out=False) if not v.get("partial")])
            alice.ctl("say", wav)
            wait_for(lambda: len([v for v in bob.voices("test", out=False) if not v.get("partial")]) > n0, 8)
            time.sleep(0.5)
            p4 = bob.played_seconds()
            bob.ctl("room")
            check("room on: the status says on", wait_for(lambda: not chan()["off"], 3), chan())
            check("switching on plays the one missed", wait_for(lambda: bob.played_seconds() - p4 >= 1.9, 8))
            settle()
            p5 = bob.played_seconds()
            alice.ctl("say", wav)
            check("and live voice plays aloud again", wait_for(lambda: bob.played_seconds() - p5 >= 1.9, 8))
            settle()
        else:
            print("rooms on/off: needs the SYN-CASTER server with the room request (G278): skipped")

        # another session of the same member and device replaces this one (4003); it takes the place back once
        other = Raw(srv, srv.token("bob"), device="window", channels=["test"])
        check("a second hello as bob/window is accepted", other.hello and other.hello.get("type") == "ok", other.hello)
        check("and replaces the running instance", wait_for(lambda: not bob.online(), 6))
        check("which takes its place straight back", wait_for(bob.online, 10))
        check("closing the other with code 4003", other.close_code() == 4003, other.close_code(1))
        other.close()

        # offline / online
        bob.ctl("offline")
        check("OFFLINE closes the session", wait_for(lambda: bob.status()["channels"][0]["state"] == "OFFLINE", 5))
        check("and the channel sees bob leave", wait_for(lambda: alice.status()["channels"][0]["users_online"] == 1, 5))
        bob.ctl("online")
        check("ONLINE brings it back", wait_for(bob.online, 8))
        check("and the channel counts two again", wait_for(lambda: alice.status()["channels"][0]["users_online"] == 2, 5))

        # the invite flow: frank has only a one-time code; the token it earns is kept and used next time
        code = srv.invite("frank")
        frank = Instance(root, "frank", srv, [], invite=code)
        check("an invite code signs frank in", wait_for(frank.online, 10), frank.status())
        cfg = wait_for(lambda: (lambda c: c if c.get("token") and not c.get("invite") else None)(frank.config()), 4)
        check("the token it earned is written to the config and the invite removed", bool(cfg), frank.config())
        check("the member id is written too", cfg and cfg.get("member") == "frank", cfg)
        st = frank.status()
        check("with no channels configured, hello joined every channel frank belongs to",
              st and sorted(c["name"] for c in st["channels"]) == ["second", "test"], st)
        frank.stop()
        frank.start()
        check("frank restarts on the kept token", wait_for(frank.online, 10), frank.status())
        frank.stop()
        used = Raw(srv, None, invite=code)
        check("the invite code works once only", used.hello and used.hello.get("code") == "unauthorized", used.hello)
        used.close()

        # a refused token is tried once, then shown as REFUSED
        eve = Instance(root, "eve", srv, ["test"], token="sct1.bad.token")
        check("a refused sign-in shows REFUSED", wait_for(lambda: eve.status()["channels"][0]["state"] == "REFUSED", 10), eve.status())
        time.sleep(4)
        tries = eve.log_count("sign-in refused")
        check("and is tried exactly once", tries == 1, tries)
        eve.stop()

        # a revoked token closes the session (4004) and is not retried
        srv.cli("revoke", "member:alice")
        check("a revoked member's session is closed and shown REFUSED",
              wait_for(lambda: alice.status()["channels"][0]["state"] == "REFUSED", 10), alice.status())
    finally:
        rc = [alice.stop(), bob.stop()]
        srv.stop()
    check("clean exits", rc == [0, 0], rc)
    if failures:
        print(f"\n{len(failures)} failed; logs in {root}")
        sys.exit(1)
    shutil.rmtree(root, ignore_errors=True)
    print("\nall passed")


if __name__ == "__main__":
    main()
