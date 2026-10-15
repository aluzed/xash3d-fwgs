#!/usr/bin/env python3
"""Xash3D protocol 49 load client: connects N fake players to a dedicated server.

Story 036, counterpart of the ReHLDS fork's GoldSrc load client. Each client
performs the real handshake (challenge, connect, netchan with reliable acks and
fragment reassembly), signs on (new, spawn, begin), joins a team, then sends
clc_move at a fixed command rate with moving players and clc_delta acks, like a
real client.

Server packets are only decoded during the sign-on, to find the spawn count in
the answer to "new". In game, the netchan header is enough to acknowledge,
which keeps the client cheap enough to drive 32 players from one Python process.

Each client binds its own 127.0.1.x address: the server limits challenges and
connections per address (1 per second, 3 signing on at once).

usage: loadclient.py --server 127.0.0.1:27016 --clients 32 --duration 60 --output clients.json
"""

import argparse
import json
import math
import random
import selectors
import socket
import struct
import sys
import time

PROTOCOL_VERSION = 49
OOB_HEADER = b"\xff\xff\xff\xff"

CLC_NOP = 1
CLC_MOVE = 2
CLC_STRINGCMD = 3
CLC_DELTA = 4

SVC_SERVERDATA = 11

IN_ATTACK = 1 << 0
IN_JUMP = 1 << 1
IN_SCORE = 1 << 15


# --- CRC32_BlockSequence (public/crclib.c) ---------------------------------------

def _crc_table():
    table = []
    for n in range(256):
        c = n
        for _ in range(8):
            c = (c >> 1) ^ 0xEDB88320 if c & 1 else c >> 1
        table.append(c)
    return table


CRC_TABLE = _crc_table()
CRC_TABLE_BYTES = struct.pack("<256I", *CRC_TABLE)


def block_sequence_crc_byte(data, sequence):
    sequence = abs(sequence)
    salt = CRC_TABLE_BYTES[sequence % 0x3FC: sequence % 0x3FC + 4]
    chunk = bytes(data[:60]) + salt
    crc = 0xFFFFFFFF
    for b in chunk:
        crc = CRC_TABLE[(crc ^ b) & 0xFF] ^ (crc >> 8)
    return ~crc & 0xFF


# --- LZSS (engine/common/common.c) ---------------------------------------------

def lzss_decompress(buf):
    """Decompresses a fragment chain that starts with "LZSS" and its size."""
    size = struct.unpack_from("<I", buf, 4)[0]
    out = bytearray()
    pos = 8
    while pos < len(buf) and len(out) < size:
        command = buf[pos]
        pos += 1
        for bit in range(8):
            if command & (1 << bit):
                b0, b1 = buf[pos], buf[pos + 1]
                pos += 2
                offset = (b0 << 4) | (b1 >> 4)
                count = (b1 & 0x0F) + 1
                if count == 1:
                    return bytes(out)
                start = len(out) - offset - 1
                for i in range(count):
                    out.append(out[start + i])
            else:
                out.append(buf[pos])
                pos += 1
            if pos >= len(buf) or len(out) >= size:
                break
    return bytes(out)


# --- Bit writer (MSG_WriteBits, little-endian bit order) ---------------------

class BitWriter:
    def __init__(self):
        self.acc = 0
        self.nbits = 0

    def bits(self, value, numbits):
        mask = (1 << numbits) - 1
        self.acc |= (value & mask) << self.nbits
        self.nbits += numbits

    def sbits(self, value, numbits):
        """MSG_WriteSBitLong: magnitude bits, then the sign bit."""
        limit = 1 << (numbits - 1)
        value = max(-limit, min(limit - 1, value))
        self.bits(value & (limit - 1), numbits - 1)
        self.bits(1 if value < 0 else 0, 1)

    def angle(self, degrees, numbits):
        shift = 1 << numbits
        self.bits(int(shift * math.fmod(degrees, 360.0)) // 360 & (shift - 1), numbits)

    def to_bytes(self):
        return self.acc.to_bytes((self.nbits + 7) // 8, "little")


# usercmd_t fields in delta.lst order: name, kind, bits
USERCMD_FIELDS = [
    ("lerp_msec", "u", 9),
    ("msec", "u", 8),
    ("yaw", "angle", 16),       # viewangles[1]
    ("pitch", "angle", 16),     # viewangles[0]
    ("buttons", "u", 16),
    ("forwardmove", "s", 12),
    ("lightlevel", "u", 8),
    ("sidemove", "s", 12),
    ("upmove", "s", 12),
    ("impulse", "u", 8),
    ("roll", "angle", 16),      # viewangles[2]
    ("impact_index", "u", 6),
    ("impact_x", "s", 16),
    ("impact_y", "s", 16),
    ("impact_z", "s", 16),
]


def write_usercmd(w, cmd):
    """MSG_WriteDeltaUsercmd against an all-zero command: a changed bit per field."""
    for name, kind, numbits in USERCMD_FIELDS:
        value = cmd.get(name, 0)
        if not value:
            w.bits(0, 1)
            continue
        w.bits(1, 1)
        if kind == "u":
            w.bits(int(value), numbits)
        elif kind == "s":
            w.sbits(int(value), numbits)
        else:
            w.angle(value, numbits)


# --- Netchan (engine/common/net_chan.c) --------------------------------------

class Netchan:
    def __init__(self, qport):
        self.qport = qport
        self.outgoing_sequence = 1
        self.incoming_sequence = 0
        self.incoming_acknowledged = 0
        self.incoming_reliable_acknowledged = 0
        self.incoming_reliable_sequence = 0
        self.reliable_sequence = 0
        self.last_reliable_sequence = 0
        self.reliable_buf = b""
        self.pending_reliable = []
        self.fragments = {}  # buffer id -> data, normal stream only

    def queue_reliable(self, data):
        self.pending_reliable.append(data)

    def transmit(self, unreliable=b""):
        send_reliable = False
        if (self.incoming_acknowledged > self.last_reliable_sequence
                and self.incoming_reliable_acknowledged != self.reliable_sequence):
            send_reliable = True  # resend the unacknowledged reliable message

        if not self.reliable_buf and self.pending_reliable:
            self.reliable_buf = b"".join(self.pending_reliable)
            self.pending_reliable = []
            self.reliable_sequence ^= 1
            send_reliable = True

        w1 = self.outgoing_sequence | (int(send_reliable) << 31)
        w2 = self.incoming_sequence | (self.incoming_reliable_sequence << 31)
        sequence = self.outgoing_sequence
        self.outgoing_sequence += 1

        payload = bytearray(self.reliable_buf if send_reliable else b"")
        if send_reliable:
            self.last_reliable_sequence = sequence
        return struct.pack("<IIH", w1, w2, self.qport) + bytes(payload), len(payload), sequence

    def process(self, packet, decode):
        """Updates the channel from a server packet. Returns a completed
        normal-stream fragment chain, decompressed, when decode is set."""
        if len(packet) < 8:
            return None
        sequence, sequence_ack = struct.unpack_from("<II", packet)
        reliable_message = sequence >> 31
        reliable_ack = sequence_ack >> 31
        has_fragments = bool(sequence & (1 << 30)) and reliable_message
        sequence &= 0x3FFFFFFF
        sequence_ack &= 0x3FFFFFFF

        if sequence <= self.incoming_sequence:
            return None  # duplicate or out of order

        if reliable_ack == self.reliable_sequence and sequence_ack >= self.last_reliable_sequence:
            self.reliable_buf = b""  # acknowledged

        self.incoming_sequence = sequence
        self.incoming_acknowledged = sequence_ack
        self.incoming_reliable_acknowledged = reliable_ack
        if reliable_message:
            self.incoming_reliable_sequence ^= 1

        if not decode or not has_fragments:
            return None

        pos = 8
        frags = []
        for stream in range(2):
            if packet[pos]:
                fragid, offset, length = struct.unpack_from("<III", packet, pos + 1)
                frags.append((stream, fragid, offset, length))
                pos += 13
            else:
                pos += 1

        payload = int.from_bytes(packet[pos:], "little")
        completed = None
        for stream, fragid, offset, length in frags:
            if stream != 0:
                continue
            chunk = ((payload >> offset) & ((1 << length) - 1)).to_bytes(length // 8, "little")
            completed = self._add_fragment(fragid, chunk) or completed
        return completed

    def _add_fragment(self, fragid, chunk):
        index, count = fragid >> 16, fragid & 0xFFFF
        self.fragments[index] = chunk
        if all(i in self.fragments for i in range(1, count + 1)):
            buf = b"".join(self.fragments[i] for i in range(1, count + 1))
            self.fragments = {}
            if buf[:4] == b"LZSS":
                buf = lzss_decompress(buf)
            return buf
        return None


def find_spawncount(buf):
    """svc_serverdata, byte aligned after the svc_prints that may precede it."""
    marker = bytes([SVC_SERVERDATA]) + struct.pack("<i", PROTOCOL_VERSION)
    pos = buf.find(marker)
    if pos < 0:
        return None
    return struct.unpack_from("<i", buf, pos + len(marker))[0]


# --- Client --------------------------------------------------------------------

class Client:
    def __init__(self, index, server, cmdrate, updaterate, stats, play=False):
        self.index = index
        self.server = server
        self.cmdrate = cmdrate
        self.updaterate = updaterate
        self.stats = stats
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setblocking(False)
        # own address: per-address limits on challenges and sign-ons
        self.sock.bind((f"127.0.{1 + index // 250}.{1 + index % 250}", 0))
        self.qport = random.randint(1, 65535)
        self.state = "idle"
        self.netchan = None
        self.next_send = 0.0
        self.state_since = 0.0
        self.yaw = random.uniform(0, 360)
        self.next_scoreboard = 0.0
        self.scoreboard_until = 0.0
        self.last_receive = None
        self.intervals = []
        # Input latency: from sending a command to receiving the first server packet
        # that acknowledges it, i.e. sent after the server processed it: the wait for
        # the server's tick and for the client's next snapshot
        self.sent_at = {}
        self.last_ack = 0
        self.input_latency = []
        self.play = play
        self.join_at = None
        self.next_fire = 0.0
        self.fire_until = 0.0

    def log(self, message):
        print(f"[client {self.index:02d}] {message}", file=sys.stderr)

    def send_oob(self, text):
        self.sock.sendto(OOB_HEADER + text.encode("latin-1"), self.server)

    def start(self, now):
        self.set_state("challenge", now)
        self.send_oob("getchallenge steam\n")
        self.next_send = now + 1.0

    def stringcmd(self, text):
        self.netchan.queue_reliable(bytes([CLC_STRINGCMD]) + text.encode("latin-1") + b"\x00")

    def set_state(self, state, now):
        self.state = state
        self.state_since = now

    def on_packet(self, packet, now):
        if packet[:4] == OOB_HEADER:
            self.on_oob(packet[4:], now)
            return

        if not self.netchan:
            return

        completed = self.netchan.process(packet, self.state == "new")

        if self.state == "ingame":
            if self.last_receive is not None:
                self.intervals.append(now - self.last_receive)
            self.last_receive = now

            # Every command acknowledged by this packet waited until now
            ack = self.netchan.incoming_acknowledged
            if ack > self.last_ack:
                for sequence in [k for k in self.sent_at if k <= ack]:
                    self.input_latency.append(now - self.sent_at.pop(sequence))
                self.last_ack = ack
            return

        if self.state == "new" and completed is not None:
            spawncount = find_spawncount(completed)
            if spawncount is None:
                return  # a chain the game printed into before the sign-on data
            # spawn and begin run one after the other in the same message
            self.stringcmd(f"spawn {spawncount}")
            self.stringcmd("begin")
            if self.play:
                # The game DLL only accepts a team once the message of the day
                # is closed (attack pressed, see build_move)
                self.join_at = now + 1.5
            else:
                self.stringcmd("jointeam 5")
                self.stringcmd("joinclass 5")
            self.set_state("ingame", now)
            self.stats["ingame"] += 1
            self.log("in game")
            # real players send their commands at unrelated times: a random phase,
            # else clients connected a whole number of intervals apart send together
            self.next_send = now + random.uniform(0.0, 1.0 / self.cmdrate)

    def on_oob(self, payload, now):
        text = payload.decode("latin-1", "replace")
        words = text.split()
        if not words:
            return
        if words[0] == "challenge" and self.state == "challenge":
            challenge = int(words[1])
            protinfo = f"\\uuid\\{random.getrandbits(128):032x}\\qport\\{self.qport}\\ext\\0"
            userinfo = (f"\\name\\load{self.index:02d}\\rate\\100000\\cl_updaterate\\{self.updaterate}"
                        f"\\cl_lw\\1\\cl_lc\\1\\cl_dlmax\\64000\\model\\gsg9\\_vgui_menus\\0")
            self.send_oob(f'connect {PROTOCOL_VERSION} {challenge} "{protinfo}" "{userinfo}"\n')
            self.set_state("connect", now)
        elif words[0] == "client_connect" and self.state == "connect":
            self.netchan = Netchan(self.qport)
            self.stringcmd("new")
            self.set_state("new", now)
            self.next_send = now
        elif words[0] in ("errormsg", "disconnect") or "reject" in text.lower():
            if self.state in ("failed", "ingame"):
                return
            self.log(f"rejected: {text.strip()[:120]}")
            self.set_state("failed", now)
            self.stats["failed"] += 1

    def build_move(self, w, now):
        self.yaw = (self.yaw + random.uniform(-4, 4)) % 360

        # Look at the scoreboard for a second every few seconds, like players do:
        # the server then sends this client the pings
        if now >= self.next_scoreboard:
            self.scoreboard_until = now + 1.0
            self.next_scoreboard = now + random.uniform(6.0, 10.0)
        buttons = IN_SCORE if now < self.scoreboard_until else 0
        if random.random() < 0.01:
            buttons |= IN_JUMP
        if self.play and self.join_at is None:
            # Fire a short burst every few seconds: weapon events and damage
            if now >= self.next_fire:
                self.fire_until = now + 0.2
                self.next_fire = now + random.uniform(2.0, 4.0)
            if now < self.fire_until:
                buttons |= IN_ATTACK
        if self.join_at is not None:
            if now < self.join_at:
                # Close the message of the day: hold attack, release before joining
                buttons |= IN_ATTACK if now < self.join_at - 0.3 else 0
            else:
                self.stringcmd("jointeam 5")
                self.stringcmd("joinclass 5")
                self.join_at = None

        cmd = {
            "lerp_msec": 100,
            "msec": int(1000 / self.cmdrate),
            "yaw": self.yaw,
            "forwardmove": 250,
            "sidemove": random.choice((0, 0, 0, 250, -250)),
            "buttons": buttons,
        }

        # clc_move, checksum (patched below), packet loss, backup and new commands
        w.bits(CLC_MOVE, 8)
        w.bits(0, 8)
        w.bits(0, 8)
        w.bits(0, 8)
        w.bits(1, 8)
        write_usercmd(w, cmd)
        return w.nbits

    def tick(self, now):
        """Sends the next packet if due. Returns the time of the next send."""
        if self.state in ("idle", "failed"):
            return None

        if self.state != "ingame" and now - self.state_since > 15.0:
            self.log(f"timed out in state {self.state}")
            self.set_state("failed", now)
            self.stats["failed"] += 1
            return None

        if now < self.next_send:
            return self.next_send

        if self.state in ("challenge", "connect"):
            # Retry the out-of-band step
            if self.state == "challenge":
                self.send_oob("getchallenge steam\n")
            self.next_send = now + 1.5
            return self.next_send

        header, reliable_len, sequence = self.netchan.transmit()
        unreliable = b""
        if self.state == "ingame":
            w = BitWriter()
            end_of_cmds = self.build_move(w, now)
            w.bits(CLC_DELTA, 8)
            w.bits(self.netchan.incoming_sequence & 0xFF, 8)
            unreliable = bytearray(w.to_bytes())
            # the server checks the bytes after the checksum up to the last whole
            # byte of the commands, the move starting on a byte boundary
            unreliable[1] = block_sequence_crc_byte(unreliable[2:end_of_cmds // 8], sequence)
        packet = header + bytes(unreliable)
        if len(packet) < 16:
            packet += bytes([CLC_NOP]) * (16 - len(packet))
        self.sock.sendto(packet, self.server)
        if self.state == "ingame":
            self.sent_at[sequence] = time.monotonic()

        interval = 1.0 / self.cmdrate if self.state == "ingame" else 0.02
        self.next_send = max(self.next_send + interval, now - interval)
        return self.next_send


def percentile(values, p):
    if not values:
        return None
    s = sorted(values)
    return s[min(len(s) - 1, int(p * (len(s) - 1) + 0.5))]


def summarize(clients, duration):
    intervals = [v * 1000.0 for c in clients for v in c.intervals]
    per_client_rate = [len(c.intervals) / duration for c in clients if c.intervals]
    summary = {
        "clients": len(clients),
        "ingame": sum(1 for c in clients if c.state == "ingame"),
        "snapshots": len(intervals),
        "snapshot_rate_per_client": sum(per_client_rate) / len(per_client_rate) if per_client_rate else 0.0,
    }
    if intervals:
        avg = sum(intervals) / len(intervals)
        summary.update({
            "interval_ms_avg": avg,
            "interval_ms_p50": percentile(intervals, 0.50),
            "interval_ms_p95": percentile(intervals, 0.95),
            "interval_ms_p99": percentile(intervals, 0.99),
            "interval_ms_max": max(intervals),
            "interval_ms_stddev": math.sqrt(sum((v - avg) ** 2 for v in intervals) / len(intervals)),
        })
    latency = [v * 1000.0 for c in clients for v in c.input_latency]
    if latency:
        summary.update({
            "input_latency_ms_avg": sum(latency) / len(latency),
            "input_latency_ms_p50": percentile(latency, 0.50),
            "input_latency_ms_p95": percentile(latency, 0.95),
            "input_latency_ms_p99": percentile(latency, 0.99),
        })
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--server", default="127.0.0.1:27016")
    parser.add_argument("--clients", type=int, default=32)
    parser.add_argument("--cmdrate", type=int, default=100)
    parser.add_argument("--updaterate", type=int, default=100)
    parser.add_argument("--stagger", type=float, default=0.25, help="seconds between client connections")
    parser.add_argument("--warmup", type=float, default=10.0, help="seconds in game before measuring")
    parser.add_argument("--measure-at", type=float,
                        help="wall clock time (epoch seconds) at which measuring starts, instead of --warmup")
    parser.add_argument("--duration", type=float, default=30.0, help="measured seconds")
    parser.add_argument("--output", help="write the JSON summary to this file")
    parser.add_argument("--play", action="store_true",
                        help="join a team for real (close the message of the day first), so that "
                             "the clients spawn as players; without it they stay dead")
    args = parser.parse_args()

    host, port = args.server.rsplit(":", 1)
    server = (socket.gethostbyname(host), int(port))
    stats = {"ingame": 0, "failed": 0}
    clients = [Client(i + 1, server, args.cmdrate, args.updaterate, stats, args.play)
               for i in range(args.clients)]

    sel = selectors.DefaultSelector()
    for c in clients:
        sel.register(c.sock, selectors.EVENT_READ, c)

    start = time.monotonic()
    connect_times = [start + i * args.stagger for i in range(len(clients))]
    measure_start = None
    measure_end = None
    reset = False

    while True:
        now = time.monotonic()

        for i, c in enumerate(clients):
            if c.state == "idle" and now >= connect_times[i]:
                c.start(now)

        settled = stats["ingame"] + stats["failed"] == len(clients)
        if measure_start is None and args.measure_at is not None:
            measure_start = now + max(0.0, args.measure_at - time.time())
            measure_end = measure_start + args.duration
        if measure_start is None and (settled or now - start > 60.0):
            measure_start = now + args.warmup
            measure_end = measure_start + args.duration
            print(f"[loadclient] {stats['ingame']}/{len(clients)} clients in game, measuring "
                  f"{args.duration:.0f} s after {args.warmup:.0f} s warm-up", file=sys.stderr)
            if stats["ingame"] == 0 and args.measure_at is None:
                break
        if measure_start is not None and now >= measure_start and not reset:
            for c in clients:
                c.intervals = []
                c.input_latency = []
            reset = True
        if measure_end is not None and now >= measure_end:
            break

        next_times = [t for t in (c.tick(now) for c in clients) if t is not None]
        next_times += [t for i, t in enumerate(connect_times) if clients[i].state == "idle"]
        timeout = max(0.0, min(next_times) - time.monotonic()) if next_times else 0.05

        for key, _ in sel.select(min(timeout, 0.05)):
            c = key.data
            while True:
                try:
                    packet, _ = c.sock.recvfrom(65535)
                except BlockingIOError:
                    break
                c.on_packet(packet, time.monotonic())

    summary = summarize(clients, args.duration)
    text = json.dumps(summary, indent=2)
    print(text)
    if args.output:
        with open(args.output, "w") as f:
            f.write(text + "\n")
    return 0 if summary["ingame"] else 1


if __name__ == "__main__":
    sys.exit(main())
