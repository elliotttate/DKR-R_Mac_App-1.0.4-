"""Blender: rebake the original palm without regenerating its approved meshes.

Arguments: SOURCE.glb PREPARED_ASSET_DIR NEW_OUTPUT_DIR
Uses the source's high-detail surface and tangent normal map in the same UV
space as the existing near/far/full/canopy meshes. Requires a fresh output
directory and verifies every copied mesh byte-for-byte. Never edits inputs.
"""
import bpy
import hashlib
import json
from mathutils import Matrix
from pathlib import Path
import shutil
import sys

source, prepared, destination = map(Path, sys.argv[sys.argv.index('--') + 1:])
assert source.suffix.lower() == '.glb'
assert not destination.exists(), 'Use a fresh destination; inputs are never overwritten'
manifest = json.loads((prepared / 'manifest.json').read_text())
with source.open('rb') as stream:
    source_hash = hashlib.file_digest(stream, 'sha256').hexdigest()
assert source_hash == manifest['source_sha256'], 'Source does not match prepared UVs'
mesh_names = ('near.dkrmesh', 'far.dkrmesh', 'canopy-near.dkrmesh', 'canopy-far.dkrmesh')
assert all((prepared / name).is_file() for name in mesh_names)
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
bpy.ops.import_scene.gltf(filepath=str(source))
objects = [obj for obj in bpy.context.scene.objects if obj.type == 'MESH']
assert len(objects) == 1
obj = objects[0]
assert not obj.animation_data and not obj.modifiers
assert len(obj.data.materials) == 1 and obj.data.uv_layers.active
obj.data.transform(obj.matrix_world)
obj.parent = None
obj.matrix_world = Matrix.Identity(4)
height = max(v.co.z for v in obj.data.vertices) - min(v.co.z for v in obj.data.vertices)
assert height > 0
material = obj.data.materials[0]
bsdf = next(n for n in material.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
assert bsdf.inputs['Base Color'].is_linked and bsdf.inputs['Normal'].is_linked
image = bsdf.inputs['Base Color'].links[0].from_node.image
sys.path.insert(0, str(Path(__file__).resolve().parent))
from bake_plant_lighting import bake_plant_lighting, LIGHTING_PROFILES
image = bake_plant_lighting(obj, material, image, height, 'tropical-palm')
destination.mkdir(parents=True)
for name in (*mesh_names, 'rt64.json'):
    shutil.copy2(prepared / name, destination / name)
image.filepath_raw = str((destination / manifest['texture']).resolve())
image.file_format = 'PNG'
image.save()
manifest['texture_size'] = list(image.size)
manifest['baked_lighting'] = True
manifest['baked_lighting_profile'] = LIGHTING_PROFILES['tropical-palm']
manifest['geometry_preserved'] = True
manifest['mesh_sha256'] = {}
for name in mesh_names:
    original = (prepared / name).read_bytes()
    assert (destination / name).read_bytes() == original
    manifest['mesh_sha256'][name] = hashlib.sha256(original).hexdigest()
(destination / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
print('PALM_MATERIAL_COMPLETE', destination, manifest['baked_lighting_profile'], flush=True)
