#!/usr/bin/env python3
"""A stand-in Zello Channel API server on ws://127.0.0.1, for tests only.

It follows the public reference: logon (auth_token verified as an RS256 JWT
against --pubkey, or a refresh_token it issued), on_channel_status,
start_stream / binary audio / stop_stream relayed with server packet ids,
send_text_message, send_image (thumbnail then full packet), send_location,
"for" routing, errors for commands before logon, and Zello's one session
per account: a second logon closes the first with code 3003 ("kicked").
On ws://HOST:PORT/work it acts as a Zello Work network (many channels per
logon); elsewhere as Friends & Family (one channel per logon). One talker per channel
at a time; a second start_stream gets "channel busy".

Test hooks, sent as JSON by any connection:
  {"command": "standin_drop", "every": N}   drop every Nth relayed audio packet
  {"command": "standin_kick"}               close every session
"""
import argparse
import asyncio
import base64
import itertools
import json
import sys
import time

from websockets.asyncio.server import serve

ap = argparse.ArgumentParser()
ap.add_argument("--port", type=int, required=True)
ap.add_argument("--pubkey", help="PEM public key that auth tokens must verify against")
ap.add_argument("--log", default="-")
ap.add_argument("--ping", type=float, default=20, help="seconds between server pings (Zello: 30)")
args = ap.parse_args()

LOG = sys.stderr if args.log == "-" else open(args.log, "a", buffering=1)


def log(*a):
    print(time.strftime("%H:%M:%S"), *a, file=LOG)


PUB = None
if args.pubkey:
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import padding

    PUB = serialization.load_pem_public_key(open(args.pubkey, "rb").read())


def b64url(s):
    return base64.urlsafe_b64decode(s + "=" * (-len(s) % 4))


def token_ok(tok):
    try:
        head, body, sig = tok.split(".")
        h = json.loads(b64url(head))
        b = json.loads(b64url(body))
        if h.get("alg") != "RS256" or not b.get("iss") or b.get("exp", 0) < time.time():
            return False
        if PUB:
            PUB.verify(b64url(sig), f"{head}.{body}".encode(), padding.PKCS1v15(), hashes.SHA256())
        return True
    except Exception as e:
        log("bad token:", e)
        return False


class Session:
    def __init__(self, ws):
        self.ws = ws
        self.user = None
        self.channels = set()


sessions = []
refresh = {}                    # token -> user
streams = {}                    # stream_id -> dict(channel, from, for, owner, pid)
talking = {}                    # channel -> stream_id
images = {}                     # image_id -> dict(...)
ids = itertools.count(1000)
drop_every = 0
relayed = 0


async def send(s, obj):
    try:
        await s.ws.send(json.dumps(obj) if isinstance(obj, dict) else obj)
    except Exception:
        pass


def members(channel, exclude=None, forwho=None):
    for s in sessions:
        if s is exclude or s.user is None or channel.lower() not in s.channels:
            continue
        if forwho and s.user != forwho:
            continue
        yield s


async def channel_status(channel):
    users = len({s.user for s in sessions if s.user and channel.lower() in s.channels})
    for s in members(channel):
        await send(s, {"command": "on_channel_status", "channel": channel, "status": "online",
                       "users_online": users, "images_supported": True, "texting_supported": True,
                       "locations_supported": True})


async def end_stream(sid):
    st = streams.pop(sid, None)
    if not st:
        return
    if talking.get(st["channel"].lower()) == sid:
        talking.pop(st["channel"].lower())
    for s in members(st["channel"], exclude=st["owner"], forwho=st["for"]):
        await send(s, {"command": "on_stream_stop", "stream_id": sid})


async def handle(ws):
    global drop_every, relayed
    me = Session(ws)
    sessions.append(me)
    try:
        async for m in ws:
            if isinstance(m, bytes):
                if me.user is None or len(m) < 9:
                    continue
                kind, ident = m[0], int.from_bytes(m[1:5], "big")
                if kind == 1:
                    st = streams.get(ident)
                    if not st or st["owner"] is not me:
                        await send(me, {"command": "on_error", "error": "invalid audio packet"})
                        continue
                    pid = st["pid"]
                    st["pid"] += 1
                    relayed += 1
                    if drop_every and relayed % drop_every == 0:
                        continue
                    out = bytes([1]) + ident.to_bytes(4, "big") + pid.to_bytes(4, "big") + m[9:]
                    for s in members(st["channel"], exclude=me, forwho=st["for"]):
                        await send(s, out)
                elif kind == 2:
                    img = images.get(ident)
                    if not img or img["owner"] is not me:
                        continue
                    sub = int.from_bytes(m[5:9], "big")
                    img["thumb" if sub == 2 else "full"] = m[9:]
                    if "thumb" in img and "full" in img:
                        images.pop(ident)
                        mid = next(ids)
                        for s in members(img["channel"], exclude=me, forwho=img["for"]):
                            await send(s, {"command": "on_image", "channel": img["channel"], "from": me.user,
                                           "for": img["for"] or False, "message_id": mid, "type": "jpeg",
                                           "width": img["width"], "height": img["height"], "source": "library"})
                            await send(s, bytes([2]) + mid.to_bytes(4, "big") + (2).to_bytes(4, "big") + img["thumb"])
                            await send(s, bytes([2]) + mid.to_bytes(4, "big") + (1).to_bytes(4, "big") + img["full"])
                continue

            try:
                d = json.loads(m)
            except ValueError:
                await send(me, {"command": "on_error", "error": "invalid json"})
                continue
            cmd, seq = d.get("command"), d.get("seq")

            def reply(**kw):
                return send(me, {"seq": seq, **kw})

            if cmd == "standin_drop":
                drop_every = int(d.get("every", 0))
                await reply(success=True)
                continue
            if cmd == "standin_kick":
                await reply(success=True)
                for s in list(sessions):
                    await s.ws.close()
                continue
            if cmd == "logon":
                user = d.get("username")
                ok = False
                if d.get("refresh_token"):
                    ok = refresh.get(d["refresh_token"]) == user
                    log("logon", user, "refresh", "ok" if ok else "refused")
                elif d.get("auth_token"):
                    ok = token_ok(d["auth_token"]) and d.get("password") == "pw-" + str(user)
                    log("logon", user, "auth_token", "ok" if ok else "refused")
                if not ok:
                    await reply(error="not authorized")
                    continue
                chans = d.get("channels") or []
                if len(chans) > 1 and not ws.request.path.startswith("/work"):
                    await reply(error="channels limit exceeded")  # Friends & Family: one per logon
                    continue
                for other in [s for s in sessions if s is not me and s.user == user]:
                    log("kicked", user)
                    await other.ws.close(code=3003, reason="kicked")
                me.user = user
                me.channels = {c.lower() for c in chans}
                tok = f"rt-{user}-{next(ids)}"
                refresh[tok] = user
                await reply(success=True, refresh_token=tok)
                for c in chans:
                    await channel_status(c)
                continue
            if me.user is None:
                await reply(error="not logged in")
                continue
            ch = d.get("channel", "")
            if cmd in ("start_stream", "send_text_message", "send_image", "send_location") and \
                    ch.lower() not in me.channels:
                await reply(error="channel is not ready")
                continue
            to = d.get("for") or None
            if cmd == "start_stream":
                if d.get("codec") != "opus" or not d.get("codec_header"):
                    await reply(error="not enough params")
                    continue
                if ch.lower() in talking:
                    await reply(error="channel busy")
                    continue
                sid = next(ids)
                streams[sid] = {"channel": ch, "from": me.user, "for": to, "owner": me, "pid": 0}
                talking[ch.lower()] = sid
                await reply(success=True, stream_id=sid)
                for s in members(ch, exclude=me, forwho=to):
                    ev = {"command": "on_stream_start", "type": "audio", "codec": "opus",
                          "codec_header": d["codec_header"], "packet_duration": d.get("packet_duration", 60),
                          "stream_id": sid, "channel": ch, "from": me.user}
                    if to:
                        ev["for"] = to
                    await send(s, ev)
            elif cmd == "stop_stream":
                await end_stream(d.get("stream_id"))
                await reply(success=True)
            elif cmd == "send_text_message":
                await reply(success=True)
                mid = next(ids)
                for s in members(ch, exclude=me, forwho=to):
                    await send(s, {"command": "on_text_message", "channel": ch, "from": me.user,
                                   "for": to or False, "message_id": mid, "text": d.get("text", "")})
            elif cmd == "send_image":
                iid = next(ids)
                images[iid] = {"owner": me, "channel": ch, "for": to, "width": d.get("width"),
                               "height": d.get("height")}
                await reply(success=True, image_id=iid)
            elif cmd == "send_location":
                await reply(success=True)
                mid = next(ids)
                for s in members(ch, exclude=me, forwho=to):
                    await send(s, {"command": "on_location", "channel": ch, "from": me.user, "for": to or False,
                                   "message_id": mid, "latitude": d.get("latitude"),
                                   "longitude": d.get("longitude"), "formatted_address": d.get("formatted_address"),
                                   "accuracy": d.get("accuracy")})
            else:
                await reply(error="unknown command")
    except Exception as e:
        log("session error:", repr(e))
    finally:
        sessions.remove(me)
        for sid, st in list(streams.items()):
            if st["owner"] is me:
                await end_stream(sid)
        for c in me.channels:
            await channel_status(c)


async def main():
    async with serve(handle, "127.0.0.1", args.port, ping_interval=args.ping, max_size=64 << 20):
        log("stand-in listening on", args.port)
        await asyncio.Future()


asyncio.run(main())
