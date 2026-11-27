"""Add the Latin glyphs French needs (accents, ligatures, typographic
punctuation) to a ttf2bin v3 font pack, rendered with the same method as
tools/ttf2bin.py so they match the existing glyphs.

Usage: python add_latin_glyphs.py <font.ttf> <in.bin> <out.bin>
"""
import struct
import sys

from PIL import Image, ImageDraw, ImageFont

HEADER = struct.Struct('<IBB64s64s')
ENTRY = struct.Struct('<HHBBbbII')

EXTRA = (list(range(0x00A0, 0x0180))            # Latin-1 Supplement + Latin Extended-A
         + [0x0178, 0x02C6, 0x02DC]
         + list(range(0x2010, 0x2027))          # dashes, quotes, bullet, ellipsis
         + [0x202F, 0x2039, 0x203A, 0x20AC, 0x2122])


def render(pil_font, u):
    ch = chr(u)
    width = int(pil_font.getlength(ch))
    bbox = pil_font.getbbox(ch)
    if not bbox or bbox[2] == bbox[0] or bbox[3] == bbox[1]:
        return (u, width, 0, 0, 0, 0), b''
    x0, y0, x1, y1 = bbox
    w, h = x1 - x0, y1 - y0
    img = Image.new('L', (w, h), 255)
    ImageDraw.Draw(img).text((-x0, -y0), ch, font=pil_font, fill=0)
    px = list(img.getdata())
    out = bytearray()
    for i in range(0, len(px), 4):
        q = [(px[i + k] >> 6) if i + k < len(px) else 3 for k in range(4)]
        out.append((q[0] << 6) | (q[1] << 4) | (q[2] << 2) | q[3])
    return (u, width, w, h, x0, y0), bytes(out)


def main(ttf, src, dst):
    data = open(src, 'rb').read()
    count, size, version, name, style = HEADER.unpack_from(data, 0)
    assert version == 3, version
    base = HEADER.size + count * ENTRY.size
    glyphs = {}
    for i in range(count):
        u, wd, bw, bh, xo, yo, off, sz = ENTRY.unpack_from(data, HEADER.size + i * ENTRY.size)
        glyphs[u] = ((u, wd, bw, bh, xo, yo), data[base + off: base + off + sz])

    pil_font = ImageFont.truetype(ttf, size)
    try:  # Noto Sans SC on Google Fonts is a variable font; the pack is Regular.
        pil_font.set_variation_by_axes([400])
    except Exception:
        pass
    # The existing pack was made with a different baseline convention: measure
    # the vertical shift on glyphs it already has and apply it to new ones.
    probes = [ord(c) for c in 'AEHMWaegx.,0' if ord(c) in glyphs]
    shifts = sorted(glyphs[u][0][5] - render(pil_font, u)[0][5] for u in probes)
    dy = shifts[len(shifts) // 2]
    widths_ok = sum(glyphs[u][0][1] == render(pil_font, u)[0][1] for u in probes)
    print(f'baseline shift {dy}, shifts {shifts}, advances matching {widths_ok}/{len(probes)}')
    assert shifts[0] == shifts[-1], 'inconsistent baseline shift: wrong font or size?'

    added = 0
    for u in EXTRA:
        if u in glyphs:
            continue
        (u, wd, bw, bh, xo, yo), bmp = render(pil_font, u)
        if wd == 0 and not bmp:
            continue
        glyphs[u] = ((u, wd, bw, bh, xo, yo + dy), bmp)
        added += 1

    entries, bitmaps, off = [], bytearray(), 0
    for u in sorted(glyphs):
        (u, wd, bw, bh, xo, yo), bmp = glyphs[u]
        entries.append(ENTRY.pack(u, wd, bw, bh, xo, yo, off, len(bmp)))
        bitmaps += bmp
        off += len(bmp)
    with open(dst, 'wb') as f:
        f.write(HEADER.pack(len(entries), size, version, name, style))
        f.write(b''.join(entries))
        f.write(bitmaps)
    print(f'{count} glyphs -> {len(entries)} (+{added})')


if __name__ == '__main__':
    main(*sys.argv[1:4])
