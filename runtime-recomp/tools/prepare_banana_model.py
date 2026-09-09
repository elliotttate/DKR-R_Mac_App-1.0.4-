"""Blender: prepare the supplied banana for the existing rigid-mesh renderer.

Arguments: SOURCE.usdz OUTPUT_DIR. Input is read-only. Animation is runtime
rotation, not baked mesh frames. This compact source is retained at both LODs
to avoid texture-seam artifacts from aggressive decimation.
"""
import bpy
import bmesh
import hashlib
import json
import math
import struct
import sys
from pathlib import Path
from mathutils import Matrix, Vector

source, destination = map(Path, sys.argv[sys.argv.index('--')+1:])
destination.mkdir(parents=True, exist_ok=True)
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
bpy.ops.wm.usd_import(filepath=str(source))
objects = [o for o in bpy.context.scene.objects if o.type == 'MESH']
assert len(objects) == 1
obj = objects[0]
obj.data.transform(obj.matrix_world)
obj.parent = None
obj.matrix_world = Matrix.Identity(4)
# Stand the fruit upright with the stem at the top. Preserve its curved
# silhouette; do not spin around the source's diagonal/lying-down axis.
obj.data.transform(Matrix.Rotation(math.pi, 4, 'Z') @ Matrix.Rotation(math.radians(-52), 4, 'Y'))
minimum = min(v.co.z for v in obj.data.vertices)
height = max(v.co.z for v in obj.data.vertices)-minimum
center_x = (min(v.co.x for v in obj.data.vertices)+max(v.co.x for v in obj.data.vertices))/2
center_y = (min(v.co.y for v in obj.data.vertices)+max(v.co.y for v in obj.data.vertices))/2
for vertex in obj.data.vertices:
    vertex.co = Vector(((vertex.co.x-center_x)/height, (vertex.co.y-center_y)/height,
                        (vertex.co.z-minimum)/height))
bm = bmesh.new()
bm.from_mesh(obj.data)
bmesh.ops.remove_doubles(bm, verts=list(bm.verts), dist=1e-6)
bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
boundary = sum(e.is_boundary for e in bm.edges)
assert not any(len(e.link_faces) > 2 for e in bm.edges)
bm.to_mesh(obj.data)
bm.free()
for polygon in obj.data.polygons:
    polygon.use_smooth = True
material = obj.data.materials[0]
nodes, links = material.node_tree.nodes, material.node_tree.links
bsdf = next(n for n in nodes if n.type == 'BSDF_PRINCIPLED')
albedo = bsdf.inputs['Base Color'].links[0].from_socket
normal = bsdf.inputs['Normal'].links[0].from_socket if bsdf.inputs['Normal'].is_linked else nodes.new('ShaderNodeNewGeometry').outputs['Normal']


def math_node(operation, a, b):
    node = nodes.new('ShaderNodeMath')
    node.operation = operation
    for i, value in enumerate((a,b)):
        if isinstance(value, (float,int)):
            node.inputs[i].default_value = value
        else:
            links.new(value, node.inputs[i])
    return node.outputs[0]


def dot(direction):
    node = nodes.new('ShaderNodeVectorMath')
    node.operation = 'DOT_PRODUCT'
    links.new(normal, node.inputs[0])
    node.inputs[1].default_value = Vector(direction).normalized()
    return math_node('MAXIMUM', node.outputs['Value'], 0)


front, back = dot((-.35,-.75,.7)), dot((.4,.7,.6))
lighting = math_node('ADD', .80, math_node('ADD', math_node('MULTIPLY', front, .30),
                                        math_node('MULTIPLY', back, .18)))
diffuse = nodes.new('ShaderNodeMixRGB')
diffuse.blend_type = 'MULTIPLY'
diffuse.inputs[0].default_value = 1
links.new(albedo, diffuse.inputs[1])
links.new(lighting, diffuse.inputs[2])
spec = math_node('MULTIPLY', math_node('MAXIMUM', math_node('POWER', front, 24),
                                   math_node('POWER', back, 24)), .08)
combined = nodes.new('ShaderNodeMixRGB')
combined.blend_type = 'ADD'
combined.inputs[0].default_value = 1
links.new(diffuse.outputs[0], combined.inputs[1])
links.new(spec, combined.inputs[2])
emission = nodes.new('ShaderNodeEmission')
links.new(combined.outputs[0], emission.inputs['Color'])
output = next(n for n in nodes if n.type == 'OUTPUT_MATERIAL')
links.new(emission.outputs[0], output.inputs['Surface'])
atlas = bpy.data.images.new('banana-wrap-bake', 1024, 1024, alpha=False)
target = nodes.new('ShaderNodeTexImage')
target.image = atlas
nodes.active = target
bpy.ops.object.select_all(action='DESELECT')
obj.select_set(True)
bpy.context.view_layer.objects.active = obj
bpy.context.scene.render.engine = 'CYCLES'
bpy.context.scene.cycles.samples = 16
bpy.context.scene.render.bake.margin = 8
bpy.ops.object.bake(type='EMIT')
texture_hash = '42414e414e413301'
atlas.filepath_raw = str((destination / f'{texture_hash}.png').resolve())
atlas.file_format = 'PNG'
atlas.save()
triangles = sum(len(p.vertices)-2 for p in obj.data.polygons)
manifest = {'source': source.name, 'source_sha256': hashlib.file_digest(source.open('rb'), 'sha256').hexdigest(),
            'source_triangles': triangles, 'source_boundary_edges': boundary,
            'texture': f'{texture_hash}.png', 'texture_size': [1024,1024],
            'coordinate_system': 'Y-up, upright, centered spin axis, bottom 0, height 1',
            'baked_lighting_profile': 'banana-gentle-wrap-v1', 'sprite_id': 156,
            'animation': 'guest animFrame low byte drives a world-space Y rotation', 'lods': []}
assert triangles <= 3000, 'Revisit the runtime budget for a different input model'
for name, budget in (('near', 3000), ('far', 3000)):
    decimate = obj.modifiers.new('distance-only LOD', 'DECIMATE')
    decimate.ratio = min(1, budget/triangles)
    decimate.use_collapse_triangulate = True
    graph = bpy.context.evaluated_depsgraph_get()
    evaluated = obj.evaluated_get(graph)
    mesh = evaluated.to_mesh(preserve_all_data_layers=True, depsgraph=graph)
    mesh.calc_loop_triangles()
    vertices, indices, remap = [], [], {}
    for triangle in mesh.loop_triangles:
        for loop in triangle.loops:
            v = mesh.vertices[mesh.loops[loop].vertex_index]
            p, n, uv = v.co, v.normal, mesh.uv_layers.active.data[loop].uv
            packed = struct.pack('<8f', p.x,p.z,-p.y,n.x,n.z,-n.y,uv.x,1-uv.y)
            if packed not in remap:
                remap[packed] = len(vertices)
                vertices.append(packed)
            indices.append(remap[packed])
    path = destination / f'{name}.dkrmesh'
    path.write_bytes(struct.pack('<8sII', b'DKRPM001', len(vertices), len(indices))+
                     b''.join(vertices)+struct.pack(f'<{len(indices)}I', *indices))
    manifest['lods'].append({'file': path.name, 'vertices': len(vertices), 'triangles': len(indices)//3})
    evaluated.to_mesh_clear()
    obj.modifiers.remove(decimate)
(destination / 'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
(destination / 'rt64.json').write_text(json.dumps({'configuration': {
    'configurationVersion':3, 'hashVersion':5, 'autoPath':'rt64',
    'defaultOperation':'preload', 'defaultShift':'none'}, 'textures':[{
    'hashes':{'rt64':texture_hash}, 'path':f'{texture_hash}.png',
    'operation':'preload', 'shift':'none'}]},indent=2)+'\n')
print('BANANA_PREPARED', json.dumps(manifest), flush=True)
