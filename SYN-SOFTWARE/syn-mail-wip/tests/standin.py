#!/usr/bin/env python3
# ------------------------------------------------------------------------
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-MAIL
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------
# A stand-in mail provider for the tests: IMAP (TLS), SMTP submission (TLS) and an OAuth token endpoint (HTTPS),
# all on localhost with a throwaway certificate. No live traffic. Usage: standin.py DIR (DIR holds cert.pem, key.pem;
# the ports go to DIR/ports, every received message to DIR/sent/N.eml, what was signed in to DIR/log).
import base64, hashlib, http.server, json, os, shlex, socket, ssl, sys, threading, urllib.parse

D = sys.argv[1]
USERS = {"pw@example.test": "s3cret pass\"word"}
TOKENS = {"rt-good": "at-1", "rt-from-login": "at-1"}            # refresh token -> access token
ACCESS = {"at-1": "oauth@example.test"}  # access token -> mailbox
LOCK = threading.Lock()

def log(line):
    with LOCK, open(os.path.join(D, "log"), "a") as f:
        f.write(line + "\n")

def msg(h, body):
    return (h.replace("\n", "\r\n") + "\r\n\r\n" + body.replace("\n", "\r\n")).encode("latin-1")

MAILBOX = [
    (True, msg("From: =?UTF-8?B?Q2Fmw6kgQ2x1Yg==?= <club@example.test>\nSubject: =?UTF-8?B?Q2Fmw6kg?=\n =?UTF-8?B?csOpdW5pb24=?=\n"
               "Date: Sat, 31 Oct 2026 10:00:00 +0000\nContent-Type: text/plain; charset=UTF-8", "Plain body.")),
    (False, msg("From: Shop <shop@example.test>\nSubject: Your order\nDate: Sat, 31 Oct 2026 11:00:00 +0000\n"
                "MIME-Version: 1.0\nContent-Type: multipart/mixed; boundary=\"outer\"",
                "--outer\nContent-Type: multipart/alternative; boundary=inner\n\n--inner\n"
                "Content-Type: text/plain; charset=UTF-8\nContent-Transfer-Encoding: quoted-printable\n\n"
                "Your caf=C3=A9 order is on its way, and this line is long enough to be wrapped by a soft=\n"
                " break.\n--inner\nContent-Type: text/html; charset=UTF-8\n\n<p>HTML VERSION</p>\n--inner--\n"
                "--outer\nContent-Type: application/pdf; name=r.pdf\nContent-Disposition: attachment; filename=r.pdf\n"
                "Content-Transfer-Encoding: base64\n\nQVRUQUNITUVOVA==\n--outer--\n")),
    (False, msg("From: Chip Shop <chips@example.test>\nSubject: =?ISO-8859-1?Q?Gr=FC=DFe_aus_Berlin?=\n"
                "Date: Sun, 01 Nov 2026 09:00:00 +0000\nMIME-Version: 1.0\nContent-Type: text/html; charset=ISO-8859-1\n"
                "Content-Transfer-Encoding: base64",
                base64.b64encode("<html><style>p{x}</style><body><p>Fish &amp; chips</p><br>\xe0 bient\xf4t</body></html>"
                                 .encode("latin-1")).decode())),
]

def ctx():
    c = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    c.load_cert_chain(os.path.join(D, "cert.pem"), os.path.join(D, "key.pem"))
    return c

class Conn:
    def __init__(self, s):
        self.s, self.f = s, s.makefile("rb")
    def line(self):
        l = self.f.readline(1 << 20)
        if not l:
            raise EOFError
        return l.rstrip(b"\r\n").decode("utf-8", "replace")
    def send(self, b):
        self.s.sendall(b if isinstance(b, bytes) else b.encode())

def xoauth2(b64):
    try:
        parts = base64.b64decode(b64).decode().split("\x01")
        user = parts[0][5:]
        tok = parts[1].split("Bearer ", 1)[1]
        return user if ACCESS.get(tok) == user else None
    except Exception:
        return None

def imap(c):
    c.send("* OK stand-in IMAP ready\r\n")
    who = None
    while True:
        l = c.line()
        tag, _, rest = l.partition(" ")
        cmd, _, arg = rest.partition(" ")
        cmd = cmd.upper()
        if cmd == "LOGIN":
            # two quoted strings
            try:
                u, p = shlex.split(arg)[:2]
            except ValueError:
                u = p = None
            if USERS.get(u) == p:
                who = u; log("imap LOGIN " + u); c.send(tag + " OK LOGIN done\r\n")
            else:
                c.send(tag + " NO [AUTHENTICATIONFAILED] Invalid credentials\r\n")
        elif cmd == "AUTHENTICATE":
            mech, _, ir = arg.partition(" ")
            u = xoauth2(ir) if mech.upper() == "XOAUTH2" else None
            if u:
                who = u; log("imap XOAUTH2 " + u); c.send(tag + " OK AUTHENTICATE done\r\n")
            else:
                c.send("+ " + base64.b64encode(b'{"status":"400","schemes":"Bearer"}').decode() + "\r\n")
                c.line()
                c.send(tag + " NO [AUTHENTICATIONFAILED] Invalid credentials (Failure)\r\n")
        elif cmd == "LOGOUT":
            c.send("* BYE\r\n" + tag + " OK LOGOUT\r\n"); return
        elif not who:
            c.send(tag + " BAD sign in first\r\n")
        elif cmd == "EXAMINE":
            c.send("* FLAGS (\\Seen)\r\n* %d EXISTS\r\n* 0 RECENT\r\n%s OK [READ-ONLY] EXAMINE done\r\n" % (len(MAILBOX), tag))
        elif cmd == "STATUS":
            c.send("* STATUS INBOX (UNSEEN %d)\r\n%s OK STATUS done\r\n" % (sum(1 for s, _ in MAILBOX if not s), tag))
        elif cmd == "FETCH":
            rng, _, what = arg.partition(" ")
            a, _, b = rng.partition(":")
            a, b = int(a), int(b or a)
            out = b""
            for i in range(a, b + 1):
                if i < 1 or i > len(MAILBOX):
                    continue
                seen, m = MAILBOX[i - 1]
                if "HEADER.FIELDS" in what:
                    hdr = m.split(b"\r\n\r\n")[0] + b"\r\n\r\n"
                    keep, cur = [], False
                    for hl in hdr.split(b"\r\n"):
                        if hl[:1] in (b" ", b"\t"):
                            if cur: keep.append(hl)
                            continue
                        cur = hl.split(b":")[0].lower() in (b"from", b"subject", b"date")
                        if cur: keep.append(hl)
                    data = b"\r\n".join(keep) + b"\r\n\r\n"
                    out += b"* %d FETCH (FLAGS (%s) BODY[HEADER.FIELDS (FROM SUBJECT DATE)] {%d}\r\n%s)\r\n" % (
                        i, b"\\Seen" if seen else b"", len(data), data)
                else:
                    out += b"* %d FETCH (BODY[] {%d}\r\n%s)\r\n" % (i, len(m), m)
            c.send(out + (tag + " OK FETCH done\r\n").encode())
        else:
            c.send(tag + " BAD unknown\r\n")

def smtp(c):
    c.send("220 stand-in ESMTP\r\n")
    who, rcpt = None, []
    while True:
        l = c.line()
        u = l.upper()
        if u.startswith("EHLO"):
            c.send("250-stand-in\r\n250-SIZE 1000000\r\n250 AUTH PLAIN XOAUTH2\r\n")
        elif u.startswith("AUTH PLAIN "):
            try:
                _, user, pw = base64.b64decode(l[11:]).decode().split("\x00")
            except Exception:
                user = pw = None
            if USERS.get(user) == pw:
                who = user; log("smtp PLAIN " + user); c.send("235 ok\r\n")
            else:
                c.send("535 5.7.8 bad credentials\r\n")
        elif u.startswith("AUTH XOAUTH2 "):
            user = xoauth2(l[13:])
            if user:
                who = user; log("smtp XOAUTH2 " + user); c.send("235 ok\r\n")
            else:
                c.send("334 " + base64.b64encode(b'{"status":"400"}').decode() + "\r\n")
                c.line()
                c.send("535 5.7.8 Username and Password not accepted\r\n")
        elif u.startswith("MAIL FROM:"):
            c.send("250 ok\r\n" if who else "530 auth first\r\n")
        elif u.startswith("RCPT TO:"):
            rcpt.append(l[8:]); c.send("250 ok\r\n")
        elif u == "DATA":
            c.send("354 go\r\n")
            lines = []
            while True:
                x = c.f.readline(1 << 20)
                if x == b".\r\n":
                    break
                if not x.endswith(b"\r\n"):
                    raise EOFError("bare LF on the wire")
                lines.append(x[1:] if x.startswith(b"..") else x)
            os.makedirs(os.path.join(D, "sent"), exist_ok=True)
            with LOCK:
                n = len(os.listdir(os.path.join(D, "sent"))) + 1
                with open(os.path.join(D, "sent", "%d.eml" % n), "wb") as f:
                    f.write(b"X-Standin-Rcpt: " + ",".join(rcpt).encode() + b"\r\n" + b"".join(lines))
            c.send("250 2.0.0 queued as %d\r\n" % n)
        elif u == "QUIT":
            c.send("221 bye\r\n"); return
        else:
            c.send("502 unknown\r\n")

class Token(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        q = urllib.parse.parse_qs(self.rfile.read(int(self.headers["Content-Length"])).decode())
        ok = q.get("client_id") == ["cid"] and q.get("client_secret") == ["csec"]
        g = q.get("grant_type", [""])[0]
        extra = {}
        if g == "authorization_code":
            # the code the test handed to the loopback, and the PKCE check against the challenge it saw in the URL
            try:
                chal = open(os.path.join(D, "challenge")).read().strip()
            except OSError:
                chal = ""
            v = q.get("code_verifier", [""])[0].encode()
            calc = base64.urlsafe_b64encode(hashlib.sha256(v).digest()).rstrip(b"=").decode()
            at = "at-1" if ok and q.get("code") == ["code-1"] and calc == chal and q.get("redirect_uri", [""])[0].startswith(
                "http://127.0.0.1:") else None
            extra = {"refresh_token": "rt-from-login"} if at else {}
            log("token authorization_code " + ("ok" if at else "refused"))
        else:
            at = TOKENS.get(q.get("refresh_token", [""])[0]) if ok and g == "refresh_token" else None
        body = json.dumps(dict({"access_token": at, "expires_in": 3599, "token_type": "Bearer"}, **extra) if at else
                          {"error": "invalid_grant", "error_description": "Token has been expired or revoked."}).encode()
        self.send_response(200 if at else 400)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)
    def log_message(self, *a):
        pass

def serve(handler):
    ls = socket.socket(); ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    ls.bind(("127.0.0.1", 0)); ls.listen(8)
    tls = ctx()
    def loop():
        while True:
            s, _ = ls.accept()
            def run(s=s):
                try:
                    t = tls.wrap_socket(s, server_side=True)
                except (ssl.SSLError, OSError):
                    s.close(); return
                try:
                    handler(Conn(t))
                except (EOFError, OSError, ssl.SSLError):
                    pass
                finally:
                    t.close()
            threading.Thread(target=run, daemon=True).start()
    threading.Thread(target=loop, daemon=True).start()
    return ls.getsockname()[1]

pi, ps = serve(imap), serve(smtp)
hs = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Token)
hs.socket = ctx().wrap_socket(hs.socket, server_side=True)
with open(os.path.join(D, "ports.tmp"), "w") as f:
    f.write("%d %d %d\n" % (pi, ps, hs.server_address[1]))
os.rename(os.path.join(D, "ports.tmp"), os.path.join(D, "ports"))
hs.serve_forever()
