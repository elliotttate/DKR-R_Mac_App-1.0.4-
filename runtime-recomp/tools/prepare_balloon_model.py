"""Blender: rebuild the supplied balloon's entire material, not its painted UVs.

SOURCE.usdz OUTPUT_DIRECTORY REVIEW.blend REFERENCE.png WRAPS_DIRECTORY.
Seven reference-guided 360-degree finishes are baked to non-overlapping UVs.
The same repaired source geometry serves all colors. Gold/silver omit the
string, matching their retail sprite extents. Input is never modified.
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
sys.path.insert(0, str(Path(__file__).resolve().parent))
from balloon_wrap_material import VARIANTS, build_material

source, destination, review, reference, wraps = map(Path, sys.argv[sys.argv.index('--')+1:])
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
minimum = min(v.co.z for v in obj.data.vertices)
height = max(v.co.z for v in obj.data.vertices)-minimum
for v in obj.data.vertices:
    v.co = Vector((v.co.x/height, v.co.y/height, (v.co.z-minimum)/height))
bm = bmesh.new()
bm.from_mesh(obj.data)
bmesh.ops.remove_doubles(bm, verts=list(bm.verts), dist=1e-5)
bmesh.ops.dissolve_degenerate(bm, edges=list(bm.edges), dist=1e-7)
bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
assert not any(len(e.link_faces)>2 for e in bm.edges)
boundary = sum(e.is_boundary for e in bm.edges)
bm.to_mesh(obj.data)
bm.free()
for p in obj.data.polygons:
    p.use_smooth = True
# A small source mesh needs no decimation. Welded smooth normals remove the
# imported faceted highlights without changing its silhouette.
bpy.context.view_layer.objects.active = obj
bpy.ops.object.select_all(action='DESELECT')
obj.select_set(True)
while obj.data.uv_layers:
    obj.data.uv_layers.remove(obj.data.uv_layers[0])
obj.data.uv_layers.new(name='BalloonBake')
bpy.ops.object.mode_set(mode='EDIT')
bpy.ops.mesh.select_all(action='SELECT')
bpy.ops.uv.smart_project(angle_limit=math.radians(70), island_margin=.016)
bpy.ops.object.mode_set(mode='OBJECT')
mat = bpy.data.materials.new('Fresh wraparound balloon finish')
mat.use_nodes = True
obj.data.materials.clear()
obj.data.materials.append(mat)
nodes, links = mat.node_tree.nodes, mat.node_tree.links
nodes.clear()

finish, reference_mappings = build_material(bpy, nodes, links, wraps)
emission=nodes.new('ShaderNodeEmission')
output=nodes.new('ShaderNodeOutputMaterial'); links.new(emission.outputs[0],output.inputs['Surface'])
target=nodes.new('ShaderNodeTexImage')
scene=bpy.context.scene
scene.render.engine='CYCLES'; scene.cycles.samples=8
scene.render.bake.margin=12
scene.view_settings.view_transform='Standard'
manifest={'source':source.name,'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),
          'source_triangles':sum(len(p.vertices)-2 for p in obj.data.polygons),
          'source_boundary_edges':boundary,'texture_size':[2048,2048],
          'material_profile':'reference-guided-gloss-wrap-v2','source_textures_used':False,
          'reference':reference.name,'reference_sha256':hashlib.sha256(reference.read_bytes()).hexdigest(),
          'reference_mappings':reference_mappings,
          'uv_layout':'new non-overlapping smart unwrap with 12px bake gutter',
          'variants':[],'meshes':[]}
for variant in VARIANTS:
    name,sprite,slot = variant
    lit = finish(variant)
    links.new(lit,emission.inputs['Color'])
    atlas=bpy.data.images.new(name,2048,2048,alpha=False)
    target.image=atlas; nodes.active=target
    bpy.ops.object.select_all(action='DESELECT'); obj.select_set(True)
    bpy.context.view_layer.objects.active=obj
    bpy.ops.object.bake(type='EMIT')
    hash_value=f'{0x42414C4C33440000+sprite:016x}'
    atlas.filepath_raw=str((destination/f'{hash_value}.png').resolve())
    atlas.file_format='PNG'; atlas.save()
    manifest['variants'].append({'name':name,'sprite_id':sprite,'hash':hash_value,'texture':f'{hash_value}.png'})
    # Review instances use EXACTLY the runtime's baked emission texture.
    preview_mat=bpy.data.materials.new(name); preview_mat.use_nodes=True
    pn,pl=preview_mat.node_tree.nodes,preview_mat.node_tree.links
    tex=pn.new('ShaderNodeTexImage'); tex.image=atlas
    pe=pn.new('ShaderNodeEmission'); pl.new(tex.outputs[0],pe.inputs[0])
    pl.new(pe.outputs[0],next(n for n in pn if n.type=='OUTPUT_MATERIAL').inputs['Surface'])
    instance=obj.copy(); instance.data=obj.data.copy(); scene.collection.objects.link(instance)
    instance.name=name; instance.data.materials.clear(); instance.data.materials.append(preview_mat)
    instance.location.x=(len(manifest['variants'])-1)*.8

def export_mesh(mesh_object,prefix,trim):
    mesh=mesh_object.data.copy()
    if trim:
        bm=bmesh.new(); bm.from_mesh(mesh)
        # Keep knot, omit the narrow dangling string for gold/silver.
        bmesh.ops.bisect_plane(bm,geom=list(bm.verts)+list(bm.edges)+list(bm.faces),
            dist=1e-7,plane_co=(0,0,.305),plane_no=(0,0,1),clear_inner=True)
        holes=[e for e in bm.edges if e.is_boundary and all(abs(v.co.z-.305)<1e-5 for v in e.verts)]
        bmesh.ops.holes_fill(bm,edges=holes,sides=0)
        bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces)); bm.to_mesh(mesh); bm.free()
    zmin=min(v.co.z for v in mesh.vertices); zmax=max(v.co.z for v in mesh.vertices)
    width=max(v.co.x for v in mesh.vertices)-min(v.co.x for v in mesh.vertices)
    cx=(max(v.co.x for v in mesh.vertices)+min(v.co.x for v in mesh.vertices))*.5
    cy=(max(v.co.y for v in mesh.vertices)+min(v.co.y for v in mesh.vertices))*.5
    mesh.calc_loop_triangles()
    packed_vertices=[]; indices=[]; remap={}
    for triangle in mesh.loop_triangles:
        for loop in triangle.loops:
            v=mesh.vertices[mesh.loops[loop].vertex_index]; p,n=v.co,v.normal
            uv=mesh.uv_layers.active.data[loop].uv
            packed=struct.pack('<8f',(p.x-cx)/width,(p.z-zmin)/(zmax-zmin),-(p.y-cy)/width,n.x,n.z,-n.y,uv.x,1-uv.y)
            if packed not in remap: remap[packed]=len(packed_vertices); packed_vertices.append(packed)
            indices.append(remap[packed])
    data=struct.pack('<8sII',b'DKRPM001',len(packed_vertices),len(indices))+b''.join(packed_vertices)+struct.pack(f'<{len(indices)}I',*indices)
    for lod in ('near','far'):
        filename=f'{prefix}{lod}.dkrmesh'; (destination/filename).write_bytes(data)
        manifest['meshes'].append({'file':filename,'triangles':len(indices)//3,'vertices':len(packed_vertices)})
    bpy.data.meshes.remove(mesh)

export_mesh(obj,'',False)
export_mesh(obj,'collectible-',True)
bpy.data.objects.remove(obj,do_unlink=True)
manifest['runtime_placement']={'147':{'height':66,'bottom':4,'width':43},
 '148-151':{'height':66,'bottom':6,'width':43},
 '154':{'height':48,'bottom':22,'width':43},'155':{'height':50,'bottom':21,'width':43}}
(destination/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
(destination/'rt64.json').write_text(json.dumps({'configuration':{'configurationVersion':3,'hashVersion':5,'autoPath':'rt64','defaultOperation':'preload','defaultShift':'none'},
 'textures':[{'hashes':{'rt64':v['hash']},'path':v['texture'],'operation':'preload','shift':'none'} for v in manifest['variants']]},indent=2)+'\n')
for img in bpy.data.images:
    if img.source=='FILE' or img.has_data:
        try: img.pack()
        except RuntimeError: pass
bpy.ops.wm.save_as_mainfile(filepath=str(review.resolve()))
print('BALLOONS_PREPARED',json.dumps(manifest),flush=True)
