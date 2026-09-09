"""Offline bounds, topology, atlas and all-color coverage regression check."""
from collections import Counter
import json
import math
from pathlib import Path
import struct
import sys

directory=Path(sys.argv[1])
manifest=json.loads((directory/'manifest.json').read_text())
variants=manifest['variants']
assert [v['sprite_id'] for v in variants] == [147,148,149,150,151,154,155]
assert len({v['hash'] for v in variants}) == 7
assert not manifest['source_textures_used']
rt64=json.loads((directory/'rt64.json').read_text())
assert {v['path'] for v in rt64['textures']} == {v['texture'] for v in variants}
for v in variants:
    png=(directory/v['texture']).read_bytes()
    assert png[:8] == b'\x89PNG\r\n\x1a\n'
    assert struct.unpack_from('>II',png,16) == (2048,2048)
    assert int(v['hash'],16) == 0x42414C4C33440000+v['sprite_id']
for entry in manifest['meshes']:
    data=(directory/entry['file']).read_bytes()
    magic,count,n=struct.unpack_from('<8sII',data)
    assert magic == b'DKRPM001' and len(data)==16+count*32+n*4
    verts=[struct.unpack_from('<8f',data,16+i*32) for i in range(count)]
    indices=struct.unpack_from(f'<{n}I',data,16+count*32)
    assert count==entry['vertices'] and n//3==entry['triangles'] and n//3<=4000
    assert all(i<count for i in indices)
    assert all(math.isfinite(x) for v in verts for x in v)
    assert all(0<=v[6]<=1 and 0<=v[7]<=1 for v in verts)
    assert abs(min(v[1] for v in verts))<1e-5 and abs(max(v[1] for v in verts)-1)<1e-5
    assert abs(max(v[0] for v in verts)-min(v[0] for v in verts)-1)<1e-5
    edges=Counter()
    for i in range(0,n,3):
        points=[verts[j][:3] for j in indices[i:i+3]]
        assert len(set(points))==3,'degenerate triangle'
        for a,b in zip(points,points[1:]+points[:1]): edges[tuple(sorted((a,b)))]+=1
    assert not any(n>2 for n in edges.values()), 'nonmanifold geometry'
    boundary=sum(n==1 for n in edges.values())
    assert boundary<=manifest['source_boundary_edges'], 'new open mesh seam'
    print(entry['file'],n//3,'triangles;',boundary,'boundary edges')
for prefix in ('','collectible-'):
    assert (directory/f'{prefix}near.dkrmesh').read_bytes()==(directory/f'{prefix}far.dkrmesh').read_bytes()
print('Seven balloon finishes, optional-family meshes and stable LODs validated')
