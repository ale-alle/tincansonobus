#!/usr/bin/env python3
"""Tests the connection server + relay (server/relay) without the app. Starts it on loopback.

    python tests/relay_test.py <path to canduet-server>
"""
import hashlib
import select
import socket
import statistics
import struct
import subprocess
import sys
import time

SERVER_PORT = 11998
FIRST_RELAY_PORT = 41000
FAKE_PUBLIC = "203.0.113.7"
FAKE_LOCAL = "198.51.100.8"
failures = []


def check(name, ok, detail=""):
    print(("PASS " if ok else "FAIL ") + name + (f" ({detail})" if detail else ""))
    if not ok:
        failures.append(name)


def osc_string(text):
    raw = text.encode() + b"\0"
    return raw + b"\0" * (-len(raw) % 4)


def osc(address, *args):
    tags, body = ",", b""
    for arg in args:
        if arg is False:
            tags += "F"
        elif isinstance(arg, str):
            tags += "s"
            body += osc_string(arg)
        elif isinstance(arg, tuple):
            tags += "h"
            body += struct.pack(">q", arg[0])
        else:
            tags += "i"
            body += struct.pack(">i", arg)
    return osc_string(address) + osc_string(tags) + body


def slip(packet):
    return b"\xc0" + packet.replace(b"\xdb", b"\xdb\xdd").replace(b"\xc0", b"\xdb\xdc") + b"\xc0"


def parse_osc(packet):
    def read_string(pos):
        end = packet.index(b"\0", pos)
        return packet[pos:end].decode(), pos + ((end - pos) // 4 + 1) * 4

    address, pos = read_string(0)
    tags, pos = read_string(pos)
    args = []
    for tag in tags[1:]:
        if tag == "s":
            value, pos = read_string(pos)
            args.append(value)
        elif tag == "i":
            args.append(struct.unpack(">i", packet[pos:pos + 4])[0])
            pos += 4
        elif tag == "h":
            args.append(struct.unpack(">q", packet[pos:pos + 8])[0])
            pos += 8
    return address, args


def md5(text):
    return hashlib.md5(text.encode()).hexdigest().upper()


class Client:
    def __init__(self, name, group):
        self.udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.udp.bind(("127.0.0.1", 0))
        self.tcp = socket.create_connection(("127.0.0.1", SERVER_PORT), 5)
        self.tcp.sendall(slip(osc("/aoo/server/login", name, md5(""), FAKE_PUBLIC, 40000, FAKE_LOCAL, 40001, (1234,))))
        time.sleep(0.2)
        self.tcp.sendall(slip(osc("/aoo/server/group/join", group, md5("pw"), False)))
        self.messages = []

    def read_tcp(self, seconds=0.5):
        self.tcp.settimeout(seconds)
        try:
            data = self.tcp.recv(65536)
        except socket.timeout:
            return
        for packet in data.split(b"\xc0"):
            if packet:
                self.messages.append(parse_osc(packet.replace(b"\xdb\xdc", b"\xc0").replace(b"\xdb\xdd", b"\xdb")))

    def peer_join(self):
        for address, args in self.messages:
            if address == "/aoo/client/peer/join":
                return args
        return None

    def send_to(self, port, data):
        self.udp.sendto(data, ("127.0.0.1", port))

    def receive(self, seconds=0.5):
        if select.select([self.udp], [], [], seconds)[0]:
            return self.udp.recvfrom(65536)
        return None, None

    def close(self):
        self.tcp.close()
        self.udp.close()


def wait_for_server():
    for _ in range(50):
        try:
            socket.create_connection(("127.0.0.1", SERVER_PORT), 0.2).close()
            return True
        except OSError:
            time.sleep(0.1)
    return False


def main():
    server = subprocess.Popen([sys.argv[1], "--relay-ip", "127.0.0.1", "--port", str(SERVER_PORT),
                               "--relay-ports", f"{FIRST_RELAY_PORT}-{FIRST_RELAY_PORT + 99}"])
    try:
        check("server starts", wait_for_server())
        time.sleep(0.2)

        a = Client("A", "room1")
        time.sleep(0.3)
        b = Client("B", "room1")
        for _ in range(4):
            a.read_tcp(0.3)
            b.read_tcp(0.3)

        seen_by_a, seen_by_b = a.peer_join(), b.peer_join()
        check("both are introduced", bool(seen_by_a and seen_by_b))
        text = repr(a.messages) + repr(b.messages)
        check("no real address is shared", FAKE_PUBLIC not in text and FAKE_LOCAL not in text)
        check("both addresses are the relay", seen_by_a[2] == "127.0.0.1" and seen_by_a[4] == "127.0.0.1"
              and seen_by_b[2] == "127.0.0.1")
        check("relay ports are in range", all(FIRST_RELAY_PORT <= p <= FIRST_RELAY_PORT + 99
                                              for p in (seen_by_a[3], seen_by_b[3])))
        port_of_b, port_of_a = seen_by_a[3], seen_by_b[3]
        check("each person has their own port", port_of_a != port_of_b)

        a.send_to(port_of_b, b"first")  # B's address isn't known yet, so this is dropped
        check("nothing is sent before the destination is known", b.receive(0.3)[0] is None)

        b.send_to(port_of_a, b"hello a")
        data, source = a.receive()
        check("B reaches A", data == b"hello a", repr(data))
        check("A sees only B's relay port", source == ("127.0.0.1", port_of_b), repr(source))

        a.send_to(port_of_b, b"hello b")
        data, source = b.receive()
        check("A reaches B", data == b"hello b", repr(data))
        check("B sees only A's relay port", source == ("127.0.0.1", port_of_a), repr(source))

        stranger = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        stranger.bind(("127.0.0.1", 0))
        stranger.sendto(b"intruder", ("127.0.0.1", port_of_b))
        stranger.sendto(b"intruder", ("127.0.0.1", port_of_a))
        check("a stranger reaches nobody", b.receive(0.3)[0] is None and a.receive(0.3)[0] is None)

        other = Client("C", "room2")
        other.read_tcp(0.3)
        other.send_to(port_of_b, b"other room")
        check("another room's member reaches nobody", b.receive(0.3)[0] is None)
        other.close()

        loop_ms = []
        for i in range(200):
            start = time.perf_counter()
            a.send_to(port_of_b, b"x" * 200)
            data, _ = b.receive(1)
            if data:
                b.send_to(port_of_a, data)
                if a.receive(1)[0]:
                    loop_ms.append((time.perf_counter() - start) * 1000)
        check("forwarding keeps up", len(loop_ms) == 200, f"{len(loop_ms)}/200 round trips")
        if loop_ms:
            print(f"     two forwards round trip: median {statistics.median(loop_ms):.3f} ms, "
                  f"worst {max(loop_ms):.3f} ms; per forward about {statistics.median(loop_ms) / 2:.3f} ms")

        # Flood at about 80 Mbit/s for 1 s: the limit is 5 Mbit/s plus a 1 Mbit burst
        a.udp.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1 << 20)
        b.udp.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1 << 22)
        time.sleep(1)
        payload, sent, received = b"y" * 1000, 0, 0
        until = time.time() + 1
        while time.time() < until:
            for _ in range(100):
                a.send_to(port_of_b, payload)
                sent += 1000
            time.sleep(0.01)
        while b.receive(0.2)[0]:
            received += 1000
        check("rate limit holds", 0 < received <= 1000000, f"sent {sent}, delivered {received} bytes")

        time.sleep(0.5)
        a.close()
        time.sleep(0.5)
        b.send_to(port_of_a, b"after leaving")
        rejected = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        rejected.bind(("127.0.0.1", 0))
        rejected.sendto(b"after leaving", ("127.0.0.1", port_of_a))
        check("server still answers after someone leaves", server.poll() is None)
        b.close()
    finally:
        server.terminate()
        try:
            server.wait(5)
        except subprocess.TimeoutExpired:
            server.kill()

    print(f"{len(failures)} failed" if failures else "all passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
