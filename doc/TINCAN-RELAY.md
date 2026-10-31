# Lesson audio relay (T-022, D-009 phase 2)

**Status: design approved by the owner 2026-10-31, with the 10 ms limit below, to be tightened later if connections prove stable. First version built (`server/relay`) and tested on one PC with `tests/relay_test.py` and two app copies. Not yet tested on the VPS or between two homes.**

Not built yet: tying relay ports to lesson tickets (known gap below), and lesson rooms still offer the manual "Connect to Other" option.

All lesson audio goes through a relay on the Toronto server. Participants never receive each other's IP addresses, and the relay can't read the audio.

## Why
Direct connections share each person's public and LAN addresses, and they fail on some home networks. In the first two-home test (2026-10-31) both PCs joined the room but no audio connection appeared; a participant who can't be reached looks the same. See D-009 and risks R-004, R-019.

AOO 2.0-pre4 has a relay, but it's a fallback after direct connection fails and peers still receive each other's addresses, so it doesn't meet D-009 as it stands. The owner chose a relay on the current AOO; moving to AOO 2 stays possible once it has a stable release.

## How it works
The connection server gives each participant a port on the relay that stands in for them, and tells the others that port instead of the person's real address. To the app, the relay port looks like an ordinary peer.

```
Teacher app ── to relay:40001 ──> Relay ── from relay:40002 ──> Student app
Teacher app <── from relay:40001 ── Relay <── to relay:40002 ── Student app

40001 stands in for Student; 40002 stands in for Teacher.
```

1. A participant joins a lesson group. The connection server gives them a relay port, unique within that lesson.
2. The join message to other members carries `relay host:that port` as both the public and local address, never the real ones.
3. The apps run their normal ping handshake and audio to those addresses. The existing lesson-key encryption stays on, end to end.
4. The relay learns each participant's real address from their first packets, and forwards each packet:
   - a packet arriving on port P is for the participant who owns P;
   - it's sent out from the sender's own port, so the receiver sees the same address it was told about.
5. The port is released when the participant leaves or stops sending.

Because the server decides what address gets shared, a modified or old app can't obtain the other person's address from it.

## What needs building
| Part | Change |
|---|---|
| Connection server | Fork `essej/aooserver` (T-023): allocate relay ports, and send them in place of real addresses |
| Relay | Small UDP forwarder on the same VPS, with no audio processing or decryption |
| App | Lesson rooms stop offering direct-connect actions and manual peer connection; blocking must act on the person, not the address (below) |
| Firewall | Open one UDP port range for the relay |

### App behaviour that assumes a real address
- **Blocking:** `addBlockedAddress` blocks an IP. With a relay, that address is the server, so blocking one person could block everyone.
- **Reconnect and peer menus:** `PeersContainerView` reconnects through `getRemotePeerAddressInfo`. This should still work, because it would reconnect to the relay port, but needs testing.
- **Address display:** anything showing a peer's address would show only the relay. This is safe.
- **Message wording:** the `Could not connect` text mentions port forwarding, which won't apply to relayed lessons.

## Security
- **No key on the server:** the relay forwards sealed packets and can't read audio or chat. It can see packet sizes and timing, and both participants' IP addresses.
- **No open forwarding:** it forwards only between participants of a current lesson, and only from the IP the connection server saw at login.
- **Abuse limits:** a bandwidth cap per port, and no reply larger than the packet that triggered it, so it can't be used to amplify attacks.
- **No storage:** no audio or packet contents are stored, and logs hold no addresses by default.
- **Known gap:** group names and password hashes reach the server unencrypted, and joining isn't tied to the lesson ticket. Real lessons should only allocate relay ports for rooms the app server created (T-021 or a server-to-server call).

## Latency
| Item | Figure | Status |
|---|---|---|
| Owner's PC to the VPS, round trip | 7 ms average (20 pings, 6–9 ms, no loss) | Measured 2026-10-31 |
| Teacher to student through the relay | Roughly 7 ms if the student's line is similar | Estimate |
| Forwarding inside the relay | Expected to be under 1 ms | Not measured |
| Direct path for the same two people | Unknown | Must be measured |

Added delay is the relayed path minus the direct path. Proposed limit, for the owner to confirm: **no more than 10 ms one way above direct for two Toronto homes**.

Audio is never delayed to match video (D-002).

## Test plan
1. **No student needed:** two app copies on one PC through the relay show tiles and pass audio. A packet capture confirms they exchange traffic only with the relay.
2. **Privacy check:** a fake participant logs what the server sends, and it holds no real addresses.
3. **Abuse check:** packets from an unknown address aren't forwarded, and the rate limit holds.
4. **Long session:** stable for 60 minutes, covering router port changes, leaving and rejoining.
5. **Two homes:** repeat the owner's test with the student on new kits, and record latency and dropouts (T-008).

## Open points
- **Bandwidth:** each lesson's audio passes through the VPS twice, once in and once out. Measure usage per lesson before choosing the production server size.
- **Router port changes:** if a router changes the port mid-lesson, the relay must accept the new one without letting anyone else take over.
- **More than two people:** the design allows it (one port per person), but the first version only needs to be tested with two.
