"""Puts the WitchCraft logo into the Flash menu in place of the Witcher logo.

panel_ingamemenu.redswf (r4gui.bundle) is a CR2W file whose CSwfTexture
exports hold the menu's bitmaps as single-mip DXT5 data. The English
"The Witcher Wild Hunt Remastered" logo is the lower half of one 1024x1004
atlas. This tool draws the logo into that half, re-encodes the touched DXT5
blocks, and writes the result back with zlib level 9 (which reproduces the
shipped stream exactly), so the logo sits on the menu panel where the game
shows its own and follows the menu's own animations.

The bundle stores this file compressed with 11 spare bytes before the next
entry, so the patched stream must compress no larger than the original; the
unused Ukrainian logo in the same atlas is cleared if needed to make room.

    python swf_logo.py preview --out PNG
    python swf_logo.py install --backup DIR
    python swf_logo.py restore --backup DIR
"""
import argparse
import io
import os
import struct
import sys
import zlib

import cv2
import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bundle  # noqa: E402

GAME = r'C:\Program Files (x86)\Steam\steamapps\common\The Witcher 3'
GUI = os.path.join(GAME, 'content', 'content0', 'bundles', 'r4gui.bundle')
ENTRY = r'gameplay\gui_new\swf\mainmenu\panel_ingamemenu.redswf'
ORIGINAL_CRC = 0xAA1378D5  # of the uncompressed file
ORIGINAL_ZSIZE = 5179957
SLOT = ORIGINAL_ZSIZE + 11  # bytes before the next entry's data
LOGO = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'witchcraft_logo.png')

TEXTURE = 'panel-ingamemenu{6a4fe923}_i132.dds'
LOGO_AREA = (168, 524, 884, 980)    # the English logo's half of the atlas, cleared
LOGO_FIT = (192, 544, 840, 962)     # where the old logo drew; the new one fits inside
SPARE_AREA = (148, 80, 884, 424)    # the Ukrainian logo, cleared only if needed


def cr2w_textures(data):
    """Returns {linkage name: (offset of DXT data, width, height)}."""
    head = struct.calcsize('<4sIIQIIIII')
    tables = [struct.unpack_from('<III', data, head + 12 * i) for i in range(10)]
    strings = tables[0][0]

    def string(o):
        return data[strings + o:data.index(b'\0', strings + o)].decode('latin1')

    names = [string(struct.unpack_from('<I', data, tables[1][0] + 8 * i)[0]) for i in range(tables[1][1])]
    out = {}
    for i in range(tables[4][1]):
        cls, _, _, size, offset, _, _ = struct.unpack_from('<HHIIIII', data, tables[4][0] + 24 * i)
        if names[cls] != 'CSwfTexture':
            continue
        p, props = offset + 1, {}
        while True:
            name, kind = struct.unpack_from('<HH', data, p)
            if name == 0:
                p += 2
                break
            length = struct.unpack_from('<I', data, p + 4)[0]
            props[names[name]] = data[p + 8:p + 4 + length]
            p += 4 + length
        linkage = props['linkageName'][1:].decode('latin1')
        _, mips, w, h, pitch, size, _ = struct.unpack_from('<IIIIIII', data, p)
        if mips != 1 or size != w * h:
            raise ValueError('%s is not a single-mip DXT5 texture' % linkage)
        out[linkage] = (p + 28, w, h)
    return out


def decode_dxt5(raw, w, h):
    header = struct.pack('<4sIIIIIII44sII4sIIIIIIIIII', b'DDS ', 124, 0x81007, h, w, len(raw), 0, 1,
                         b'\0' * 44, 32, 4, b'DXT5', 0, 0, 0, 0, 0, 0x1000, 0, 0, 0, 0)
    return np.array(Image.open(io.BytesIO(header + raw)).convert('RGBA'))


def _565(rgb):
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    return ((r * 31 + 127) // 255 << 11) | ((g * 63 + 127) // 255 << 5) | ((b * 31 + 127) // 255)


def _unpack565(c):
    r, g, b = (c >> 11) & 31, (c >> 5) & 63, c & 31
    return np.stack([(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)], -1).astype(np.int32)


def encode_dxt5(rgba):
    """Bounding-box DXT5 encoder for an (H, W, 4) uint8 image, H and W
    multiples of 4. Returns the block bytes in row-major block order."""
    h, w = rgba.shape[:2]
    blocks = rgba.reshape(h // 4, 4, w // 4, 4, 4).transpose(0, 2, 1, 3, 4).reshape(-1, 16, 4).astype(np.int32)
    n = len(blocks)
    out = np.zeros((n, 16), np.uint8)

    # Alpha: 8-value interpolation between the block's max (a0) and min (a1).
    a = blocks[..., 3]
    a0, a1 = a.max(1), a.min(1)
    flat = a0 == a1
    a0 = np.where(flat & (a0 < 255), a0 + 1, a0)
    a1 = np.where(flat & (a0 == a1), a1 - 1, a1)
    steps = np.stack([a0, a1] + [((7 - k) * a0 + k * a1) // 7 for k in range(1, 7)], 1)
    ai = np.abs(a[:, :, None] - steps[:, None, :]).argmin(2)
    bits = np.zeros(n, np.uint64)
    for k in range(16):
        bits |= ai[:, k].astype(np.uint64) << np.uint64(3 * k)
    out[:, 0], out[:, 1] = a0, a1
    for k in range(6):
        out[:, 2 + k] = (bits >> np.uint64(8 * k)) & np.uint64(255)

    # Colour: endpoints from the RGB bounding box, four-colour mode.
    rgb = blocks[..., :3]
    c0, c1 = _565(rgb.max(1)), _565(rgb.min(1))
    same = c0 == c1
    c0 = np.where(same & (c0 < 0xFFFF), c0 + 1, c0)
    c1 = np.where(same & (c0 == c1), c1 - 1, c1)
    swap = c0 < c1
    c0, c1 = np.where(swap, c1, c0), np.where(swap, c0, c1)
    p0, p1 = _unpack565(c0), _unpack565(c1)
    palette = np.stack([p0, p1, (2 * p0 + p1) // 3, (p0 + 2 * p1) // 3], 1)
    ci = ((rgb[:, :, None, :] - palette[:, None, :, :]) ** 2).sum(3).argmin(2)
    cbits = np.zeros(n, np.uint32)
    for k in range(16):
        cbits |= ci[:, k].astype(np.uint32) << np.uint32(2 * k)
    out[:, 8:10] = np.stack([c0 & 255, c0 >> 8], 1)
    out[:, 10:12] = np.stack([c1 & 255, c1 >> 8], 1)
    out[:, 12:16] = np.stack([(cbits >> np.uint32(8 * k)) & np.uint32(255) for k in range(4)], 1)
    return out.tobytes()


def logo_rgba(width, height):
    """The logo with its black backdrop keyed out, fitted inside width x height."""
    img = cv2.cvtColor(cv2.imread(LOGO), cv2.COLOR_BGR2RGB).astype(np.float32) / 255.0
    scale = min(width / img.shape[1], height / img.shape[0])
    w, h = int(img.shape[1] * scale), int(img.shape[0] * scale)
    img = cv2.resize(img, (w, h), interpolation=cv2.INTER_AREA)
    alpha = np.clip((img.max(axis=2) - 0.07) / 0.18, 0, 1)
    # Unpremultiply what the black backdrop darkened, so edges stay clean.
    color = np.clip(img / np.maximum(alpha[..., None], 1e-3), 0, 1) * (alpha[..., None] > 0)
    out = np.zeros((height, width, 4), np.float32)
    x, y = (width - w) // 2, (height - h) // 2
    out[y:y + h, x:x + w, :3] = color
    out[y:y + h, x:x + w, 3] = alpha
    return (out * 255 + 0.5).astype(np.uint8)


def patched_texture(atlas, clear_spare):
    x0, y0, x1, y1 = LOGO_AREA
    atlas = atlas.copy()
    atlas[y0:y1, x0:x1] = 0
    fx0, fy0, fx1, fy1 = LOGO_FIT
    atlas[fy0:fy1, fx0:fx1] = logo_rgba(fx1 - fx0, fy1 - fy0)
    if clear_spare:
        sx0, sy0, sx1, sy1 = SPARE_AREA
        atlas[sy0:sy1, sx0:sx1] = 0
    return atlas


def build(original, clear_spare):
    data = bytearray(original)
    offset, w, h = cr2w_textures(original)[TEXTURE]
    atlas = decode_dxt5(bytes(original[offset:offset + w * h]), w, h)
    new = patched_texture(atlas, clear_spare)
    # Re-encode only the blocks that changed; keep the rest bit-exact.
    for x0, y0, x1, y1 in [LOGO_AREA] + ([SPARE_AREA] if clear_spare else []):
        bx0, by0, bx1, by1 = x0 // 4, y0 // 4, (x1 + 3) // 4, (y1 + 3) // 4
        encoded = encode_dxt5(new[by0 * 4:by1 * 4, bx0 * 4:bx1 * 4])
        row = (bx1 - bx0) * 16
        for k, by in enumerate(range(by0, by1)):
            start = offset + (by * (w // 4) + bx0) * 16
            data[start:start + row] = encoded[k * row:(k + 1) * row]
    return bytes(data)


def original_file(backup):
    data = bundle.read(GUI, ENTRY)
    if zlib.crc32(data) & 0xFFFFFFFF == ORIGINAL_CRC:
        return data
    saved = os.path.join(backup or '', 'panel_ingamemenu.redswf')
    if backup and os.path.exists(saved):
        data = zlib.decompress(open(saved, 'rb').read())
        if zlib.crc32(data) & 0xFFFFFFFF == ORIGINAL_CRC:
            return data
    raise SystemExit('panel_ingamemenu.redswf is modified and no original backup was given')


def compressed(original):
    for clear_spare in (False, True):
        packed = zlib.compress(build(original, clear_spare), 9)
        if len(packed) <= SLOT:
            return packed, clear_spare
    raise SystemExit('the patched menu does not fit its slot in r4gui.bundle')


def cmd_preview(args):
    original = original_file(args.backup)
    packed, cleared = compressed(original)
    data = zlib.decompress(packed)
    offset, w, h = cr2w_textures(data)[TEXTURE]
    Image.fromarray(decode_dxt5(data[offset:offset + w * h], w, h)).save(args.out)
    print('%s (%d bytes compressed, slot %d, spare logo cleared: %s)' % (args.out, len(packed), SLOT, cleared))


def cmd_install(args):
    os.makedirs(args.backup, exist_ok=True)
    saved = os.path.join(args.backup, 'panel_ingamemenu.redswf')
    saved_toc = os.path.join(args.backup, 'panel_ingamemenu.toc')
    current = bundle.read(GUI, ENTRY)
    if zlib.crc32(current) & 0xFFFFFFFF == ORIGINAL_CRC and not os.path.exists(saved):
        toc, _, offset, _, zsize, _, _ = bundle.find(GUI, ENTRY)
        with open(GUI, 'rb') as f:
            f.seek(offset)
            open(saved, 'wb').write(f.read(zsize))
        open(saved_toc, 'wb').write(bundle.toc_entry(GUI, ENTRY))
    original = original_file(args.backup)
    packed, cleared = compressed(original)
    data = zlib.decompress(packed)
    bundle.write_compressed(GUI, ENTRY, packed, len(data), zlib.crc32(data) & 0xFFFFFFFF, SLOT)
    if bundle.read(GUI, ENTRY) != data:
        raise SystemExit('verification failed after writing; run restore')
    print('installed the WitchCraft logo (%d bytes, spare logo cleared: %s)' % (len(packed), cleared))


def cmd_restore(args):
    packed = open(os.path.join(args.backup, 'panel_ingamemenu.redswf'), 'rb').read()
    toc = open(os.path.join(args.backup, 'panel_ingamemenu.toc'), 'rb').read()
    if zlib.crc32(zlib.decompress(packed)) & 0xFFFFFFFF != ORIGINAL_CRC:
        raise SystemExit('the backup is not the original menu')
    bundle.restore(GUI, ENTRY, packed, toc)
    print('restored the original menu logo')


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='cmd', required=True)
    s = sub.add_parser('preview')
    s.add_argument('--out', default='menu-logo-atlas.png')
    s.add_argument('--backup')
    for name in ('install', 'restore'):
        sub.add_parser(name).add_argument('--backup', required=True)
    args = p.parse_args()
    {'preview': cmd_preview, 'install': cmd_install, 'restore': cmd_restore}[args.cmd](args)


if __name__ == '__main__':
    main()
