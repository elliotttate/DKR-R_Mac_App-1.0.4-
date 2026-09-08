#!/usr/bin/env python3
"""Inspect locally captured DKR texture identities and terrain; no ROM assets ship."""
import argparse
import json
from pathlib import Path
from PIL import Image, ImageDraw

p = argparse.ArgumentParser()
p.add_argument('snapshot', type=Path)
p.add_argument('--model', type=lambda x: int(x, 16), required=True)
p.add_argument('--cache', type=lambda x: int(x, 16), required=True)
p.add_argument('--count', type=int, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
data = a.snapshot.read_bytes()
def u8(addr): return data[(addr & 0x7fffff) ^ 3]
def u16(addr): return (u8(addr) << 8) | u8(addr+1)
def u32(addr): return (u16(addr) << 16) | u16(addr+2)
def s16(addr):
    v = u16(addr)
    return v-65536 if v > 32767 else v

ids = {u32(a.cache+i*8+4): u32(a.cache+i*8) & 0x7fff for i in range(a.count)}
textures = u32(a.model)
nt, ns = u16(a.model+0x18), u16(a.model+0x1a)
records = []
images = []
for i in range(nt):
    ptr = u32(textures+i*8)
    w, h, fmt = u8(ptr), u8(ptr+1), u8(ptr+2)&15
    rec = dict(index=i, id=ids.get(ptr), surface=u8(textures+i*8+7),
               flags=u16(ptr+6), width=w, height=h, format=fmt, triangles=0)
    records.append(rec)
    image = Image.new('RGB', (w or 1,h or 1), (110,0,110))
    for y in range(h):
        for x in range(w):
            pixel = y*w+x
            if fmt==1:
                offset=pixel*2
                if y%2: offset^=4
                v=u16(ptr+32+offset)
                rgb=((v>>11)*255//31,((v>>6)&31)*255//31,((v>>1)&31)*255//31)
            elif fmt==0:
                offset=pixel*4
                if y%2: offset^=8
                rgb=tuple(u8(ptr+32+offset+c) for c in range(3))
            elif fmt in (2,3,4,5,6):
                if fmt in (3,6):
                    value=u8(ptr+32+pixel//2);value=(value>>(4 if pixel%2==0 else 0))&15
                    value=value*17 if fmt==3 else (value>>1)*255//7
                else:
                    value=u8(ptr+32+pixel*(2 if fmt==4 else 1))
                    if fmt==5:value=(value>>4)*17
                rgb=(value,value,value)
            else: continue
            image.putpixel((x,y),rgb)
    images.append(image)

segments=u32(a.model+4)
for si in range(ns):
    seg=segments+si*0x44;batches=u32(seg+12);nb=u16(seg+0x20)
    for bi in range(nb):
        b=batches+bi*12;ti=u8(b)
        if ti<nt:records[ti]['triangles']+=u16(b+16)-u16(b+4)
cols=6;cw=190;ch=198
sheet=Image.new('RGB',(cols*cw,((nt+cols-1)//cols)*ch),(28,30,34))
draw=ImageDraw.Draw(sheet)
for i,(im,rec) in enumerate(zip(images,records)):
    x=(i%cols)*cw;y=(i//cols)*ch
    sheet.paste(im.resize((160,145),Image.Resampling.NEAREST),(x+10,y+5))
    draw.text((x+10,y+153),f"ID {rec['id']} / slot {i} / surface {rec['surface']}",fill='white')
    draw.text((x+10,y+170),f"{rec['triangles']} tris / fmt {rec['format']} / {rec['flags']:04x}",fill='white')
a.output.mkdir(parents=True,exist_ok=True)
sheet.save(a.output/'materials.png')
(a.output/'materials.json').write_text(json.dumps(records,indent=2)+'\n')
print(json.dumps(dict(textures=nt,segments=ns,output=str(a.output/'materials.png'))))
