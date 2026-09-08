"""Blender: remove the Haven's complete basal stem, preserving outer fruit/leaves.

Arguments: ASSET_DIR. Full meshes and texture atlas are read-only inputs.
Uses a narrow Boolean recess rather than a horizontal slice through fruit.
"""
import bpy
import bmesh
import json
import struct
import sys
from pathlib import Path
from mathutils import Vector
from mathutils.geometry import barycentric_transform, closest_point_on_tri

directory = Path(sys.argv[sys.argv.index('--') + 1])
manifest = json.loads((directory / 'manifest.json').read_text())
CENTER_X, CENTER_Z = -.006, .008
CUT_HEIGHT, CUT_RADIUS = .61, .035
PIVOT = (-.010, .50, .0025)
atlas = bpy.data.images.load(str(directory / manifest['texture']))
width, height = atlas.size
pixels = list(atlas.pixels)


def sample(uv):
    x = min(width-1, max(0, int(uv[0] * width)))
    y = min(height-1, max(0, int(uv[1] * height)))
    return pixels[4*(y*width+x):4*(y*width+x)+3]


alignments = []
for lod in ('near', 'far'):
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    data = (directory / f'{lod}.dkrmesh').read_bytes()
    magic, nv, ni = struct.unpack_from('<8sII', data)
    assert magic == b'DKRPM001'
    values = [struct.unpack_from('<8f', data, 16+32*i) for i in range(nv)]
    indices = struct.unpack_from(f'<{ni}I', data, 16+32*nv)
    mesh = bpy.data.meshes.new(lod)
    mesh.from_pydata([(v[0], -v[2], v[1]) for v in values], [],
                     [indices[i:i+3] for i in range(0, ni, 3)])
    uv = mesh.uv_layers.new()
    for poly in mesh.polygons:
        for loop in poly.loop_indices:
            v = values[mesh.loops[loop].vertex_index]
            uv.data[loop].uv = (v[6], 1-v[7])
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bmesh.ops.remove_doubles(bm, verts=list(bm.verts), dist=1e-6)
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new(lod, mesh)
    bpy.context.collection.objects.link(obj)
    original_material = bpy.data.materials.new('existing-atlas')
    recess_material = bpy.data.materials.new('gold-recess')
    mesh.materials.append(original_material)
    mesh.materials.append(recess_material)
    # Reuse nearby gold fruit UV patches for newly generated recess faces.
    # Project within one source triangle per face to avoid crossing UV islands.
    gold_candidates = []
    uv = mesh.uv_layers.active
    for poly in mesh.polygons:
        c = poly.center
        if .58 < c.z < .64 and c.x*c.x+c.y*c.y < .14**2:
            coords = [uv.data[i].uv.copy() for i in poly.loop_indices]
            center_uv = tuple(sum(p[k] for p in coords)/len(coords) for k in (0,1))
            r,g,b = sample(center_uv)
            if r > 1.15*g and g > 1.35*b and all(
                    sample(u)[0] > sample(u)[1] * 1.05 for u in coords):
                points = [mesh.vertices[i].co.copy() for i in poly.vertices]
                gold_candidates.append((c.copy(), points, coords))
    assert gold_candidates, 'No safe gold fruit UV patch found'

    # This cutter surrounds the complete stalk but not the hanging fruit
    # lobes or low frond tips. Its roof is hidden inside the fruit junction.
    bpy.ops.mesh.primitive_cone_add(vertices=48, radius1=.060, radius2=CUT_RADIUS,
        depth=CUT_HEIGHT+.1, location=(CENTER_X, -CENTER_Z, (CUT_HEIGHT-.1)/2))
    cutter = bpy.context.object
    cutter.data.materials.append(original_material)
    cutter.data.materials.append(recess_material)
    for poly in cutter.data.polygons:
        poly.material_index = 1
    bpy.context.view_layer.objects.active = obj
    modifier = obj.modifiers.new('remove-entire-green-stem', 'BOOLEAN')
    modifier.operation = 'DIFFERENCE'
    modifier.solver = 'EXACT'
    modifier.object = cutter
    bpy.ops.object.modifier_apply(modifier=modifier.name)
    bpy.data.objects.remove(cutter, do_unlink=True)
    mesh = obj.data
    uv = mesh.uv_layers.active
    recess_faces = 0
    for poly in mesh.polygons:
        if poly.material_index == 1:
            recess_faces += 1
            _, points, coords = min(gold_candidates, key=lambda candidate:
                                    (candidate[0]-poly.center).length_squared)
            uv_points = [Vector((u.x, u.y, 0)) for u in coords]
            for loop in poly.loop_indices:
                point = mesh.vertices[mesh.loops[loop].vertex_index].co
                nearest = closest_point_on_tri(point, *points)
                mapped = barycentric_transform(nearest, *points, *uv_points)
                uv.data[loop].uv = mapped.xy
    assert recess_faces > 0
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bmesh.ops.triangulate(bm, faces=list(bm.faces))
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    assert not any(e.is_boundary or len(e.link_faces) != 2 or not e.is_contiguous for e in bm.edges)
    assert not any(f.calc_area() < 1e-12 for f in bm.faces)
    bm.to_mesh(mesh)
    bm.free()
    mesh.update()
    delta = (PIVOT[0]-CENTER_X, PIVOT[1]-CUT_HEIGHT, PIVOT[2]-CENTER_Z)
    vertices, output_indices, remap = [], [], {}
    uv = mesh.uv_layers.active
    for poly in mesh.polygons:
        for loop in poly.loop_indices:
            v = mesh.vertices[mesh.loops[loop].vertex_index]
            p, n = v.co, v.normal
            u = uv.data[loop].uv
            vertex = (p.x+delta[0], p.z+delta[1], -p.y+delta[2],
                      n.x, n.z, -n.y, u.x, 1-u.y)
            if vertex not in remap:
                remap[vertex] = len(vertices)
                vertices.append(vertex)
            output_indices.append(remap[vertex])
    with (directory / f'canopy-{lod}.dkrmesh').open('wb') as output:
        output.write(struct.pack('<8sII', magic, len(vertices), len(output_indices)))
        for v in vertices:
            output.write(struct.pack('<8f', *v))
        output.write(struct.pack(f'<{len(output_indices)}I', *output_indices))
    alignments.append({'lod': lod, 'cut_center_before': [CENTER_X, CUT_HEIGHT, CENTER_Z],
        'translation': list(delta), 'cut_center_after': list(PIVOT),
        'recess_radius': CUT_RADIUS, 'recess_base_radius': .060, 'recess_faces': recess_faces,
        'closed_surface': True, 'triangles': len(output_indices)//3})
    print('HAVEN_STEM_REMOVAL', alignments[-1], flush=True)
manifest['canopy_alignment'] = alignments
manifest['canopy_preparation'] = 'haven-shaped-stem-recess-v1'
(directory / 'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
