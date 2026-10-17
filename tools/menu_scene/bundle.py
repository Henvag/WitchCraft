"""Read and patch entries of Witcher 3 next-gen POTATO70 bundles.

The next-gen table of contents starts at 0x20 with 0x130-byte entries:
name[0x100], hash[16], u64 offset, u32 size, u32 stored size, u32 CRC32 of the
data, u32 compression (0 = stored), 8 unused bytes. Movies are stored
uncompressed, so a menu video can be replaced in place by a file no larger
than the original slot; the entry's size and CRC are updated to match.
"""
import os
import struct
import zlib

ENTRY = 0x130
HEADER = 0x20


def entries(path):
    """Yields (toc offset, name, data offset, size, stored size, crc, compression)."""
    with open(path, 'rb') as f:
        head = f.read(HEADER)
        if head[:8] != b'POTATO70':
            raise ValueError('%s is not a POTATO70 bundle' % path)
        toc_size = struct.unpack_from('<I', head, 0x10)[0]
        toc = f.read(toc_size)
    for o in range(0, len(toc) - ENTRY + 1, ENTRY):
        e = toc[o:o + ENTRY]
        name = e[:0x100].split(b'\0')[0].decode('latin1')
        if not name:
            continue
        offset, size, zsize, crc, comp = struct.unpack_from('<QIIII', e, 0x110)
        yield HEADER + o, name, offset, size, zsize, crc, comp


def find(path, name):
    for entry in entries(path):
        if entry[1].lower() == name.lower():
            return entry
    raise KeyError('%s not in %s' % (name, path))


def read(path, name):
    toc, _, offset, size, zsize, crc, comp = find(path, name)
    with open(path, 'rb') as f:
        f.seek(offset)
        data = f.read(zsize)
    if comp == 1:
        data = zlib.decompress(data)
    elif comp != 0:
        raise ValueError('compression %d is not supported' % comp)
    return data


def toc_entry(path, name):
    toc = find(path, name)[0]
    with open(path, 'rb') as f:
        f.seek(toc)
        return f.read(ENTRY)


def write(path, name, data, original_size):
    """Overwrites a stored entry in place. original_size is the slot size of
    the unmodified game file, which bounds what fits without moving data."""
    toc, _, offset, size, zsize, crc, comp = find(path, name)
    if comp != 0:
        raise ValueError('%s is compressed; only stored entries can be replaced' % name)
    if len(data) > original_size:
        raise ValueError('%s: %d bytes do not fit the %d-byte slot' % (name, len(data), original_size))
    with open(path, 'r+b') as f:
        f.seek(offset)
        f.write(data)
        f.seek(toc + 0x118)
        f.write(struct.pack('<III', len(data), len(data), zlib.crc32(data) & 0xFFFFFFFF))
        f.flush()
        os.fsync(f.fileno())


def restore(path, name, original, original_entry):
    """Puts back the original data and table-of-contents entry."""
    toc = find(path, name)[0]
    offset = struct.unpack_from('<Q', original_entry, 0x110)[0]
    with open(path, 'r+b') as f:
        f.seek(offset)
        f.write(original)
        f.seek(toc)
        f.write(original_entry)
        f.flush()
        os.fsync(f.fileno())
