"""Check prepared static plant topology against its imported source baseline."""
from collections import Counter
import json
from pathlib import Path
import struct
import sys

directory = Path(sys.argv[1])
manifest = json.loads((directory / 'manifest.json').read_text())
for lod in manifest['lods']:
    data = (directory / lod['file']).read_bytes()
    magic, count, index_count = struct.unpack_from('<8sII', data)
    assert magic == b'DKRPM001' and len(data) == 16+32*count+4*index_count
    positions = [struct.unpack_from('<3f', data, 16+32*i) for i in range(count)]
    indices = struct.unpack_from('<%dI' % index_count, data, 16+32*count)
    assert index_count//3 == lod['triangles'] and count == lod['vertices']
    edges = Counter()
    for i in range(0, index_count, 3):
        points = [positions[j] for j in indices[i:i+3]]
        for a,b in zip(points, points[1:]+points[:1]):
            if a != b:
                edges[tuple(sorted((a,b)))] += 1
    boundary = sum(value == 1 for value in edges.values())
    assert boundary <= manifest['source_boundary_edges'], 'Conversion introduced open seams'
    assert abs(min(p[1] for p in positions)) < 1e-5, 'Ground pivot moved'
    assert abs(max(p[1] for p in positions)-1) < 1e-5, 'Height normalization changed'
    print(json.dumps({'mesh': lod['file'], 'triangles': index_count//3,
        'boundary_edges': boundary, 'source_boundary_edges': manifest['source_boundary_edges']}))
if manifest['source_triangles'] <= 5000 or manifest.get('preserve_topology', False):
    assert (directory / 'near.dkrmesh').read_bytes() == (directory / 'far.dkrmesh').read_bytes()
    assert all(lod['triangles'] == manifest['source_triangles'] for lod in manifest['lods'])
