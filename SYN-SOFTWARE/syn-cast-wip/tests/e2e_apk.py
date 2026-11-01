#!/usr/bin/env python3
"""SYN-CAST-APK's engine against desktop SYN-CAST, through the stand-in.

The Android app's core (apk/java/.../core, with libopus through the host
build of its JNI glue) runs on this JVM as "jim"; the desktop binary runs
headless as "alice". They talk, text, send images and locations both ways,
and each one's stored voice files are decoded by the other's code.

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
import e2e  # noqa: E402  (helpers: Instance, check, wait_for, tone_wav, wav_info, ...)
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
    subprocess.run(["javac", "-nowarn", "-d", os.path.join(HOST, "classes"), *srcs], check=True,
                   stderr=subprocess.DEVNULL)


class Jim:
    """The APK engine in a JVM, one command per line."""

    def __init__(self, root, port, key, channels, name="jim", password=None):
        self.dir = os.path.join(root, name)
        os.makedirs(self.dir)
        cfg = os.path.join(self.dir, "config.json")
        with open(cfg, "w") as f:
            json.dump({"server": f"ws://127.0.0.1:{port}", "username": name,
                       "password": password or "pw-" + name,
                       "issuer": "TESTISSUER", "key_pem": open(key).read(), "channels": channels,
                       "tones": False}, f)
        self.playout = os.path.join(self.dir, "playout.raw")
        self.log = open(os.path.join(self.dir, "log.txt"), "w")
        self.proc = subprocess.Popen(
            ["java", "--enable-native-access=ALL-UNNAMED", f"-Dsyncast.jni={os.path.join(HOST, 'libsyncast-host.so')}",
             "-Xmx256m", "-Dsyncast.deadAfterMs=4000", "-cp", os.path.join(HOST, "classes"), "com.syntax990.syncast.test.HeadlessClient", cfg,
             os.path.join(self.dir, "data"), self.playout],
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

    def history(self, k=0):
        return json.loads(self.cmd(f"history {k}"))

    def find(self, k=0, **want):
        for m in reversed(self.history(k)):
            if all(m.get(a) == b for a, b in want.items()):
                return m
        return None

    def played_seconds(self):
        return os.path.getsize(self.playout) / 2 / 16000 if os.path.exists(self.playout) else 0

    def spt(self, channel, msg_id):
        return os.path.join(self.dir, "data", channel, f"v{msg_id}.spt")

    def stop(self):
        if self.proc.poll() is None:
            try:
                self.cmd("quit")
                self.proc.wait(5)
            except Exception:
                self.proc.kill()
        return self.proc.returncode


def decode(spt, out):
    subprocess.run([BIN, "decode", spt, out], capture_output=True)
    return e2e.wav_info(out)


def main():
    build_core()
    gain = subprocess.run(["java", "-Xmx64m", "-cp", os.path.join(HOST, "classes"), "com.syntax990.syncast.test.GainTest"],
                          capture_output=True, text=True)
    for line in gain.stdout.splitlines():
        if line.startswith(("ok ", "FAIL ")):
            check("gain: " + line[5:].split("  (")[0], line.startswith("ok "), line)
    check("gain: the test ran", gain.returncode in (0, 1) and ("passed" in gain.stdout or "failed" in gain.stdout),
          gain.stderr)
    root = tempfile.mkdtemp(prefix="syn-cast-apk-e2e-")
    port = e2e.free_port()
    key, pub = os.path.join(root, "key.pem"), os.path.join(root, "pub.pem")
    subprocess.run(["openssl", "genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:2048", "-out", key],
                   check=True, capture_output=True)
    subprocess.run(["openssl", "pkey", "-in", key, "-pubout", "-out", pub], check=True, capture_output=True)
    log = os.path.join(root, "standin.log")
    server = subprocess.Popen([sys.executable, "-I", os.path.join(HERE, "standin.py"), "--port", str(port),
                               "--pubkey", pub, "--log", log, "--ping", "1"])
    time.sleep(0.6)
    alice = e2e.Instance(root, "alice", port, key, ["test", "second"], playout=True)
    jim = Jim(root, port, key, ["test", "second"])
    try:
        check("the APK engine signs in to the selected channel (its RS256 token verified)",
              wait_for(lambda: jim.online() and alice.online(), 15))

        time.sleep(9)
        lost = sum(1 for m in jim.history() if (m.get("text") or "").startswith("connection lost"))
        check("a quiet spell longer than the APK's dead-link timeout keeps its session", lost == 0 and jim.online())

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

        jim.cmd("ptt down")
        jim.cmd("ptt up")
        check("a quick tap on the APK ends cleanly", wait_for(lambda: jim.status()["talk"] in ("idle", "receiving"), 4))

        wait_for(lambda: alice.status()["talk"] == "idle", 6)
        alice.ctl("ptt", "down")
        wait_for(lambda: alice.status()["talk"] == "sending", 3)
        n0 = len([v for v in jim.history() if v["kind"] == "voice" and v["out"]])
        jim.cmd("ptt down")
        check("the APK is refused while the desktop holds the channel",
              wait_for(lambda: jim.status()["talk"] in ("idle", "receiving"), 4))
        jim.cmd("ptt up")
        time.sleep(0.3)
        check("and keeps no voice message for it",
              len([v for v in jim.history() if v["kind"] == "voice" and v["out"]]) == n0)
        alice.ctl("ptt", "up")
        wait_for(lambda: alice.status()["talk"] == "idle", 4)

        from PIL import Image
        jpg = os.path.join(root, "pic.jpg")
        Image.new("RGB", (640, 400), (200, 20, 20)).save(jpg)
        jim.cmd(f"image {jpg}")
        m = wait_for(lambda: (lambda i: i if i and not i.get("partial") else None)(alice.find("test", kind="image")), 6)
        check("APK image reaches the desktop complete", m and m.get("w") == 640 and m.get("h") == 400, m)
        alice.ctl("image", jpg)
        m = wait_for(lambda: (lambda i: i if i and not i["partial"] else None)(jim.find(kind="image", **{"from": "alice"})), 6)
        check("desktop image reaches the APK", m)
        if m:
            data = open(os.path.join(jim.dir, "data", "test", f"i{m['id']}.jpg"), "rb").read()
            check("stored as a JPEG", data[:2] == b"\xff\xd8")

        jim.cmd("loc 48.8584 2.2945 Eiffel Tower")
        m = wait_for(lambda: alice.find("test", kind="location"))
        check("APK location reaches the desktop", m and abs(m["lat"] - 48.8584) < 1e-6 and m.get("text") == "Eiffel Tower", m)
        alice.ctl("loc", "51.5", "-0.12", "London")
        check("desktop location reaches the APK", wait_for(lambda: jim.find(kind="location", text="London")))

        dave = e2e.raw_client(port, "dave", e2e.mint(key), channel="test")
        e2e.recv_reply(dave, 1)
        dave.send(json.dumps({"command": "send_text_message", "seq": 2, "channel": "test", "for": "jim",
                              "text": "just for jim"}))
        m = wait_for(lambda: jim.find(kind="text", text="just for jim"))
        check("an incoming private text is shown on the APK, marked for it", m and m["for"] == "jim", m)
        check("and the desktop doesn't get it", not alice.find("test", kind="text", text="just for jim"))
        dave.close()

        ws = e2e.raw_client(port, "carol", e2e.mint(key))
        e2e.recv_reply(ws, 1)
        ws.send(json.dumps({"command": "standin_drop", "seq": 2, "every": 5}))
        e2e.recv_reply(ws, 2)
        wait_for(lambda: jim.status()["queue"] == 0 and jim.status()["talk"] == "idle", 8)
        n0 = len([v for v in jim.history() if v["kind"] == "voice" and not v["out"]])
        alice.ctl("say", wav)
        m = wait_for(lambda: (lambda v: v[-1] if len(v) > n0 and not v[-1]["partial"] else None)(
            [v for v in jim.history() if v["kind"] == "voice" and not v["out"]]), 8)
        if m:
            lost = sum(1 for p in e2e.spt_packets(jim.spt("test", m["id"])) if not p)
            check("the APK marks lost packets", 4 <= lost <= 9, lost)
            dur, _ = decode(jim.spt("test", m["id"]), os.path.join(root, "jim-lossy.wav"))
            check("and the message keeps its length", abs(dur - 2.04) < 0.15, dur)
        else:
            check("lossy message reaches the APK", False)
        ws.send(json.dumps({"command": "standin_drop", "seq": 3, "every": 0}))
        e2e.recv_reply(ws, 3)

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

        jim.cmd("busy")
        p2 = jim.played_seconds()
        n0 = len([v for v in jim.history() if v["kind"] == "voice" and not v["out"]])
        alice.ctl("say", wav)
        wait_for(lambda: len([v for v in jim.history() if v["kind"] == "voice" and not v["partial"] and not v["out"]]) > n0, 8)
        time.sleep(0.5)
        last = [v for v in jim.history() if v["kind"] == "voice"][-1]
        check("Busy on the APK keeps the message unplayed", not last["played"] and jim.played_seconds() - p2 < 0.1, last)
        jim.cmd("busy")

        ws.send(json.dumps({"command": "standin_kick", "seq": 4}))
        check("the APK notices a kick", wait_for(lambda: not jim.online(), 8))
        check("and comes back", wait_for(lambda: jim.online(), 15))
        check("on its refresh token", "logon jim refresh ok" in open(log).read())

        jim.cmd("offline")
        check("OFFLINE on the APK closes the session",
              wait_for(lambda: jim.status()["channels"][0]["state"] == "OFFLINE", 5))
        check("and the channel sees jim leave", wait_for(lambda: alice.status()["channels"][0]["users_online"] == 1, 5))
        jim.cmd("online")
        check("ONLINE brings it back", wait_for(jim.online, 8))

        app = e2e.raw_client(port, "jim", e2e.mint(key), channel="test")
        e2e.recv_reply(app, 1)
        check("another client signing in as jim kicks the APK", wait_for(lambda: not jim.online(), 5))
        check("and the APK takes the account straight back", wait_for(jim.online, 8))
        app.close()

        jim.cmd("channel 1")
        check("selecting another channel moves the APK's session there",
              wait_for(lambda: [c["state"] for c in jim.status()["channels"]] == ["NOT JOINED", "ONLINE"], 8),
              jim.status())

        eve = Jim(root, port, key, ["test"], name="eve", password="wrong")
        check("a refused sign-in on the APK shows REFUSED",
              wait_for(lambda: eve.status()["channels"][0]["state"] == "REFUSED", 10), eve.status())
        time.sleep(4)
        tries = open(log).read().count("logon eve")
        check("and is tried exactly once", tries == 1, tries)
        eve.stop()
        ws.close()
    finally:
        rc = [jim.stop(), alice.stop()]
        server.kill()
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
