"""Builds and installs the WitchCraft main-menu background.

The Hearts of Stone menu (movies\\gui\\menus\\main_menu_ep1.usm) shows Geralt
sitting against a wall. This tool removes him from every frame (exemplar fill
of a clean plate), renders the player's Minecraft skin sitting in his place
with a slow breathing loop, re-encodes the MPEG-1 stream and wraps it in the
original USM so the audio, timing and loop stay as they were.

The game ignores loose movie files and mods need a generated metadata.store,
so `install` writes the new video into the original slot of movies.bundle
(stored uncompressed) and updates that entry's size and CRC, after saving the
original bytes and entry; `restore` puts them back. Steam's file verification
also restores the original.

    python make_menu.py preview  [--skin PNG] [--out PNG]
    python make_menu.py build    [--skin PNG] [--out USM]
    python make_menu.py install  --video USM --backup DIR
    python make_menu.py restore  --backup DIR

Requires numpy, opencv-python, Pillow and ffmpeg on PATH.
"""
import argparse
import glob
import hashlib
import os
import subprocess
import sys
import zlib

import cv2
import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bundle  # noqa: E402
import steve  # noqa: E402
import usm  # noqa: E402
from inpaint import exemplar_inpaint  # noqa: E402

GAME = r'C:\Program Files (x86)\Steam\steamapps\common\The Witcher 3'
MOVIES = os.path.join(GAME, 'content', 'content0', 'bundles', 'movies.bundle')
ENTRY = r'movies\gui\menus\main_menu_ep1.usm'
ORIGINAL_SIZE = 45400832
ORIGINAL_CRC = 0x5525F8C5
WIDTH, HEIGHT, FRAMES = 2264, 1280, 263

# Geralt's silhouette in the ep1 menu video (pixels; he barely moves).
GERALT = [(1590, 215), (1660, 195), (1730, 215), (1775, 270), (1800, 330), (1850, 380),
          (1960, 420), (2030, 470), (2075, 560), (2090, 690), (2070, 780), (2010, 860),
          (1950, 900), (1960, 960), (1940, 1080), (1930, 1280), (1480, 1280), (1420, 1220),
          (1300, 1190), (1220, 1170), (1195, 1060), (1230, 990), (1330, 960), (1460, 940),
          (1500, 880), (1480, 760), (1510, 640), (1540, 520), (1560, 430), (1580, 350),
          (1575, 290)]

# Props that must not be copied into the filled background (they would show
# up twice): the gear and sword on the ground and the bottle by the wall.
PROPS = [(300, 820, 1240, 1180), (1940, 990, 2020, 1280)]

# Camera and placement of the seated player (orthographic, skin pixels).
YAW, PITCH, SCALE, ORIGIN, HEAD_YAW = -1.25, 0.12, 44.0, (1800.0, 1170.0), 0.65
CROP_X = 900  # the player is rendered into x >= CROP_X only
BREATH_PHASES = 24
BREATHS_PER_LOOP = 2  # whole cycles per video loop, so the loop stays seamless


def _unit(v):
    v = np.array(v, float)
    return v / np.linalg.norm(v)


# Camera-space lights: warm firelight from the right and behind, cool moonlight
# from the left and front, matching the scene.
LIGHTS = [(_unit([0.9, 0.35, -0.3]), np.array([1.0, 0.55, 0.28]) * 1.7),
          (_unit([-0.7, 0.5, 0.5]), np.array([0.30, 0.42, 0.70]) * 1.0)]
AMBIENT = np.array([0.22, 0.24, 0.32])


def find_skin():
    """The newest 64x64 skin in SkyCraft Prism's skin cache (the player's)."""
    root = os.path.join(os.environ.get('LOCALAPPDATA', ''), 'SkyCraft', 'Prism', 'assets', 'skins')
    best = None
    for path in glob.glob(os.path.join(root, '*', '*')):
        try:
            with Image.open(path) as im:
                if im.size != (64, 64):
                    continue
        except OSError:
            continue
        if best is None or os.path.getmtime(path) > os.path.getmtime(best):
            best = path
    if not best:
        raise SystemExit('no 64x64 skin found in %s; pass --skin' % root)
    return best


def original_video(backup=None):
    """The unmodified ep1 menu USM, from the bundle or from a backup."""
    data = bundle.read(MOVIES, ENTRY)
    if zlib.crc32(data) & 0xFFFFFFFF == ORIGINAL_CRC:
        return data
    if backup and os.path.exists(os.path.join(backup, 'main_menu_ep1.usm')):
        data = open(os.path.join(backup, 'main_menu_ep1.usm'), 'rb').read()
        if zlib.crc32(data) & 0xFFFFFFFF == ORIGINAL_CRC:
            return data
    raise SystemExit('the menu video in movies.bundle is modified and no original backup was given')


def decode_frames(usm_path):
    proc = subprocess.Popen(['ffmpeg', '-v', 'quiet', '-i', usm_path, '-an', '-frames:v', str(FRAMES),
                             '-f', 'rawvideo', '-pix_fmt', 'bgr24', '-'], stdout=subprocess.PIPE)
    size = WIDTH * HEIGHT * 3
    try:
        while True:
            buf = proc.stdout.read(size)
            if len(buf) < size:
                break
            yield np.frombuffer(buf, np.uint8).reshape(HEIGHT, WIDTH, 3)
    finally:
        if proc.poll() is None:
            proc.kill()
        proc.wait()


def render_player(skin, breath):
    pose = steve.Pose(breath)
    pose.head_yaw = HEAD_YAW
    view = steve.rot_x(PITCH) @ steve.rot_y(YAW)
    color, alpha = steve.render(skin, pose, view, (WIDTH - CROP_X, HEIGHT), SCALE,
                                (ORIGIN[0] - CROP_X, ORIGIN[1]), LIGHTS, AMBIENT)
    full_c = np.zeros((HEIGHT, WIDTH, 3), np.float32)
    full_a = np.zeros((HEIGHT, WIDTH), np.float32)
    full_c[:, CROP_X:] = color[..., ::-1]  # RGB -> BGR, premultiplied
    full_a[:, CROP_X:] = alpha
    return full_c, full_a


class Scene:
    def __init__(self, skin_path, original_path, cache_dir):
        skin = np.array(Image.open(skin_path).convert('RGBA'))
        if skin.shape[:2] != (64, 64):
            raise SystemExit('only 64x64 skins are supported')
        self.layers = [render_player(skin, float(np.sin(2 * np.pi * k / BREATH_PHASES)))
                       for k in range(BREATH_PHASES)]
        covered = np.min([a for _, a in self.layers], axis=0)  # covered in every phase
        geralt = np.zeros((HEIGHT, WIDTH), np.uint8)
        cv2.fillPoly(geralt, [np.array(GERALT, np.int32)], 255)
        geralt = cv2.dilate(geralt, np.ones((21, 21), np.uint8)) > 0
        inside = cv2.erode((covered > 0.5).astype(np.uint8), np.ones((15, 15), np.uint8)) > 0
        self.hole = geralt & ~inside
        feather = cv2.GaussianBlur(cv2.dilate(self.hole.astype(np.uint8) * 255, np.ones((7, 7), np.uint8)),
                                   (0, 0), 3).astype(np.float32) / 255.0
        self.plate_weight = np.maximum(feather, self.hole.astype(np.float32))[..., None]
        self.plate = self._clean_plate(original_path, geralt, cache_dir)
        rest = np.mean([a for _, a in self.layers], axis=0)
        shadow = cv2.GaussianBlur(np.roll(rest, 18, axis=0), (0, 0), 22)
        shadow[:int(ORIGIN[1]) - 140] = 0  # only on the ground around the seat
        self.shade = (1.0 - 0.5 * np.clip(shadow * 1.6, 0, 1))[..., None]

    def _clean_plate(self, original_path, geralt, cache_dir):
        forbid = geralt.copy()
        for x0, y0, x1, y1 in PROPS:
            forbid[y0:y1, x0:x1] = True
        key = hashlib.sha1(np.packbits(self.hole).tobytes() + np.packbits(forbid).tobytes()).hexdigest()[:12]
        cached = os.path.join(cache_dir, 'plate-%s.png' % key)
        if os.path.exists(cached):
            return cv2.imread(cached)
        frames = [f.astype(np.float32) for i, f in enumerate(decode_frames(original_path)) if i % 6 == 0]
        median = np.median(np.stack(frames), axis=0).astype(np.uint8)
        print('filling the background behind Geralt (a few minutes)...', flush=True)
        plate = exemplar_inpaint(median, self.hole, forbid)
        cv2.imwrite(cached, plate)
        return plate

    def compose(self, frame, index):
        phase = (index * BREATHS_PER_LOOP * BREATH_PHASES // FRAMES) % BREATH_PHASES
        color, alpha = self.layers[phase]
        out = frame.astype(np.float32) / 255.0
        out = out * (1 - self.plate_weight) + self.plate.astype(np.float32) / 255.0 * self.plate_weight
        out *= self.shade
        out = out * (1 - alpha[..., None]) + color
        return (np.clip(out, 0, 1) * 255 + 0.5).astype(np.uint8)


def encode(frames, out_m1v, bitrate):
    proc = subprocess.Popen(['ffmpeg', '-v', 'error', '-y', '-f', 'rawvideo', '-pix_fmt', 'bgr24',
                             '-s', '%dx%d' % (WIDTH, HEIGHT), '-r', '30', '-i', '-',
                             '-c:v', 'mpeg1video', '-b:v', bitrate, '-maxrate', '60M', '-bufsize', '40M',
                             '-g', '13', '-bf', '2', '-pix_fmt', 'yuv420p', '-f', 'mpeg1video', out_m1v],
                            stdin=subprocess.PIPE)
    for frame in frames:
        proc.stdin.write(frame.tobytes())
    proc.stdin.close()
    if proc.wait():
        raise SystemExit('ffmpeg failed')


def work_dir():
    d = os.path.join(os.environ.get('TEMP', '.'), 'witchcraft-menu')
    os.makedirs(d, exist_ok=True)
    return d


def cmd_preview(args):
    wd = work_dir()
    original = os.path.join(wd, 'original_ep1.usm')
    if not os.path.exists(original):
        open(original, 'wb').write(original_video(args.backup))
    scene = Scene(args.skin or find_skin(), original, wd)
    frame = next(decode_frames(original))
    cv2.imwrite(args.out, scene.compose(frame, 0))
    print(args.out)


def cmd_build(args):
    wd = work_dir()
    data = original_video(args.backup)
    original = os.path.join(wd, 'original_ep1.usm')
    open(original, 'wb').write(data)
    skin = args.skin or find_skin()
    print('skin:', skin)
    scene = Scene(skin, original, wd)
    m1v = os.path.join(wd, 'menu.m1v')
    for bitrate in ('34M', '28M', '22M'):
        encode((scene.compose(f, i) for i, f in enumerate(decode_frames(original))), m1v, bitrate)
        pictures = usm.split_pictures(open(m1v, 'rb').read())
        out = usm.replace_video(data, pictures)
        if len(out) <= ORIGINAL_SIZE:
            break
        print('%d bytes at %s is too large for the slot; lowering the bitrate' % (len(out), bitrate))
    else:
        raise SystemExit('could not fit the video into the original slot')
    open(args.out, 'wb').write(out)
    print('%s: %d pictures, %d bytes' % (args.out, len(pictures), len(out)))


def cmd_install(args):
    data = open(args.video, 'rb').read()
    os.makedirs(args.backup, exist_ok=True)
    saved = os.path.join(args.backup, 'main_menu_ep1.usm')
    saved_toc = os.path.join(args.backup, 'main_menu_ep1.toc')
    current = bundle.read(MOVIES, ENTRY)
    if zlib.crc32(current) & 0xFFFFFFFF == ORIGINAL_CRC:
        if not os.path.exists(saved):
            open(saved, 'wb').write(current)
            open(saved_toc, 'wb').write(bundle.toc_entry(MOVIES, ENTRY))
    elif not (os.path.exists(saved) and os.path.exists(saved_toc)):
        raise SystemExit('movies.bundle is already modified and %s has no original backup' % args.backup)
    original = open(saved, 'rb').read()
    if zlib.crc32(original) & 0xFFFFFFFF != ORIGINAL_CRC:
        raise SystemExit('the backup in %s is not the original video' % args.backup)
    bundle.write(MOVIES, ENTRY, data, ORIGINAL_SIZE)
    if bundle.read(MOVIES, ENTRY) != data:
        raise SystemExit('verification failed after writing; run restore')
    print('installed %d bytes; original saved in %s' % (len(data), args.backup))


def cmd_restore(args):
    saved = open(os.path.join(args.backup, 'main_menu_ep1.usm'), 'rb').read()
    toc = open(os.path.join(args.backup, 'main_menu_ep1.toc'), 'rb').read()
    if zlib.crc32(saved) & 0xFFFFFFFF != ORIGINAL_CRC:
        raise SystemExit('the backup is not the original video')
    bundle.restore(MOVIES, ENTRY, saved, toc)
    if zlib.crc32(bundle.read(MOVIES, ENTRY)) & 0xFFFFFFFF != ORIGINAL_CRC:
        raise SystemExit('restore verification failed')
    print('restored the original menu video')


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='cmd', required=True)
    for name in ('preview', 'build'):
        s = sub.add_parser(name)
        s.add_argument('--skin')
        s.add_argument('--backup', help='folder with the original video, if the bundle is already patched')
        s.add_argument('--out', default='menu-preview.png' if name == 'preview' else 'main_menu_ep1.usm')
    s = sub.add_parser('install')
    s.add_argument('--video', required=True)
    s.add_argument('--backup', required=True)
    s = sub.add_parser('restore')
    s.add_argument('--backup', required=True)
    args = p.parse_args()
    {'preview': cmd_preview, 'build': cmd_build, 'install': cmd_install, 'restore': cmd_restore}[args.cmd](args)


if __name__ == '__main__':
    main()
