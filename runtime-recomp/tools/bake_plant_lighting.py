"""Native Blender material bake for fixed-function, unlit game renderers.

Preserves the source albedo/UVs and uses the supplied normal map, geometrical
ambient occlusion, bright wrap lighting and glossy foliage highlights.
The input asset is never saved or altered. Only the exported atlas is generated.
"""
import bpy
from mathutils import Vector

LIGHTING_PROFILE = 'bright-sprite-wrap-gloss-v2'
LIGHTING_PROFILES = {'rubber-tree': LIGHTING_PROFILE,
                     'blueberry': 'blueberry-gloss-wrap-v2',
                     'beach-tree': 'beach-tree-gold-gloss-v2',
                     'tropical-palm': 'tropical-palm-gold-wrap-gloss-v1',
                     'haven-palm': 'haven-palm-repaired-gloss-v1'}


def bake_plant_lighting(obj, material, source_image, height, profile='rubber-tree'):
    assert profile in LIGHTING_PROFILES
    blueberry = profile == 'blueberry'
    beach_tree = profile == 'beach-tree'
    haven_palm = profile == 'haven-palm'
    tropical_palm = profile in ('tropical-palm', 'haven-palm')
    nodes, links = material.node_tree.nodes, material.node_tree.links
    bsdf = next(n for n in nodes if n.type == 'BSDF_PRINCIPLED')
    albedo = bsdf.inputs['Base Color'].links[0].from_socket
    normal = (bsdf.inputs['Normal'].links[0].from_socket if bsdf.inputs['Normal'].is_linked
              else nodes.new('ShaderNodeNewGeometry').outputs['Normal'])

    def math(operation, a, b=None):
        n = nodes.new('ShaderNodeMath')
        n.operation = operation
        for i, value in enumerate((a, b)):
            if value is None:
                continue
            if isinstance(value, (int, float)):
                n.inputs[i].default_value = value
            else:
                links.new(value, n.inputs[i])
        return n.outputs[0]

    def color_multiply(a, b):
        n = nodes.new('ShaderNodeMixRGB')
        n.blend_type = 'MULTIPLY'
        n.inputs[0].default_value = 1
        links.new(a, n.inputs[1])
        if isinstance(b, tuple):
            n.inputs[2].default_value = b
        else:
            links.new(b, n.inputs[2])
        return n.outputs[0]

    def dot(direction):
        n = nodes.new('ShaderNodeVectorMath')
        n.operation = 'DOT_PRODUCT'
        links.new(normal, n.inputs[0])
        n.inputs[1].default_value = Vector(direction).normalized()
        return math('MAXIMUM', n.outputs['Value'], 0)

    separate = nodes.new('ShaderNodeSeparateColor')
    links.new(albedo, separate.inputs['Color'])
    foliage = math('GREATER_THAN', separate.outputs['Green'],
                   math('MULTIPLY', separate.outputs['Red'], 1.12))
    berry = None
    if beach_tree or haven_palm:
        # This USDZ has no normal map. A restrained albedo-derived bump gives
        # the painted golden scales/bark local relief, while sculpted leaf ribs
        # keep their original geometry normals. No mesh positions are changed.
        bump = nodes.new('ShaderNodeBump')
        links.new(albedo, bump.inputs['Height'])
        links.new(normal, bump.inputs['Normal'])
        bump.inputs['Strength'].default_value = .28
        bump.inputs['Distance'].default_value = height * .003
        mixed_normal = nodes.new('ShaderNodeMixRGB')
        links.new(foliage, mixed_normal.inputs[0])
        links.new(bump.outputs['Normal'], mixed_normal.inputs[1])
        links.new(normal, mixed_normal.inputs[2])
        normal = mixed_normal.outputs[0]
    if blueberry:
        berry = math('MULTIPLY',
                     math('GREATER_THAN', separate.outputs['Blue'],
                          math('MULTIPLY', separate.outputs['Green'], 1.20)),
                     math('GREATER_THAN', separate.outputs['Blue'],
                          math('MULTIPLY', separate.outputs['Red'], 1.50)))
        foliage = math('SUBTRACT', 1, berry)
    tint = nodes.new('ShaderNodeMixRGB')
    links.new(foliage, tint.inputs[0])
    # The supplied PBR albedo is a dark diffuse base. The sprite reference is
    # already illuminated: warm orange-brown bark and vivid green leaf faces.
    # Gains are linear-light material exposure, not a global screen adjustment.
    tint.inputs[1].default_value = (2.05, 1.85, 1.40, 1)
    tint.inputs[2].default_value = (1.20, 1.55, 1.12, 1)
    if blueberry:
        # Preserve the authored leaf pattern. Cool the dark markings toward
        # forest green, while lighting the yellow-green surrounding blade.
        marking = math('MINIMUM', math('MAXIMUM', math('DIVIDE',
            math('SUBTRACT', .28, separate.outputs['Green']), .18), 0), 1)
        leaf_tint = nodes.new('ShaderNodeMixRGB')
        links.new(marking, leaf_tint.inputs[0])
        leaf_tint.inputs[1].default_value = (.95, 1.25, 1.05, 1)
        leaf_tint.inputs[2].default_value = (.30, .72, 1.40, 1)
        tint.inputs[1].default_value = (.90, 1.30, 1.55, 1)
        links.new(leaf_tint.outputs[0], tint.inputs[2])
    elif beach_tree:
        tint.inputs[1].default_value = (1.45, 1.30, .90, 1)
        tint.inputs[2].default_value = (1.00, 1.65, .85, 1)
    elif tropical_palm:
        # This palm's diffuse already contains vivid yellow-green leaves, but
        # its fruit is beige. Preserve leaf patterning and warm the fruit into
        # the reference's gold; the original normal map supplies fine relief.
        tint.inputs[1].default_value = (2.55, 1.80, .65, 1)
        tint.inputs[2].default_value = (1.08, 1.18, .72, 1)
    tinted = color_multiply(albedo, tint.outputs[0])

    ao = nodes.new('ShaderNodeAmbientOcclusion')
    ao.inputs['Distance'].default_value = height * (.14 if tropical_palm else .10 if beach_tree else .12 if blueberry else .22)
    ao.samples = 16
    ao.only_local = True
    links.new(normal, ao.inputs['Normal'])
    # Bright, illustrative wrap light: a single directional key made the
    # opposite half of the tree much darker than the always-prelit sprite.
    # Keep shadowing local to leaf overlaps instead of dimming whole sides.
    ambient = math('ADD', math('MULTIPLY', ao.outputs['AO'], .60), .40)
    keys = [Vector(direction).normalized() for direction in
            [(-.45, -.55, .72), (.45, .55, .72), (-.55, .45, .72), (.55, -.45, .72)]]
    wrapped_key = dot(keys[0])
    for key in keys[1:]:
        wrapped_key = math('MAXIMUM', wrapped_key, dot(key))
    diffuse = math('ADD', math('MULTIPLY', wrapped_key, .35), .65)
    if blueberry:
        # Spherical berries need a darker lower rim, rather than uniform blue.
        berry_diffuse = math('ADD', math('MULTIPLY', wrapped_key, .80), .32)
        diffuse = math('ADD', math('MULTIPLY', diffuse, foliage),
                       math('MULTIPLY', berry_diffuse, berry))
    elif tropical_palm:
        gold_diffuse = math('ADD', math('MULTIPLY', wrapped_key, .70), .35)
        diffuse = math('ADD', math('MULTIPLY', diffuse, foliage),
                       math('MULTIPLY', gold_diffuse, math('SUBTRACT', 1, foliage)))
    lit = color_multiply(tinted, math('MULTIPLY', ambient, diffuse))

    # A fixed highlight is intentional: the original reference is pre-lit art.
    # Use the source normal map to keep leaf veins and rounded highlights rather
    # than coloring each low-poly triangle uniformly.
    views = [Vector(direction).normalized() for direction in
             [(0, -1, .25), (0, 1, .25), (-1, 0, .25), (1, 0, .25)]]
    gloss = dot(keys[0] + views[0])
    for key, view in zip(keys[1:], views[1:]):
        gloss = math('MAXIMUM', gloss, dot(key + view))
    highlight = math('MULTIPLY', math('POWER', gloss, 24), .85)
    highlight = math('MULTIPLY', highlight, foliage)
    if blueberry:
        leaf_gloss = math('MULTIPLY', math('POWER', gloss, 22),
                         math('SUBTRACT', .60, math('MULTIPLY', marking, .56)))
        berry_gloss = math('ADD', math('MULTIPLY', math('POWER', gloss, 48), 1.55),
                           math('MULTIPLY', math('POWER', gloss, 10), .22))
        highlight = math('ADD', math('MULTIPLY', leaf_gloss, foliage),
                         math('MULTIPLY', berry_gloss, berry))
    elif beach_tree:
        # The supplied model has sculpted ribs but no normal map. Smooth
        # geometry normals and cavity AO retain the ridges without flat faces.
        leaf_gloss = math('MULTIPLY', math('POWER', gloss, 30), .95)
        gold_gloss = math('MULTIPLY', math('POWER', gloss, 28), .45)
        highlight = math('ADD', math('MULTIPLY', leaf_gloss, foliage),
                         math('MULTIPLY', gold_gloss, math('SUBTRACT', 1, foliage)))
    elif tropical_palm:
        leaf_gloss = math('MULTIPLY', math('POWER', gloss, 28), .65)
        gold_gloss = math('ADD', math('MULTIPLY', math('POWER', gloss, 30), 1.10),
                          math('MULTIPLY', math('POWER', gloss, 64), .35))
        highlight = math('ADD', math('MULTIPLY', leaf_gloss, foliage),
                         math('MULTIPLY', gold_gloss, math('SUBTRACT', 1, foliage)))
    highlight = math('MULTIPLY', highlight, ao.outputs['AO'])
    if tropical_palm:
        gloss_tint = nodes.new('ShaderNodeMixRGB')
        links.new(foliage, gloss_tint.inputs[0])
        gloss_tint.inputs[1].default_value = (1.0, .80, .38, 1)
        gloss_tint.inputs[2].default_value = (1.0, 1.0, .85, 1)
        highlight = color_multiply(highlight, gloss_tint.outputs[0])
    add = nodes.new('ShaderNodeMixRGB')
    add.blend_type = 'ADD'
    add.inputs[0].default_value = 1
    links.new(lit, add.inputs[1])
    links.new(highlight, add.inputs[2])
    emission = nodes.new('ShaderNodeEmission')
    links.new(add.outputs[0], emission.inputs['Color'])
    output = next(n for n in nodes if n.type == 'OUTPUT_MATERIAL')
    links.new(emission.outputs[0], output.inputs['Surface'])

    result = bpy.data.images.new('Baked plant lighting', width=2048, height=2048, alpha=False)
    target = nodes.new('ShaderNodeTexImage')
    target.image = result
    nodes.active = target
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    scene = bpy.context.scene
    scene.render.engine = 'CYCLES'
    scene.cycles.samples = 32
    scene.render.bake.margin = 8
    scene.render.bake.use_clear = True
    smoothing = [polygon.use_smooth for polygon in obj.data.polygons]
    if blueberry or beach_tree:
        # Smooth the bake's lighting across polygon edges without subdividing,
        # moving vertices or altering the exported mesh topology/UVs.
        for polygon in obj.data.polygons:
            polygon.use_smooth = True
    try:
        bpy.ops.object.bake(type='EMIT')
    finally:
        for polygon, original in zip(obj.data.polygons, smoothing):
            polygon.use_smooth = original
    print('PLANT_BAKE', LIGHTING_PROFILES[profile], '2K wrap light + local AO + material-specific gloss', flush=True)
    return result
