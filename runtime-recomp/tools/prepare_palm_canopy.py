"""Derive canopy-only LODs from the prepared Meshy tree, retaining UVs.

python3 runtime-recomp/tools/prepare_palm_canopy.py assets/models/palm
Removes only the narrow central trunk below the fruit. Low, outlying frond
tips remain intact; a horizontal cut would incorrectly chop those leaves.
The cut surface is inside the retained retail trunk/coconut cluster.
"""
from pathlib import Path
import json
import struct
import sys

directory = Path(sys.argv[1])
align_cut = '--align-cut' in sys.argv[2:]
alignments = []
CUT_HEIGHT = 0.50  # Below the fruit; 0.66 intersected the central fruit shell.
for lod in ('near', 'far'):
    data = (directory / (lod + '.dkrmesh')).read_bytes()
    magic, count, index_count = struct.unpack_from('<8sII', data)
    assert magic == b'DKRPM001'
    vertices = [struct.unpack_from('<8f', data, 16 + i * 32) for i in range(count)]
    indices = struct.unpack_from('<%dI' % index_count, data, 16 + count * 32)
    retained, used, remap = [], [], {}
    for i in range(0, index_count, 3):
        triangle = indices[i:i + 3]
        points = [vertices[index] for index in triangle]
        trunk = all((p[0] + 0.015)**2 + p[2]**2 < 0.085**2 for p in points)
        if trunk:
            # Clip crossing triangles as well as removing low triangles. Simply
            # dropping triangles with all three vertices below the cut can
            # leave a long, thin strip of trunk attached to the canopy.
            clipped = []
            for a, b in zip(points, points[1:] + points[:1]):
                if a[1] >= CUT_HEIGHT:
                    clipped.append(a)
                if (a[1] >= CUT_HEIGHT) != (b[1] >= CUT_HEIGHT):
                    weight = (CUT_HEIGHT - a[1]) / (b[1] - a[1])
                    clipped.append(tuple(x + weight * (y - x) for x, y in zip(a, b)))
            points = clipped
        for fan in range(1, len(points) - 1):
            for vertex in (points[0], points[fan], points[fan + 1]):
                if vertex not in remap:
                    remap[vertex] = len(used)
                    used.append(vertex)
                retained.append(remap[vertex])
    if align_cut:
        ring = [v for v in used if abs(v[1] - CUT_HEIGHT) < 1e-6]
        assert len(ring) >= 3
        center_x = (min(v[0] for v in ring) + max(v[0] for v in ring)) / 2
        center_z = (min(v[2] for v in ring) + max(v[2] for v in ring)) / 2
        # Match the runtime's existing canopy attachment pivot. This moves
        # only the canopy asset, leaving full-tree ground placement unchanged.
        dx, dz = -.010 - center_x, .0025 - center_z
        assert abs(dx) < .06 and abs(dz) < .06
        used = [(v[0] + dx, v[1], v[2] + dz, *v[3:]) for v in used]
        alignments.append({'lod': lod, 'cut_center_before': [center_x, CUT_HEIGHT, center_z],
                           'translation': [dx, 0, dz], 'cut_center_after': [-.010, CUT_HEIGHT, .0025]})
    with (directory / ('canopy-' + lod + '.dkrmesh')).open('wb') as output:
        output.write(struct.pack('<8sII', magic, len(used), len(retained)))
        for vertex in used:
            output.write(struct.pack('<8f', *vertex))
        output.write(struct.pack('<%dI' % len(retained), *retained))
    print(lod, 'canopy vertices', len(used), 'triangles', len(retained) // 3)
if align_cut:
    path = directory / 'manifest.json'
    manifest = json.loads(path.read_text())
    manifest['canopy_alignment'] = alignments
    path.write_text(json.dumps(manifest, indent=2) + '\n')
