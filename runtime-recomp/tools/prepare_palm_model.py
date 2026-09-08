"""Blender: prepare a UV-preserving, bounded host mesh; never changes the GLB.

blender -b --factory-startup --python runtime-recomp/tools/prepare_palm_model.py -- SOURCE.glb OUTPUT
The binary is little endian: DKRPM001, u32 vertex count, u32 index count,
vertices (8 floats: Y-up position, normal, UV), then u32 triangle indices.
Positions are ground-pivoted and normalized to height 1. UV V is top-down.
"""
import bpy
import bmesh
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile

source, destination = map(Path, sys.argv[sys.argv.index('--') + 1:])
destination.mkdir(parents=True, exist_ok=True)
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
bpy.ops.import_scene.gltf(filepath=str(source))
objects = [o for o in bpy.context.scene.objects if o.type == 'MESH']
assert len(objects) == 1, 'Expected a single static mesh'
obj = objects[0]
bpy.context.view_layer.objects.active = obj
obj.select_set(True)
assert not obj.modifiers and not obj.animation_data
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
original_triangles = sum(len(p.vertices) - 2 for p in obj.data.polygons)
minimum = min(v.co.z for v in obj.data.vertices)
maximum = max(v.co.z for v in obj.data.vertices)
height = maximum - minimum
# glTF splits vertices at UV/normal seams. Collapsing those disconnected edges
# independently opens cracks between the fruit's scales. Weld coincident
# positions before simplification; UVs stay on face corners, so the atlas is
# preserved. This changes topology, not the shape or original input file.
topology = bmesh.new()
topology.from_mesh(obj.data)
before_weld = len(topology.verts)
bmesh.ops.remove_doubles(topology, verts=list(topology.verts), dist=height * 0.000001)
print('PALM_WELD', before_weld, 'to', len(topology.verts), flush=True)
topology.to_mesh(obj.data)
topology.free()
# Extract the original embedded JPEG without resampling or changing its color.
raw = source.read_bytes()
json_size = struct.unpack_from('<I', raw, 12)[0]
document = json.loads(raw[20:20 + json_size])
binary_start = 20 + json_size + 8
base_index = document['materials'][0]['pbrMetallicRoughness']['baseColorTexture']['index']
view = document['bufferViews'][document['images'][document['textures'][base_index]['source']]['bufferView']]
offset = binary_start + view.get('byteOffset', 0)
atlas = destination / '50414c4d33440001.png'
# RT64 accepts PNG/DDS. Decode the source JPEG once, retaining its original
# resolution and color pixels (no AI generation, baking, or resampling).
with tempfile.TemporaryDirectory(prefix='dkr-palm-atlas-') as temporary:
    embedded = Path(temporary) / 'base.jpg'
    embedded.write_bytes(raw[offset:offset + view['byteLength']])
    image = bpy.data.images.load(str(embedded))
    image.filepath_raw = str(atlas.resolve())
    image.file_format = 'PNG'
    image.save()
    bpy.data.images.remove(image)

manifest = {'source': source.name, 'source_sha256': hashlib.sha256(raw).hexdigest(),
            'source_triangles': original_triangles, 'texture': atlas.name,
            'coordinate_system': 'Y-up, ground pivot, height 1', 'lods': []}
for name, target in [('near', 20000), ('far', 5000)]:
    modifier = obj.modifiers.new('Runtime reduction', 'DECIMATE')
    modifier.ratio = target / original_triangles
    modifier.use_collapse_triangulate = True
    evaluated = obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
    mesh = evaluated.to_mesh(preserve_all_data_layers=True, depsgraph=bpy.context.evaluated_depsgraph_get())
    mesh.calc_loop_triangles()
    uv_layer = mesh.uv_layers.active.data
    vertices, indices, unique = [], [], {}
    for triangle in mesh.loop_triangles:
        for loop_index in triangle.loops:
            vertex = mesh.vertices[mesh.loops[loop_index].vertex_index]
            p, n, uv = vertex.co, vertex.normal, uv_layer[loop_index].uv
            # Blender Z-up to glTF Y-up; Blender already inverted glTF UV V.
            value = (p.x / height, (p.z - minimum) / height, -p.y / height,
                     n.x, n.z, -n.y, uv.x, 1.0 - uv.y)
            packed = struct.pack('<8f', *value)
            if packed not in unique:
                unique[packed] = len(vertices)
                vertices.append(packed)
            indices.append(unique[packed])
    path = destination / (name + '.dkrmesh')
    with path.open('wb') as output:
        output.write(struct.pack('<8sII', b'DKRPM001', len(vertices), len(indices)))
        output.write(b''.join(vertices))
        output.write(struct.pack('<%dI' % len(indices), *indices))
    manifest['lods'].append({'file': path.name, 'vertices': len(vertices),
                             'triangles': len(indices) // 3, 'bytes': path.stat().st_size})
    print('PALM_LOD', manifest['lods'][-1], flush=True)
    evaluated.to_mesh_clear()
    obj.modifiers.remove(modifier)
(destination / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
(destination / 'rt64.json').write_text(json.dumps({'configuration': {
    'configurationVersion': 3, 'hashVersion': 5, 'autoPath': 'rt64',
    'defaultOperation': 'preload', 'defaultShift': 'none'}, 'textures': [{
        'hashes': {'rt64': '50414c4d33440001'}, 'path': atlas.name,
        'operation': 'preload', 'shift': 'none'}]}, indent=2) + '\n')
print('PALM_COMPLETE', destination, flush=True)
