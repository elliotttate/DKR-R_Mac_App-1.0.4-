"""Blender: prepare a static USDZ/GLB plant for the host scenery renderer.

blender -b --factory-startup --python prepare_static_plant.py -- SOURCE OUTPUT HASH
Output uses DKRPM001, a ground pivot, normalized height 1, Y-up and top-down UVs.
The original model and its textures are never modified.
"""
import bpy
import bmesh
import argparse
import hashlib
import json
from mathutils import Matrix
from pathlib import Path
import struct
import sys

parser = argparse.ArgumentParser()
parser.add_argument('source')
parser.add_argument('output')
parser.add_argument('texture_hash')
parser.add_argument('--preserve-topology', action='store_true')
parser.add_argument('--bake-lighting', action='store_true', help='Bake the native 3D material for the unlit N64 path')
parser.add_argument('--lighting-profile', choices=('rubber-tree', 'blueberry', 'beach-tree', 'tropical-palm', 'haven-palm'), default='rubber-tree')
args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:])
source, destination, texture_hash = Path(args.source), Path(args.output), args.texture_hash
assert len(texture_hash) == 16 and int(texture_hash, 16)
destination.mkdir(parents=True, exist_ok=True)
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
if source.suffix.lower() in ('.usd', '.usdc', '.usda', '.usdz'):
    bpy.ops.wm.usd_import(filepath=str(source))
else:
    bpy.ops.import_scene.gltf(filepath=str(source))
objects = [o for o in bpy.context.scene.objects if o.type == 'MESH']
assert len(objects) == 1, 'Expected one static mesh'
obj = objects[0]
assert not obj.animation_data and not obj.modifiers
assert len(obj.data.materials) == 1 and obj.data.uv_layers.active
obj.data.transform(obj.matrix_world)
obj.parent = None
obj.matrix_world = Matrix.Identity(4)
original_triangles = sum(len(p.vertices)-2 for p in obj.data.polygons)
minimum = min(v.co.z for v in obj.data.vertices)
maximum = max(v.co.z for v in obj.data.vertices)
height = maximum - minimum
assert height > 0

# USD/glTF split vertices at normal and UV seams. Weld geometric seams before
# decimation so they do not become cracks; corner UVs remain on their faces.
topology = bmesh.new()
topology.from_mesh(obj.data)
before_weld = len(topology.verts)
bmesh.ops.remove_doubles(topology, verts=list(topology.verts), dist=height*1e-6)
after_weld = len(topology.verts)
source_boundary_edges = sum(edge.is_boundary for edge in topology.edges)
topology.to_mesh(obj.data)
topology.free()
print('PLANT_WELD', before_weld, 'to', after_weld, flush=True)

material = obj.data.materials[0]
principled = next(n for n in material.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
link = principled.inputs['Base Color'].links
assert len(link) == 1 and link[0].from_node.type == 'TEX_IMAGE'
image = link[0].from_node.image
assert image is not None and image.size[0] and image.size[1]
atlas = destination / (texture_hash.lower() + '.png')
if args.bake_lighting:
    # The N64 sprite combiner cannot consume the supplied PBR normal/roughness
    # maps. Render a fixed, source-faithful material into the original UV layout
    # instead. This changes no mesh positions, UVs, collision, or other plants.
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from bake_plant_lighting import bake_plant_lighting, LIGHTING_PROFILES
    image = bake_plant_lighting(obj, material, image, height, args.lighting_profile)
image.filepath_raw = str(atlas.resolve())
image.file_format = 'PNG'
image.save()

with source.open('rb') as source_file:
    source_sha256 = hashlib.file_digest(source_file, 'sha256').hexdigest()
manifest = {'source': source.name,
    'source_sha256': source_sha256,
    'source_triangles': original_triangles, 'vertices_before_weld': before_weld,
    'vertices_after_weld': after_weld, 'texture': atlas.name,
    'source_boundary_edges': source_boundary_edges,
    'texture_size': list(image.size), 'coordinate_system': 'Y-up, ground pivot, height 1',
    'preserve_topology': args.preserve_topology,
    'baked_lighting': args.bake_lighting, 'lods': []}
if args.bake_lighting:
    manifest['baked_lighting_profile'] = LIGHTING_PROFILES[args.lighting_profile]
for name, target in [('near', 20000), ('far', 5000)]:
    modifier = obj.modifiers.new('Runtime reduction', 'DECIMATE')
    modifier.ratio = 1.0 if args.preserve_topology else min(1.0, target / original_triangles)
    modifier.use_collapse_triangulate = True
    graph = bpy.context.evaluated_depsgraph_get()
    evaluated = obj.evaluated_get(graph)
    mesh = evaluated.to_mesh(preserve_all_data_layers=True, depsgraph=graph)
    mesh.calc_loop_triangles()
    uv_layer = mesh.uv_layers.active.data
    vertices, indices, unique = [], [], {}
    for triangle in mesh.loop_triangles:
        for loop_index in triangle.loops:
            vertex = mesh.vertices[mesh.loops[loop_index].vertex_index]
            p, n, uv = vertex.co, vertex.normal, uv_layer[loop_index].uv
            fields = (p.x/height, (p.z-minimum)/height, -p.y/height,
                      n.x, n.z, -n.y, uv.x, 1.0-uv.y)
            packed = struct.pack('<8f', *fields)
            if packed not in unique:
                unique[packed] = len(vertices)
                vertices.append(packed)
            indices.append(unique[packed])
    assert len(vertices) <= 60000 and len(indices) <= 120000
    path = destination / (name + '.dkrmesh')
    with path.open('wb') as output:
        output.write(struct.pack('<8sII', b'DKRPM001', len(vertices), len(indices)))
        output.write(b''.join(vertices))
        output.write(struct.pack('<%dI' % len(indices), *indices))
    manifest['lods'].append({'file': path.name, 'vertices': len(vertices),
                            'triangles': len(indices)//3, 'bytes': path.stat().st_size})
    print('PLANT_LOD', manifest['lods'][-1], flush=True)
    evaluated.to_mesh_clear()
    obj.modifiers.remove(modifier)
(destination / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
(destination / 'rt64.json').write_text(json.dumps({'configuration': {
    'configurationVersion': 3, 'hashVersion': 5, 'autoPath': 'rt64',
    'defaultOperation': 'preload', 'defaultShift': 'none'}, 'textures': [{
    'hashes': {'rt64': texture_hash.lower()}, 'path': atlas.name,
    'operation': 'preload', 'shift': 'none'}]}, indent=2) + '\n')
print('PLANT_COMPLETE', destination, flush=True)
