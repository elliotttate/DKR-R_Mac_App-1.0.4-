"""Blender: render prepared plant meshes for topology/material QA.

Arguments: ASSET_DIR OUTPUT [YAW_DEGREES] [white] [COMPARISON_ASSET_DIR]
           [--canopy-join] [--far].
With a comparison directory, show its near mesh on the right; otherwise show
the first directory's far/canopy mesh. No runtime assets are modified.
--canopy-join compares attached canopies with schematic white trunk tips,
using sprite 116's aspect ratio and attachment overlap (not a game capture).
"""
import bpy
import json
from pathlib import Path
import struct
import sys
import math
from mathutils import Vector

arguments = sys.argv[sys.argv.index('--') + 1:]
canopy_join = '--canopy-join' in arguments
far = '--far' in arguments
arguments = [arg for arg in arguments if arg not in ('--canopy-join', '--far')]
directory, output = map(Path, arguments[:2])
yaw = float(arguments[2]) if len(arguments) > 2 else 0
white_background = len(arguments) > 3 and arguments[3] == 'white'
comparison_directory = Path(arguments[4]) if len(arguments) > 4 else None
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
def make_material(asset_directory):
    material = bpy.data.materials.new(asset_directory.name)
    material.use_nodes = True
    nodes = material.node_tree.nodes
    nodes.clear()
    image = nodes.new('ShaderNodeTexImage')
    manifest = json.loads((asset_directory / 'manifest.json').read_text())
    image.image = bpy.data.images.load(str(asset_directory / manifest['texture']))
    emission = nodes.new('ShaderNodeEmission')
    surface = nodes.new('ShaderNodeOutputMaterial')
    material.node_tree.links.new(image.outputs['Color'], emission.inputs['Color'])
    material.node_tree.links.new(emission.outputs[0], surface.inputs['Surface'])
    return material

second = 'canopy-near' if (directory / 'canopy-near.dkrmesh').exists() else 'far'
for x, asset_directory, name in [(-0.65, directory, 'near'),
        (0.65, comparison_directory or directory, 'near' if comparison_directory else second)]:
    if canopy_join:
        name = 'canopy-far' if far else 'canopy-near'
    data = (asset_directory / (name + '.dkrmesh')).read_bytes()
    magic, count, index_count = struct.unpack_from('<8sII', data)
    values = [struct.unpack_from('<8f', data, 16 + 32 * i) for i in range(count)]
    indices = struct.unpack_from('<%dI' % index_count, data, 16 + 32 * count)
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata([((p[0] + .010, -(p[2] - .0025), (p[1] - .50) * 60 / 78 + .50 - 3 / 78)
                      if canopy_join else (p[0], -p[2], p[1])) for p in values], [],
                     [indices[i:i+3] for i in range(0, index_count, 3)])
    uv = mesh.uv_layers.new()
    for polygon in mesh.polygons:
        for loop in polygon.loop_indices:
            v = values[mesh.loops[loop].vertex_index]
            uv.data[loop].uv = (v[6], 1-v[7])
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    obj.location.x = x
    obj.rotation_euler.z = math.radians(yaw)
    obj.data.materials.append(make_material(asset_directory))
    if canopy_join:
        # A tapered leaning tip stands in for the existing retail trunk.
        # Its top is y=.50; the actual canopy cut overlaps it by 3 game units.
        material = bpy.data.materials.new('schematic-white-trunk')
        material.diffuse_color = (.85, .85, .85, 1)
        verts = []
        for z, center_x, radius in ((.05, -.23, .036), (.50, 0, .028)):
            for j in range(12):
                a = math.tau * j / 12
                verts.append((center_x + radius * math.cos(a), radius * math.sin(a), z))
        trunk = bpy.data.meshes.new('schematic-trunk')
        trunk.from_pydata(verts, [], [(j, (j+1)%12, (j+1)%12+12, j+12) for j in range(12)]
                         + [tuple(range(12,24))])
        trunk_obj = bpy.data.objects.new('schematic-trunk', trunk)
        bpy.context.collection.objects.link(trunk_obj)
        trunk_obj.location.x = x
        trunk_obj.rotation_euler.z = math.radians(yaw)
        trunk_obj.data.materials.append(material)
world = bpy.context.scene.world
world.use_nodes = True
world.node_tree.nodes['Background'].inputs['Color'].default_value = ((1,1,1,1) if
    white_background else (0.8,0,0.6,1))
bpy.ops.object.camera_add(location=(0,-4,0.58))
camera = bpy.context.object
camera.rotation_euler = (Vector((0,0,0.6)) - camera.location).to_track_quat('-Z','Y').to_euler()
camera.data.type = 'ORTHO'
camera.data.ortho_scale = 2.5
if canopy_join:
    camera.location.z = .05
    camera.rotation_euler = (Vector((0,0,.52)) - camera.location).to_track_quat('-Z','Y').to_euler()
scene = bpy.context.scene
scene.camera = camera
scene.render.engine = 'CYCLES'
scene.cycles.samples = 8
scene.view_settings.view_transform = 'Standard'
scene.render.resolution_x = 1800
scene.render.resolution_y = 1000
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = 'PNG'
scene.render.filepath = str(output)
bpy.ops.render.render(write_still=True)
