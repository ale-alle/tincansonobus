#!/usr/bin/env python3
"""Local stand-in for the Tin Can room-ticket API (T-019) so the client join flow can be tested.

    python scripts/tincan-mock-redeem.py            # listens on http://localhost:8788
    set TINCAN_API_BASE=http://localhost:8788       (PowerShell: $env:TINCAN_API_BASE="http://localhost:8788")
    CanDuet.exe "canduet://join?ticket=demo-ok"

Ticket "demo-ok" (any number of uses) returns a room on the connection server built into the
standalone app (localhost:10999), with a lesson key that is fixed for this run so two app instances
land in the same encrypted room. Any other ticket gets 403. Tickets are never printed.

Video (T-009): with MOCK_VIDEO_URL set (e.g. ws://localhost:7880 for `livekit-server --dev`), each
redeem also returns {"video": {"url", "token"}}: a LiveKit access token for this run's room, signed
with MOCK_VIDEO_KEY / MOCK_VIDEO_SECRET (defaults devkey / secret, the --dev credentials), camera only.

Two PCs (test server, doc/TINCAN-VPS.md): set the same MOCK_ROOM_SEED on both so their mocks hand
out the same room and lesson key; MOCK_USER_NAME sets the name the other person sees.
"""
import base64
import hashlib
import hmac
import json
import os
import secrets
import time
from http.server import BaseHTTPRequestHandler, HTTPServer

PORT = int(os.environ.get("MOCK_PORT", "8788"))
ROOM_SEED = os.environ.get("MOCK_ROOM_SEED", "")


def room_bytes(label, n):
    """Random bytes for this run, or derived from MOCK_ROOM_SEED so two PCs land in the same room."""
    if not ROOM_SEED:
        return secrets.token_bytes(n)
    return hmac.new(ROOM_SEED.encode(), label.encode(), hashlib.sha256).digest()[:n]


ROOM = {
    "server": os.environ.get("MOCK_SERVER", "localhost:10999"),
    "group": "lesson-" + room_bytes("group", 8).hex(),
    "groupPassword": base64.urlsafe_b64encode(room_bytes("password", 24)).decode(),
    "lessonKey": base64.b64encode(room_bytes("lesson-key", 32)).decode(),
    "title": "Demo lesson",
    "userName": os.environ.get("MOCK_USER_NAME", "Demo Student"),
}
VIDEO_URL = os.environ.get("MOCK_VIDEO_URL", "")
VIDEO_KEY = os.environ.get("MOCK_VIDEO_KEY", "devkey")
VIDEO_SECRET = os.environ.get("MOCK_VIDEO_SECRET", "secret")
VIDEO_ROOM = "lesson-" + room_bytes("video-room", 8).hex()


def b64url(data):
    return base64.urlsafe_b64encode(data).rstrip(b"=").decode()


def video_token(identity, name, ttl=3600):
    """LiveKit access token (HS256 JWT): join one room, publish camera only, no data channel."""
    now = int(time.time())
    header = {"alg": "HS256", "typ": "JWT"}
    claims = {
        "iss": VIDEO_KEY, "sub": identity, "name": name, "nbf": now - 10, "exp": now + ttl,
        "video": {"room": VIDEO_ROOM, "roomJoin": True, "canPublish": True, "canSubscribe": True,
                  "canPublishData": False, "canPublishSources": ["camera"]},
    }
    signing_input = b64url(json.dumps(header).encode()) + "." + b64url(json.dumps(claims).encode())
    sig = hmac.new(VIDEO_SECRET.encode(), signing_input.encode(), hashlib.sha256).digest()
    return signing_input + "." + b64url(sig)


class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        if self.path != "/api/rooms/redeem":
            return self.reply(404, {"error": "not_found"})
        try:
            length = int(self.headers.get("Content-Length", "0"))
            ticket = json.loads(self.rfile.read(length) or b"{}").get("ticket", "")
        except (ValueError, json.JSONDecodeError):
            return self.reply(400, {"error": "bad_request"})
        if ticket == "demo-ok":
            body = dict(ROOM)
            if VIDEO_URL:
                # a new identity per redeem so two app instances are two participants
                body["video"] = {"url": VIDEO_URL,
                                 "token": video_token("p-" + secrets.token_hex(6), ROOM["userName"])}
            return self.reply(200, body)
        return self.reply(403, {"error": "ticket_rejected"})

    def reply(self, status, body):
        data = json.dumps(body).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)
        print(f"redeem -> {status}")

    def log_message(self, *args):  # keep request lines (and tickets) out of the console
        pass


if __name__ == "__main__":
    print(f"Mock Tin Can redeem API on http://localhost:{PORT}/api/rooms/redeem")
    HTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
