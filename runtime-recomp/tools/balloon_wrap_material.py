"""Bake reference-guided surface maps onto a continuous cylindrical material.

The runtime is unlit: the source wraps carry the reference's lacquer/mirror
finish. The maps were offset, seam-repaired, and composited in Photoshop with
their matching outer edges locked before baking to the existing runtime UVs.
No source mesh UVs or source-model paint are used.
"""
import hashlib
import math

VARIANTS = [('boost-blue',147,'blue'),('missile-red',148,'red'),
            ('trap-green',149,'green'),('shield-yellow',150,'shield'),
            ('magnet-rainbow',151,'rainbow'),('collectible-gold',154,'gold'),
            ('collectible-silver',155,'silver')]

def build_material(bpy, nodes, links, wraps):
    metadata=[]
    def m(op,a,b=0):
        n=nodes.new('ShaderNodeMath'); n.operation=op
        for i,v in enumerate((a,b)):
            if isinstance(v,(int,float)): n.inputs[i].default_value=float(v)
            else: links.new(v,n.inputs[i])
        return n.outputs[0]
    def mix(a,b,f=1,blend='MIX'):
        n=nodes.new('ShaderNodeMixRGB'); n.blend_type=blend
        for i,v in enumerate((f,a,b)):
            if isinstance(v,(int,float)): n.inputs[i].default_value=v
            elif isinstance(v,tuple): n.inputs[i].default_value=v
            else: links.new(v,n.inputs[i])
        return n.outputs[0]
    def vector(u,v):
        n=nodes.new('ShaderNodeCombineXYZ')
        links.new(u,n.inputs[0]); links.new(v,n.inputs[1])
        return n.outputs[0]
    geo=nodes.new('ShaderNodeNewGeometry')
    pos=nodes.new('ShaderNodeSeparateXYZ'); links.new(geo.outputs['Position'],pos.inputs[0])
    x,y,z=pos.outputs
    u=m('FRACT',m('ADD',m('DIVIDE',m('ARCTAN2',y,x),2*math.pi),.5))
    height=m('MINIMUM',1,m('MAXIMUM',0,m('DIVIDE',m('SUBTRACT',z,.365),.635)))
    # Latitude mapping prevents the art from stretching vertically at the
    # crown and shoulder; it is derived from normalized body height, not UVs.
    v=m('ADD',.5,m('DIVIDE',m('ARCSINE',m('SUBTRACT',m('MULTIPLY',height,2),1)),math.pi))
    normal=nodes.new('ShaderNodeSeparateXYZ'); links.new(geo.outputs['Normal'],normal.inputs[0])
    light=m('ABSOLUTE',m('ADD',m('MULTIPLY',normal.outputs['X'],.4),m('MULTIPLY',normal.outputs['Y'],.9)))
    shade=m('ADD',.3,m('MULTIPLY',light,.65))
    shine=m('MULTIPLY',m('POWER',light,26),1.2)
    knot=m('LESS_THAN',z,.365); cord=m('LESS_THAN',z,.305)

    def finish(variant):
        name,sprite,key=variant
        path=wraps/f'{key}.png'
        image=bpy.data.images.load(str(path),check_existing=True)
        def sample(at):
            tex=nodes.new('ShaderNodeTexImage'); tex.image=image
            tex.extension='REPEAT'; tex.interpolation='Linear'
            links.new(vector(at,v),tex.inputs[0]); return tex.outputs['Color']
        body=sample(u)
        color=(1,.87,.001,1) if sprite<151 else (.001,.008,.75,1) if sprite in (151,154) else (.25,.55,.85,1)
        neck=mix(mix(color,shade,1,'MULTIPLY'),shine,1,'ADD')
        base=mix(body,neck,knot)
        string=mix(mix((.8,.82,.88,1),shade,1,'MULTIPLY'),m('MULTIPLY',shine,.25),1,'ADD')
        metadata.append({'sprite_id':sprite,'source_wrap':path.name,
                         'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
                         'mapping':'full-circumference latitude/longitude; repaired periodic map with locked outer edges',
                         'source_wrap_size':list(image.size)})
        return mix(base,string,cord)
    return finish,metadata
