"""Blender: inspect a supplied static GLB/USDZ and render its original material.

blender -b --factory-startup --python inspect_static_plant.py -- INPUT OUTPUT.png
"""
import bpy
import json
from mathutils import Vector
from pathlib import Path
import sys

source, output = sys.argv[sys.argv.index('--') + 1:]
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
if Path(source).suffix.lower() in ('.usd', '.usdc', '.usda', '.usdz'):
    bpy.ops.wm.usd_import(filepath=source)
else:
    bpy.ops.import_scene.gltf(filepath=source)
objects = [o for o in bpy.context.scene.objects if o.type == 'MESH']
points = [o.matrix_world @ Vector(v) for o in objects for v in o.bound_box]
print('PLANT_SOURCE', json.dumps([{'name': o.name, 'vertices': len(o.data.vertices),
    'triangles': sum(len(p.vertices)-2 for p in o.data.polygons),
    'materials': [m.name for m in o.data.materials],
    'uv_layers': [u.name for u in o.data.uv_layers]} for o in objects]), flush=True)
for material in bpy.data.materials:
    if material.use_nodes:
        print('PLANT_MATERIAL', material.name, [(n.name, n.type,
            n.image.name if n.type == 'TEX_IMAGE' and n.image else '')
            for n in material.node_tree.nodes], flush=True)
minimum = Vector(tuple(min(p[i] for p in points) for i in range(3)))
maximum = Vector(tuple(max(p[i] for p in points) for i in range(3)))
center = (minimum + maximum) / 2
size = max(maximum-minimum)
print('PLANT_BOUNDS', list(minimum), list(maximum), flush=True)
bpy.ops.object.camera_add(location=center + Vector((1.8,-4,1.0))*size)
camera = bpy.context.object
camera.rotation_euler = (center-camera.location).to_track_quat('-Z','Y').to_euler()
camera.data.type = 'ORTHO'
camera.data.ortho_scale = size * 1.25
scene = bpy.context.scene
scene.camera = camera
scene.world.use_nodes = True
scene.world.node_tree.nodes['Background'].inputs['Color'].default_value = (.14,.16,.19,1)
scene.world.node_tree.nodes['Background'].inputs['Strength'].default_value = .8
bpy.ops.object.light_add(type='AREA', location=center+Vector((-2,-3,4))*size)
bpy.context.object.data.energy = 600*size*size
bpy.context.object.data.size = size*3
scene.render.engine = 'CYCLES'
scene.cycles.samples = 12
scene.view_settings.view_transform = 'Standard'
scene.render.resolution_x = scene.render.resolution_y = 1100
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = 'PNG'
scene.render.filepath = output
bpy.ops.render.render(write_still=True)
