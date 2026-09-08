"""Repair the supplied Haven palm conservatively, preserving corner UVs.

Blender arguments: SOURCE.usdz FRESH_OUTPUT_DIR
First pass only: produces an editable scene and retains the original UVs.
Run reconstruct_haven_palm.py on this scene to resolve tangled leaf roots.
"""
import bpy
import bmesh
import hashlib
import json
from mathutils import Matrix, Vector
from pathlib import Path
import sys
import numpy as np

source, destination = map(Path, sys.argv[sys.argv.index('--') + 1:])
assert not destination.exists(), 'Use a fresh repair directory'
destination.mkdir(parents=True)
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
bpy.ops.wm.usd_import(filepath=str(source))
obj = next(o for o in bpy.context.scene.objects if o.type == 'MESH')
obj.data.transform(obj.matrix_world)
obj.parent = None
obj.matrix_world = Matrix.Identity(4)
minimum = min(v.co.z for v in obj.data.vertices)
height = max(v.co.z for v in obj.data.vertices) - minimum
for v in obj.data.vertices:
    v.co = Vector((v.co.x / height, v.co.y / height, (v.co.z - minimum) / height))
bm = bmesh.new()
bm.from_mesh(obj.data)
bmesh.ops.remove_doubles(bm, verts=list(bm.verts), dist=1e-6)

def counts():
    return {'vertices': len(bm.verts), 'faces': len(bm.faces),
            'boundary_edges': sum(e.is_boundary for e in bm.edges),
            'overconnected_edges': sum(len(e.link_faces) > 2 for e in bm.edges),
            'noncontiguous_edges': sum(e.is_manifold and not e.is_contiguous for e in bm.edges)}

report = {'source': source.name, 'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
          'before': counts()}
# Extra triangular fins have free edges while also sharing an already occupied
# edge with the actual surface. Removing just those faces closes those defects
# without moving the frond silhouette or filling intentional leaf notches.
removed_flaps = 0
while True:
    flaps = [f for f in bm.faces if any(e.is_boundary for e in f.edges) and
             any(len(e.link_faces) > 2 for e in f.edges)]
    if not flaps:
        break
    removed_flaps += len(flaps)
    bmesh.ops.delete(bm, geom=flaps, context='FACES')
report['removed_flap_faces'] = removed_flaps
report['after_flaps'] = counts()

material = obj.data.materials[0]
bsdf = next(n for n in material.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
image = bsdf.inputs['Base Color'].links[0].from_node.image
width, image_height = image.size
pixels = np.empty(width * image_height * 4, dtype=np.float32)
image.pixels.foreach_get(pixels)
pixels = pixels.reshape(image_height, width, 4)
uv_layer = bm.loops.layers.uv.active
def fruit(face):
    center = face.calc_center_median()
    if not (.46 < center.z < .76 and center.x**2 + center.y**2 < .22**2):
        return False
    uv = sum((loop[uv_layer].uv for loop in face.loops), Vector((0, 0))) / len(face.loops)
    r, g, b = pixels[min(image_height-1, max(0, int(uv.y * image_height))),
                     min(width-1, max(0, int(uv.x * width))), :3]
    return r > g * 1.06 and r > b * 1.30

fruit_faces = {f for f in bm.faces if fruit(f)}
fruit_vertices = {v for f in fruit_faces for v in f.verts
                  if all(face in fruit_faces for face in v.link_faces)}
before = {v: v.co.copy() for v in fruit_vertices}
# Bounded fairing removes local spikes, not the three fruit lobes. Constrain
# the seam to the surrounding leaves/trunk and retain every face-corner UV.
for iteration in range(5):
    positions = {}
    for v in fruit_vertices:
        adjacent = [e.other_vert(v) for e in v.link_edges]
        average = sum((p.co for p in adjacent), Vector()) / len(adjacent)
        next_position = v.co.lerp(average, .38)
        delta = next_position - before[v]
        if delta.length > .012:
            next_position = before[v] + delta.normalized() * .012
        positions[v] = next_position
    for v, p in positions.items():
        v.co = p
report['fruit_faces'] = len(fruit_faces)
report['smoothed_fruit_vertices'] = len(fruit_vertices)
report['maximum_fruit_displacement'] = max(((v.co - p).length for v, p in before.items()), default=0)
bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
report['after'] = counts()
bm.to_mesh(obj.data)
bm.free()
obj.data.update()
for polygon in obj.data.polygons:
    polygon.use_smooth = True
bpy.context.view_layer.objects.active = obj
bpy.ops.object.select_all(action='DESELECT')
obj.select_set(True)
bpy.ops.wm.save_as_mainfile(filepath=str(destination / 'repaired.blend'))
bpy.ops.export_scene.gltf(filepath=str(destination / 'repaired.glb'), export_format='GLB',
    use_selection=True, export_animations=False)
(destination / 'repair-report.json').write_text(json.dumps(report, indent=2) + '\n')
print('HAVEN_REPAIR', json.dumps(report), flush=True)
