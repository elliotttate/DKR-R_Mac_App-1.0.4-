"""Reconstruct a clean palm surface and reproject its original albedo in Blender.

Arguments: FIRST_PASS_REPAIRED.blend FRESH_OUTPUT_DIR
The input is retained. Thin leaves are sampled at 0.15% of tree height; a
bounded decimation yields a 16K-triangle surface with a new 2K UV atlas.
"""
import bpy
import bmesh
import json
from pathlib import Path
import sys

source, destination = map(Path, sys.argv[sys.argv.index('--') + 1:])
assert not destination.exists()
destination.mkdir(parents=True)
bpy.ops.wm.open_mainfile(filepath=str(source))
original = next(o for o in bpy.context.scene.objects if o.type == 'MESH')
original.name = 'Source texture projection'
target = original.copy()
target.data = original.data.copy()
bpy.context.collection.objects.link(target)
target.name = 'Repaired Haven palm'
bpy.ops.object.select_all(action='DESELECT')
target.select_set(True)
bpy.context.view_layer.objects.active = target
target.data.remesh_voxel_size = .0015
target.data.use_remesh_preserve_volume = True
bpy.ops.object.voxel_remesh()
target.data.calc_loop_triangles()
voxel_triangles = len(target.data.loop_triangles)
bm = bmesh.new()
bm.from_mesh(target.data)
remaining = set(bm.verts)
tiny = []
while remaining:
    pending = [remaining.pop()]
    group = set(pending)
    while pending:
        vertex = pending.pop()
        for edge in vertex.link_edges:
            other = edge.other_vert(vertex)
            if other in remaining:
                remaining.remove(other)
                group.add(other)
                pending.append(other)
    if len(group) < 20:
        tiny.extend(group)
if tiny:
    bmesh.ops.delete(bm, geom=tiny, context='VERTS')
bm.to_mesh(target.data)
bm.free()
target.data.calc_loop_triangles()
modifier = target.modifiers.new('Bounded clean mesh', 'DECIMATE')
modifier.ratio = min(1, 16000 / len(target.data.loop_triangles))
modifier.use_collapse_triangulate = True
bpy.ops.object.modifier_apply(modifier=modifier.name)
for face in target.data.polygons:
    face.use_smooth = True
bm = bmesh.new()
bm.from_mesh(target.data)
report = {'voxel_size': .0015, 'voxel_triangles': voxel_triangles,
    'removed_tiny_component_vertices': len(tiny), 'vertices': len(bm.verts),
    'triangles': sum(len(f.verts)-2 for f in bm.faces),
    'boundary_edges': sum(e.is_boundary for e in bm.edges),
    'overconnected_edges': sum(len(e.link_faces) > 2 for e in bm.edges),
    'noncontiguous_edges': sum(e.is_manifold and not e.is_contiguous for e in bm.edges)}
bm.free()
print('RECONSTRUCTED', json.dumps(report), flush=True)
assert report['boundary_edges'] == report['overconnected_edges'] == report['noncontiguous_edges'] == 0
bpy.ops.object.mode_set(mode='EDIT')
bpy.ops.mesh.select_all(action='SELECT')
bpy.ops.uv.smart_project(angle_limit=1.151917, island_margin=.008)
bpy.ops.object.mode_set(mode='OBJECT')

source_material = original.data.materials[0]
nodes, links = source_material.node_tree.nodes, source_material.node_tree.links
bsdf = next(n for n in nodes if n.type == 'BSDF_PRINCIPLED')
albedo = bsdf.inputs['Base Color'].links[0].from_socket
emission = nodes.new('ShaderNodeEmission')
links.new(albedo, emission.inputs['Color'])
output = next(n for n in nodes if n.type == 'OUTPUT_MATERIAL')
links.new(emission.outputs[0], output.inputs['Surface'])
material = bpy.data.materials.new('Reprojected original palm texture')
material.use_nodes = True
target.data.materials.clear()
target.data.materials.append(material)
image = bpy.data.images.new('Reprojected palm albedo', width=2048, height=2048, alpha=False)
texture = material.node_tree.nodes.new('ShaderNodeTexImage')
texture.image = image
material.node_tree.nodes.active = texture
scene = bpy.context.scene
scene.render.engine = 'CYCLES'
scene.cycles.samples = 16
scene.render.bake.margin = 8
original.select_set(True)
bpy.context.view_layer.objects.active = target
bpy.ops.object.bake(type='EMIT', use_selected_to_active=True, cage_extrusion=.008, max_ray_distance=.025)
material.node_tree.links.new(texture.outputs['Color'], material.node_tree.nodes['Principled BSDF'].inputs['Base Color'])
image.filepath_raw = str(destination / 'reprojected-albedo.png')
image.file_format = 'PNG'
image.save()
image.pack()
original.hide_render = True
original.hide_set(True)
original.select_set(False)
bpy.ops.wm.save_as_mainfile(filepath=str(destination / 'repaired.blend'))
bpy.ops.export_scene.gltf(filepath=str(destination / 'repaired.glb'), export_format='GLB',
    use_selection=True, export_animations=False)
(destination / 'reconstruction-report.json').write_text(json.dumps(report, indent=2) + '\n')
