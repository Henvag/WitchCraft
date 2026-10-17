"""Minimal CRI USM reader/writer for Witcher 3 menu videos.

Witcher's menu backgrounds are Sofdec USM files whose @SFV stream is a plain
MPEG-1 elementary stream (VIDEO_HDRINFO mpeg_codec 1), one picture per chunk.
This module splits such a stream back out and wraps a new MPEG-1 stream in the
same container layout: CRID directory, VIDEO_HDRINFO header, VIDEO_SEEKINFO
metadata, one data chunk per picture and the three end markers. Remuxing the
original stream reproduces the original file byte for byte (see selftest()).
"""
import struct

# @UTF column types (low nibble) and their big-endian encodings.
_FMT = {0: '>B', 1: '>b', 2: '>H', 3: '>h', 4: '>I', 5: '>i', 6: '>Q', 7: '>q',
        8: '>f', 0xA: '>I', 0xB: '>II'}
_PER_ROW = 0x50
_CONST = 0x30

U8, S8, U16, S16, U32, S32, U64, S64, F32, STR = 0, 1, 2, 3, 4, 5, 6, 7, 8, 0xA


def parse_utf(b):
    """Returns (table name, [(column, type)], rows as dicts)."""
    if b[:4] != b'@UTF':
        raise ValueError('not an @UTF table')
    size = struct.unpack('>I', b[4:8])[0]
    t = b[8:8 + size]
    _, rows_off, str_off, data_off, name_off, ncol, rowlen, nrows = \
        struct.unpack('>HHIIIHHI', t[:24])

    def string(o):
        e = t.index(b'\0', str_off + o)
        return t[str_off + o:e].decode('latin1')

    p = 24
    cols = []
    for _ in range(ncol):
        flags = t[p]
        name = string(struct.unpack('>I', t[p + 1:p + 5])[0])
        p += 5
        kind, storage, const = flags & 0xF, flags & 0xF0, None
        if storage == _CONST:
            n = struct.calcsize(_FMT[kind])
            const = struct.unpack(_FMT[kind], t[p:p + n])
            p += n
        elif storage != _PER_ROW:
            raise ValueError('unsupported @UTF column storage %#x' % storage)
        cols.append((name, kind, storage, const))
    rows = []
    for r in range(nrows):
        q = rows_off + r * rowlen
        row = {}
        for name, kind, storage, const in cols:
            if storage == _PER_ROW:
                n = struct.calcsize(_FMT[kind])
                v = struct.unpack(_FMT[kind], t[q:q + n])
                q += n
            else:
                v = const
            row[name] = string(v[0]) if kind == STR else (v[0] if len(v) == 1 else v)
        rows.append(row)
    return string(name_off), [(c[0], c[1]) for c in cols], rows


def build_utf(name, cols, rows):
    """Builds an @UTF table with every column stored per row (as CRI does)."""
    strings = bytearray(b'<NULL>\0')
    offsets = {}

    def intern(s):
        if s not in offsets:
            offsets[s] = len(strings)
            strings.extend(s.encode('latin1') + b'\0')
        return offsets[s]

    name_off = intern(name)
    col_bytes = bytearray()
    for cname, kind in cols:
        col_bytes += bytes([_PER_ROW | kind]) + struct.pack('>I', intern(cname))
    row_bytes = bytearray()
    for row in rows:
        for cname, kind in cols:
            v = row[cname]
            row_bytes += struct.pack(_FMT[kind], intern(v) if kind == STR else v)
    rowlen = len(row_bytes) // max(1, len(rows))
    rows_off = 24 + len(col_bytes)
    str_off = rows_off + len(row_bytes)
    body = struct.pack('>HHIIIHHI', 0, rows_off, str_off, 0, name_off,
                       len(cols), rowlen, len(rows)) + col_bytes + row_bytes + strings
    body += b'\0' * (-len(body) % 8)
    body = body[:8] + struct.pack('>I', len(body)) + body[12:]  # data offset = end
    return b'@UTF' + struct.pack('>I', len(body)) + body


DATA, HEADER, END, METADATA = 0, 1, 2, 3


def _chunk(magic, kind, payload, frame_time=0, frame_rate=30, align=32):
    pad = -(8 + 0x18 + len(payload)) % align
    head = bytes([0, 0x18]) + struct.pack('>H', pad) + bytes([0, 0, 0, kind]) + \
        struct.pack('>II', frame_time, frame_rate) + b'\0' * 8
    return magic + struct.pack('>I', 0x18 + len(payload) + pad) + head + payload + b'\0' * pad


def read_chunks(data):
    """Yields (offset, magic, kind, frame_time, payload)."""
    p = 0
    while p + 8 <= len(data):
        size = struct.unpack('>I', data[p + 4:p + 8])[0]
        h = data[p + 8:p + 0x20]
        pad = struct.unpack('>H', h[2:4])[0]
        yield p, data[p:p + 4], h[7] & 3, struct.unpack('>I', h[8:12])[0], \
            data[p + 8 + h[1]:p + 8 + size - pad]
        p += 8 + size


def read_video(data):
    """Returns (header row, [picture payloads]) of a USM's @SFV stream."""
    header, pictures = None, []
    for _, magic, kind, _, payload in read_chunks(data):
        if magic != b'@SFV':
            continue
        if kind == HEADER:
            header = parse_utf(payload)[2][0]
        elif kind == DATA:
            pictures.append(payload)
    return header, pictures


def split_pictures(es):
    """Splits an MPEG-1 elementary stream into one unit per picture, keeping
    sequence and GOP headers with the picture that follows them."""
    starts = []
    i = es.find(b'\0\0\1')
    unit_start = None
    while i >= 0:
        code = es[i + 3]
        if code in (0xB3, 0xB8) and unit_start is None:
            unit_start = i
        elif code == 0x00:
            starts.append(i if unit_start is None else unit_start)
            unit_start = None
        i = es.find(b'\0\0\1', i + 3)
    return [es[a:b] for a, b in zip(starts, starts[1:] + [len(es)])]


def _is_keyframe(picture):
    i = picture.find(b'\0\0\1\0')
    return i >= 0 and (picture[i + 5] >> 3) & 7 == 1


_HDR_COLS = [('width', U32), ('height', U32), ('mat_width', U32), ('mat_height', U32),
             ('disp_width', U32), ('disp_height', U32), ('scrn_width', U32),
             ('mpeg_dcprec', U8), ('mpeg_codec', U8), ('alpha_type', U32),
             ('total_frames', U32), ('framerate_n', U32), ('framerate_d', U32),
             ('metadata_count', U32), ('metadata_size', U32), ('ixsize', U32),
             ('pre_padding', U32), ('max_picture_size', U32), ('color_space', U32),
             ('picture_type', U32)]
_CRID_COLS = [('fmtver', U32), ('filename', STR), ('filesize', U32), ('datasize', U32),
              ('stmid', U32), ('chno', U16), ('minchk', U16), ('minbuf', U32),
              ('avbps', U32)]
_SEEK_COLS = [('ofs_byte', U64), ('ofs_frmid', S32), ('num_skip', S16), ('resv', S16)]


def build_usm(pictures, width, height, fps_num=30000, fps_den=1000,
              name='menu_blicks_00001', dcprec=11):
    """Wraps MPEG-1 pictures (decode order) in a Witcher-style USM."""
    rate = 3000
    # CRI's own muxer stamps frame i at floor(i * 0.999 / fps) in 1/3000 s.
    times = [i * rate * fps_den * 999 // (fps_num * 1000) for i in range(len(pictures))]
    data_chunks = [_chunk(b'@SFV', DATA, p, t, rate) for p, t in zip(pictures, times)]
    keyframes = [i for i, p in enumerate(pictures) if _is_keyframe(p)]
    chunk_sizes = [len(c) for c in data_chunks]
    es_bytes = sum(len(p) for p in pictures)
    duration = len(pictures) * fps_den / fps_num
    avbps = int(es_bytes * 8 / duration)

    def header_chunk():
        row = dict(width=width, height=height, mat_width=width, mat_height=height,
                   disp_width=width, disp_height=height, scrn_width=0, mpeg_dcprec=dcprec,
                   mpeg_codec=1, alpha_type=0, total_frames=len(pictures),
                   framerate_n=fps_num, framerate_d=fps_den, metadata_count=1,
                   metadata_size=len(seek_chunk(0)), ixsize=max(chunk_sizes),
                   pre_padding=0, max_picture_size=0, color_space=0, picture_type=0)
        return _chunk(b'@SFV', HEADER, build_utf('VIDEO_HDRINFO', _HDR_COLS, [row]))

    def seek_chunk(base):
        offsets, o = [], base
        for i, size in enumerate(chunk_sizes):
            offsets.append(o)
            o += size
        rows = [dict(ofs_byte=offsets[k], ofs_frmid=k, num_skip=0, resv=0) for k in keyframes]
        return _chunk(b'@SFV', METADATA, build_utf('VIDEO_SEEKINFO', _SEEK_COLS, rows))

    def end(text):
        return _chunk(b'@SFV', END, text.ljust(31).encode('latin1')[:31] + b'\0')

    def crid(total):
        minbuf = max(sum(chunk_sizes[i:i + 3]) for i in range(len(chunk_sizes)))
        rows = [dict(fmtver=0, filename=name + '.usm', filesize=total, datasize=0, stmid=0,
                     chno=0xFFFF, minchk=1, minbuf=minbuf, avbps=avbps),
                dict(fmtver=0, filename=name + '.avi', filesize=es_bytes, datasize=0,
                     stmid=struct.unpack('>I', b'@SFV')[0], chno=0, minchk=3,
                     minbuf=max(len(p) for p in pictures), avbps=avbps)]
        return _chunk(b'CRID', HEADER, build_utf('CRIUSF_DIR_STREAM', _CRID_COLS, rows), align=2048)

    head_end = end('#HEADER END     ===============')
    meta_end = end('#METADATA END   ===============')
    contents_end = end('#CONTENTS END   ===============')
    crid_len = len(crid(0))
    base = crid_len + len(header_chunk()) + len(head_end)
    data_base = base + len(seek_chunk(0)) + len(meta_end)
    total = data_base + sum(chunk_sizes) + len(contents_end)
    return b''.join([crid(total), header_chunk(), head_end, seek_chunk(data_base), meta_end]
                    + data_chunks + [contents_end])


def _set_header(chunk, payload, align=32):
    """Rebuilds a chunk around a new payload, keeping its header fields."""
    h = chunk[8:0x20]
    pad = struct.unpack('>H', h[2:4])[0]
    if len(payload) == len(chunk) - 0x20 - pad:  # same size: keep CRI's own padding
        return chunk[:0x20] + payload + chunk[len(chunk) - pad:]
    return _chunk(chunk[:4], h[7] & 3, payload, *struct.unpack('>II', h[8:16]), align=align)


def replace_video(data, pictures):
    """Swaps the @SFV pictures of an existing USM, keeping every other chunk
    (audio, headers, end markers) and its interleaving and timing.

    The new stream must have as many pictures as the old one. Sizes, seek
    offsets and buffer hints are recomputed for the new pictures."""
    chunks = [(off, magic, kind, data[off:off + 8 + struct.unpack('>I', data[off + 4:off + 8])[0]])
              for off, magic, kind, _, _ in read_chunks(data)]
    video = [i for i, (_, m, k, _) in enumerate(chunks) if m == b'@SFV' and k == DATA]
    if len(video) != len(pictures):
        raise ValueError('picture count %d does not match the original %d' % (len(pictures), len(video)))
    out = [c[3] for c in chunks]
    for i, pic in zip(video, pictures):
        out[i] = _set_header(out[i], pic)

    def find(magic, kind):
        return next(i for i, (_, m, k, _) in enumerate(chunks) if m == magic and k == kind)

    crid_i, vhdr_i, seek_i = find(b'CRID', HEADER), find(b'@SFV', HEADER), find(b'@SFV', METADATA)
    keyframes = [k for k, pic in enumerate(pictures) if _is_keyframe(pic)]
    for _ in range(2):  # header sizes do not depend on the values, so two passes settle
        offsets, o = [], 0
        for c in out:
            offsets.append(o)
            o += len(c)
        total = o
        seek_rows = [dict(ofs_byte=offsets[video[k]], ofs_frmid=k, num_skip=0, resv=0) for k in keyframes]
        out[seek_i] = _set_header(out[seek_i], build_utf('VIDEO_SEEKINFO', _SEEK_COLS, seek_rows))
        _, cols, rows = parse_utf(chunks[vhdr_i][3][0x20:])
        duration = rows[0]['total_frames'] * rows[0]['framerate_d'] / rows[0]['framerate_n']
        sizes = [len(out[i]) for i in video]
        rows[0].update(total_frames=len(pictures), ixsize=max(sizes), metadata_size=len(out[seek_i]))
        out[vhdr_i] = _set_header(out[vhdr_i], build_utf('VIDEO_HDRINFO', cols, rows))
        _, cols, rows = parse_utf(chunks[crid_i][3][0x20:])
        for row in rows:
            if row['stmid'] == 0:
                row['filesize'] = total
                all_sizes = [len(c) for c in out]
                row['minbuf'] = max(row['minbuf'], max(sum(all_sizes[i:i + 3]) for i in range(len(all_sizes))))
            elif row['stmid'] == struct.unpack('>I', b'@SFV')[0]:
                row['filesize'] = sum(len(p) for p in pictures)
                row['minbuf'] = max(len(p) for p in pictures)
                row['avbps'] = int(row['filesize'] * 8 / duration)
        out[crid_i] = _set_header(out[crid_i], build_utf('CRIUSF_DIR_STREAM', cols, rows), align=2048)
    return b''.join(out)


def selftest(path):
    """Remuxes an existing USM and reports whether it is byte-identical."""
    data = open(path, 'rb').read()
    header, pictures = read_video(data)
    out = build_usm(pictures, header['width'], header['height'], header['framerate_n'],
                    header['framerate_d'], dcprec=header['mpeg_dcprec'])
    same = out == data
    if not same:
        n = next((i for i in range(min(len(out), len(data))) if out[i] != data[i]), None)
        print('first difference at', n, 'sizes', len(out), len(data))
    return same


if __name__ == '__main__':
    import sys
    print('identical' if selftest(sys.argv[1]) else 'different')
