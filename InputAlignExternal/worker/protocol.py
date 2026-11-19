"""IAX1 wire format. Must agree with include/Protocol.hpp (Windows x64 is little endian)."""
from __future__ import annotations

from dataclasses import dataclass
import math
import struct

MAGIC = 0x31584149
HELLO, FRAME, DECISION, STATUS = 1, 2, 3, 4
MINI, UPSIDE, GROUND, HELD, VALID = 16, 32, 64, 128, 256
HEADER = struct.Struct('<II5Q')
STATE = struct.Struct('<4dI')
FRAME_BODY = struct.Struct('<4dIQ4I')
DECISION_BODY = struct.Struct('<4dI4dIiII64f')
STATUS_BODY = struct.Struct('<I120s')
MODES = ['cube', 'ship', 'ball', 'ufo', 'wave', 'robot', 'spider', 'swing']


@dataclass(frozen=True)
class Header:
    kind: int
    hi: int
    lo: int
    session: int = 0
    tick: int = 0
    epoch: int = 0

    def pack(self, kind=None, tick=None):
        return HEADER.pack(MAGIC, self.kind if kind is None else kind, self.hi, self.lo,
                           self.session, self.tick if tick is None else tick, self.epoch)


def state_tuple(view: dict) -> tuple:
    flags = (MODES.index(view['mode']) | MINI * view['mini'] | UPSIDE * view['upside_down']
             | GROUND * view['on_ground'] | HELD * view['held'])
    return view['x'], view['y'], view['y_vel'], view['speed'], flags


def matches(actual, expected) -> bool:
    return (all(math.isfinite(x) for x in actual[:4] + expected[:4])
            and actual[4] & 255 == expected[4] & 255
            and all(abs(x - y) <= tol for x, y, tol in zip(actual[:4], expected[:4], (.1, .1, .05, .0001))))


def decode_frame(data: bytes, token: tuple[int, int]):
    if len(data) != HEADER.size + FRAME_BODY.size:
        raise ValueError('wrong packet size')
    magic, kind, hi, lo, session, tick, epoch = HEADER.unpack_from(data)
    if magic != MAGIC or kind != FRAME or (hi, lo) != token:
        raise ValueError('wrong protocol or token')
    x, y, vy, speed, flags, history, count, horizon, budget, options = FRAME_BODY.unpack_from(data, HEADER.size)
    if (not all(math.isfinite(v) for v in (x, y, vy, speed)) or flags & ~511 or flags & 15 > 7
            or count > 64 or count > tick or horizon > 400 or not 500 <= budget <= 20000
            or tick > 2**53 or session == 0 or options & ~1):
        raise ValueError('invalid frame fields')
    return Header(kind, hi, lo, session, tick, epoch), (x, y, vy, speed, flags), history, count, horizon, budget, options


def decision(header, tick, expected, due, delay, down, points):
    points = points[:32]
    flat = [v for xy in points for v in xy]
    return header.pack(DECISION, tick) + DECISION_BODY.pack(*expected, *due, delay, int(down), len(points),
                                                          *flat, *([0.] * (64 - len(flat))))


def status(header, code, text):
    return header.pack(STATUS) + STATUS_BODY.pack(code, text.encode('utf-8')[:119])
