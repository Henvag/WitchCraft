"""Host stand-in for WitchCraft.

Creates the SkyCraft shared-memory link and a flat floor, then drives look and keys
until a real Witcher 3 plugin exists. Minecraft's Fabric mod (the one shipped with
SkyCraft) is the other side.

    python tools/standin.py --self-test
    python tools/standin.py
"""

import argparse
import ctypes
import struct
import sys
import time
from ctypes import wintypes

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
user32 = ctypes.WinDLL("user32", use_last_error=True)

kernel32.CreateFileMappingW.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.DWORD, wintypes.LPCWSTR]
kernel32.CreateFileMappingW.restype = wintypes.HANDLE
kernel32.MapViewOfFile.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.DWORD, wintypes.DWORD, ctypes.c_size_t]
kernel32.MapViewOfFile.restype = ctypes.c_void_p
kernel32.GetTickCount64.restype = ctypes.c_uint64
kernel32.GetCurrentProcessId.restype = wintypes.DWORD
user32.GetAsyncKeyState.argtypes = [ctypes.c_int]
user32.GetAsyncKeyState.restype = ctypes.c_short
user32.GetCursorPos.argtypes = [ctypes.POINTER(wintypes.POINT)]
user32.SetCursorPos.argtypes = [ctypes.c_int, ctypes.c_int]
user32.GetForegroundWindow.restype = wintypes.HWND
kernel32.GetConsoleWindow.restype = wintypes.HWND

FILE_MAP_ALL_ACCESS = 0xF001F
PAGE_READWRITE = 0x04
INVALID_HANDLE_VALUE = wintypes.HANDLE(-1).value

MAGIC = 0x43594B53
VERSION = 11
MAPPING_NAME = "Local\\SkyCraft_v1"
NO_WATER = -1.0e30

OFF_HEADER = 0x0
OFF_SKY = 0x100
OFF_MC = 0x200
OFF_WATER = 0x400
OFF_INPUT = 0x1000
OFF_ACTORS = 0x12000
OFF_EVENTS = 0x17000
OFF_COLLISION = 0x20000
COL_RING_BYTES = 32 << 20
OFF_OVERLAY = OFF_COLLISION + COL_RING_BYTES
OVERLAY_SLOT = 3840 * 2160 * 4
OFF_RENDER = OFF_OVERLAY + OVERLAY_SLOT * 3
RENDER_RING = 64 << 20
MAPPING_BYTES = OFF_RENDER + RENDER_RING

IN_KEY = 1
IN_MOUSE = 2
COL_CLEAR = 1
COL_REGION = 2
SKY_IN_GAME = 1
MC_FLYING = 1 << 7
EV_HIT = 1
EV_EXPLOSION = 3

INPUT_ENTRIES = 4096
EVENT_ENTRIES = 512
FLOOR_Y = 63
HALF = 8
EPOCH = 1
WORLD = 1


def poke(base, offset, data):
    ctypes.memmove(base + offset, data, len(data))


def peek(base, offset, size):
    return ctypes.string_at(base + offset, size)


def write_seqlock(base, offset, body):
    seq = struct.unpack_from("<I", peek(base, offset, 4))[0] & ~1
    poke(base, offset, struct.pack("<I", seq + 1))
    poke(base, offset + 4, body)
    poke(base, offset, struct.pack("<I", seq + 2))


def write_collision(base, msg_type, payload):
    head = struct.unpack_from("<Q", peek(base, OFF_COLLISION, 8))[0]
    tail = struct.unpack_from("<Q", peek(base, OFF_COLLISION + 0x40, 8))[0]
    size = COL_RING_BYTES - 0x80
    msg = (8 + len(payload) + 7) & ~7
    if msg > size // 2 or size - (head - tail) < msg:
        return False
    pos = head % size
    data = OFF_COLLISION + 0x80
    if pos + msg > size:
        poke(base, data + pos, struct.pack("<II", 0, 0))
        head += size - pos
        pos = 0
    poke(base, data + pos, struct.pack("<II", msg_type, len(payload)) + payload)
    poke(base, OFF_COLLISION, struct.pack("<Q", head + msg))
    return True


def solid_block(x, y, z):
    return struct.pack("<iiiI", x, y, z, 0) + (b"\xff" * 8) * 8


def publish_floor(base):
    if not write_collision(base, COL_CLEAR, struct.pack("<I", EPOCH)):
        return False
    count = (HALF * 2 + 1) ** 2
    region = struct.pack("<iiiiiiii", -HALF, 56, -HALF, HALF, 64, HALF, EPOCH, count)
    blocks = b"".join(solid_block(x, FLOOR_Y, z) for x in range(-HALF, HALF + 1) for z in range(-HALF, HALF + 1))
    return write_collision(base, COL_REGION, region + blocks)


def publish_water(base):
    body = struct.pack("<iiI", -8, -8, WORLD) + struct.pack("<" + "f" * 256, *([NO_WATER] * 256))
    write_seqlock(base, OFF_WATER, body)


def publish_sky(base, yaw, pitch):
    # seq is written by write_seqlock; body is the rest of SkyState (60 bytes, total 0x40).
    body = struct.pack(
        "<IIIdddffIIIf",
        SKY_IN_GAME,
        WORLD,
        EPOCH,
        0.5,
        64.0,
        0.5,
        yaw,
        pitch,
        1,
        1920,
        1080,
        14.0,
    )
    if len(body) != 60:
        raise RuntimeError("SkyState body is %s bytes" % len(body))
    write_seqlock(base, OFF_SKY, body)


def scancode(vk):
    if ord("A") <= vk <= ord("Z"):
        return 4 + vk - ord("A")
    if ord("1") <= vk <= ord("9"):
        return 30 + vk - ord("1")
    if vk == ord("0"):
        return 39
    return {0x20: 44, 0x10: 225, 0x11: 224, 0x1B: 41, 0x09: 43, 0x74: 62}.get(vk, 0)


def push_input(base, kind, code, a=0, b=0, c=0):
    head = struct.unpack_from("<Q", peek(base, OFF_INPUT, 8))[0]
    tail = struct.unpack_from("<Q", peek(base, OFF_INPUT + 0x40, 8))[0]
    if head - tail >= INPUT_ENTRIES:
        return
    slot = head & (INPUT_ENTRIES - 1)
    poke(base, OFF_INPUT + 0x80 + slot * 16, struct.pack("<HHiii", kind, code, a, b, c))
    poke(base, OFF_INPUT, struct.pack("<Q", head + 1))


def open_mapping():
    handle = kernel32.CreateFileMappingW(INVALID_HANDLE_VALUE, None, PAGE_READWRITE, 0, MAPPING_BYTES & 0xFFFFFFFF, MAPPING_NAME)
    if not handle:
        raise OSError("CreateFileMapping failed (%s)" % ctypes.get_last_error())
    view = kernel32.MapViewOfFile(handle, FILE_MAP_ALL_ACCESS, 0, 0, 0)
    if not view:
        raise OSError("MapViewOfFile failed (%s)" % ctypes.get_last_error())
    ctypes.memset(view, 0, MAPPING_BYTES)
    poke(view, OFF_HEADER, struct.pack("<IIIIQQ", MAGIC, VERSION, kernel32.GetCurrentProcessId(), 0, kernel32.GetTickCount64(), 0))
    return handle, view


def heartbeat(base):
    poke(base, OFF_HEADER + 16, struct.pack("<Q", kernel32.GetTickCount64()))


def mc_alive(base):
    beat = struct.unpack_from("<Q", peek(base, OFF_HEADER + 24, 8))[0]
    return beat != 0 and kernel32.GetTickCount64() - beat < 3000


def mc_pid(base):
    return struct.unpack_from("<I", peek(base, OFF_HEADER + 12, 4))[0]


def read_mc(base):
    raw = peek(base, OFF_MC, 0xC8)
    seq = struct.unpack_from("<I", raw)[0]
    if seq & 1:
        return None
    flags, x, y, z = struct.unpack_from("<I3d", raw, 4)
    return flags, x, y, z


def pop_event(base):
    head = struct.unpack_from("<Q", peek(base, OFF_EVENTS, 8))[0]
    tail = struct.unpack_from("<Q", peek(base, OFF_EVENTS + 0x40, 8))[0]
    if tail >= head:
        return None
    slot = tail & (EVENT_ENTRIES - 1)
    event = peek(base, OFF_EVENTS + 0x80 + slot * 32, 32)
    poke(base, OFF_EVENTS + 0x40, struct.pack("<Q", tail + 1))
    return struct.unpack("<IIffffII", event)


def self_test():
    assert MAPPING_BYTES > 180_000_000
    handle, view = open_mapping()
    try:
        magic, version = struct.unpack_from("<II", peek(view, 0, 8))
        assert magic == MAGIC and version == VERSION
        assert publish_floor(view)
        publish_water(view)
        publish_sky(view, 10.0, -5.0)
        sky = peek(view, OFF_SKY, 64)
        seq, flags, world, epoch, x, y, z, yaw, pitch = struct.unpack_from("<IIIIdddff", sky)
        assert seq % 2 == 0 and seq != 0
        assert flags == SKY_IN_GAME and world == WORLD and epoch == EPOCH
        assert abs(y - 64.0) < 1e-6 and abs(yaw - 10.0) < 1e-4
        head = struct.unpack_from("<Q", peek(view, OFF_COLLISION, 8))[0]
        assert head > 32
    finally:
        kernel32.UnmapViewOfFile(ctypes.c_void_p(view))
        kernel32.CloseHandle(handle)
    print("SELF_TEST_OK mapping %d MB" % (MAPPING_BYTES >> 20))


def run():
    handle, view = open_mapping()
    if not publish_floor(view):
        print("could not publish the floor", file=sys.stderr)
        return 1
    publish_water(view)
    write_seqlock(view, OFF_ACTORS, struct.pack("<I", 0) + bytes(0x40 - 8))
    print("WitchCraft stand-in. Flat floor, feet at y=64.")
    print("Start SkyCraft's Minecraft without Skyrim. Click this window to drive it. Ctrl+C quits.")
    yaw = 0.0
    pitch = 0.0
    was_alive = False
    down = {}
    last = wintypes.POINT()
    user32.GetCursorPos(ctypes.byref(last))
    next_log = 0.0
    keys = [ord(c) for c in "WASDEQF123456789"] + [0x20, 0x10, 0x11, 0x1B, 0x09, 0x74]
    try:
        while True:
            focused = user32.GetForegroundWindow() == kernel32.GetConsoleWindow()
            if focused:
                now = wintypes.POINT()
                user32.GetCursorPos(ctypes.byref(now))
                yaw -= (now.x - last.x) * 0.15
                pitch = max(-90.0, min(90.0, pitch + (now.y - last.y) * 0.15))
                user32.SetCursorPos(last.x, last.y)
            for vk in keys:
                pressed = focused and bool(user32.GetAsyncKeyState(vk) & 0x8000)
                if down.get(vk) == pressed:
                    continue
                down[vk] = pressed
                code = scancode(vk)
                if code:
                    push_input(view, IN_KEY, code, 1 if pressed else 0)
            if focused:
                for button, code in ((0x01, 1), (0x02, 3)):
                    pressed = bool(user32.GetAsyncKeyState(button) & 0x8000)
                    key = ("m", code)
                    if down.get(key) != pressed:
                        down[key] = pressed
                        push_input(view, IN_MOUSE, code, 1 if pressed else 0)
            publish_sky(view, yaw, pitch)
            heartbeat(view)
            alive = mc_alive(view)
            if alive and not was_alive:
                print("Minecraft connected (pid %s)" % mc_pid(view))
            if was_alive and not alive:
                print("Minecraft disconnected")
            was_alive = alive
            if alive and time.time() >= next_log:
                state = read_mc(view)
                if state:
                    flags, x, y, z = state
                    print("player %.2f %.2f %.2f%s" % (x, y, z, "  flying" if flags & MC_FLYING else ""))
                next_log = time.time() + 0.5
            while True:
                event = pop_event(view)
                if not event:
                    break
                kind, form_id, a, b, c, d, _flags, _weapon = event
                if kind == EV_EXPLOSION:
                    print("explosion at %.1f %.1f %.1f radius %.1f" % (a, b, c, d))
                elif kind == EV_HIT:
                    print("hit actor %s for %.1f" % (form_id, a))
            time.sleep(0.016)
    except KeyboardInterrupt:
        print("\nstopped")
    finally:
        kernel32.UnmapViewOfFile(ctypes.c_void_p(view))
        kernel32.CloseHandle(handle)
    return 0


def main():
    parser = argparse.ArgumentParser(description="WitchCraft host stand-in")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    return run()


if __name__ == "__main__":
    sys.exit(main())
