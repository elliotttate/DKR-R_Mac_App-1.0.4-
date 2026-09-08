#!/usr/bin/env python3
"""Local contact sheets of unique level materials for the ROM-backed audit."""
import json
import sys
from pathlib import Path
from PIL import Image, ImageDraw

manifest = json.loads(Path(sys.argv[1]).read_text())
output = Path(sys.argv[2])
output.mkdir(parents=True, exist_ok=True)
records = {}
images = {}
for level in manifest['levels']:
    data = Path(level['file']).read_bytes()
    def u8(a): return data[(a & 0x7fffff) ^ 3]
    def u16(a): return u8(a) * 256 + u8(a+1)
    def u32(a): return u16(a) * 65536 + u16(a+2)
    textures = u32(int(level['model'], 16))
    for i, identity in enumerate(level['identities']):
        texture_id = identity['id']
        ptr = u32(textures + i * 8)
        w, h, fmt, flags = u8(ptr), u8(ptr+1), u8(ptr+2) & 15, u16(ptr+6)
        rec = records.setdefault(texture_id, dict(id=texture_id, surfaces=[], levels=[], triangles=0, flags=flags, format=fmt))
        rec['triangles'] += identity['triangles']
        for field, value in [('surfaces', identity['surface']), ('levels', level['level'])]:
            if value not in rec[field]: rec[field].append(value)
        if texture_id in images: continue
        im = Image.new('RGB', (w or 1, h or 1), (110, 0, 110))
        for y in range(h):
            for x in range(w):
                pixel = y*w + x
                if fmt == 1:
                    v = u16(ptr+32+(pixel*2 ^ (4 if y%2 else 0)))
                    rgb = ((v>>11)*255//31, ((v>>6)&31)*255//31, ((v>>1)&31)*255//31)
                elif fmt == 0:
                    off = pixel*4 ^ (8 if y%2 else 0)
                    rgb = tuple(u8(ptr+32+off+c) for c in range(3))
                else: continue
                im.putpixel((x, y), rgb)
        images[texture_id] = im

candidates = sorted([r for r in records.values() if not r['flags'] & 0x14 and
                     (any(s in [1, 2, 4, 10, 13] for s in r['surfaces']) or r['triangles'] >= 50)], key=lambda r:r['id'])
for start in range(0, len(candidates), 48):
    page = candidates[start:start+48]
    sheet = Image.new('RGB', (1200, ((len(page)+7)//8)*162), (28, 30, 34))
    draw = ImageDraw.Draw(sheet)
    for i, rec in enumerate(page):
        x, y = (i%8)*150, (i//8)*162
        sheet.paste(images[rec['id']].resize((140, 110), Image.Resampling.NEAREST), (x+5, y+3))
        draw.text((x+5, y+115), f"ID {rec['id']} s={rec['surfaces']}", fill='white')
        draw.text((x+5, y+130), f"{rec['triangles']} tris / {rec['flags']:04x}", fill='white')
        draw.text((x+5, y+144), f"Maps {str(rec['levels'])[:23]}", fill='white')
    sheet.save(output / f"materials-{start//48+1}.png")
(output / 'materials.json').write_text(json.dumps(candidates, indent=2)+'\n')
print(f"{len(candidates)} opaque terrain candidates / {(len(candidates)+47)//48} contact sheets")
