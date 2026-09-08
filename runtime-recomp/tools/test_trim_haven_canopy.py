"""Read-only regression checks of the shipped Haven shaped-stem canopies."""
from collections import Counter
import json
from pathlib import Path
import struct
import unittest

ASSETS = Path(__file__).parents[2] / 'assets/models/palm'


def read_mesh(path):
    data = path.read_bytes()
    magic, nv, ni = struct.unpack_from('<8sII', data)
    assert magic == b'DKRPM001'
    vertices = [struct.unpack_from('<8f', data, 16+32*i) for i in range(nv)]
    indices = struct.unpack_from(f'<{ni}I', data, 16+32*nv)
    return [tuple(vertices[j] for j in indices[i:i+3]) for i in range(0, ni, 3)]


class HavenCanopyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        manifest = json.loads((ASSETS / 'manifest.json').read_text())
        assert manifest['canopy_preparation'] == 'haven-shaped-stem-recess-v1'
        cls.alignments = manifest['canopy_alignment']

    def test_leaf_and_outer_fruit_geometry_and_uv_preserved(self):
        for entry in self.alignments:
            lod = entry['lod']
            output = read_mesh(ASSETS / f'canopy-{lod}.dkrmesh')
            faces = {tuple(sorted(struct.pack('<3f', *v[:3]) for v in tri)): tri for tri in output}
            delta = entry['translation']
            preserved = 0
            for triangle in read_mesh(ASSETS / f'{lod}.dkrmesh'):
                # The shaded root recess is allowed to change, not outer
                # pineapple lobes, fronds, or the rest of the canopy.
                if not (all((v[0]+.006)**2+(v[2]-.008)**2 > .07**2 for v in triangle)
                        or all(v[1] > .62 for v in triangle)):
                    continue
                transformed = [(*(v[i]+delta[i] for i in range(3)), *v[3:]) for v in triangle]
                key = tuple(sorted(struct.pack('<3f', *v[:3]) for v in transformed))
                self.assertIn(key, faces, (lod, triangle))
                uvs = {struct.pack('<3f', *v[:3]): v[6:] for v in faces[key]}
                for vertex in transformed:
                    for original, actual in zip(vertex[6:], uvs[struct.pack('<3f', *vertex[:3])]):
                        self.assertAlmostEqual(original, actual, places=6)
                preserved += 1
            self.assertGreater(preserved, 15000 if lod == 'near' else 4500)

    def test_closed_consistently_oriented_surface(self):
        for entry in self.alignments:
            edges, winding = Counter(), Counter()
            for triangle in read_mesh(ASSETS / f"canopy-{entry['lod']}.dkrmesh"):
                points = [v[:3] for v in triangle]
                self.assertEqual(len(set(points)), 3)
                for a, b in zip(points, points[1:] + points[:1]):
                    edge = tuple(sorted((a, b)))
                    edges[edge] += 1
                    winding[edge] += 1 if a < b else -1
            self.assertTrue(all(count == 2 for count in edges.values()))
            self.assertTrue(all(value == 0 for value in winding.values()))

    def test_complete_stem_removed_and_pivot_preserved(self):
        for entry in self.alignments:
            self.assertEqual(entry['cut_center_after'], [-.010, .50, .0025])
            self.assertAlmostEqual(entry['translation'][1], -.11)
            delta = entry['translation']
            for triangle in read_mesh(ASSETS / f"canopy-{entry['lod']}.dkrmesh"):
                for vertex in triangle:
                    x, y, z = (vertex[i]-delta[i] for i in range(3))
                    if y < .61 - 1e-6:
                        # No surviving stalk inside the tapered cutter.
                        radius = .060 + (.035-.060) * (y+.1) / (.61+.1)
                        # Polygonal frustum's inscribed radius, plus float tolerance.
                        self.assertGreaterEqual(((x+.006)**2+(z-.008)**2)**.5,
                                                radius * .9978 - 1e-6)


if __name__ == '__main__':
    unittest.main()
