"""Check geometric seams independently of UV/normal vertex splits."""
from collections import Counter
from pathlib import Path
import json
import struct
import sys

for name in sys.argv[1:]:
    path = Path(name)
    data = path.read_bytes()
    magic, vertex_count, index_count = struct.unpack_from('<8sII', data)
    assert magic == b'DKRPM001'
    positions = [struct.unpack_from('<3f', data, 16 + 32*i) for i in range(vertex_count)]
    indices = struct.unpack_from('<%dI' % index_count, data, 16 + 32 * vertex_count)
    edges = Counter()
    winding = Counter()
    for i in range(0, index_count, 3):
        points = [positions[j] for j in indices[i:i+3]]
        for a, b in zip(points, points[1:] + points[:1]):
            if a != b:
                edges[tuple(sorted((a,b)))] += 1
                winding[tuple(sorted((a,b)))] += 1 if a < b else -1
    boundary = [edge for edge, count in edges.items() if count == 1]
    fruit = [edge for edge in boundary if all(0.53 < p[1] < 0.82 and p[0]*p[0]+p[2]*p[2] < 0.22**2 for p in edge)]
    overloaded = sum(count > 2 for count in edges.values())
    backwards = sum(count == 2 and winding[edge] != 0 for edge, count in edges.items())
    print(json.dumps({'mesh': path.name, 'triangles': index_count // 3,
                     'boundary_edges': len(boundary), 'fruit_boundary_edges': len(fruit),
                     'overconnected_edges': overloaded, 'inconsistent_edge_winding': backwards}))
    if fruit:
        raise SystemExit('Open seams in the fruit cluster')
    if overloaded or backwards:
        raise SystemExit('Non-manifold or inconsistent palm surface')
    if not path.name.startswith('canopy-') and boundary:
        raise SystemExit('Full palm must be closed')
    if path.name.startswith('canopy-') and any(abs(p[1] - .5) > 1e-6 for e in boundary for p in e):
        raise SystemExit('Canopy opening outside the intended trunk cut')
