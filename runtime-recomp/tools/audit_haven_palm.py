"""Read-only structural audit and underside renders of the supplied Haven palm."""
import bpy
import bmesh
import json
import math
from mathutils import Matrix, Vector
from pathlib import Path
import sys

arguments = sys.argv[sys.argv.index('--') + 1:]
source, destination = map(Path, arguments[:2])
destination.mkdir(parents=True, exist_ok=True)
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
if source.suffix.lower() == '.glb':
    bpy.ops.import_scene.gltf(filepath=str(source))
else:
    bpy.ops.wm.usd_import(filepath=str(source))
obj = next(o for o in bpy.context.scene.objects if o.type == 'MESH')
obj.data.transform(obj.matrix_world)
obj.parent = None
obj.matrix_world = Matrix.Identity(4)
minimum = min(v.co.z for v in obj.data.vertices)
height = max(v.co.z for v in obj.data.vertices) - minimum
bm = bmesh.new()
bm.from_mesh(obj.data)
original_vertices = len(bm.verts)
bmesh.ops.remove_doubles(bm, verts=list(bm.verts), dist=height * 1e-6)
bm.verts.ensure_lookup_table()
bm.verts.index_update()
bm.faces.ensure_lookup_table()
bm.faces.index_update()
remaining = set(bm.verts)
components = []
while remaining:
    pending = [remaining.pop()]
    group = set(pending)
    while pending:
        v = pending.pop()
        for edge in v.link_edges:
            other = edge.other_vert(v)
            if other in remaining:
                remaining.remove(other)
                group.add(other)
                pending.append(other)
    faces = set(f for v in group for f in v.link_faces)
    edges = set(e for v in group for e in v.link_edges)
    bounds = [[min(v.co[i] for v in group) / height for i in range(3)],
              [max(v.co[i] for v in group) / height for i in range(3)]]
    for p in bounds:
        p[2] -= minimum / height
    components.append({'vertices': len(group), 'faces': len(faces), 'bounds': bounds,
        'boundary_edges': sum(e.is_boundary for e in edges),
        'overconnected_edges': sum(len(e.link_faces) > 2 for e in edges),
        'noncontiguous_edges': sum(e.is_manifold and not e.is_contiguous for e in edges)})
report = {'source': source.name, 'source_vertices': original_vertices,
    'welded_vertices': len(bm.verts), 'faces': len(bm.faces),
    'boundary_edges': sum(e.is_boundary for e in bm.edges),
    'wire_edges': sum(e.is_wire for e in bm.edges),
    'overconnected_edges': sum(len(e.link_faces) > 2 for e in bm.edges),
    'noncontiguous_edges': sum(e.is_manifold and not e.is_contiguous for e in bm.edges),
    'degenerate_faces': sum(f.calc_area() < height**2 * 1e-12 for f in bm.faces),
    'components': sorted(components, key=lambda c: c['faces'], reverse=True)}
face_keys = {}
duplicates = []
for face in bm.faces:
    key = tuple(sorted(v.index for v in face.verts))
    if key in face_keys:
        duplicates.append([face_keys[key], face.index])
    else:
        face_keys[key] = face.index
report['duplicate_faces'] = duplicates
report['problem_edges'] = [dict(faces=len(e.link_faces),
    center=[sum(v.co[i] for v in e.verts) / (2 * height) - (minimum / height if i == 2 else 0)
            for i in range(3)]) for e in bm.edges if len(e.link_faces) != 2]
(destination / 'audit.json').write_text(json.dumps(report, indent=2) + '\n')
print('HAVEN_AUDIT', json.dumps(report), flush=True)
bm.free()
if '--no-render' in arguments:
    raise SystemExit(0)
for v in obj.data.vertices:
    v.co = Vector((v.co.x / height, v.co.y / height, (v.co.z - minimum) / height))
scene = bpy.context.scene
scene.world.use_nodes = True
scene.world.node_tree.nodes['Background'].inputs['Color'].default_value = (.22, .22, .22, 1)
bpy.ops.object.light_add(type='AREA', location=(-2, -3, 3))
bpy.context.object.data.energy = 450
bpy.context.object.data.size = 3
bpy.ops.object.light_add(type='AREA', location=(2, 3, 2))
bpy.context.object.data.energy = 300
bpy.context.object.data.size = 3
bpy.ops.object.camera_add()
camera = bpy.context.object
scene.camera = camera
camera.data.type = 'ORTHO'
camera.data.ortho_scale = .58
scene.render.engine = 'CYCLES'
scene.cycles.samples = 16
scene.view_settings.view_transform = 'Standard'
scene.render.resolution_x = scene.render.resolution_y = 1100
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = 'PNG'
for yaw in (0, 90, 180):
    angle = math.radians(yaw)
    camera.location = Vector((3 * math.sin(angle), -3 * math.cos(angle), -.10))
    camera.rotation_euler = (Vector((0, 0, .58)) - camera.location).to_track_quat('-Z', 'Y').to_euler()
    scene.render.filepath = str(destination / f'fruit-{yaw}.png')
    bpy.ops.render.render(write_still=True)
