#!/usr/bin/env python3
"""Synthetic dual-frequency rover + nearby base. Standard library, macOS/Linux."""

import argparse
import asyncio
from math import cos, sin, sqrt, atan2, pi, dist, radians, asin
import os
import signal
import struct
import time

C, R, WE = 299792458.0, 26560000.0, 7.2921151467e-5
LAT, LON = map(radians, (45.28558, -75.93252))
N = 6378137 / sqrt(1 - 0.00669437999014 * sin(LAT) ** 2)
ROVER = (
    (N + 81.2) * cos(LAT) * cos(LON),
    (N + 81.2) * cos(LAT) * sin(LON),
    (N * (1 - 0.00669437999014) + 81.2) * sin(LAT),
)
BASE = tuple(a + b for a, b in zip(ROVER, (12.0, -15.0, 3.0)))
GPS = lambda: int(time.time()) - 315964800 + 18
WEEK, TOE = divmod(GPS(), 604800)


def frame(kind, data):
    body = bytes((0, kind, len(data))) + data
    return b"\x02" + body + bytes((sum(body) & 255, 3))


def rtcm(fields):
    bits = "".join(f"{n & ((1 << size) - 1):0{size}b}" for n, size in fields)
    bits += "0" * (-len(bits) % 8)
    payload = int(bits, 2).to_bytes(len(bits) // 8, "big")
    block = b"\xd3" + len(payload).to_bytes(2, "big") + payload
    crc = 0
    for b in block:
        crc ^= b << 16
        for _ in range(8):
            crc = ((crc << 1) ^ (0x1864CFB if crc & 0x800000 else 0)) & 0xFFFFFF
    return block + crc.to_bytes(3, "big")


def orbit(prn):
    # Construct well-distributed, circular test orbits above the antenna at TOE.
    az, el = radians(prn * 137.5), radians(18 + (prn * 23) % 65)
    e, n, u = sin(az) * cos(el), cos(az) * cos(el), sin(el)
    direction = (
        -sin(LON) * e - sin(LAT) * cos(LON) * n + cos(LAT) * cos(LON) * u,
        cos(LON) * e - sin(LAT) * sin(LON) * n + cos(LAT) * sin(LON) * u,
        cos(LAT) * n + sin(LAT) * u,
    )
    dot = sum(a * b for a, b in zip(ROVER, direction))
    d = sqrt(dot * dot + R * R - sum(x * x for x in ROVER)) - dot
    x, y, z = (a + d * b for a, b in zip(ROVER, direction))
    return atan2(y, x) - pi / 2 + WE * TOE, asin(z / R)


def eph(prn, week=None):
    node, inc = orbit(prn)
    return struct.pack(
        ">BBHHBBiii19dI",
        1,
        prn,
        WEEK - 1024 if week is None else week,
        1,
        0,
        1,
        TOE,
        TOE,
        TOE,
        0,
        0,
        0,
        0,
        0,
        0,
        0.5,
        0,
        0,
        0,
        sqrt(R),
        0,
        node / pi,
        0,
        inc / pi,
        0,
        0,
        0,
        0,
        0,
    )


def pseudorange(prn, t, receiver):
    node, inc = orbit(prn)
    d = 22000000.0
    for _ in range(3):
        dt = t - d / C - TOE
        a = pi / 2 + dt * sqrt(3.986005e14 / R**3)
        o = node - WE * (TOE + dt)
        xyz = (
            R * (cos(a) * cos(o) - sin(a) * cos(inc) * sin(o)),
            R * (cos(a) * sin(o) + sin(a) * cos(inc) * cos(o)),
            R * sin(a) * sin(inc),
        )
        d = dist(xyz, receiver) + WE / C * (xyz[0] * receiver[1] - xyz[1] * receiver[0])
    return d


def walk_position(seconds):
    east = min(seconds, 35) - 10
    return (ROVER[0] - sin(LON) * east, ROVER[1] + cos(LON) * east, ROVER[2])


def observations(t, receiver=ROVER):
    data = struct.pack(">ddB", t * 1000.0, 0.0, 9)
    for prn in range(1, 10):
        d = pseudorange(prn, t, receiver)
        data += struct.pack(
            ">BBBbhBddfBdfBBB",
            prn,
            0x71,
            4,
            45,
            0,
            180,
            d,
            -d * 1575.42e6 / C,
            0,
            180,
            -d * 1227.60e6 / C,
            0,
            1,
            0,
            0,
        )
    pages = [data[i : i + 210] for i in range(0, len(data), 210)]
    return b"".join(
        frame(0x57, bytes((0, 16 * i + len(pages), t % 256, 3)) + p)
        for i, p in enumerate(pages, 1)
    )


def corrections(t):
    station = rtcm(
        [
            (1005, 12),
            (42, 12),
            (0, 6),
            (8, 4),
            (round(BASE[0] * 10000), 38),
            (0, 2),
            (round(BASE[1] * 10000), 38),
            (0, 2),
            (round(BASE[2] * 10000), 38),
        ]
    )
    fields = [(1004, 12), (42, 12), (t * 1000, 30), (0, 1), (9, 5), (0, 4)]
    for prn in range(1, 10):
        d = pseudorange(prn, t, BASE)
        amb = int(d / (C * 0.001))
        coarse = round((d - amb * C * 0.001) / 0.02)
        phase = round((d - (amb * C * 0.001 + coarse * 0.02)) / 0.0005)
        fields.extend(
            [
                (prn, 6),
                (0, 1),
                (coarse, 24),
                (phase, 20),
                (90, 7),
                (amb, 8),
                (180, 8),
                (3, 2),
                (0, 14),
                (phase, 20),
                (90, 7),
                (180, 8),
            ]
        )
    return station + rtcm(fields)


async def main(port, walk):
    import pty
    import tty

    master, slave = pty.openpty()
    tty.setraw(slave)
    buf = bytearray()
    paused = False
    repeat = False
    corrupt = False
    old = 0
    first_epoch = None
    loop = asyncio.get_running_loop()

    def handle():
        nonlocal old, corrupt, first_epoch
        buf.extend(os.read(master, 4096))
        while buf:
            if buf[0] == 120:
                first_epoch = None
                del buf[0]
                out = frame(
                    0x6E,
                    b"PRODUCT,5700;PORT,0,38400,38400,8,1,N,F;VERSION,1.04,MOCK;COMM,DCOL,NMEA;SIMULATED;",
                )
            elif buf[0] == 5:
                del buf[0]
                out = b"\x06"
            elif buf[0] != 2:
                del buf[0]
                continue
            else:
                if len(buf) < 4 or len(buf) < buf[3] + 6:
                    return
                size = buf[3] + 6
                pkt = bytes(buf[:size])
                del buf[:size]
                kind = pkt[2]
                data = pkt[4:-2]
                if pkt != frame(kind, data):
                    out = b"\x15"
                elif kind == 0x54 and data == bytes(3):
                    out = frame(0x55, b"\0\0" + (511).to_bytes(4, "big") + bytes(20))
                elif kind == 0x54 and data[:1] == b"\1" and 1 <= data[1] <= 9:
                    out = frame(0x55, eph(data[1]))
                elif kind == 0x56 and data == b"\0\3\0":
                    if not repeat or not old:
                        old = GPS() % 604800
                    if first_epoch is None:
                        first_epoch = old
                    position = walk_position((old-first_epoch) % 604800) if walk else ROVER
                    out = observations(old, position)
                    if corrupt:
                        out = out[:-2] + b"\xff\x03"
                        corrupt = False
                else:
                    out = b"\x15"
            os.write(master, out[:3])
            loop.call_later(0.005, os.write, master, out[3:])

    async def client(reader, writer):
        try:
            request = await asyncio.wait_for(reader.readuntil(b"\r\n\r\n"), 5)
            if not request.startswith(b"GET /MOCK "):
                writer.write(b"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n")
                return
            writer.write(
                b"HTTP/1.1 200 OK\r\nContent-Type: gnss/data\r\nConnection: close\r\n\r\n"
            )
            while not reader.at_eof():
                if not paused:
                    writer.write(corrections(GPS() % 604800))
                    await writer.drain()
                await asyncio.sleep(0.5)
        except (ConnectionError, asyncio.IncompleteReadError, TimeoutError):
            pass
        finally:
            writer.close()

    def pause():
        nonlocal paused
        paused = not paused

    def freeze():
        nonlocal repeat
        repeat = not repeat

    def corrupt_next():
        nonlocal corrupt
        corrupt = True

    loop.add_signal_handler(signal.SIGUSR1, pause)
    loop.add_signal_handler(signal.SIGUSR2, freeze)
    loop.add_signal_handler(signal.SIGHUP, corrupt_next)
    server = await asyncio.start_server(client, "127.0.0.1", port)
    loop.add_reader(master, handle)
    print(os.ttyname(slave), flush=True)
    print(f"http://127.0.0.1:{server.sockets[0].getsockname()[1]}/MOCK", flush=True)
    print(
        f"Synthetic GNSS · PID {os.getpid()} · USR1 pause base, USR2 freeze epochs, HUP corrupt next reply",
        flush=True,
    )
    async with server:
        await server.serve_forever()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=2102)
    parser.add_argument(
        "--write-replay",
        metavar="FILE",
        help="Generate the deterministic built-in 90-second replay",
    )
    parser.add_argument("--walk", action="store_true", help="Generate west-to-east observations for the grading pad")
    args = parser.parse_args()
    if args.write_replay:
        WEEK, TOE = 2400, 201000 if args.walk else 200000
        with open(args.write_replay, "wb") as f:
            f.write(struct.pack(">HI", WEEK, TOE))

            def record(tag, data):
                f.write(struct.pack(">cH", tag, len(data)) + data)

            for prn in range(1, 10):
                record(b"E", frame(0x55, eph(prn, WEEK)))
            for t in range(TOE, TOE + (36 if args.walk else 90)):
                record(b"B", corrections(t))
                record(b"R", observations(t, walk_position(t-TOE) if args.walk else ROVER))
    else:
        try:
            asyncio.run(main(args.port, args.walk))
        except KeyboardInterrupt:
            pass
