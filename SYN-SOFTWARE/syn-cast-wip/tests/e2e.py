#!/usr/bin/env python3
"""End to end: two headless syn-cast instances (alice, bob) and a raw client
(carol) on the stand-in server, everything isolated in a temp dir. No audio
devices, no syn-bar-core, no live Zello.

usage: tests/e2e.py path/to/syn-cast
"""
import json
import math
import os
import shutil
import signal
import socket
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


def frame_purity(path, hz=440, frame_ms=60):
    """Share of each frame's energy at hz (Goertzel), frame by frame."""
    with wave.open(path) as w:
        n, rate = w.getnframes(), w.getframerate()
        s = struct.unpack(f"<{n}h", w.readframes(n))
    fr = rate * frame_ms // 1000
    out = []
    c = 2 * math.cos(2 * math.pi * hz / rate)
    for i in range(0, n - fr + 1, fr):
        seg = s[i:i + fr]
        s1 = s2 = 0.0
        for x in seg:
            s1, s2 = x + c * s1 - s2, s1
        power = (s1 * s1 + s2 * s2 - c * s1 * s2) / fr / (fr / 2)
        energy = sum(x * x for x in seg) / fr
        out.append(power / (energy + 1e-9))
    return out


def pitch(samples, rate):
    mid = samples[len(samples) // 10: len(samples) * 9 // 10]
    if len(mid) < 2:
        return 0
    z = sum(1 for a, b in zip(mid, mid[1:]) if (a < 0) != (b < 0))
    return z / 2 / (len(mid) / rate)


class Instance:
    def __init__(self, root, name, port, key, channels, playout=False, password=None, user=None):
        self.name = name
        self.dir = os.path.join(root, name)
        os.makedirs(self.dir)
        self.cfg = os.path.join(self.dir, "config.json")
        with open(self.cfg, "w") as f:
            json.dump({"server": f"ws://127.0.0.1:{port}", "username": user or name,
                       "password": password or "pw-" + (user or name),
                       "issuer": "TESTISSUER", "key": key, "channels": channels, "tones": False,
                       "notify": False, "noise_suppression": False, "history_days": 0, "history_mb": 0}, f)
        os.chmod(self.cfg, 0o600)
        self.playout = os.path.join(self.dir, "playout.raw") if playout else ""
        self.env = dict(os.environ, SYN_CAST_DEAD_AFTER_MS="4000", SYN_CAST_CONFIG=self.cfg, SYN_CAST_DATA=os.path.join(self.dir, "data"),
                        SYN_CAST_SOCK=os.path.join(self.dir, "ctl.sock"), SYN_CAST_NOAUDIO="1",
                        SYN_CAST_PLAYOUT=self.playout, XDG_RUNTIME_DIR=self.dir, WAYLAND_DISPLAY="")
        self.log = open(os.path.join(self.dir, "log.txt"), "w")
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

    def played_seconds(self):
        return os.path.getsize(self.playout) / 2 / 48000 if self.playout and os.path.exists(self.playout) else 0

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


def raw_client(port, user, key_token, channel="second"):
    from websockets.sync.client import connect
    ws = connect(f"ws://127.0.0.1:{port}", max_size=64 << 20)
    ws.send(json.dumps({"command": "logon", "seq": 1, "auth_token": key_token, "username": user,
                        "password": "pw-" + user, "channels": [channel]}))
    return ws


def recv_reply(ws, seq, timeout=5):
    end = time.time() + timeout
    while time.time() < end:
        m = ws.recv(timeout=max(0.1, end - time.time()))
        if isinstance(m, str):
            d = json.loads(m)
            if d.get("seq") == seq:
                return d
    return None


def mint(key):
    import base64
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import padding
    pk = serialization.load_pem_private_key(open(key, "rb").read(), None)
    enc = lambda b: base64.urlsafe_b64encode(b).rstrip(b"=").decode()
    head = enc(b'{"alg":"RS256","typ":"JWT"}')
    body = enc(json.dumps({"iss": "TESTISSUER", "exp": int(time.time()) + 60}).encode())
    sig = pk.sign(f"{head}.{body}".encode(), padding.PKCS1v15(), hashes.SHA256())
    return f"{head}.{body}.{enc(sig)}"


def main():
    root = tempfile.mkdtemp(prefix="syn-cast-e2e-")
    port = free_port()
    key = os.path.join(root, "key.pem")
    pub = os.path.join(root, "pub.pem")
    subprocess.run(["openssl", "genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:2048", "-out", key],
                   check=True, capture_output=True)
    subprocess.run(["openssl", "pkey", "-in", key, "-pubout", "-out", pub], check=True, capture_output=True)
    server_log = os.path.join(root, "standin.log")

    def start_server():
        return subprocess.Popen([sys.executable, "-I", os.path.join(HERE, "standin.py"), "--port", str(port),
                                 "--pubkey", pub, "--log", server_log, "--ping", "1"])

    server = start_server()
    time.sleep(0.6)
    alice = Instance(root, "alice", port, key, ["test", "second"])
    bob = Instance(root, "bob", port, key, ["test", "second"], playout=True)
    carol = None
    try:
        check("both sign in to the selected channel (RS256 token verified)",
              wait_for(lambda: alice.online() and bob.online(), 10))
        st = bob.status()
        check("channel status counts both users", st and st["channels"][0]["users_online"] == 2, st)

        # a quiet channel is not a dead link: the server's pings keep it (dead after 4 s here, pings every 1 s)
        n_lost = lambda inst: sum(1 for m in inst.history("test") if (m.get("text") or "").startswith("connection lost"))
        lost0 = n_lost(bob)
        time.sleep(9)
        check("a quiet spell longer than the dead-link timeout keeps the session", n_lost(bob) == lost0 and bob.online())

        # text, then private text
        alice.ctl("text", "hello bob")
        m = wait_for(lambda: bob.find("test", kind="text", text="hello bob"))
        check("text arrives", m and m["from"] == "alice")
        check("sender's copy confirmed", wait_for(lambda: (alice.find("test", kind="text", text="hello bob") or {}).get("partial") is None))
        # SYN-CAST sends to the whole channel only; private messages that arrive are shown
        out, rc = alice.ctl("to", "bob")
        check("there is no private send (`to` is not a command)", rc != 0, out)
        dave = raw_client(port, "dave", mint(key), channel="test")
        recv_reply(dave, 1)
        dave.send(json.dumps({"command": "send_text_message", "seq": 2, "channel": "test", "for": "bob",
                              "text": "just you"}))
        m = wait_for(lambda: bob.find("test", kind="text", text="just you"))
        check("an incoming private text is shown, marked for its recipient", m and m.get("for") == "bob", m)
        check("and nobody else gets it", not alice.find("test", kind="text", text="just you"))
        dave.close()
        wait_for(lambda: bob.status()["channels"][0]["users_online"] == 2, 5)

        # a WAV talked in
        wav = os.path.join(root, "tone440.wav")
        tone_wav(wav, 440, 2.0)
        before = bob.played_seconds()
        out, rc = alice.ctl("say", wav)
        check("say accepted", rc == 0, out)
        m = wait_for(lambda: bob.find("test", kind="voice", **{"from": "alice"}) and
                     not bob.find("test", kind="voice", **{"from": "alice"}).get("partial") and
                     bob.find("test", kind="voice", **{"from": "alice"}), 8)
        check("voice message stored", m and abs(m.get("duration_ms", 0) - 2040) <= 120, m)
        check("played live", wait_for(lambda: bob.played_seconds() - before >= 1.9, 6), bob.played_seconds() - before)
        check("marked played", wait_for(lambda: bob.find("test", kind="voice", **{"from": "alice"}).get("played"), 4))
        if m:
            spt = os.path.join(bob.dir, "data", "test", f"v{m['id']}.spt")
            dec = os.path.join(root, "bob-440.wav")
            subprocess.run([BIN, "decode", spt, dec], capture_output=True)
            dur, hz = wav_info(dec)
            check("decoded copy is 2 s of 440 Hz", abs(dur - 2.04) < 0.15 and abs(hz - 440) < 20, (dur, hz))
        mine = alice.find("test", kind="voice", out=True)
        check("sender keeps its own voice message", mine and mine.get("played") and mine.get("duration_ms", 0) >= 1900, mine)

        # push to talk from the (test tone) microphone
        alice.ctl("ptt", "down")
        check("talk state goes through connecting/sending",
              wait_for(lambda: alice.status()["talk"] == "sending", 3))
        time.sleep(1.5)
        alice.ctl("ptt", "up")
        check("back to idle after release", wait_for(lambda: alice.status()["talk"] in ("idle", "receiving"), 3))
        vs = [v for v in bob.history("test") if v["kind"] == "voice"]
        last = wait_for(lambda: [v for v in bob.history("test") if v["kind"] == "voice" and not v.get("partial")][-1]
                        if len([v for v in bob.history("test") if v["kind"] == "voice" and not v.get("partial")]) >= 2 else None, 5)
        check("PTT message arrives with its length", last and 1300 <= last.get("duration_ms", 0) <= 2000, last)
        if last:
            dec = os.path.join(root, "bob-ptt.wav")
            subprocess.run([BIN, "decode", os.path.join(bob.dir, "data", "test", f"v{last['id']}.spt"), dec],
                           capture_output=True)
            dur, hz = wav_info(dec)
            check("PTT audio is the 600 Hz mic tone", abs(hz - 600) < 25, hz)

        # a tap shorter than a frame still opens and closes cleanly
        alice.ctl("ptt", "down")
        alice.ctl("ptt", "up")
        check("a quick tap ends cleanly", wait_for(lambda: alice.status()["talk"] in ("idle", "receiving"), 4))

        # busy channel: bob holds it, alice is refused
        wait_for(lambda: bob.status()["talk"] == "idle", 6)
        bob.ctl("ptt", "down")
        wait_for(lambda: bob.status()["talk"] == "sending", 3)
        n_before = len([v for v in alice.history("test") if v["kind"] == "voice" and v.get("out")])
        alice.ctl("say", wav)
        check("second talker is refused and returns to idle",
              wait_for(lambda: alice.status()["talk"] in ("idle", "receiving"), 4))
        time.sleep(0.3)
        n_after = len([v for v in alice.history("test") if v["kind"] == "voice" and v.get("out")])
        check("refused talk leaves no voice message", n_after == n_before, (n_before, n_after))
        bob.ctl("ptt", "up")
        wait_for(lambda: bob.status()["talk"] == "idle", 4)

        # image (a PPM goes through the JPEG encoder)
        ppm = os.path.join(root, "pic.ppm")
        with open(ppm, "wb") as f:
            w, h = 320, 200
            f.write(f"P6 {w} {h} 255\n".encode())
            f.write(bytes((x % 256, y % 256, (x + y) % 256)[c] for y in range(h) for x in range(w) for c in range(3)))
        out, rc = alice.ctl("image", ppm)
        check("image accepted", rc == 0, out)
        m = wait_for(lambda: (lambda i: i if i and not i.get("partial") else None)(bob.find("test", kind="image")), 6)
        check("image arrives complete", m and m.get("w") == 320 and m.get("h") == 200, m)
        if m:
            full = open(os.path.join(bob.dir, "data", "test", f"i{m['id']}.jpg"), "rb").read()
            thumb = open(os.path.join(bob.dir, "data", "test", f"i{m['id']}.t.jpg"), "rb").read()
            check("full image and thumbnail are JPEGs", full[:2] == b"\xff\xd8" and thumb[:2] == b"\xff\xd8")

        # location
        alice.ctl("loc", "51.5074", "-0.1278", "Trafalgar", "Square")
        m = wait_for(lambda: bob.find("test", kind="location"))
        check("location arrives", m and abs(m["lat"] - 51.5074) < 1e-6 and m.get("text") == "Trafalgar Square", m)

        # loss: the server drops every 5th packet; lost packets are marked and concealed
        carol_ws = raw_client(port, "carol", mint(key))
        recv_reply(carol_ws, 1)
        carol_ws.send(json.dumps({"command": "standin_drop", "seq": 2, "every": 5}))
        recv_reply(carol_ws, 2)
        wait_for(lambda: alice.status()["talk"] == "idle" and bob.status()["queue"] == 0, 8)
        n0 = len([v for v in bob.history("test") if v["kind"] == "voice"])
        alice.ctl("say", wav)
        m = wait_for(lambda: (lambda v: v[-1] if len(v) > n0 and not v[-1].get("partial") else None)(
            [v for v in bob.history("test") if v["kind"] == "voice"]), 8)
        if m:
            pk = spt_packets(os.path.join(bob.dir, "data", "test", f"v{m['id']}.spt"))
            lost = sum(1 for p in pk if not p)
            check("lost packets are marked in the file", 4 <= lost <= 9, lost)
            dec = os.path.join(root, "bob-lossy.wav")
            subprocess.run([BIN, "decode", os.path.join(bob.dir, "data", "test", f"v{m['id']}.spt"), dec],
                           capture_output=True)
            dur, _ = wav_info(dec)
            check("loss is concealed: the message keeps its full length", abs(dur - 2.04) < 0.15, dur)
            purity = frame_purity(dec)
            at_loss = [purity[i] for i, p in enumerate(pk) if not p and i < len(purity)]
            good = sum(1 for v in at_loss if v > 0.85)
            check("most lost frames come back as the tone (FEC/PLC)", at_loss and good / len(at_loss) >= 0.5,
                  [round(v, 2) for v in at_loss])
            clean = sum(1 for v in purity[1:-1] if v > 0.85) / max(1, len(purity) - 2)
            check("and the message as a whole stays clean", clean >= 0.8, round(clean, 2))
        else:
            check("lossy message arrives", False)
        carol_ws.send(json.dumps({"command": "standin_drop", "seq": 3, "every": 0}))
        recv_reply(carol_ws, 3)

        # a live message arriving during a replay waits its turn, then both play in full
        wait_for(lambda: alice.status()["talk"] == "idle" and bob.status()["queue"] == 0, 8)
        played0 = bob.played_seconds()
        bob.ctl("replay")
        wait_for(lambda: bob.status()["queue"] >= 1 or bob.status()["on_air"], 3)
        alice.ctl("say", wav)
        check("the live message waits behind the replay", wait_for(lambda: bob.status()["queue"] >= 1, 4), bob.status())
        check("both play in full, one after the other",
              wait_for(lambda: bob.played_seconds() - played0 >= 3.9, 12), bob.played_seconds() - played0)
        carol_ws.close()

        # replay at 1x and 1.5x
        wait_for(lambda: bob.status()["queue"] == 0, 6)
        p0 = bob.played_seconds()
        bob.ctl("channel", "test")
        bob.ctl("replay")
        wait_for(lambda: bob.status()["queue"] == 0 and bob.played_seconds() > p0, 8)
        wait_for(lambda: bob.status()["queue"] == 0, 8)
        one = bob.played_seconds() - p0
        bob.ctl("speed")
        bob.ctl("speed")
        p1 = bob.played_seconds()
        bob.ctl("replay")
        wait_for(lambda: bob.played_seconds() > p1, 4)
        wait_for(lambda: bob.status()["queue"] == 0, 8)
        fast = bob.played_seconds() - p1
        check("replay plays the last message", 1.8 < one < 2.3, one)
        check("1.5x replay takes two thirds of the time", 0.6 < fast / one < 0.73, (one, fast))

        # busy: incoming voice is kept, not played
        bob.ctl("busy")
        p2 = bob.played_seconds()
        n0 = len([v for v in bob.history("test") if v["kind"] == "voice"])
        alice.ctl("say", wav)
        wait_for(lambda: len([v for v in bob.history("test") if v["kind"] == "voice" and not v.get("partial")]) > n0, 8)
        time.sleep(0.5)
        last = [v for v in bob.history("test") if v["kind"] == "voice"][-1]
        check("Busy keeps the message unplayed", not last.get("played") and bob.played_seconds() - p2 < 0.1,
              (last, bob.played_seconds() - p2))
        check("and counts it unread", bob.status()["channels"][0]["unread"] >= 1, bob.status())
        bob.ctl("busy")

        # dropped by the server: both come straight back on their refresh tokens
        kicker = raw_client(port, "dave", mint(key))
        recv_reply(kicker, 1)
        kicker.send(json.dumps({"command": "standin_kick", "seq": 2}))
        time.sleep(0.5)
        check("a kick is noticed", wait_for(lambda: not bob.online(), 8))
        check("both reconnect after a kick", wait_for(lambda: alice.online() and bob.online(), 15))
        check("the reconnect used refresh tokens", open(server_log).read().count("refresh ok") >= 2)

        # the server restarts (its refresh tokens forgotten): fresh signed tokens
        server.send_signal(signal.SIGTERM)
        server.wait(5)
        check("going offline is noticed", wait_for(lambda: not bob.online(), 8))
        server = start_server()
        check("both reconnect after a restart", wait_for(lambda: alice.online() and bob.online(), 20))
        log = open(server_log).read()
        check("a refused refresh token falls back to a signed token",
              "refresh refused" in log and log.rindex("auth_token ok") > log.rindex("refresh refused"))
        alice.ctl("text", "back again")
        check("and work after it", wait_for(lambda: bob.find("test", kind="text", text="back again")))

        # Friends & Family: one channel at a time; selecting another moves the session there
        fred = Instance(root, "fred", port, key, ["test", "second"])
        joined = lambda: [c["state"] for c in fred.status()["channels"]]
        check("F&F joins only the selected channel", wait_for(lambda: joined() == ["ONLINE", "NOT JOINED"], 8), joined())
        alice.ctl("channel", "second")
        wait_for(alice.online, 8)
        alice.ctl("text", "in second")
        alice.ctl("channel", "test")
        wait_for(alice.online, 8)
        time.sleep(1)
        check("a channel it hasn't joined doesn't reach it", not fred.find("second", kind="text", text="in second"))
        fred.ctl("channel", "second")
        check("selecting another channel moves the session there",
              wait_for(lambda: joined() == ["NOT JOINED", "ONLINE"], 8), joined())
        alice.ctl("channel", "second")
        wait_for(alice.online, 8)
        alice.ctl("text", "second again")
        alice.ctl("channel", "test")
        wait_for(alice.online, 8)
        check("and that channel's messages arrive", wait_for(lambda: fred.find("second", kind="text", text="second again"), 5))
        fred.stop()

        # one session per account. The Zello app signing in kicks SYN-CAST, which takes it straight back
        # (the app doesn't fight back)
        app = raw_client(port, "bob", mint(key), channel="test")
        recv_reply(app, 1)
        check("the other client's sign-in kicks bob off", wait_for(lambda: not bob.online(), 5))
        check("and bob takes the account straight back", wait_for(lambda: bob.online(), 8))
        try:
            while True:
                app.recv(timeout=3)
        except Exception as e:
            code = getattr(getattr(e, "rcvd", None), "code", None)
        check("the other client is the one left out (closed with 3003)", code == 3003, code)

        # OFFLINE frees the account; ONLINE takes it again
        bob.ctl("offline")
        check("OFFLINE closes the session", wait_for(lambda: bob.status()["channels"][0]["state"] == "OFFLINE", 5))
        check("and the channel sees bob leave", wait_for(lambda: alice.status()["channels"][0]["users_online"] == 1, 5))
        bob.ctl("online")
        check("ONLINE brings bob back", wait_for(bob.online, 8))

        # two SYN-CASTs on one account: one keeps it, the other goes offline; no endless fight
        before = open(server_log).read().count("logon bob")
        twin = Instance(root, "bobtwin", port, key, ["test", "second"], user="bob")
        time.sleep(10)
        states = [bob.status()["channels"][0]["state"], twin.status()["channels"][0]["state"]]
        check("two copies on one account settle: one online, one gone offline",
              sorted(states) == ["OFFLINE", "ONLINE"], states)
        tries = open(server_log).read().count("logon bob") - before
        check("in a handful of sign-ins, not a loop", tries <= 8, tries)
        twin.stop()
        if bob.status()["channels"][0]["state"] != "ONLINE":
            bob.ctl("online")
            check("ONLINE takes the account back", wait_for(bob.online, 8), bob.status())

        # a wrong password: one attempt, then it stops (retrying only risks a lockout)
        mallory = Instance(root, "mallory", port, key, ["test"], password="wrong")
        check("a refused sign-in shows REFUSED",
              wait_for(lambda: mallory.status()["channels"][0]["state"] == "REFUSED", 8), mallory.status())
        time.sleep(4)
        tries = open(server_log).read().count("logon mallory")
        check("and is tried exactly once", tries == 1, tries)
        mallory.stop()

        # a second instance refuses to start
        r = subprocess.run([BIN, "--headless"], env=alice.env, capture_output=True, text=True, timeout=5)
        check("one instance per socket", r.returncode == 1 and "already running" in r.stderr, r.stderr)
    finally:
        rcs = [alice.stop(), bob.stop()]
        server.kill()
    check("clean exits", rcs == [0, 0], rcs)
    for inst in (alice, bob):
        errs = [l for l in open(os.path.join(inst.dir, "log.txt")) if "Traceback" in l or "Assertion" in l]
        check(f"{inst.name}: no crashes in the log", not errs, errs)
    if failures:
        print(f"\n{len(failures)} failed; logs in {root}")
        sys.exit(1)
    shutil.rmtree(root, ignore_errors=True)
    print("\nall passed")


if __name__ == "__main__":
    main()
