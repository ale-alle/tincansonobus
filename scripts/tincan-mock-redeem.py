#!/usr/bin/env python3
"""Local stand-in for the Tin Can room-ticket API (T-019) so the client join flow can be tested.

    python scripts/tincan-mock-redeem.py            # listens on http://localhost:8788
    set TINCAN_API_BASE=http://localhost:8788       (PowerShell: $env:TINCAN_API_BASE="http://localhost:8788")
    CanDuet.exe "canduet://join?ticket=demo-ok"

Ticket "demo-ok" (any number of uses) returns a room on the connection server built into the
standalone app (localhost:10999), with a lesson key that is fixed for this run so two app instances
land in the same encrypted room. Any other ticket gets 403. Tickets are never printed.
"""
import base64
import json
import os
import secrets
from http.server import BaseHTTPRequestHandler, HTTPServer

PORT = int(os.environ.get("MOCK_PORT", "8788"))
ROOM = {
    "server": os.environ.get("MOCK_SERVER", "localhost:10999"),
    "group": "lesson-" + secrets.token_hex(8),
    "groupPassword": secrets.token_urlsafe(24),
    "lessonKey": base64.b64encode(secrets.token_bytes(32)).decode(),
    "title": "Demo lesson",
}


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
            return self.reply(200, ROOM)
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
